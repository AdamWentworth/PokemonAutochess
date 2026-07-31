#include "game/editor/PokemonPrefabPreview.h"

#include "engine/render/Camera3D.h"
#include "engine/render/OpenGLRenderBackend.h"
#include "game/PokemonInstance.h"
#include "game/config/GameDataDb.h"
#include "game/runtime/phlosion/PhlosionModelObject.h"
#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "game/runtime/render_prep/ProceduralPose.h"
#include "game/runtime/session/SessionRenderScratch.h"
#include "game/runtime/session/SessionTextureCache.h"
#include "game/runtime/shared/backend/SharedBackendPoseEval.h"
#include "game/runtime/shared/projected/core/SharedProjectedBodyPresentation.h"
#include "game/runtime/shared/projected/core/SharedProjectedDebugVfx.h"
#include "game/runtime/shared/projected/world_scene/SharedProjectedWorldSceneHelpers.h"
#include "game/runtime/shared/scene/SharedWorldScene.h"
#include "game/runtime/shared/world/SharedWorldContentSubmit.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace game::editor {

namespace {

using MeshData =
    game::runtime::render_model::MeshData;
using CachedTexture =
    game::runtime::render_model::CachedTextureRgba;

void clearTexturePixels(
    std::vector<CachedTexture>& textures) {
    for (auto& texture : textures) {
        texture.rgba.clear();
        texture.rgba.shrink_to_fit();
    }
}

void removeTextures(MeshData& mesh) {
    clearTexturePixels(mesh.submeshBaseTextures);
    clearTexturePixels(mesh.submeshNormalTextures);
    clearTexturePixels(
        mesh.submeshMetallicRoughnessTextures);
    clearTexturePixels(mesh.submeshOcclusionTextures);
    clearTexturePixels(mesh.submeshEmissiveTextures);
}

void makeGeometryOnly(MeshData& mesh) {
    removeTextures(mesh);
    const glm::vec4 neutral(0.20f, 0.24f, 0.30f, 1.0f);
    std::fill(
        mesh.submeshBaseColors.begin(),
        mesh.submeshBaseColors.end(),
        neutral);
    std::fill(
        mesh.triangleBaseColors.begin(),
        mesh.triangleBaseColors.end(),
        glm::vec3(neutral));
    std::fill(
        mesh.vertexBaseColors.begin(),
        mesh.vertexBaseColors.end(),
        glm::vec3(neutral));
    for (auto& vertex : mesh.vertices) {
        vertex.color = neutral;
    }
    std::fill(
        mesh.submeshEmissiveFactors.begin(),
        mesh.submeshEmissiveFactors.end(),
        glm::vec3(0.0f));
    std::fill(
        mesh.submeshMetallicFactor.begin(),
        mesh.submeshMetallicFactor.end(),
        0.0f);
    std::fill(
        mesh.submeshRoughnessFactor.begin(),
        mesh.submeshRoughnessFactor.end(),
        0.92f);
    // The fast indexed path reads its neutral preview tint from authored
    // vertex color, so keep that channel enabled for geometry-only mode.
    mesh.hasVertexColor = true;
    mesh.hasVertexBaseColor = false;
}

bool containsInsensitive(
    std::string value,
    std::string_view query) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(
                std::tolower(character));
        });
    std::string foldedQuery(query);
    std::transform(
        foldedQuery.begin(),
        foldedQuery.end(),
        foldedQuery.begin(),
        [](unsigned char character) {
            return static_cast<char>(
                std::tolower(character));
        });
    return value.find(foldedQuery) !=
           std::string::npos;
}

