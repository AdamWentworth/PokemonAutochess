#include "game/runtime/shared/capture/SharedCaptureD3d12FastPath.h"

#include "game/GameWorld.h"
#include "game/runtime/shared/capture/SharedCapturePresentation.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/gtc/type_ptr.hpp>

namespace game::runtime::shared_capture_d3d12_fast {

Result tryAppend(
    IRenderBackend& renderer,
    bool hasWorldViewProj,
    const float* worldViewProj,
    int drawableW,
    int drawableH,
    const runtime::render_model::MeshData& mesh,
    const std::vector<GameWorld::CaptureAttemptRenderSnapshot>& captureSnaps,
    bool d3d12CapturePrewarmRequested,
    bool treatPokeballAsUntextured,
    bool enableNodeChunkPath,
    const std::function<shared_backend_pose::PoseEval(int animIndex, float animTimeSec)>& evaluateScenePoseForClipTime) {
    Result result;
    const char* backendId = renderer.backendId();
    if (!backendId || std::string(backendId) != "d3d12") return result;
    if (!hasWorldViewProj || !worldViewProj) return result;
    result.handled = true;
    (void)treatPokeballAsUntextured;
    (void)enableNodeChunkPath;

    struct D3d12CaptureSubmeshCache {
        int nodeIndex = -1;
        std::uint8_t alphaMode = 0u;
        float alphaCutoff = 0.5f;
        std::vector<IRenderBackend::WorldMeshVertex> localVertices;
        std::vector<std::uint32_t> localIndices;
        std::string geomKey;
    };
    struct D3d12CaptureFastCache {
        const runtime::render_model::MeshData* sourceMesh = nullptr;
        std::size_t sourceVertexCount = 0u;
        std::size_t sourceIndexCount = 0u;
        std::vector<D3d12CaptureSubmeshCache> submeshes;
        std::vector<IRenderBackend::WorldMeshVertex> rigidCombinedVertices;
        std::vector<std::uint32_t> rigidCombinedIndices;
        bool rigidCombinedPrewarmed = false;
        bool submeshesPrewarmed = false;
    };
    static thread_local D3d12CaptureFastCache sFastCache;

    const bool fastCacheValid =
        (sFastCache.sourceMesh == &mesh) &&
        (sFastCache.sourceVertexCount == mesh.vertices.size()) &&
        (sFastCache.sourceIndexCount == mesh.indices.size()) &&
        !sFastCache.submeshes.empty();
    if (!fastCacheValid) {
        sFastCache = {};
        sFastCache.sourceMesh = &mesh;
        sFastCache.sourceVertexCount = mesh.vertices.size();
        sFastCache.sourceIndexCount = mesh.indices.size();

        const std::size_t batchCount = std::max<std::size_t>(
            1u,
            std::max(mesh.submeshBaseTextures.size(), mesh.submeshIndexCount.size()));
        sFastCache.submeshes.resize(batchCount);

        std::vector<int> submeshNodeFallback(batchCount, -1);
        if (!mesh.submeshMeshIndex.empty()) {
            for (std::size_t si = 0; si < batchCount && si < mesh.submeshMeshIndex.size(); ++si) {
                const int meshIndex = mesh.submeshMeshIndex[si];
                if (meshIndex >= 0 &&
                    static_cast<std::size_t>(meshIndex) < mesh.meshIndexToNode.size()) {
                    submeshNodeFallback[si] = mesh.meshIndexToNode[static_cast<std::size_t>(meshIndex)];
                }
            }
        }
        const int fallbackNode = !mesh.sceneRoots.empty() ? mesh.sceneRoots.front() : -1;

        std::size_t totalRigidVerts = 0u;
        std::size_t totalRigidIndices = 0u;

        for (std::size_t si = 0; si < batchCount; ++si) {
            auto& sub = sFastCache.submeshes[si];
            sub.nodeIndex = (si < submeshNodeFallback.size() && submeshNodeFallback[si] >= 0)
                ? submeshNodeFallback[si]
                : fallbackNode;
            if (si < mesh.submeshAlphaMode.size()) sub.alphaMode = mesh.submeshAlphaMode[si];
            if (si < mesh.submeshAlphaCutoff.size()) sub.alphaCutoff = mesh.submeshAlphaCutoff[si];
            sub.geomKey = "assets/models/pokeball.glb#d3d12fastsubmesh:" + std::to_string(si);

            std::size_t indexOffset = (si < mesh.submeshIndexOffset.size()) ? mesh.submeshIndexOffset[si] : 0u;
            std::size_t indexCount = (si < mesh.submeshIndexCount.size()) ? mesh.submeshIndexCount[si] : 0u;
            if (indexOffset >= mesh.indices.size()) continue;
            indexCount = std::min(indexCount, mesh.indices.size() - indexOffset);
            if (indexCount < 3u) continue;

            std::unordered_map<std::uint32_t, std::uint32_t> remap;
            remap.reserve(indexCount);
            sub.localVertices.reserve(indexCount);
            sub.localIndices.reserve(indexCount);

            const glm::vec3 subColor = (si < mesh.submeshBaseColors.size())
                ? glm::clamp(glm::vec3(mesh.submeshBaseColors[si]), 0.0f, 1.0f)
                : glm::vec3(1.0f);
            const glm::mat4 bindNodeGlobal =
                (sub.nodeIndex >= 0 && static_cast<std::size_t>(sub.nodeIndex) < mesh.bindNodeGlobals.size())
                    ? mesh.bindNodeGlobals[static_cast<std::size_t>(sub.nodeIndex)]
                    : glm::mat4(1.0f);

            for (std::size_t ii = 0; ii < indexCount; ++ii) {
                const std::uint32_t srcIdx = mesh.indices[indexOffset + ii];
                if (srcIdx >= mesh.vertices.size()) continue;
                const auto it = remap.find(srcIdx);
                if (it != remap.end()) {
                    sub.localIndices.push_back(it->second);
                    continue;
                }
                const auto& src = mesh.vertices[srcIdx];
                glm::vec3 color = subColor;
                if (srcIdx < mesh.vertexBaseColors.size()) {
                    color = glm::clamp(mesh.vertexBaseColors[srcIdx], 0.0f, 1.0f);
                } else {
                    color = glm::clamp(glm::vec3(src.color), 0.0f, 1.0f);
                }
                const std::uint32_t outIdx = static_cast<std::uint32_t>(sub.localVertices.size());
                sub.localVertices.push_back(
                    IRenderBackend::WorldMeshVertex{
                        src.position.x, src.position.y, src.position.z, src.uv.x, src.uv.y, color.r, color.g, color.b, 1.0f});
                remap.emplace(srcIdx, outIdx);
                sub.localIndices.push_back(outIdx);
            }

            totalRigidVerts += sub.localVertices.size();
            totalRigidIndices += sub.localIndices.size();

            const std::uint32_t baseVertex =
                static_cast<std::uint32_t>(sFastCache.rigidCombinedVertices.size());
            sFastCache.rigidCombinedVertices.reserve(totalRigidVerts);
            sFastCache.rigidCombinedIndices.reserve(totalRigidIndices);
            for (const auto& lv : sub.localVertices) {
                const glm::vec3 bindPos = glm::vec3(bindNodeGlobal * glm::vec4(lv.x, lv.y, lv.z, 1.0f));
                auto v = lv;
                v.x = bindPos.x;
                v.y = bindPos.y;
                v.z = bindPos.z;
                sFastCache.rigidCombinedVertices.push_back(v);
            }
            for (std::uint32_t idx : sub.localIndices) {
                sFastCache.rigidCombinedIndices.push_back(baseVertex + idx);
            }
        }
    }

    if (captureSnaps.empty() && d3d12CapturePrewarmRequested) {
        bool didPrewarmAny = false;
        if (!sFastCache.rigidCombinedPrewarmed &&
            !sFastCache.rigidCombinedVertices.empty() &&
            sFastCache.rigidCombinedIndices.size() >= 3u) {
            renderer.prewarmWorldIndexedMeshCached(
                "assets/models/pokeball.glb#geomcombined",
                sFastCache.rigidCombinedVertices.data(),
                sFastCache.rigidCombinedVertices.size(),
                sFastCache.rigidCombinedIndices.data(),
                sFastCache.rigidCombinedIndices.size());
            sFastCache.rigidCombinedPrewarmed = true;
            didPrewarmAny = true;
        }
        if (!sFastCache.submeshesPrewarmed) {
            for (const auto& sub : sFastCache.submeshes) {
                if (sub.localVertices.empty() || sub.localIndices.size() < 3u) continue;
                renderer.prewarmWorldIndexedMeshCached(
                    sub.geomKey.c_str(),
                    sub.localVertices.data(),
                    sub.localVertices.size(),
                    sub.localIndices.data(),
                    sub.localIndices.size());
                didPrewarmAny = true;
            }
            sFastCache.submeshesPrewarmed = true;
        }
        if (didPrewarmAny) return result;
    }

    int captureAnimIndex = -1;
    float captureAnimDurationSec = 0.0f;
    if (!mesh.animations.empty()) {
        captureAnimIndex = runtime::shared_capture::findPokeballAnimIndex(mesh);
        if (captureAnimIndex >= 0 &&
            static_cast<std::size_t>(captureAnimIndex) < mesh.animations.size()) {
            captureAnimDurationSec =
                std::max(0.0f, mesh.animations[static_cast<std::size_t>(captureAnimIndex)].durationSec);
        }
    }

    const glm::mat4 viewProjM = glm::make_mat4(worldViewProj);
    for (const auto& snap : captureSnaps) {
        if (snap.timeLeftSec <= 0.0f) continue;

        const float baseScale =
            std::max(0.01f, mesh.modelScaleFactor) * std::max(0.02f, snap.ballScale);
        const glm::mat4 modelM = runtime::shared_capture::buildBallModelMatrix(snap, baseScale);

        shared_backend_pose::PoseEval capturePoseEval;
        bool hasCaptureClipPose = false;
        if (captureAnimIndex >= 0 && captureAnimDurationSec > 0.0f && snap.phase == 1) {
            const float clipAnimTimeSec =
                runtime::shared_capture::ballClipTimeSec(snap, captureAnimDurationSec);
            capturePoseEval = evaluateScenePoseForClipTime(captureAnimIndex, clipAnimTimeSec);
            hasCaptureClipPose = capturePoseEval.hasScenePose && !capturePoseEval.nodeGlobals.empty();
        }

        if (!hasCaptureClipPose &&
            !sFastCache.rigidCombinedVertices.empty() &&
            sFastCache.rigidCombinedIndices.size() >= 3u) {
            const glm::mat4 rigidMvp = viewProjM * modelM;
            renderer.drawWorldIndexedMeshCached(
                "assets/models/pokeball.glb#geomcombined",
                sFastCache.rigidCombinedVertices.data(),
                sFastCache.rigidCombinedVertices.size(),
                sFastCache.rigidCombinedIndices.data(),
                sFastCache.rigidCombinedIndices.size(),
                glm::value_ptr(rigidMvp),
                drawableW,
                drawableH);
            result.appendedAny = true;
            continue;
        }

        for (const auto& sub : sFastCache.submeshes) {
            if (sub.localVertices.empty() || sub.localIndices.size() < 3u) continue;
            glm::mat4 nodeGlobal(1.0f);
            if (hasCaptureClipPose &&
                sub.nodeIndex >= 0 &&
                static_cast<std::size_t>(sub.nodeIndex) < capturePoseEval.nodeGlobals.size()) {
                nodeGlobal = capturePoseEval.nodeGlobals[static_cast<std::size_t>(sub.nodeIndex)];
            } else if (
                sub.nodeIndex >= 0 &&
                static_cast<std::size_t>(sub.nodeIndex) < mesh.bindNodeGlobals.size()) {
                nodeGlobal = mesh.bindNodeGlobals[static_cast<std::size_t>(sub.nodeIndex)];
            }
            const glm::mat4 subMvp = viewProjM * modelM * nodeGlobal;
            renderer.drawWorldIndexedMeshCached(
                sub.geomKey.c_str(),
                sub.localVertices.data(),
                sub.localVertices.size(),
                sub.localIndices.data(),
                sub.localIndices.size(),
                glm::value_ptr(subMvp),
                drawableW,
                drawableH);
            result.appendedAny = true;
        }
    }

    return result;
}

} // namespace game::runtime::shared_capture_d3d12_fast
