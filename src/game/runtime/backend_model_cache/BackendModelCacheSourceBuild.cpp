#include "game/runtime/backend_model_cache/BackendModelCacheSourceBuild.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "engine/render/gltf/FastGLTFLoader.h"
#include "engine/render/gltf/ModelFastGltfLoaderHelpers.h"
#include "engine/render/gltf/ModelFastGltfMaterial.h"
#include "engine/render/gltf/ModelFastGltfSceneData.h"
#include "engine/render/gltf/ModelFastGltfTextures.h"

#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace game::runtime::backend_model::detail {

bool buildBackendCacheSourceData(const std::string& filepath,
                                 SourceCacheBuildData& outData,
                                 std::string* outError) {
    outData = SourceCacheBuildData{};
    auto fg = pac::fastgltf_loader::tryLoad(filepath);
    if (!fg.has_value()) {
        if (outError) *outError = "fastgltf parse failed for source model";
        return false;
    }

    const fastgltf::Asset& asset = fg->asset;
    fastgltf::DefaultBufferDataAdapter adapter{};
    pac::model_fastgltf::buildSceneData(asset,
                                        adapter,
                                        outData.nodesDefault,
                                        outData.nodeChildren,
                                        outData.nodeMesh,
                                        outData.nodeSkin,
                                        outData.sceneRoots,
                                        outData.skins,
                                        outData.animations);

    outData.vertices.reserve(20000);
    outData.indices.reserve(60000);
    outData.submeshes.reserve(64);

    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float minZ = std::numeric_limits<float>::max();
    float maxX = -minX;
    float maxY = -minY;
    float maxZ = -minZ;

    const bool dbgThisModel = pac::model_fastgltf::envTruthy("PAC_GLTF_DEBUG");

    for (std::size_t meshIdx = 0; meshIdx < asset.meshes.size(); ++meshIdx) {
        const auto& mesh = asset.meshes[meshIdx];
        for (const auto& p : mesh.primitives) {
            if (p.type != fastgltf::PrimitiveType::Triangles) continue;

            int materialIndex = -1;
            if (p.materialIndex.has_value()) {
                materialIndex = static_cast<int>(p.materialIndex.value());
            }

            auto itPos = p.findAttribute("POSITION");
            if (itPos == p.attributes.end()) continue;

            int requiredTexCoord = pac::model_fastgltf::requiredTexCoordForMaterial(asset, materialIndex);
            std::string uvAttr = "TEXCOORD_" + std::to_string(requiredTexCoord);
            auto itUv = p.findAttribute(uvAttr);
            if (itUv == p.attributes.end()) {
                requiredTexCoord = 0;
                itUv = p.findAttribute("TEXCOORD_0");
            }
            const bool hasUv = (itUv != p.attributes.end());

            const std::size_t posAcc = itPos->accessorIndex;
            const std::size_t uvAcc = hasUv ? itUv->accessorIndex : 0u;

            std::vector<glm::vec3> pos;
            std::vector<glm::vec2> uv;
            std::vector<glm::vec3> normals;
            pos.reserve(asset.accessors[posAcc].count);
            uv.reserve(hasUv ? asset.accessors[uvAcc].count : asset.accessors[posAcc].count);
            normals.reserve(asset.accessors[posAcc].count);

            fastgltf::iterateAccessorWithIndex<glm::vec3>(
                asset, asset.accessors[posAcc],
                [&](glm::vec3 v, std::size_t) { pos.push_back(v); }, adapter);

            if (hasUv) {
                fastgltf::iterateAccessorWithIndex<glm::vec2>(
                    asset, asset.accessors[uvAcc],
                    [&](glm::vec2 v, std::size_t) { uv.push_back(v); }, adapter);
            } else {
                glm::vec3 pMin(std::numeric_limits<float>::max());
                glm::vec3 pMax(-std::numeric_limits<float>::max());
                for (const auto& p3 : pos) {
                    pMin = glm::min(pMin, p3);
                    pMax = glm::max(pMax, p3);
                }
                const float r[3] = {pMax.x - pMin.x, pMax.y - pMin.y, pMax.z - pMin.z};
                int a = 0;
                if (r[1] > r[a]) a = 1;
                if (r[2] > r[a]) a = 2;
                int b = (a == 0) ? 1 : 0;
                for (int i = 0; i < 3; ++i) {
                    if (i == a) continue;
                    if (r[i] > r[b]) b = i;
                }
                const float minA = (a == 0) ? pMin.x : ((a == 1) ? pMin.y : pMin.z);
                const float minB = (b == 0) ? pMin.x : ((b == 1) ? pMin.y : pMin.z);
                const float denA = std::max(1e-6f, (a == 0) ? r[0] : ((a == 1) ? r[1] : r[2]));
                const float denB = std::max(1e-6f, (b == 0) ? r[0] : ((b == 1) ? r[1] : r[2]));
                uv.reserve(pos.size());
                for (const auto& p3 : pos) {
                    const float ca = (a == 0) ? p3.x : ((a == 1) ? p3.y : p3.z);
                    const float cb = (b == 0) ? p3.x : ((b == 1) ? p3.y : p3.z);
                    uv.emplace_back((ca - minA) / denA, (cb - minB) / denB);
                }
            }

            // ---- NORMAL ----
            bool hasExplicitNormals = false;
            normals.assign(pos.size(), glm::vec3(0.0f, 1.0f, 0.0f));
            auto itN = p.findAttribute("NORMAL");
            if (itN != p.attributes.end()) {
                normals.clear();
                const std::size_t normAcc = itN->accessorIndex;
                normals.reserve(asset.accessors[normAcc].count);
                fastgltf::iterateAccessorWithIndex<glm::vec3>(
                    asset, asset.accessors[normAcc],
                    [&](glm::vec3 v, std::size_t) { normals.push_back(v); }, adapter);
                if (normals.size() != pos.size()) {
                    normals.assign(pos.size(), glm::vec3(0.0f, 1.0f, 0.0f));
                } else {
                    hasExplicitNormals = true;
                }
            }

            std::vector<glm::vec4> color(pos.size(), glm::vec4(1.0f));
            auto itC = p.findAttribute("COLOR_0");
            if (itC != p.attributes.end()) {
                const std::size_t colAcc = itC->accessorIndex;
                const auto& acc = asset.accessors[colAcc];
                color.clear();
                color.reserve(acc.count);
                if (acc.type == fastgltf::AccessorType::Vec3) {
                    fastgltf::iterateAccessorWithIndex<glm::vec3>(
                        asset, acc,
                        [&](glm::vec3 v, std::size_t) { color.emplace_back(v.x, v.y, v.z, 1.0f); },
                        adapter);
                } else {
                    fastgltf::iterateAccessorWithIndex<glm::vec4>(
                        asset, acc,
                        [&](glm::vec4 v, std::size_t) { color.push_back(v); },
                        adapter);
                }
                if (color.size() != pos.size()) {
                    color.assign(pos.size(), glm::vec4(1.0f));
                }
            }

            auto applyKHRTextureTransform = [&](const fastgltf::TextureInfo* ti) {
                if (!ti || !ti->transform) return;
                const auto& tr = *ti->transform;
                glm::vec2 offset(tr.uvOffset[0], tr.uvOffset[1]);
                glm::vec2 scale(tr.uvScale[0], tr.uvScale[1]);
                const float rot = static_cast<float>(tr.rotation);
                const float c = std::cos(rot);
                const float s = std::sin(rot);
                for (auto& t : uv) {
                    glm::vec2 p2 = t;
                    p2 *= scale;
                    p2 = glm::vec2(c * p2.x - s * p2.y, s * p2.x + c * p2.y);
                    p2 += offset;
                    t = p2;
                }
            };

            const fastgltf::TextureInfo* baseTI = nullptr;
            if (materialIndex >= 0 && materialIndex < static_cast<int>(asset.materials.size())) {
                const auto& mat = asset.materials[static_cast<std::size_t>(materialIndex)];
                if (mat.pbrData.baseColorTexture.has_value()) {
                    const auto& ti = mat.pbrData.baseColorTexture.value();
                    if (static_cast<int>(ti.texCoordIndex) == requiredTexCoord) {
                        baseTI = &ti;
                    }
                }
            }
            applyKHRTextureTransform(baseTI);

            if (pos.empty() || uv.empty() || pos.size() != uv.size()) {
                continue;
            }

            std::vector<glm::u16vec4> joints;
            std::vector<glm::vec4> weights;
            auto itJ = p.findAttribute("JOINTS_0");
            auto itW = p.findAttribute("WEIGHTS_0");
            if (itJ != p.attributes.end() && itW != p.attributes.end()) {
                joints.reserve(asset.accessors[itJ->accessorIndex].count);
                weights.reserve(asset.accessors[itW->accessorIndex].count);
                fastgltf::iterateAccessorWithIndex<glm::u16vec4>(
                    asset, asset.accessors[itJ->accessorIndex],
                    [&](glm::u16vec4 v, std::size_t) { joints.push_back(v); }, adapter);
                fastgltf::iterateAccessorWithIndex<glm::vec4>(
                    asset, asset.accessors[itW->accessorIndex],
                    [&](glm::vec4 v, std::size_t) { weights.push_back(v); }, adapter);
                if (joints.size() != pos.size() || weights.size() != pos.size()) {
                    joints.clear();
                    weights.clear();
                }
            }

            std::vector<std::uint32_t> primIdxU32;
            if (p.indicesAccessor.has_value()) {
                const auto& idxAcc = asset.accessors[p.indicesAccessor.value()];
                primIdxU32.reserve(idxAcc.count);
                fastgltf::iterateAccessorWithIndex<std::uint32_t>(
                    asset, idxAcc,
                    [&](std::uint32_t v, std::size_t) { primIdxU32.push_back(v); }, adapter);
            }
            if (primIdxU32.empty()) {
                primIdxU32.resize(pos.size());
                for (std::size_t i = 0; i < pos.size(); ++i) primIdxU32[i] = static_cast<std::uint32_t>(i);
            }

            if (!hasExplicitNormals) {
                normals.assign(pos.size(), glm::vec3(0.0f));
                const std::size_t triCount = primIdxU32.size() / 3u;
                for (std::size_t triIdx = 0; triIdx < triCount; ++triIdx) {
                    const std::size_t i = triIdx * 3u;
                    const std::uint32_t i0 = primIdxU32[i + 0u];
                    const std::uint32_t i1 = primIdxU32[i + 1u];
                    const std::uint32_t i2 = primIdxU32[i + 2u];
                    if (i0 >= pos.size() || i1 >= pos.size() || i2 >= pos.size()) continue;
                    const glm::vec3 e1 = pos[i1] - pos[i0];
                    const glm::vec3 e2 = pos[i2] - pos[i0];
                    const glm::vec3 n = glm::cross(e1, e2);
                    const float lenSq = glm::dot(n, n);
                    if (lenSq <= 1e-12f) continue;
                    normals[i0] += n;
                    normals[i1] += n;
                    normals[i2] += n;
                }
                for (auto& n : normals) {
                    const float lenSq = glm::dot(n, n);
                    n = (lenSq > 1e-12f) ? glm::normalize(n) : glm::vec3(0.0f, 1.0f, 0.0f);
                }
            }

            const std::size_t baseVertex = outData.vertices.size();
            const std::size_t subIndexOffset = outData.indices.size();

            for (std::size_t i = 0; i < pos.size(); ++i) {
                pac_model_types::Vertex v{};
                v.px = pos[i].x; v.py = pos[i].y; v.pz = pos[i].z;
                v.u = uv[i].x; v.v = uv[i].y;
                v.nx = normals[i].x; v.ny = normals[i].y; v.nz = normals[i].z;
                v.j0 = v.j1 = v.j2 = v.j3 = 0u;
                v.w0 = 1.0f; v.w1 = v.w2 = v.w3 = 0.0f;
                v.r = color[i].r; v.g = color[i].g; v.b = color[i].b; v.a = color[i].a;
                if (!joints.empty() && !weights.empty()) {
                    auto j = joints[i];
                    auto w = weights[i];
                    float sum = w.x + w.y + w.z + w.w;
                    if (sum <= 0.0001f) {
                        w = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
                    } else {
                        w /= sum;
                    }
                    v.j0 = j.x; v.j1 = j.y; v.j2 = j.z; v.j3 = j.w;
                    v.w0 = w.x; v.w1 = w.y; v.w2 = w.z; v.w3 = w.w;
                }
                outData.vertices.push_back(v);
                minX = std::min(minX, v.px); minY = std::min(minY, v.py); minZ = std::min(minZ, v.pz);
                maxX = std::max(maxX, v.px); maxY = std::max(maxY, v.py); maxZ = std::max(maxZ, v.pz);
            }
            for (const std::uint32_t idx : primIdxU32) {
                outData.indices.push_back(static_cast<std::uint32_t>(baseVertex + idx));
            }

            int baseTexCoordUsed = 0;
            int normalTexCoordUsed = 0;
            int metallicRoughnessTexCoordUsed = 0;
            int occlusionTexCoordUsed = 0;
            int emissiveTexCoordUsed = 0;
            auto baseCPU = pac::model_fastgltf::decodeBaseColorTextureFast(
                asset, fg->baseDir, materialIndex, dbgThisModel, filepath, &baseTexCoordUsed);
            auto normalCPU = pac::model_fastgltf::decodeNormalTextureFast(
                asset, fg->baseDir, materialIndex, dbgThisModel, filepath, &normalTexCoordUsed);
            auto metallicRoughnessCPU = pac::model_fastgltf::decodeMetallicRoughnessTextureFast(
                asset, fg->baseDir, materialIndex, dbgThisModel, filepath, &metallicRoughnessTexCoordUsed);
            auto occlusionCPU = pac::model_fastgltf::decodeOcclusionTextureFast(
                asset, fg->baseDir, materialIndex, dbgThisModel, filepath, &occlusionTexCoordUsed);
            auto emissiveCPU = pac::model_fastgltf::decodeEmissiveTextureFast(
                asset, fg->baseDir, materialIndex, dbgThisModel, filepath, &emissiveTexCoordUsed);
            const pac::model_fastgltf::MaterialRenderInfo materialInfo =
                pac::model_fastgltf::resolveMaterialRenderInfo(asset, materialIndex, baseCPU, dbgThisModel);

            SourceSubmeshRecord sm{};
            sm.indexOffset = subIndexOffset;
            sm.indexCount = primIdxU32.size();
            sm.meshIndex = static_cast<int>(meshIdx);
            sm.emissiveFactor = materialInfo.emissiveFactor;
            sm.normalScale = materialInfo.normalScale;
            sm.metallicFactor = materialInfo.metallicFactor;
            sm.roughnessFactor = materialInfo.roughnessFactor;
            sm.occlusionStrength = materialInfo.occlusionStrength;
            sm.alphaMode = static_cast<std::uint8_t>(materialInfo.alphaMode);
            sm.alphaCutoff = materialInfo.alphaCutoff;
            sm.doubleSided = materialInfo.doubleSided;
            sm.baseTexture = std::move(baseCPU);
            sm.normalTexture = std::move(normalCPU);
            sm.metallicRoughnessTexture = std::move(metallicRoughnessCPU);
            sm.occlusionTexture = std::move(occlusionCPU);
            sm.emissiveTexture = std::move(emissiveCPU);
            outData.submeshes.push_back(std::move(sm));
        }
    }

    if (outData.vertices.empty() || outData.indices.empty()) {
        if (outError) *outError = "source model produced empty geometry";
        return false;
    }

    const float desiredHeight = 0.8f;
    const float ex = std::max(0.0f, maxX - minX);
    const float ey = std::max(0.0f, maxY - minY);
    const float ez = std::max(0.0f, maxZ - minZ);
    float denom = std::max(ex, std::max(ey, ez));
    if (std::abs(denom) < 1e-6f) denom = 1.0f;
    outData.modelScaleFactor = desiredHeight / denom;
    return true;
}

} // namespace game::runtime::backend_model::detail