void hideInactiveAuxiliaryMeshes(MeshData& mesh) {
    std::vector<std::uint8_t> hiddenSubmeshes(
        mesh.submeshMeshIndex.size(),
        0u);
    for (std::size_t submeshIndex = 0u;
         submeshIndex < mesh.submeshMeshIndex.size() &&
         submeshIndex < mesh.submeshIndexCount.size();
         ++submeshIndex) {
        const int meshIndex =
            mesh.submeshMeshIndex[submeshIndex];
        if (meshIndex < 0 ||
            static_cast<std::size_t>(meshIndex) >=
                mesh.meshIndexToNode.size()) {
            continue;
        }
        const int nodeIndex =
            mesh.meshIndexToNode[
                static_cast<std::size_t>(meshIndex)];
        if (nodeIndex < 0 ||
            static_cast<std::size_t>(nodeIndex) >=
                mesh.nodeNames.size()) {
            continue;
        }
        const std::string& nodeName =
            mesh.nodeNames[
                static_cast<std::size_t>(nodeIndex)];
        if (containsInsensitive(
                nodeName,
                "_tuta_mesh")) {
            hiddenSubmeshes[submeshIndex] = 1u;
            mesh.submeshIndexCount[submeshIndex] = 0u;
        }
    }

    const std::size_t triangleCount =
        mesh.indices.size() / 3u;
    if (triangleCount == 0u ||
        mesh.triangleSubmesh.size() < triangleCount ||
        std::none_of(
            hiddenSubmeshes.begin(),
            hiddenSubmeshes.end(),
            [](std::uint8_t hidden) {
                return hidden != 0u;
            })) {
        return;
    }

    std::vector<std::uint32_t> indices;
    std::vector<std::uint16_t> triangleSubmesh;
    std::vector<glm::vec3> triangleBaseColors;
    std::vector<float> triangleOpacity;
    std::vector<std::uint8_t> triangleDoubleSided;
    std::vector<int> triangleNodeIndex;
    std::vector<int> triangleSkinIndex;

    indices.reserve(mesh.indices.size());
    triangleSubmesh.reserve(triangleCount);
    triangleBaseColors.reserve(mesh.triangleBaseColors.size());
    triangleOpacity.reserve(mesh.triangleOpacity.size());
    triangleDoubleSided.reserve(mesh.triangleDoubleSided.size());
    triangleNodeIndex.reserve(mesh.triangleNodeIndex.size());
    triangleSkinIndex.reserve(mesh.triangleSkinIndex.size());

    const auto copyTriangleValue =
        [triangleCount](
            const auto& source,
            auto& destination,
            std::size_t triangleIndex) {
            if (source.size() == triangleCount) {
                destination.push_back(source[triangleIndex]);
            }
        };

    for (std::size_t triangleIndex = 0u;
         triangleIndex < triangleCount;
         ++triangleIndex) {
        const std::size_t submeshIndex =
            mesh.triangleSubmesh[triangleIndex];
        if (submeshIndex < hiddenSubmeshes.size() &&
            hiddenSubmeshes[submeshIndex] != 0u) {
            continue;
        }

        const std::size_t indexOffset =
            triangleIndex * 3u;
        indices.insert(
            indices.end(),
            mesh.indices.begin() +
                static_cast<std::ptrdiff_t>(indexOffset),
            mesh.indices.begin() +
                static_cast<std::ptrdiff_t>(
                    indexOffset + 3u));
        triangleSubmesh.push_back(
            mesh.triangleSubmesh[triangleIndex]);
        copyTriangleValue(
            mesh.triangleBaseColors,
            triangleBaseColors,
            triangleIndex);
        copyTriangleValue(
            mesh.triangleOpacity,
            triangleOpacity,
            triangleIndex);
        copyTriangleValue(
            mesh.triangleDoubleSided,
            triangleDoubleSided,
            triangleIndex);
        copyTriangleValue(
            mesh.triangleNodeIndex,
            triangleNodeIndex,
            triangleIndex);
        copyTriangleValue(
            mesh.triangleSkinIndex,
            triangleSkinIndex,
            triangleIndex);
    }

    mesh.indices = std::move(indices);
    mesh.triangleSubmesh =
        std::move(triangleSubmesh);
    if (mesh.triangleBaseColors.size() == triangleCount) {
        mesh.triangleBaseColors =
            std::move(triangleBaseColors);
    }
    if (mesh.triangleOpacity.size() == triangleCount) {
        mesh.triangleOpacity =
            std::move(triangleOpacity);
    }
    if (mesh.triangleDoubleSided.size() == triangleCount) {
        mesh.triangleDoubleSided =
            std::move(triangleDoubleSided);
    }
    if (mesh.triangleNodeIndex.size() == triangleCount) {
        mesh.triangleNodeIndex =
            std::move(triangleNodeIndex);
    }
    if (mesh.triangleSkinIndex.size() == triangleCount) {
        mesh.triangleSkinIndex =
            std::move(triangleSkinIndex);
    }

    std::fill(
        mesh.submeshIndexOffset.begin(),
        mesh.submeshIndexOffset.end(),
        0u);
    std::fill(
        mesh.submeshIndexCount.begin(),
        mesh.submeshIndexCount.end(),
        0u);
    for (std::size_t triangleIndex = 0u;
         triangleIndex < mesh.triangleSubmesh.size();
         ++triangleIndex) {
        const std::size_t submeshIndex =
            mesh.triangleSubmesh[triangleIndex];
        if (submeshIndex >=
            mesh.submeshIndexCount.size()) {
            continue;
        }
        if (mesh.submeshIndexCount[submeshIndex] == 0u) {
            mesh.submeshIndexOffset[submeshIndex] =
                static_cast<std::uint32_t>(
                    triangleIndex * 3u);
        }
        mesh.submeshIndexCount[submeshIndex] += 3u;
    }

    glm::vec3 boundsMin(
        std::numeric_limits<float>::max());
    glm::vec3 boundsMax(
        std::numeric_limits<float>::lowest());
    bool hasReferencedVertex = false;
    for (const std::uint32_t vertexIndex : mesh.indices) {
        if (vertexIndex >= mesh.vertices.size()) {
            continue;
        }
        const glm::vec3& position =
            mesh.vertices[vertexIndex].position;
        boundsMin = glm::min(boundsMin, position);
        boundsMax = glm::max(boundsMax, position);
        hasReferencedVertex = true;
    }
    if (hasReferencedVertex) {
        mesh.boundsMin = boundsMin;
        mesh.boundsMax = boundsMax;
    }
}

