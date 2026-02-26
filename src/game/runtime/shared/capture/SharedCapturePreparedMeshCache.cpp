#include "game/runtime/shared/capture/SharedCapturePreparedMeshCache.h"

#include "game/runtime/shared/capture/SharedCapturePresentation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace game::runtime::shared_capture_mesh_cache {

PrepareResult preparePokeballCaptureMeshCache(
    const runtime::backend_model::MeshData& mesh,
    bool captureSnapsEmpty,
    bool d3d12CapturePrewarmRequested,
    bool treatPokeballAsUntextured,
    bool enableNodeChunkPath,
    IRenderBackend* renderer) {
    PrepareResult result;

    const std::size_t triCount = mesh.indices.size() / 3u;
    if (triCount == 0u) return result;

    static thread_local PreparedCaptureMeshCache sCaptureMeshCache;
    const bool captureMeshCacheValid =
        (sCaptureMeshCache.sourceMesh == &mesh) &&
        (sCaptureMeshCache.sourceVertexCount == mesh.vertices.size()) &&
        (sCaptureMeshCache.sourceIndexCount == mesh.indices.size()) &&
        !sCaptureMeshCache.submeshes.empty();

    if (!captureMeshCacheValid) {
        sCaptureMeshCache = {};
        sCaptureMeshCache.sourceMesh = &mesh;
        sCaptureMeshCache.sourceVertexCount = mesh.vertices.size();
        sCaptureMeshCache.sourceIndexCount = mesh.indices.size();
        sCaptureMeshCache.bindNodeGlobalInv.assign(mesh.bindNodeGlobals.size(), glm::mat4(1.0f));
        for (std::size_t ni = 0; ni < mesh.bindNodeGlobals.size(); ++ni) {
            sCaptureMeshCache.bindNodeGlobalInv[ni] = glm::inverse(mesh.bindNodeGlobals[ni]);
        }

        const auto& nodeGlobals = mesh.bindNodeGlobals;
        const std::size_t batchCount = std::max<std::size_t>(1u, mesh.submeshBaseTextures.size());
        sCaptureMeshCache.submeshes.resize(batchCount);

        std::vector<std::unordered_map<std::uint64_t, std::uint32_t>> remap(batchCount);
        std::vector<std::unordered_map<int, std::size_t>> nodeChunkRemap(batchCount);
        std::vector<int> submeshNodeFallback;
        if (!mesh.submeshMeshIndex.empty()) {
            submeshNodeFallback.assign(mesh.submeshMeshIndex.size(), -1);
            for (std::size_t si = 0; si < mesh.submeshMeshIndex.size(); ++si) {
                const int meshIndex = mesh.submeshMeshIndex[si];
                if (meshIndex >= 0 && static_cast<std::size_t>(meshIndex) < mesh.meshIndexToNode.size()) {
                    submeshNodeFallback[si] = mesh.meshIndexToNode[static_cast<std::size_t>(meshIndex)];
                }
            }
        }
        for (std::size_t si = 0; si < batchCount; ++si) {
            auto& sub = sCaptureMeshCache.submeshes[si];
            sub.alphaMode = 0u;
            if (si < mesh.submeshAlphaCutoff.size()) sub.alphaCutoff = mesh.submeshAlphaCutoff[si];
            sub.vertices.reserve(std::max<std::size_t>(32u, mesh.vertices.size() / batchCount));
            sub.indices.reserve(std::max<std::size_t>(96u, mesh.indices.size() / batchCount));
            sub.nodeChunks.clear();
            remap[si].reserve(std::max<std::size_t>(64u, mesh.vertices.size() / batchCount));
            if (enableNodeChunkPath) {
                nodeChunkRemap[si].reserve(8u);
            }
        }

        const auto nodeGlobalForTri = [&](int triNodeIndex) -> const glm::mat4& {
            static const glm::mat4 kIdentity(1.0f);
            if (triNodeIndex >= 0 && static_cast<std::size_t>(triNodeIndex) < nodeGlobals.size()) {
                return nodeGlobals[static_cast<std::size_t>(triNodeIndex)];
            }
            return kIdentity;
        };

        const auto appendPreparedVertex =
            [&](std::size_t submesh,
                std::uint32_t srcIndex,
                int triNodeIndex,
                const glm::vec3& triTint,
                float triAlpha) -> std::uint32_t {
            auto& sub = sCaptureMeshCache.submeshes[submesh];
            auto& subRemap = remap[submesh];
            const std::uint64_t key =
                (static_cast<std::uint64_t>(static_cast<std::uint32_t>(triNodeIndex + 1)) << 32u) |
                static_cast<std::uint64_t>(srcIndex);
            const auto it = subRemap.find(key);
            if (it != subRemap.end()) {
                return it->second;
            }
            if (sub.vertices.size() >= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
                return std::numeric_limits<std::uint32_t>::max();
            }

            const auto& src = mesh.vertices[srcIndex];
            const glm::mat4& nodeGlobal = nodeGlobalForTri(triNodeIndex);
            const glm::vec3 bindPos = glm::vec3(nodeGlobal * glm::vec4(src.position, 1.0f));

            PreparedCaptureVertex v{};
            v.bindPos = bindPos;
            v.nodeIndex = triNodeIndex;
            v.u = src.uv.x;
            v.v = src.uv.y;
            v.r = triTint.r;
            v.g = triTint.g;
            v.b = triTint.b;
            v.a = triAlpha;
            const std::uint32_t outIndex = static_cast<std::uint32_t>(sub.vertices.size());
            sub.vertices.push_back(v);
            subRemap.emplace(key, outIndex);
            return outIndex;
        };

        const auto appendPreparedNodeChunkIndices =
            [&](std::size_t submesh,
                int triNodeIndex,
                std::uint32_t o0,
                std::uint32_t o1,
                std::uint32_t o2,
                bool flipWinding,
                bool doubleSided) {
            if (!enableNodeChunkPath) return;
            auto& sub = sCaptureMeshCache.submeshes[submesh];
            auto& subChunkMap = nodeChunkRemap[submesh];
            std::size_t chunkIndex = 0u;
            const auto found = subChunkMap.find(triNodeIndex);
            if (found == subChunkMap.end()) {
                chunkIndex = sub.nodeChunks.size();
                sub.nodeChunks.push_back({});
                sub.nodeChunks.back().nodeIndex = triNodeIndex;
                sub.nodeChunks.back().indices.reserve(256u);
                subChunkMap.emplace(triNodeIndex, chunkIndex);
            } else {
                chunkIndex = found->second;
            }
            auto& chunk = sub.nodeChunks[chunkIndex];
            chunk.indices.push_back(o0);
            chunk.indices.push_back(flipWinding ? o2 : o1);
            chunk.indices.push_back(flipWinding ? o1 : o2);
            if (doubleSided) {
                chunk.indices.push_back(o0);
                chunk.indices.push_back(flipWinding ? o1 : o2);
                chunk.indices.push_back(flipWinding ? o2 : o1);
            }
        };

        for (std::size_t triIdx = 0; triIdx < triCount; ++triIdx) {
            const std::size_t idxBase = triIdx * 3u;
            const std::uint32_t i0 = mesh.indices[idxBase + 0u];
            const std::uint32_t i1 = mesh.indices[idxBase + 1u];
            const std::uint32_t i2 = mesh.indices[idxBase + 2u];
            if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size()) {
                continue;
            }
            std::size_t submesh = 0u;
            if (triIdx < mesh.triangleSubmesh.size()) {
                submesh = static_cast<std::size_t>(mesh.triangleSubmesh[triIdx]);
                if (submesh >= sCaptureMeshCache.submeshes.size()) submesh = 0u;
            }
            const int triNodeIndex =
                (triIdx < mesh.triangleNodeIndex.size()) ? mesh.triangleNodeIndex[triIdx] : -1;
            int resolvedTriNodeIndex = triNodeIndex;
            if (resolvedTriNodeIndex < 0 &&
                triIdx < mesh.triangleSubmesh.size() &&
                !submeshNodeFallback.empty()) {
                const std::uint16_t submeshIndex = mesh.triangleSubmesh[triIdx];
                if (submeshIndex < submeshNodeFallback.size()) {
                    resolvedTriNodeIndex = submeshNodeFallback[submeshIndex];
                }
            }
            const glm::mat4& nodeGlobal = nodeGlobalForTri(resolvedTriNodeIndex);
            const bool flipWinding = (glm::determinant(glm::mat3(nodeGlobal)) < 0.0f);
            const bool doubleSided = false;

            glm::vec3 triTint(1.0f, 1.0f, 1.0f);
            if (triIdx < mesh.triangleBaseColors.size()) {
                triTint = glm::clamp(mesh.triangleBaseColors[triIdx], 0.0f, 1.0f);
            } else if (submesh < mesh.submeshBaseColors.size()) {
                const glm::vec4 sc = mesh.submeshBaseColors[submesh];
                triTint = glm::clamp(glm::vec3(sc.r, sc.g, sc.b), 0.0f, 1.0f);
            }
            const float triAlpha = 1.0f;

            const std::uint32_t o0 =
                appendPreparedVertex(submesh, i0, resolvedTriNodeIndex, triTint, triAlpha);
            const std::uint32_t o1 =
                appendPreparedVertex(submesh, i1, resolvedTriNodeIndex, triTint, triAlpha);
            const std::uint32_t o2 =
                appendPreparedVertex(submesh, i2, resolvedTriNodeIndex, triTint, triAlpha);
            if (o0 == std::numeric_limits<std::uint32_t>::max() ||
                o1 == std::numeric_limits<std::uint32_t>::max() ||
                o2 == std::numeric_limits<std::uint32_t>::max()) {
                continue;
            }
            auto& sub = sCaptureMeshCache.submeshes[submesh];
            sub.indices.push_back(o0);
            sub.indices.push_back(flipWinding ? o2 : o1);
            sub.indices.push_back(flipWinding ? o1 : o2);
            if (doubleSided) {
                sub.indices.push_back(o0);
                sub.indices.push_back(flipWinding ? o1 : o2);
                sub.indices.push_back(flipWinding ? o2 : o1);
            }
            if (enableNodeChunkPath) {
                appendPreparedNodeChunkIndices(
                    submesh,
                    resolvedTriNodeIndex,
                    o0,
                    o1,
                    o2,
                    flipWinding,
                    doubleSided);
            }
        }

        if (enableNodeChunkPath) {
            for (auto& sub : sCaptureMeshCache.submeshes) {
                for (auto& nodeChunk : sub.nodeChunks) {
                    nodeChunk.compactVertices.clear();
                    nodeChunk.compactIndices.clear();
                    if (nodeChunk.indices.empty()) continue;
                    nodeChunk.compactVertices.reserve(
                        std::min<std::size_t>(sub.vertices.size(), nodeChunk.indices.size()));
                    nodeChunk.compactIndices.reserve(nodeChunk.indices.size());
                    std::unordered_map<std::uint32_t, std::uint32_t> remapChunk;
                    remapChunk.reserve(nodeChunk.indices.size());
                    for (std::uint32_t idx : nodeChunk.indices) {
                        const auto it = remapChunk.find(idx);
                        if (it != remapChunk.end()) {
                            nodeChunk.compactIndices.push_back(it->second);
                            continue;
                        }
                        if (idx >= sub.vertices.size()) continue;
                        const auto& src = sub.vertices[idx];
                        const std::uint32_t outIdx =
                            static_cast<std::uint32_t>(nodeChunk.compactVertices.size());
                        nodeChunk.compactVertices.push_back(
                            IRenderBackend::WorldMeshVertex{
                                src.bindPos.x, src.bindPos.y, src.bindPos.z, src.u, src.v, src.r, src.g, src.b, src.a});
                        remapChunk.emplace(idx, outIdx);
                        nodeChunk.compactIndices.push_back(outIdx);
                    }
                }
            }
        }

        bool canBuildRigidCombined = true;
        std::size_t totalCombinedVerts = 0u;
        std::size_t totalCombinedIndices = 0u;
        for (std::size_t si = 0; si < sCaptureMeshCache.submeshes.size(); ++si) {
            const auto& sub = sCaptureMeshCache.submeshes[si];
            if (sub.vertices.empty() || sub.indices.empty()) continue;
            if (sub.alphaMode == 2u) {
                canBuildRigidCombined = false;
                break;
            }
            if (!treatPokeballAsUntextured &&
                si < mesh.submeshBaseTextures.size() &&
                mesh.submeshBaseTextures[si].hasPixels()) {
                canBuildRigidCombined = false;
                break;
            }
            totalCombinedVerts += sub.vertices.size();
            totalCombinedIndices += sub.indices.size();
        }
        if (canBuildRigidCombined && totalCombinedVerts > 0u && totalCombinedIndices >= 3u) {
            sCaptureMeshCache.rigidCombinedVertices.clear();
            sCaptureMeshCache.rigidCombinedIndices.clear();
            sCaptureMeshCache.rigidCombinedVertices.reserve(totalCombinedVerts);
            sCaptureMeshCache.rigidCombinedIndices.reserve(totalCombinedIndices);
            std::uint32_t baseVertex = 0u;
            for (const auto& sub : sCaptureMeshCache.submeshes) {
                if (sub.vertices.empty() || sub.indices.empty()) continue;
                for (const auto& src : sub.vertices) {
                    sCaptureMeshCache.rigidCombinedVertices.push_back(
                        IRenderBackend::WorldMeshVertex{
                            src.bindPos.x, src.bindPos.y, src.bindPos.z, src.u, src.v, src.r, src.g, src.b, src.a});
                }
                for (std::uint32_t idx : sub.indices) {
                    sCaptureMeshCache.rigidCombinedIndices.push_back(baseVertex + idx);
                }
                baseVertex += static_cast<std::uint32_t>(sub.vertices.size());
            }
        }
    }

    if (captureSnapsEmpty && d3d12CapturePrewarmRequested && renderer) {
        bool didPrewarmAny = false;
        if (!sCaptureMeshCache.rigidCombinedVertices.empty() &&
            sCaptureMeshCache.rigidCombinedIndices.size() >= 3u) {
            renderer->prewarmWorldIndexedMeshCached(
                "assets/models/pokeball.glb#geomcombined",
                sCaptureMeshCache.rigidCombinedVertices.data(),
                sCaptureMeshCache.rigidCombinedVertices.size(),
                sCaptureMeshCache.rigidCombinedIndices.data(),
                sCaptureMeshCache.rigidCombinedIndices.size());
            didPrewarmAny = true;
        }
        if (didPrewarmAny) {
            result.earlyReturnAfterPrewarm = true;
            result.cache = &sCaptureMeshCache;
            return result;
        }
    }

    result.captureMeshLikelySkinned =
        !mesh.skins.empty() ||
        std::any_of(mesh.triangleSkinIndex.begin(), mesh.triangleSkinIndex.end(), [](int skinIndex) {
            return skinIndex >= 0;
        });
    if (!mesh.animations.empty()) {
        result.captureAnimIndex = runtime::shared_capture::findPokeballAnimIndex(mesh);
        if (result.captureAnimIndex >= 0 &&
            static_cast<std::size_t>(result.captureAnimIndex) < mesh.animations.size()) {
            result.captureAnimDurationSec = std::max(
                0.0f,
                mesh.animations[static_cast<std::size_t>(result.captureAnimIndex)].durationSec);
        }
    }

    result.cache = &sCaptureMeshCache;
    result.validForRender = true;
    return result;
}

} // namespace game::runtime::shared_capture_mesh_cache

