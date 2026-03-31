#include "vfx/preview/growl/GrowlSharedRenderer.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stb_image.h>

#include "engine/core/Paths.h"
#include "engine/render/Camera3D.h"
#include "engine/render/gltf/FastGLTFLoader.h"
#include "engine/render/gltf/ModelFastGltfLoaderHelpers.h"
#include "engine/utils/LogSink.h"
#include "vfx/runtime/growl/SharedGrowlBatchSubmission.h"
#include "vfx/runtime/growl/SharedGrowlWaveBridge.h"

namespace vfx::preview::growl {
namespace {

using MeshData = vfx::runtime::growl_batches::MeshData;
using MeshVertex = vfx::runtime::growl_batches::MeshVertex;
using TextureCacheEntry = GrowlSharedRenderer::TextureCacheEntry;

std::vector<glm::vec2> buildFallbackUvs(const std::vector<glm::vec3>& positions) {
    std::vector<glm::vec2> uvs;
    if (positions.empty()) return uvs;

    glm::vec3 pMin(std::numeric_limits<float>::max());
    glm::vec3 pMax(-std::numeric_limits<float>::max());
    for (const auto& position : positions) {
        pMin = glm::min(pMin, position);
        pMax = glm::max(pMax, position);
    }

    const float ranges[3] = {
        pMax.x - pMin.x,
        pMax.y - pMin.y,
        pMax.z - pMin.z,
    };

    int a = 0;
    if (ranges[1] > ranges[a]) a = 1;
    if (ranges[2] > ranges[a]) a = 2;
    int b = (a == 0) ? 1 : 0;
    for (int i = 0; i < 3; ++i) {
        if (i == a) continue;
        if (ranges[i] > ranges[b]) b = i;
    }

    const float minA = (a == 0) ? pMin.x : ((a == 1) ? pMin.y : pMin.z);
    const float minB = (b == 0) ? pMin.x : ((b == 1) ? pMin.y : pMin.z);
    const float denA = std::max(1e-6f, ranges[a]);
    const float denB = std::max(1e-6f, ranges[b]);

    uvs.reserve(positions.size());
    for (const auto& position : positions) {
        const float ca = (a == 0) ? position.x : ((a == 1) ? position.y : position.z);
        const float cb = (b == 0) ? position.x : ((b == 1) ? position.y : position.z);
        uvs.emplace_back((ca - minA) / denA, (cb - minB) / denB);
    }
    return uvs;
}

std::string resolveDataPath(const std::string& path) {
    if (path.empty()) return {};
    std::error_code ec;
    if (std::filesystem::exists(path, ec) && !ec) {
        return path;
    }
    const std::string dataPath = engine::paths::data(path);
    if (std::filesystem::exists(dataPath, ec) && !ec) {
        return dataPath;
    }
    return path;
}

bool loadMeshFromGltf(const std::string& modelPath, MeshData& out, std::string* outError) {
    out = {};
    const std::string resolvedPath = resolveDataPath(modelPath);
    auto loaded = pac::fastgltf_loader::tryLoad(resolvedPath);
    if (!loaded.has_value()) {
        if (outError) {
            *outError = "Unable to parse glTF/GLB";
        }
        return false;
    }

    fastgltf::DefaultBufferDataAdapter adapter{};
    const fastgltf::Asset& asset = loaded->asset;
    for (const auto& mesh : asset.meshes) {
        for (const auto& primitive : mesh.primitives) {
            if (primitive.type != fastgltf::PrimitiveType::Triangles) continue;

            const auto itPos = primitive.findAttribute("POSITION");
            if (itPos == primitive.attributes.end()) continue;
            int materialIndex = -1;
            if (primitive.materialIndex.has_value()) {
                materialIndex = static_cast<int>(primitive.materialIndex.value());
            }

            std::vector<glm::vec3> positions;
            fastgltf::iterateAccessorWithIndex<glm::vec3>(
                asset,
                asset.accessors[itPos->accessorIndex],
                [&](glm::vec3 value, std::size_t) { positions.push_back(value); },
                adapter);
            if (positions.empty()) continue;

            std::vector<glm::vec2> uvs = buildFallbackUvs(positions);
            int requiredTexCoord =
                pac::model_fastgltf::requiredTexCoordForMaterial(asset, materialIndex);
            std::string uvAttribute = "TEXCOORD_" + std::to_string(requiredTexCoord);
            auto itUv = primitive.findAttribute(uvAttribute);
            if (itUv == primitive.attributes.end()) {
                requiredTexCoord = 0;
                itUv = primitive.findAttribute("TEXCOORD_0");
            }
            if (itUv != primitive.attributes.end()) {
                std::vector<glm::vec2> authoredUvs;
                fastgltf::iterateAccessorWithIndex<glm::vec2>(
                    asset,
                    asset.accessors[itUv->accessorIndex],
                    [&](glm::vec2 value, std::size_t) { authoredUvs.push_back(value); },
                    adapter);
                if (authoredUvs.size() == positions.size()) {
                    uvs = std::move(authoredUvs);
                }
            }

            std::vector<glm::vec3> normals(positions.size(), glm::vec3(0.0f, 1.0f, 0.0f));
            if (const auto itNormal = primitive.findAttribute("NORMAL");
                itNormal != primitive.attributes.end()) {
                std::vector<glm::vec3> authoredNormals;
                fastgltf::iterateAccessorWithIndex<glm::vec3>(
                    asset,
                    asset.accessors[itNormal->accessorIndex],
                    [&](glm::vec3 value, std::size_t) { authoredNormals.push_back(value); },
                    adapter);
                if (authoredNormals.size() == positions.size()) {
                    normals = std::move(authoredNormals);
                }
            }

            std::vector<glm::vec4> tangents(positions.size(), glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            if (const auto itTangent = primitive.findAttribute("TANGENT");
                itTangent != primitive.attributes.end()) {
                std::vector<glm::vec4> authoredTangents;
                fastgltf::iterateAccessorWithIndex<glm::vec4>(
                    asset,
                    asset.accessors[itTangent->accessorIndex],
                    [&](glm::vec4 value, std::size_t) { authoredTangents.push_back(value); },
                    adapter);
                if (authoredTangents.size() == positions.size()) {
                    tangents = std::move(authoredTangents);
                }
            }

            std::vector<glm::vec4> colors(positions.size(), glm::vec4(1.0f));
            if (const auto itColor = primitive.findAttribute("COLOR_0");
                itColor != primitive.attributes.end()) {
                const auto& accessor = asset.accessors[itColor->accessorIndex];
                std::vector<glm::vec4> authoredColors;
                authoredColors.reserve(accessor.count);
                if (accessor.type == fastgltf::AccessorType::Vec3) {
                    fastgltf::iterateAccessorWithIndex<glm::vec3>(
                        asset,
                        accessor,
                        [&](glm::vec3 value, std::size_t) {
                            authoredColors.emplace_back(value, 1.0f);
                        },
                        adapter);
                } else {
                    fastgltf::iterateAccessorWithIndex<glm::vec4>(
                        asset,
                        accessor,
                        [&](glm::vec4 value, std::size_t) { authoredColors.push_back(value); },
                        adapter);
                }
                if (authoredColors.size() == positions.size()) {
                    colors = std::move(authoredColors);
                }
            }

            std::vector<glm::u16vec4> joints(positions.size(), glm::u16vec4(0u));
            std::vector<glm::vec4> weights(positions.size(), glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
            if (const auto itJoints = primitive.findAttribute("JOINTS_0");
                itJoints != primitive.attributes.end()) {
                std::vector<glm::u16vec4> authoredJoints;
                fastgltf::iterateAccessorWithIndex<glm::u16vec4>(
                    asset,
                    asset.accessors[itJoints->accessorIndex],
                    [&](glm::u16vec4 value, std::size_t) { authoredJoints.push_back(value); },
                    adapter);
                if (authoredJoints.size() == positions.size()) {
                    joints = std::move(authoredJoints);
                }
            }
            if (const auto itWeights = primitive.findAttribute("WEIGHTS_0");
                itWeights != primitive.attributes.end()) {
                std::vector<glm::vec4> authoredWeights;
                fastgltf::iterateAccessorWithIndex<glm::vec4>(
                    asset,
                    asset.accessors[itWeights->accessorIndex],
                    [&](glm::vec4 value, std::size_t) { authoredWeights.push_back(value); },
                    adapter);
                if (authoredWeights.size() == positions.size()) {
                    weights = std::move(authoredWeights);
                }
            }

            std::vector<std::uint32_t> indices;
            if (primitive.indicesAccessor.has_value()) {
                const auto& accessor = asset.accessors[primitive.indicesAccessor.value()];
                indices.reserve(accessor.count);
                fastgltf::iterateAccessorWithIndex<std::uint32_t>(
                    asset,
                    accessor,
                    [&](std::uint32_t value, std::size_t) { indices.push_back(value); },
                    adapter);
            } else {
                indices.resize(positions.size());
                for (std::size_t i = 0; i < positions.size(); ++i) {
                    indices[i] = static_cast<std::uint32_t>(i);
                }
            }
            if (indices.size() < 3u) continue;

            const std::uint32_t baseVertex = static_cast<std::uint32_t>(out.vertices.size());
            out.vertices.reserve(out.vertices.size() + positions.size());
            out.indices.reserve(out.indices.size() + indices.size());
            for (std::size_t i = 0; i < positions.size(); ++i) {
                MeshVertex vertex;
                vertex.position = positions[i];
                vertex.normal = normals[i];
                vertex.tangent = tangents[i];
                vertex.uv = uvs[i];
                vertex.color = colors[i];
                vertex.j0 = joints[i].x;
                vertex.j1 = joints[i].y;
                vertex.j2 = joints[i].z;
                vertex.j3 = joints[i].w;
                vertex.w0 = weights[i].x;
                vertex.w1 = weights[i].y;
                vertex.w2 = weights[i].z;
                vertex.w3 = weights[i].w;
                out.vertices.push_back(vertex);
            }
            for (std::uint32_t index : indices) {
                out.indices.push_back(baseVertex + index);
            }
        }
    }

    if (out.vertices.empty() || out.indices.empty()) {
        if (outError) {
            *outError = "No triangle mesh data";
        }
        return false;
    }
    return true;
}

} // namespace

bool detail::loadMeshForPreview(const std::string& modelPath,
                                MeshData& out,
                                std::string* outError) {
    return loadMeshFromGltf(modelPath, out, outError);
}
namespace {

TextureCacheEntry makeWhiteTexture() {
    TextureCacheEntry white;
    white.attemptedLoad = true;
    white.valid = true;
    white.width = 1;
    white.height = 1;
    white.rgba = {255u, 255u, 255u, 255u};
    return white;
}

bool loadTextureRgba(const std::string& texturePath,
                     bool flipVertical,
                     TextureCacheEntry& outEntry) {
    outEntry = {};
    outEntry.attemptedLoad = true;
    if (texturePath.empty()) {
        outEntry = makeWhiteTexture();
        return true;
    }

    const std::string resolvedPath = resolveDataPath(texturePath);
    stbi_set_flip_vertically_on_load(flipVertical ? 1 : 0);
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(resolvedPath.c_str(), &width, &height, &channels, 4);
    stbi_set_flip_vertically_on_load(0);
    if (!pixels || width <= 0 || height <= 0) {
        if (pixels) {
            stbi_image_free(pixels);
        }
        return false;
    }

    const std::size_t byteCount =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    outEntry.valid = true;
    outEntry.width = width;
    outEntry.height = height;
    outEntry.rgba.assign(pixels, pixels + byteCount);
    stbi_image_free(pixels);
    return true;
}

} // namespace

void GrowlSharedRenderer::onResize(int width, int height) {
    backend_.onResize(width, height);
}

void GrowlSharedRenderer::render(const GrowlWaveVFX& effect,
                                 const Camera3D& camera,
                                 int surfaceWidth,
                                 int surfaceHeight) {
    GrowlWaveVFX::RenderSnapshot snapshot;
    if (!effect.buildRenderSnapshot(snapshot)) return;

    std::vector<vfx::runtime::growl_batches::WorldIndexedBatch> batches;
    batches.reserve(snapshot.drawPasses.size() * 4u);

    const auto resolveMesh =
        [&](const std::string& modelPath) -> vfx::runtime::growl_batches::MeshData* {
            return ensureBackendMeshLoaded(modelPath);
        };

    const auto resolveTexture =
        [&](const GrowlWaveVFX::Config::DrawPass& pass,
            const vfx::runtime::growl::TevState& tev,
            vfx::runtime::growl_batches::TextureView& outView) -> bool {
            return fillTextureView(pass, snapshot.config, tev, outView);
        };

    if (!vfx::runtime::growl_bridge::appendBatches(
            snapshot,
            batches,
            camera.getPosition(),
            resolveMesh,
            resolveTexture)) {
        return;
    }

    const glm::mat4 viewProj = camera.getProjectionMatrix() * camera.getViewMatrix();
    const glm::vec3 cameraPos = camera.getPosition();
    const glm::vec3 cameraForward = camera.getDirection();
    const glm::vec3 cameraTarget = camera.getTarget();
    vfx::runtime::growl_submit::submitBatches(
        backend_,
        batches,
        glm::value_ptr(viewProj),
        surfaceWidth,
        surfaceHeight,
        glm::value_ptr(cameraPos),
        glm::value_ptr(cameraForward),
        glm::value_ptr(cameraTarget));
}

vfx::runtime::growl_batches::MeshData* GrowlSharedRenderer::ensureBackendMeshLoaded(
    const std::string& modelPath) {
    auto& cacheEntry = backendMeshByModelPath_[modelPath];
    if (!cacheEntry.attemptedLoad) {
        cacheEntry.attemptedLoad = true;
        cacheEntry.mesh = {};
        cacheEntry.error.clear();
        if (!detail::loadMeshForPreview(modelPath, cacheEntry.mesh, &cacheEntry.error)) {
            cacheEntry.mesh = {};
        }
    }

    if (!cacheEntry.error.empty()) {
        if (!cacheEntry.reportedFailure) {
            static engine::log::Sink log("VfxLab", &std::cout, &std::cerr);
            log.warn("[VfxLab] Unable to load mesh '" + modelPath
                     + "' (" + cacheEntry.error + ")");
            cacheEntry.reportedFailure = true;
        }
        return nullptr;
    }
    if (cacheEntry.mesh.vertices.empty() || cacheEntry.mesh.indices.empty()) return nullptr;
    return &cacheEntry.mesh;
}

GrowlSharedRenderer::TextureCacheEntry* GrowlSharedRenderer::ensureBackendTextureLoaded(
    const std::string& texturePath,
    bool flipVertical) {
    const std::string cacheKey = flipVertical ? texturePath + "#flip" : texturePath;
    auto& cacheEntry = backendTextureByPath_[cacheKey];
    if (!cacheEntry.attemptedLoad) {
        if (!loadTextureRgba(texturePath, flipVertical, cacheEntry)) {
            cacheEntry.attemptedLoad = true;
            cacheEntry.valid = false;
            cacheEntry.width = 0;
            cacheEntry.height = 0;
            cacheEntry.rgba.clear();
        }
    }
    return &cacheEntry;
}

bool GrowlSharedRenderer::fillTextureViewFromEntry(
    const TextureCacheEntry* texture,
    vfx::runtime::growl_batches::TextureView& outView) {
    if (!texture || !texture->valid || texture->rgba.empty() ||
        texture->width <= 0 || texture->height <= 0) {
        return false;
    }
    outView.rgba = texture->rgba.data();
    outView.width = texture->width;
    outView.height = texture->height;
    return true;
}

bool GrowlSharedRenderer::fillTextureView(const GrowlWaveVFX::Config::DrawPass& pass,
                                          const GrowlWaveVFX::Config& config,
                                          const vfx::runtime::growl::TevState& tev,
                                          vfx::runtime::growl_batches::TextureView& outView) {
    if (vfx::runtime::growl::isLinePass(config, pass) || pass.texturePath.empty()) {
        return fillTextureViewFromEntry(ensureBackendTextureLoaded("", false), outView);
    }

    TextureCacheEntry* rawTexture = ensureBackendTextureLoaded(pass.texturePath, false);
    if (!rawTexture || !rawTexture->valid || rawTexture->rgba.empty()) return false;

    const bool quarterPass = vfx::runtime::growl::isQuarterRingPass(config, pass);
    const std::string bakedKey = vfx::runtime::growl::makeBakedTextureKey(pass, quarterPass);
    auto& baked = backendTextureByPath_[bakedKey];
    if (!baked.attemptedLoad) {
        baked.attemptedLoad = true;
        baked.valid = false;
        baked.width = rawTexture->width;
        baked.height = rawTexture->height;
        baked.rgba.clear();
        if (!vfx::runtime::growl::bakePassTextureRgba(
                pass,
                tev,
                quarterPass,
                rawTexture->rgba,
                baked.rgba)) {
            return false;
        }
        baked.valid = true;
    }

    return fillTextureViewFromEntry(&baked, outView);
}

} // namespace vfx::preview::growl