std::size_t textureCount(const MeshData& mesh) {
    const auto countSet =
        [](const std::vector<CachedTexture>& textures) {
            return static_cast<std::size_t>(
                std::count_if(
                    textures.begin(),
                    textures.end(),
                    [](const CachedTexture& texture) {
                        return texture.hasPixels();
                    }));
        };
    return countSet(mesh.submeshBaseTextures) +
           countSet(mesh.submeshNormalTextures) +
           countSet(
               mesh.submeshMetallicRoughnessTextures) +
           countSet(mesh.submeshOcclusionTextures) +
           countSet(mesh.submeshEmissiveTextures);
}

std::size_t boneCount(const MeshData& mesh) {
    std::unordered_set<int> joints;
    for (const auto& skin : mesh.skins) {
        for (const int joint : skin.joints) {
            if (joint >= 0) {
                joints.insert(joint);
            }
        }
    }
    return joints.size();
}

float previewModelScale(const MeshData& mesh) {
    return std::max(0.01f, mesh.modelScaleFactor);
}

glm::mat4 previewModelMatrix(const MeshData& mesh) {
    const float scaleValue = previewModelScale(mesh);
    const float floorY =
        0.0025f -
        mesh.boundsMin.y * scaleValue;
    return glm::translate(
               glm::mat4(1.0f),
               glm::vec3(0.0f, floorY, 0.0f)) *
           glm::scale(
               glm::mat4(1.0f),
               glm::vec3(scaleValue));
}

std::string displayNameFromPath(
    const std::filesystem::path& path) {
    std::string value = path.stem().string();
    std::replace(value.begin(), value.end(), '_', ' ');
    return value;
}

int previewUnitId(std::string_view assetId) {
    std::uint32_t hash = 2166136261u;
    for (const unsigned char character : assetId) {
        hash ^= character;
        hash *= 16777619u;
    }
    return static_cast<int>(
        (hash & 0x3fffffffu) + 1u);
}

} // namespace

struct PokemonPrefabPreview::Impl {
    std::string assetId;
    std::string assetPath;
    std::string status;
    std::string displayName;
    MeshData texturedMesh;
    MeshData materialMesh;
    MeshData geometryMesh;
    GameDataDb dataDb;
    game::runtime::session_render_scratch::
        RenderScratch scratch;
    game::runtime::session_texture_cache::
        TextureCache textureCache;
    std::vector<
        game::runtime::shared_projected_scene::DepthTri>
        depthTriangles;
    std::vector<
        game::runtime::shared_projected_scene::DepthWorldTri>
        depthWorldTriangles;
    std::vector<IRenderBackend::DebugTriangle>
        gridTriangles;
    std::vector<IRenderBackend::WorldTriangle>
        gridWorldTriangles;
    std::vector<IRenderBackend::DebugLine>
        gridLines;
    engine::editor::EditorProjectAssetPreviewOptions
        options;
    std::unique_ptr<OpenGLRenderBackend> renderer;
    int unitId = 1;
    int rendererWidth = 0;
    int rendererHeight = 0;
    float animationTime = 0.0f;
    float boundsRadius = 1.0f;
    float boundsCenterY = 0.5f;
    std::size_t materialCount = 0u;
    std::size_t cookedTextureCount = 0u;
    std::size_t cookedBoneCount = 0u;
    bool ready = false;

    IRenderBackend& previewRenderer(
        int width,
        int height) {
        if (!renderer) {
            renderer =
                std::make_unique<OpenGLRenderBackend>();
            renderer->prewarmWorldRenderAssets();
        }
        if (rendererWidth != width ||
            rendererHeight != height) {
            rendererWidth = width;
            rendererHeight = height;
            renderer->onResize(width, height);
        }
        return *renderer;
    }

    const MeshData& displayMesh() const {
        if (!options.showMaterials) {
            return geometryMesh;
        }
        if (!options.showTextures) {
            return materialMesh;
        }
        return texturedMesh;
    }

    int resolvedAnimationIndex() const {
        if (options.animationIndex < 0 ||
            static_cast<std::size_t>(
                options.animationIndex) >=
                texturedMesh.animations.size()) {
            return -1;
        }
        return options.animationIndex;
    }

    void appendGrid(
        game::runtime::shared_projected_debug::
            ProjectedDebugVfxBuilder& debug) const {
        const float extent =
            std::max(1.25f, boundsRadius * 1.55f);
        constexpr int divisions = 8;
        for (int index = -divisions;
             index <= divisions;
             ++index) {
            const float coordinate =
                extent *
                static_cast<float>(index) /
                static_cast<float>(divisions);
            const bool axis = index == 0;
            const float alpha = axis ? 0.52f : 0.18f;
            debug.appendProjectedLine(
                glm::vec3(-extent, 0.0f, coordinate),
                glm::vec3(extent, 0.0f, coordinate),
                axis ? 0.28f : 0.42f,
                axis ? 0.78f : 0.48f,
                axis ? 0.58f : 0.52f,
                alpha,
                axis ? 1.5f : 1.0f);
            debug.appendProjectedLine(
                glm::vec3(coordinate, 0.0f, -extent),
                glm::vec3(coordinate, 0.0f, extent),
                axis ? 0.28f : 0.42f,
                axis ? 0.58f : 0.48f,
                axis ? 0.90f : 0.52f,
                alpha,
                axis ? 1.5f : 1.0f);
        }
    }

    void appendSkeleton(
        const game::runtime::shared_backend_pose::
            PoseEval& pose,
        game::runtime::shared_projected_debug::
            ProjectedDebugVfxBuilder& debug) const {
        if (!options.showSkeleton ||
            pose.nodeGlobals.empty()) {
            return;
        }
        std::unordered_set<int> joints;
        for (const auto& skin : texturedMesh.skins) {
            for (const int joint : skin.joints) {
                if (joint >= 0) {
                    joints.insert(joint);
                }
            }
        }
        const glm::mat4 model =
            previewModelMatrix(texturedMesh);
        for (const int joint : joints) {
            if (joint < 0 ||
                static_cast<std::size_t>(joint) >=
                    pose.nodeGlobals.size() ||
                static_cast<std::size_t>(joint) >=
                    texturedMesh.nodeParent.size()) {
                continue;
            }
            const int parent =
                texturedMesh.nodeParent[
                    static_cast<std::size_t>(joint)];
            if (parent < 0 ||
                !joints.contains(parent) ||
                static_cast<std::size_t>(parent) >=
                    pose.nodeGlobals.size()) {
                continue;
            }
            const glm::vec3 child =
                glm::vec3(
                    model *
                    pose.nodeGlobals[
                        static_cast<std::size_t>(joint)] *
                    glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            const glm::vec3 parentPosition =
                glm::vec3(
                    model *
                    pose.nodeGlobals[
                        static_cast<std::size_t>(parent)] *
                    glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            debug.appendProjectedLine(
                parentPosition,
                child,
                0.22f,
                0.95f,
                0.58f,
                0.95f,
                2.0f);
        }
    }

    void appendWireframe(
        const MeshData& mesh,
        const game::runtime::shared_backend_pose::
            PoseEval& pose,
        game::runtime::shared_projected_debug::
            ProjectedDebugVfxBuilder& debug) const {
        if (!options.showWireframe ||
            mesh.indices.empty()) {
            return;
        }

        const std::vector<glm::mat4>& nodeGlobals =
            pose.nodeGlobals.empty()
                ? mesh.bindNodeGlobals
                : pose.nodeGlobals;
        const glm::mat4 model =
            previewModelMatrix(mesh);
        const auto resolveTriangleNode =
            [&mesh](std::size_t triangleIndex) {
                if (triangleIndex <
                    mesh.triangleNodeIndex.size()) {
                    const int node =
                        mesh.triangleNodeIndex[
                            triangleIndex];
                    if (node >= 0) {
                        return node;
                    }
                }
                if (triangleIndex >=
                    mesh.triangleSubmesh.size()) {
                    return -1;
                }
                const std::size_t submesh =
                    mesh.triangleSubmesh[
                        triangleIndex];
                if (submesh >=
                    mesh.submeshMeshIndex.size()) {
                    return -1;
                }
                const int meshIndex =
                    mesh.submeshMeshIndex[submesh];
                if (meshIndex < 0 ||
                    static_cast<std::size_t>(meshIndex) >=
                        mesh.meshIndexToNode.size()) {
                    return -1;
                }
                return mesh.meshIndexToNode[
                    static_cast<std::size_t>(meshIndex)];
            };
        const auto transformVertex =
            [&mesh, &nodeGlobals, &model](
                const game::runtime::render_model::
                    MeshVertex& vertex,
                int triangleNode,
                int skinIndex) {
                const glm::vec4 local(
                    vertex.position,
                    1.0f);
                glm::vec4 posed(0.0f);
                float posedWeight = 0.0f;
                if (skinIndex >= 0 &&
                    static_cast<std::size_t>(skinIndex) <
                        mesh.skins.size()) {
                    const auto& skin =
                        mesh.skins[
                            static_cast<std::size_t>(
                                skinIndex)];
                    const std::uint16_t jointSlots[4] = {
                        vertex.j0,
                        vertex.j1,
                        vertex.j2,
                        vertex.j3};
                    const float weights[4] = {
                        vertex.w0,
                        vertex.w1,
                        vertex.w2,
                        vertex.w3};
                    for (std::size_t influence = 0u;
                         influence < 4u;
                         ++influence) {
                        const float weight =
                            weights[influence];
                        const std::size_t jointSlot =
                            jointSlots[influence];
                        if (weight <= 0.00001f ||
                            jointSlot >=
                                skin.joints.size()) {
                            continue;
                        }
                        const int jointNode =
                            skin.joints[jointSlot];
                        if (jointNode < 0 ||
                            static_cast<std::size_t>(
                                jointNode) >=
                                nodeGlobals.size()) {
                            continue;
                        }
                        const glm::mat4 inverseBind =
                            jointSlot <
                                    skin.inverseBind.size()
                                ? skin.inverseBind[
                                      jointSlot]
                                : glm::mat4(1.0f);
                        posed +=
                            weight *
                            (nodeGlobals[
                                 static_cast<std::size_t>(
                                     jointNode)] *
                             inverseBind *
                             local);
                        posedWeight += weight;
                    }
                }
                if (posedWeight > 0.00001f) {
                    posed /= posedWeight;
                } else if (
                    triangleNode >= 0 &&
                    static_cast<std::size_t>(
                        triangleNode) <
                        nodeGlobals.size()) {
                    posed =
                        nodeGlobals[
                            static_cast<std::size_t>(
                                triangleNode)] *
                        local;
                } else {
                    posed = local;
                }
                return glm::vec3(model * posed);
            };

        const std::size_t triangleCount =
            mesh.indices.size() / 3u;
        for (std::size_t triangleIndex = 0u;
             triangleIndex < triangleCount;
             ++triangleIndex) {
            const std::size_t base =
                triangleIndex * 3u;
            const std::uint32_t indices[3] = {
                mesh.indices[base],
                mesh.indices[base + 1u],
                mesh.indices[base + 2u]};
            if (indices[0] >= mesh.vertices.size() ||
                indices[1] >= mesh.vertices.size() ||
                indices[2] >= mesh.vertices.size()) {
                continue;
            }
            const int triangleNode =
                resolveTriangleNode(triangleIndex);
            const int skinIndex =
                triangleIndex <
                        mesh.triangleSkinIndex.size()
                    ? mesh.triangleSkinIndex[
                          triangleIndex]
                    : -1;
            const glm::vec3 points[3] = {
                transformVertex(
                    mesh.vertices[indices[0]],
                    triangleNode,
                    skinIndex),
                transformVertex(
                    mesh.vertices[indices[1]],
                    triangleNode,
                    skinIndex),
                transformVertex(
                    mesh.vertices[indices[2]],
                    triangleNode,
                    skinIndex)};
            for (std::size_t edge = 0u;
                 edge < 3u;
                 ++edge) {
                debug.appendProjectedLine(
                    points[edge],
                    points[(edge + 1u) % 3u],
                    0.12f,
                    0.95f,
                    0.72f,
                    0.72f,
                    1.0f);
            }
        }
    }

    void submit(
        IRenderBackend& backend,
        const Camera3D& camera,
        int surfaceWidth,
        int surfaceHeight) {
        const glm::mat4 viewProjection =
            camera.getProjectionMatrix() *
            camera.getViewMatrix();
        const auto worldSceneView =
            game::runtime::shared_world_scene::
                buildWorldSceneView(
                    scratch.worldSceneRegistry,
                    glm::value_ptr(viewProjection),
                    surfaceWidth,
                    surfaceHeight,
                    glm::value_ptr(camera.getPosition()),
                    glm::value_ptr(camera.getDirection()),
                    glm::value_ptr(camera.getTarget()));
        std::vector<IRenderBackend::WorldTriangle>
            noWorldTriangles;
        if (!gridLines.empty()) {
            backend.drawDebugLines(
                gridLines.data(),
                gridLines.size(),
                surfaceWidth,
                surfaceHeight);
        }
        game::runtime::shared_world_content_submit::
            submitOpaqueAndIndexedWorldContent(
                {
                    .renderer = &backend,
                    .camera = &camera,
                    .drawableW = surfaceWidth,
                    .drawableH = surfaceHeight,
                    .hasWorldViewProj = true,
                    .supportsWorldTriangles3D =
                        backend.supportsWorldTriangles3D(),
                    .supportsWorldIndexedMeshes =
                        backend.supportsWorldIndexedMeshes(),
                    .worldViewProj =
                        glm::value_ptr(viewProjection),
                    .worldBackgroundQuads =
                        &scratch.worldBackgroundQuads,
                    .world3DTriangles =
                        options.showMesh
                            ? &scratch.world3DTriangles
                            : &noWorldTriangles,
                    .worldSceneView = &worldSceneView,
                    .worldSceneFrame =
                        &scratch.worldSceneFrame,
                    .worldIndexedBatches =
                        &scratch.worldIndexedBatches,
                });
        if (!scratch.worldTriangles.empty()) {
            backend.drawDebugTriangles(
                scratch.worldTriangles.data(),
                scratch.worldTriangles.size(),
                surfaceWidth,
                surfaceHeight);
        }
        if (!scratch.lines.empty()) {
            backend.drawDebugLines(
                scratch.lines.data(),
                scratch.lines.size(),
                surfaceWidth,
                surfaceHeight);
        }
    }
};

PokemonPrefabPreview::PokemonPrefabPreview()
    : impl_(std::make_unique<Impl>()) {}

PokemonPrefabPreview::~PokemonPrefabPreview() = default;

bool PokemonPrefabPreview::select(
    const char* assetId,
    const char* phloPath,
    std::string* outError) {
    impl_->ready = false;
    impl_->assetId = assetId ? assetId : "";
    impl_->assetPath = phloPath ? phloPath : "";
    impl_->unitId = previewUnitId(impl_->assetId);
    impl_->status.clear();
    impl_->animationTime = 0.0f;
    impl_->options = {};
    // World-scene render objects are cached against the registry address.
    // Release those entries before replacing the scratch storage so a newly
    // selected prefab cannot inherit stale handles from the previous asset.
    game::runtime::session_render_scratch::
        resetSceneCaches(impl_->scratch);
    impl_->scratch =
        game::runtime::session_render_scratch::
            RenderScratch{};
    impl_->textureCache.clear();
    if (impl_->assetId.empty() ||
        impl_->assetPath.empty()) {
        if (outError) {
            *outError =
                "Prefab preview requires an asset id and .phlo path.";
        }
        return false;
    }
    std::string error;
    if (!game::runtime::phlosion::loadModelObject(
            impl_->assetPath,
            impl_->texturedMesh,
            &error)) {
        if (outError) {
            *outError =
                "Could not decode cooked prefab: " + error;
        }
        return false;
    }
    hideInactiveAuxiliaryMeshes(impl_->texturedMesh);
    const std::string cacheIdentityPrefix =
        "editor_prefab:" + impl_->assetId;
    impl_->texturedMesh.assetCacheIdentity =
        cacheIdentityPrefix + ":textured";
    impl_->materialMesh = impl_->texturedMesh;
    impl_->materialMesh.assetCacheIdentity =
        cacheIdentityPrefix + ":material";
    removeTextures(impl_->materialMesh);
    impl_->geometryMesh = impl_->materialMesh;
    impl_->geometryMesh.assetCacheIdentity =
        cacheIdentityPrefix + ":geometry";
    makeGeometryOnly(impl_->geometryMesh);
    impl_->displayName =
        displayNameFromPath(impl_->assetPath);
    impl_->materialCount = std::max({
        impl_->texturedMesh.submeshBaseColors.size(),
        impl_->texturedMesh.submeshIndexCount.size(),
        impl_->texturedMesh.submeshBaseTextures.size()});
    impl_->cookedTextureCount =
        textureCount(impl_->texturedMesh);
    impl_->cookedBoneCount =
        boneCount(impl_->texturedMesh);
    if (!impl_->texturedMesh.animations.empty()) {
        const auto idle = std::find_if(
            impl_->texturedMesh.animations.begin(),
            impl_->texturedMesh.animations.end(),
            [](const auto& animation) {
                std::string name = animation.name;
                std::transform(
                    name.begin(),
                    name.end(),
                    name.begin(),
                    [](unsigned char character) {
                        return static_cast<char>(
                            std::tolower(character));
                    });
                return name.find("wait") !=
                           std::string::npos ||
                       name.find("idle") !=
                           std::string::npos;
            });
        impl_->options.animationIndex =
            idle == impl_->texturedMesh.animations.end()
                ? 0
                : static_cast<int>(
                      std::distance(
                          impl_->texturedMesh
                              .animations.begin(),
                          idle));
    }
    const float scale =
        previewModelScale(impl_->texturedMesh);
    const glm::vec3 extent =
        (impl_->texturedMesh.boundsMax -
         impl_->texturedMesh.boundsMin) *
        scale;
    impl_->boundsRadius =
        std::max(0.45f, glm::length(extent) * 0.5f);
    impl_->boundsCenterY =
        std::max(0.1f, extent.y * 0.5f + 0.0025f);
    impl_->status =
        "Rendered directly from the selected .phlo and its cooked dependencies.";
    impl_->ready = true;
    if (outError) {
        outError->clear();
    }
    return true;
}

engine::editor::EditorProjectAssetPreviewInfo
PokemonPrefabPreview::info() const noexcept {
    return {
        .assetId = impl_->assetId.c_str(),
        .status = impl_->status.c_str(),
        .vertexCount = static_cast<std::uint32_t>(
            impl_->texturedMesh.vertices.size()),
        .triangleCount = static_cast<std::uint32_t>(
            impl_->texturedMesh.indices.size() / 3u),
        .materialCount = static_cast<std::uint32_t>(
            impl_->materialCount),
        .textureCount = static_cast<std::uint32_t>(
            impl_->cookedTextureCount),
        .boneCount = static_cast<std::uint32_t>(
            impl_->cookedBoneCount),
        .animationCount =
            impl_->texturedMesh.animations.size(),
        .boundsRadius = impl_->boundsRadius,
        .boundsCenterY = impl_->boundsCenterY,
        .ready = impl_->ready,
    };
}

engine::editor::EditorProjectAssetAnimation
PokemonPrefabPreview::animation(
    std::size_t index) const noexcept {
    if (index >= impl_->texturedMesh.animations.size()) {
        return {};
    }
    const auto& animation =
        impl_->texturedMesh.animations[index];
    return {
        .name = animation.name.c_str(),
        .durationSeconds = animation.durationSec,
    };
}

void PokemonPrefabPreview::setOptions(
    const engine::editor::
        EditorProjectAssetPreviewOptions& options) {
    const int previousAnimation =
        impl_->options.animationIndex;
    impl_->options = options;
    impl_->options.playbackSpeed =
        std::clamp(
            impl_->options.playbackSpeed,
            0.0f,
            4.0f);
    if (impl_->options.animationIndex < -1 ||
        static_cast<std::size_t>(
            std::max(0, impl_->options.animationIndex)) >=
            impl_->texturedMesh.animations.size()) {
        impl_->options.animationIndex = -1;
    }
    if (previousAnimation !=
        impl_->options.animationIndex) {
        impl_->animationTime = 0.0f;
    }
}

void PokemonPrefabPreview::update(float deltaSeconds) {
    if (!impl_->ready ||
        !impl_->options.animationPlaying) {
        return;
    }
    const int animationIndex =
        impl_->resolvedAnimationIndex();
    if (animationIndex < 0) {
        impl_->animationTime = 0.0f;
        return;
    }
    const float duration =
        impl_->texturedMesh.animations[
            static_cast<std::size_t>(animationIndex)]
            .durationSec;
    if (duration <= 0.0001f) {
        impl_->animationTime = 0.0f;
        return;
    }
    impl_->animationTime = std::fmod(
        impl_->animationTime +
            std::max(0.0f, deltaSeconds) *
                impl_->options.playbackSpeed,
        duration);
}

void PokemonPrefabPreview::render(
    const engine::editor::
        EditorProjectRenderContext& context) {
    if (!impl_->ready ||
        !context.cameraWorldPosition3 ||
        !context.cameraTarget3) {
        return;
    }
    const int width = std::max(1, context.surfaceWidth);
    const int height = std::max(1, context.surfaceHeight);
    Camera3D camera(
        36.0f,
        static_cast<float>(width) /
            static_cast<float>(height),
        0.01f,
        100.0f);
    camera.setPosition(
        glm::make_vec3(
            context.cameraWorldPosition3));
    camera.lookAt(
        glm::make_vec3(context.cameraTarget3));
    IRenderBackend& renderer =
        impl_->previewRenderer(width, height);
    renderer.beginWorldSceneColorPass(width, height);

    auto& scratch = impl_->scratch;
    game::runtime::session_render_scratch::
        ensureCapacity(scratch);
    game::runtime::session_render_scratch::
        beginFrame(
            scratch,
            true,
            &renderer);
    impl_->depthTriangles.clear();
    impl_->depthWorldTriangles.clear();

    const glm::mat4 view = camera.getViewMatrix();
    const glm::mat4 projection =
        camera.getProjectionMatrix();
    const glm::vec4 viewport(
        0.0f,
        0.0f,
        static_cast<float>(width),
        static_cast<float>(height));
    impl_->gridTriangles.clear();
    impl_->gridWorldTriangles.clear();
    impl_->gridLines.clear();
    game::runtime::shared_projected_debug::
        ProjectedDebugVfxBuilder gridDebug(
            true,
            view,
            projection,
            height,
            viewport,
            impl_->gridTriangles,
            impl_->gridWorldTriangles,
            impl_->gridLines);
    impl_->appendGrid(gridDebug);
    game::runtime::shared_projected_debug::
        ProjectedDebugVfxBuilder debug(
            true,
            view,
            projection,
            height,
            viewport,
            scratch.worldTriangles,
            scratch.world3DTriangles,
            scratch.lines);

    const MeshData& mesh = impl_->displayMesh();
    const int animationIndex =
        impl_->resolvedAnimationIndex();
    auto scenePose =
        game::runtime::shared_backend_pose::
            evaluateScenePoseForResolvedClipTime(
                mesh,
                animationIndex,
                impl_->animationTime,
                true,
                true);
    impl_->appendSkeleton(scenePose, debug);
    impl_->appendWireframe(mesh, scenePose, debug);

    if (impl_->options.showMesh) {
        PokemonInstance unit{};
        unit.id = impl_->unitId;
        unit.name = impl_->displayName;
        unit.alive = true;
        unit.position = glm::vec3(0.0f);
        unit.rotation = glm::vec3(0.0f);
        unit.speciesScale = 1.0f;
        unit.modelScaleCorrection = 1.0f;
        unit.visualScale = 1.0f;
        unit.captureScale = 1.0f;
        unit.activeAnimIndex = animationIndex;
        unit.animTimeSec = impl_->animationTime;
        const auto proceduralPose =
            game::runtime::render_prep_pose::
                computeProceduralPose(unit, 1.0f);
        IRenderBackend::DebugQuad tint{};
        tint.r = 1.0f;
        tint.g = 1.0f;
        tint.b = 1.0f;
        tint.a = 1.0f;
        std::size_t triangleBudget =
            std::max<std::size_t>(
                1u,
                mesh.indices.size() / 3u);
        game::runtime::
            shared_projected_unit_models::PerfBreakdown
                perf{};
        game::runtime::
            shared_projected_body_presentation::
                buildProjectedBodyPresentation(
                    game::runtime::
                        shared_projected_unit_models::Args{
                            .renderer =
                                &renderer,
                            .dataDb = &impl_->dataDb,
                            .unit = &unit,
                            .pose = &proceduralPose,
                            .meshForUnit = &mesh,
                            .scenePose = &scenePose,
                            .backendId =
                                renderer.backendId(),
                            .scenePoseReady = true,
                            .enableClipSkinning = true,
                            .enableGpuClipSkinning = false,
                            .tint = &tint,
                            .worldCellSize = 1.0f,
                            .boardSurfaceY = 0.0f,
                            .unitSize = 96.0f,
                            .animPitch = 0.0f,
                            .animYaw = 0.0f,
                            .animRoll = 0.0f,
                            .attackPulse = 1.0f,
                            .materialTimeSec =
                                impl_->animationTime,
                            .renderVisualScale = 1.0f,
                            .renderCaptureScale = 1.0f,
                            .captureVisualTintStrength =
                                0.0f,
                            .modelFadeAlpha = 1.0f,
                            .captureTintColor =
                                glm::vec3(1.0f),
                            .proxyCenter =
                                glm::vec3(0.0f),
                            .cameraWorldPos =
                                camera.getPosition(),
                            .supportsWorldTriangles3D =
                                renderer.
                                    supportsWorldTriangles3D(),
                            .supportsWorldIndexedMeshes =
                                renderer.
                                    supportsWorldIndexedMeshes(),
                            .characterInkingEnabled = false,
                            .graphicsQuality = 3,
                            .projectedDebug = &debug,
                            .projectedRenderItems =
                                &scratch
                                     .projectedRenderItems,
                            .worldSceneRegistry =
                                &scratch
                                     .worldSceneRegistry,
                            .worldSceneFrame =
                                &scratch.worldSceneFrame,
                            .sharedTailFireAnchors =
                                &scratch
                                     .sharedTailFireAnchors,
                            .worldIndexedBatches =
                                &scratch
                                     .worldIndexedBatches,
                            .backendTextureByPath =
                                &impl_->textureCache,
                            .modelDepthTris =
                                &impl_->depthTriangles,
                            .modelDepthWorldTris =
                                &impl_
                                     ->depthWorldTriangles,
                            .remainingModelTrianglesBudget =
                                &triangleBudget,
                            .world3DTriangles =
                                &scratch
                                     .world3DTriangles,
                            .ensureBackendTextureLoaded =
                                [&](const std::string&
                                        texturePath,
                                    bool flipVertical) {
                                    return game::runtime::
                                        session_texture_cache::
                                            ensureTextureLoaded(
                                                impl_
                                                    ->textureCache,
                                                texturePath,
                                                flipVertical);
                                },
                            .backendModelTriangleLimit =
                                [&]() {
                                    return std::max<
                                        std::size_t>(
                                        1u,
                                        mesh.indices
                                                .size() /
                                            3u);
                                },
                            .backendModelFullMeshEnabled =
                                []() { return true; },
                            .backendModelFastTexturedPathEnabled =
                                []() { return true; },
                            .backendModelBackfaceCullingEnabled =
                                []() { return true; },
                            .perfBreakdown = &perf,
                        });
        game::runtime::shared_projected_scene::
            flushModelDepthBuffers(
                impl_->depthTriangles,
                impl_->depthWorldTriangles,
                scratch.worldTriangles,
                scratch.world3DTriangles);
    }
    impl_->submit(
        renderer,
        camera,
        width,
        height);
    renderer.endWorldSceneColorPass();
}

} // namespace game::editor
