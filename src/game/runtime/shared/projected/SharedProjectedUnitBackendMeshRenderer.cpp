#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshRenderer.h"
#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshPrep.h"
#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshTriangleSubmit.h"
#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshTransforms.h"

#include "engine/core/Environment.h"
#include "game/runtime/BackendUnitVisuals.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

namespace {
std::string toLowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::size_t selectUniformTriangleIndex(std::size_t sampleIndex,
                                       std::size_t sampleCount,
                                       std::size_t triangleCount) {
    if (triangleCount == 0u || sampleCount == 0u) return 0u;
    if (sampleCount >= triangleCount) return std::min(sampleIndex, triangleCount - 1u);
    const double t = (static_cast<double>(sampleIndex) + 0.5) /
                     static_cast<double>(sampleCount);
    const std::size_t idx =
        static_cast<std::size_t>(t * static_cast<double>(triangleCount));
    return std::min(idx, triangleCount - 1u);
}

bool strictGltfParityEnabled() {
    static const bool enabled = []() -> bool {
        const auto env = engine::env::get("PAC_GLTF_PARITY_STRICT");
        if (!env.has_value()) return true;
        const std::string raw = *env;
        if (raw == "0" || raw == "false" || raw == "FALSE" || raw == "off" || raw == "OFF") {
            return false;
        }
        return true;
    }();
    return enabled;
}

struct FastTexturedBatchTemplate {
    std::size_t baseSubmeshIndex = 0u;
    int triNodeIndex = -1;
    std::vector<std::uint32_t> sourceVertexIndices;
    std::vector<std::uint32_t> indices;
    std::vector<IRenderBackend::WorldMeshVertex> gpuTemplateVertices;
};

struct FastTexturedMeshTemplateCache {
    const game::runtime::backend_model::MeshData* mesh = nullptr;
    std::size_t meshVertexCount = 0u;
    std::size_t meshIndexCount = 0u;
    std::size_t baseBatchCount = 0u;
    std::vector<int> submeshNodeFallbackSnapshot;
    std::vector<FastTexturedBatchTemplate> batches;
};

thread_local std::unordered_map<
    const game::runtime::backend_model::MeshData*,
    FastTexturedMeshTemplateCache> g_fastTexturedMeshTemplateCaches;

struct UnitNodeSkinMatrixKey {
    int unitId = 0;
    int nodeIndex = -1;

    bool operator==(const UnitNodeSkinMatrixKey& other) const {
        return unitId == other.unitId && nodeIndex == other.nodeIndex;
    }
};

struct UnitNodeSkinMatrixKeyHash {
    std::size_t operator()(const UnitNodeSkinMatrixKey& key) const noexcept {
        std::size_t h = static_cast<std::size_t>(static_cast<std::uint32_t>(key.unitId));
        h ^= (static_cast<std::size_t>(static_cast<std::uint32_t>(key.nodeIndex + 1)) << 1);
        return h;
    }
};

thread_local std::unordered_map<UnitNodeSkinMatrixKey, std::vector<float>, UnitNodeSkinMatrixKeyHash>
    g_unitNodeSkinMatrices;

bool nearlyOne(float v) {
    return std::abs(v - 1.0f) <= 1e-6f;
}

bool tintAlphaIdentity(const glm::vec3& tint, float alpha) {
    return nearlyOne(tint.r) && nearlyOne(tint.g) && nearlyOne(tint.b) && nearlyOne(alpha);
}

const FastTexturedMeshTemplateCache* ensureFastTexturedMeshTemplateCache(
    const game::runtime::backend_model::MeshData* mesh,
    const std::vector<int>& submeshNodeFallback,
    std::size_t baseBatchCount) {
    if (!mesh || baseBatchCount == 0u) return nullptr;

    auto& cache = g_fastTexturedMeshTemplateCaches[mesh];
    const bool cacheValid =
        cache.mesh == mesh &&
        cache.meshVertexCount == mesh->vertices.size() &&
        cache.meshIndexCount == mesh->indices.size() &&
        cache.baseBatchCount == baseBatchCount &&
        cache.submeshNodeFallbackSnapshot == submeshNodeFallback &&
        !cache.batches.empty();
    if (cacheValid) return &cache;

    cache = {};
    cache.mesh = mesh;
    cache.meshVertexCount = mesh->vertices.size();
    cache.meshIndexCount = mesh->indices.size();
    cache.baseBatchCount = baseBatchCount;
    cache.submeshNodeFallbackSnapshot = submeshNodeFallback;

    const std::size_t triangleCount = mesh->indices.size() / 3u;
    if (triangleCount == 0u || mesh->vertices.empty()) return nullptr;

    cache.batches.assign(baseBatchCount, FastTexturedBatchTemplate{});
    for (std::size_t si = 0; si < baseBatchCount; ++si) {
        cache.batches[si].baseSubmeshIndex = si;
    }

    std::vector<std::unordered_map<int, std::size_t>> nodeToBatch(baseBatchCount);
    std::vector<std::size_t> batchIndexByTriangle(triangleCount, 0u);
    std::vector<std::size_t> triangleCountByBatch(baseBatchCount, 0u);

    for (std::size_t triIdx = 0; triIdx < triangleCount; ++triIdx) {
        std::size_t submeshIndex = 0u;
        if (triIdx < mesh->triangleSubmesh.size()) {
            submeshIndex = static_cast<std::size_t>(mesh->triangleSubmesh[triIdx]);
            if (submeshIndex >= baseBatchCount) submeshIndex = 0u;
        }

        int triNodeIndex =
            (triIdx < mesh->triangleNodeIndex.size()) ? mesh->triangleNodeIndex[triIdx] : -1;
        if (triNodeIndex < 0 &&
            triIdx < mesh->triangleSubmesh.size() &&
            !submeshNodeFallback.empty()) {
            const std::uint16_t fallbackSubmeshIndex = mesh->triangleSubmesh[triIdx];
            if (fallbackSubmeshIndex < submeshNodeFallback.size()) {
                triNodeIndex = submeshNodeFallback[fallbackSubmeshIndex];
            }
        }

        std::size_t batchIndex = submeshIndex;
        if (triNodeIndex >= 0) {
            auto& mapForSubmesh = nodeToBatch[submeshIndex];
            const auto found = mapForSubmesh.find(triNodeIndex);
            if (found != mapForSubmesh.end()) {
                batchIndex = found->second;
            } else if (mapForSubmesh.empty()) {
                mapForSubmesh.emplace(triNodeIndex, submeshIndex);
                cache.batches[submeshIndex].triNodeIndex = triNodeIndex;
                batchIndex = submeshIndex;
            } else {
                batchIndex = cache.batches.size();
                mapForSubmesh.emplace(triNodeIndex, batchIndex);
                FastTexturedBatchTemplate newBatch{};
                newBatch.baseSubmeshIndex = submeshIndex;
                newBatch.triNodeIndex = triNodeIndex;
                cache.batches.push_back(std::move(newBatch));
                triangleCountByBatch.push_back(0u);
            }
        }

        if (batchIndex >= triangleCountByBatch.size()) {
            triangleCountByBatch.resize(batchIndex + 1u, 0u);
        }
        ++triangleCountByBatch[batchIndex];
        batchIndexByTriangle[triIdx] = batchIndex;
    }

    std::vector<std::vector<int>> remapByBatch(cache.batches.size());
    for (std::size_t bi = 0; bi < cache.batches.size(); ++bi) {
        auto& batch = cache.batches[bi];
        const std::size_t triCountForBatch =
            (bi < triangleCountByBatch.size()) ? triangleCountByBatch[bi] : 0u;
        const std::size_t indexReserve = triCountForBatch * 3u;
        batch.indices.reserve(indexReserve);
        batch.sourceVertexIndices.reserve(std::min(mesh->vertices.size(), indexReserve));
        remapByBatch[bi].assign(mesh->vertices.size(), -1);
    }

    for (std::size_t triIdx = 0; triIdx < triangleCount; ++triIdx) {
        const std::size_t batchIndex = batchIndexByTriangle[triIdx];
        if (batchIndex >= cache.batches.size()) continue;
        const std::size_t i = triIdx * 3u;
        const std::uint32_t i0 = mesh->indices[i + 0];
        const std::uint32_t i1 = mesh->indices[i + 1];
        const std::uint32_t i2 = mesh->indices[i + 2];
        if (i0 >= mesh->vertices.size() ||
            i1 >= mesh->vertices.size() ||
            i2 >= mesh->vertices.size()) {
            continue;
        }

        auto& batch = cache.batches[batchIndex];
        auto& remap = remapByBatch[batchIndex];
        const auto appendVertex = [&](std::uint32_t src) -> std::uint32_t {
            int& mapped = remap[src];
            if (mapped >= 0) return static_cast<std::uint32_t>(mapped);
            mapped = static_cast<int>(batch.sourceVertexIndices.size());
            batch.sourceVertexIndices.push_back(src);
            return static_cast<std::uint32_t>(mapped);
        };

        batch.indices.push_back(appendVertex(i0));
        batch.indices.push_back(appendVertex(i1));
        batch.indices.push_back(appendVertex(i2));
    }

    for (auto& batch : cache.batches) {
        batch.gpuTemplateVertices.resize(batch.sourceVertexIndices.size());
        for (std::size_t vi = 0; vi < batch.sourceVertexIndices.size(); ++vi) {
            const std::uint32_t srcIndex = batch.sourceVertexIndices[vi];
            const auto& src = mesh->vertices[srcIndex];
            IRenderBackend::WorldMeshVertex outVertex{};
            outVertex.x = src.position.x;
            outVertex.y = src.position.y;
            outVertex.z = src.position.z;
            outVertex.u = src.uv.x;
            outVertex.v = src.uv.y;
            const glm::vec3 authoredColor = mesh->hasVertexColor
                ? glm::clamp(glm::vec3(src.color.r, src.color.g, src.color.b), 0.0f, 1.0f)
                : glm::vec3(1.0f);
            const float authoredAlpha = mesh->hasVertexColor
                ? std::clamp(src.color.a, 0.0f, 1.0f)
                : 1.0f;
            outVertex.r = authoredColor.r;
            outVertex.g = authoredColor.g;
            outVertex.b = authoredColor.b;
            outVertex.a = authoredAlpha;
            outVertex.nx = src.normal.x;
            outVertex.ny = src.normal.y;
            outVertex.nz = src.normal.z;
            outVertex.tx = src.tangent.x;
            outVertex.ty = src.tangent.y;
            outVertex.tz = src.tangent.z;
            outVertex.tw = src.tangent.w;
            outVertex.joint0 = static_cast<float>(src.j0);
            outVertex.joint1 = static_cast<float>(src.j1);
            outVertex.joint2 = static_cast<float>(src.j2);
            outVertex.joint3 = static_cast<float>(src.j3);
            outVertex.weight0 = src.w0;
            outVertex.weight1 = src.w1;
            outVertex.weight2 = src.w2;
            outVertex.weight3 = src.w3;
            batch.gpuTemplateVertices[vi] = outVertex;
        }
    }

    return cache.batches.empty() ? nullptr : &cache;
}
} // namespace

namespace game::runtime::shared_projected_unit_backend_mesh {

Result renderProjectedUnitBackendMesh(const Args& args) {
    Result out{};
    if (!args.dataDb || !args.unit || !args.pose || !args.meshForUnit || !args.scenePose ||
        !args.tint || !args.projectedDebug || !args.sharedTailFireAnchors ||
        !args.worldIndexedBatches || !args.modelDepthTris || !args.modelDepthWorldTris ||
        !args.remainingModelTrianglesBudget || !args.world3DTriangles ||
        !args.backendModelTriangleLimit || !args.backendModelFullMeshEnabled ||
        !args.backendModelFastTexturedPathEnabled || !args.backendModelBackfaceCullingEnabled) {
        return out;
    }

    const auto& unit = *args.unit;
    const auto* meshForUnit = args.meshForUnit;

    const float captureVisualTintStrength = args.captureVisualTintStrength;
    const float modelFadeAlpha = args.modelFadeAlpha;
    const glm::vec3 captureTintColor = args.captureTintColor;
    const glm::vec3 cameraWorldPos = args.cameraWorldPos;

    const bool supportsWorldTriangles3D = args.supportsWorldTriangles3D;

    auto& projectedDebug = *args.projectedDebug;
    auto& sharedTailFireAnchors = *args.sharedTailFireAnchors;
    auto& worldIndexedBatches = *args.worldIndexedBatches;
    auto& modelDepthTris = *args.modelDepthTris;
    auto& modelDepthWorldTris = *args.modelDepthWorldTris;
    auto& world3DTriangles = *args.world3DTriangles;

    const auto& backendModelFastTexturedPathEnabled = args.backendModelFastTexturedPathEnabled;
    const auto& backendModelBackfaceCullingEnabled = args.backendModelBackfaceCullingEnabled;
    const bool strictGltfParity = strictGltfParityEnabled();

    using SharedTailFireAnchor = game::runtime::shared_tail_fire_fallback::Anchor;

    bool drewModelMesh = false;
    if (meshForUnit) {
        shared_projected_unit_backend_mesh_prep::PreparedState prep;
        if (!shared_projected_unit_backend_mesh_prep::prepareProjectedUnitBackendMesh(args, out, prep)) {
            return out;
        }

        const runtime::backend_model::MeshData* mesh = prep.mesh;
        const std::size_t triangleCount = prep.triangleCount;
        const std::size_t effectiveUnitTriangleBudget = prep.effectiveUnitTriangleBudget;
        const bool useIndexedWorldModelPath = prep.useIndexedWorldModelPath;
        const bool fullIndexedMeshPath = prep.fullIndexedMeshPath;
        const bool useFastTexturedFullMeshPath = prep.useFastTexturedFullMeshPath;
        const float resolvedScaleCorrection = prep.resolvedScaleCorrection;
        const std::size_t modelDepthCountBefore = prep.modelDepthCountBefore;
        const std::size_t modelDepthWorldCountBefore = prep.modelDepthWorldCountBefore;
        const std::size_t world3DTriangleCountBefore = prep.world3DTriangleCountBefore;
        auto& submeshNodeFallback = prep.submeshNodeFallback;
        auto& modelIndexedBatchesPerSubmesh = prep.modelIndexedBatchesPerSubmesh;
        auto& modelIndexedVertexRemap = prep.modelIndexedVertexRemap;
        const auto& nodeGlobals =
            prep.scenePose.hasScenePose ? prep.scenePose.nodeGlobals : mesh->bindNodeGlobals;
        const glm::vec3& lightDir = prep.lightDir;
        const glm::vec3& fallbackBase = prep.fallbackBase;
        const bool downsampleModelTriangles = prep.downsampleModelTriangles;
        const float fastTexturedAlpha = prep.fastTexturedAlpha;
        const glm::vec3& fastTexturedTint = prep.fastTexturedTint;
        shared_projected_unit_backend_mesh_transforms::Resolver transforms;
        transforms.initialize(args, prep);

        bool handledFastTexturedPath = false;
        const FastTexturedMeshTemplateCache* fastCachePtr = nullptr;
        if (useFastTexturedFullMeshPath && !modelIndexedBatchesPerSubmesh.empty()) {
            fastCachePtr = ensureFastTexturedMeshTemplateCache(
                mesh,
                submeshNodeFallback,
                modelIndexedBatchesPerSubmesh.size());
        }
        if (fastCachePtr) {
            const auto& fastCache = *fastCachePtr;

            const std::size_t initialBatchCount = modelIndexedBatchesPerSubmesh.size();
            if (fastCache.batches.size() > initialBatchCount) {
                for (std::size_t bi = initialBatchCount; bi < fastCache.batches.size(); ++bi) {
                    const std::size_t sourceBatchIndex = fastCache.batches[bi].baseSubmeshIndex;
                    if (sourceBatchIndex >= initialBatchCount) continue;
                    const auto& templateBatch = modelIndexedBatchesPerSubmesh[sourceBatchIndex];
                    modelIndexedBatchesPerSubmesh.push_back(templateBatch);
                    auto& newBatch = modelIndexedBatchesPerSubmesh.back();
                    newBatch.vertices.clear();
                    newBatch.indices.clear();
                    newBatch.vertices.reserve(fastCache.batches[bi].sourceVertexIndices.size());
                    newBatch.indices.reserve(fastCache.batches[bi].indices.size());
                    newBatch.sharedVertices = nullptr;
                    newBatch.sharedVertexCount = 0u;
                    newBatch.sharedIndices = nullptr;
                    newBatch.sharedIndexCount = 0u;
                    newBatch.gpuSkinning = 0u;
                    newBatch.skinMatrixCount = 0u;
                    newBatch.sharedSkinMatrices = nullptr;
                    newBatch.skinMatrices.clear();
                }
            }

            for (auto& batch : modelIndexedBatchesPerSubmesh) {
                batch.gpuSkinning = 0u;
                batch.skinMatrixCount = 0u;
                batch.sharedSkinMatrices = nullptr;
                batch.skinMatrices.clear();
                batch.sharedVertices = nullptr;
                batch.sharedVertexCount = 0u;
                batch.sharedIndices = nullptr;
                batch.sharedIndexCount = 0u;
            }

            const bool applyTintAlpha = !tintAlphaIdentity(fastTexturedTint, fastTexturedAlpha);
            struct GpuSkinBatchState {
                bool valid = false;
                std::array<float, 16> modelMatrix{};
                std::uint32_t skinMatrixCount = 0u;
                const float* sharedSkinMatrices = nullptr;
            };
            std::unordered_map<int, GpuSkinBatchState> gpuSkinBatchStateByNode;
            for (std::size_t bi = 0; bi < fastCache.batches.size(); ++bi) {
                if (bi >= modelIndexedBatchesPerSubmesh.size()) continue;
                const auto& srcBatch = fastCache.batches[bi];
                auto& dstBatch = modelIndexedBatchesPerSubmesh[bi];

                if (args.enableGpuClipSkinning) {
                    const int triNodeIndex = srcBatch.triNodeIndex;
                    auto stateIt = gpuSkinBatchStateByNode.find(triNodeIndex);
                    if (stateIt == gpuSkinBatchStateByNode.end()) {
                        GpuSkinBatchState newState{};
                        UnitNodeSkinMatrixKey key{};
                        key.unitId = unit.id;
                        key.nodeIndex = triNodeIndex;
                        auto& sharedSkinMatrices = g_unitNodeSkinMatrices[key];
                        if (transforms.configureGpuClipSkinningBatch(
                                triNodeIndex,
                                newState.modelMatrix,
                                sharedSkinMatrices,
                                newState.skinMatrixCount)) {
                            newState.valid = true;
                            newState.sharedSkinMatrices = sharedSkinMatrices.data();
                        }
                        stateIt = gpuSkinBatchStateByNode.emplace(triNodeIndex, newState).first;
                    }
                    if (stateIt->second.valid) {
                        dstBatch.gpuSkinning = 1u;
                        dstBatch.modelMatrix = stateIt->second.modelMatrix;
                        dstBatch.skinMatrixCount = stateIt->second.skinMatrixCount;
                        dstBatch.sharedSkinMatrices = stateIt->second.sharedSkinMatrices;
                        dstBatch.skinMatrices.clear();
                    }
                }

                if (dstBatch.gpuSkinning != 0u) {
                    if (!applyTintAlpha &&
                        !srcBatch.gpuTemplateVertices.empty() &&
                        !srcBatch.indices.empty()) {
                        dstBatch.vertices.clear();
                        dstBatch.indices.clear();
                        dstBatch.sharedVertices = srcBatch.gpuTemplateVertices.data();
                        dstBatch.sharedVertexCount = srcBatch.gpuTemplateVertices.size();
                        dstBatch.sharedIndices = srcBatch.indices.data();
                        dstBatch.sharedIndexCount = srcBatch.indices.size();
                    } else {
                        dstBatch.sharedVertices = nullptr;
                        dstBatch.sharedVertexCount = 0u;
                        dstBatch.sharedIndices = nullptr;
                        dstBatch.sharedIndexCount = 0u;
                        dstBatch.indices.resize(srcBatch.indices.size());
                        if (!srcBatch.indices.empty()) {
                            std::memcpy(
                                dstBatch.indices.data(),
                                srcBatch.indices.data(),
                                srcBatch.indices.size() * sizeof(std::uint32_t));
                        }
                        dstBatch.vertices.resize(srcBatch.gpuTemplateVertices.size());
                        if (!srcBatch.gpuTemplateVertices.empty()) {
                            std::memcpy(
                                dstBatch.vertices.data(),
                                srcBatch.gpuTemplateVertices.data(),
                                srcBatch.gpuTemplateVertices.size() *
                                    sizeof(IRenderBackend::WorldMeshVertex));
                        }
                        for (auto& v : dstBatch.vertices) {
                            v.r *= fastTexturedTint.r;
                            v.g *= fastTexturedTint.g;
                            v.b *= fastTexturedTint.b;
                            v.a *= fastTexturedAlpha;
                        }
                    }
                } else {
                    dstBatch.sharedVertices = nullptr;
                    dstBatch.sharedVertexCount = 0u;
                    dstBatch.sharedIndices = nullptr;
                    dstBatch.sharedIndexCount = 0u;
                    dstBatch.sharedSkinMatrices = nullptr;
                    dstBatch.indices.resize(srcBatch.indices.size());
                    if (!srcBatch.indices.empty()) {
                        std::memcpy(
                            dstBatch.indices.data(),
                            srcBatch.indices.data(),
                            srcBatch.indices.size() * sizeof(std::uint32_t));
                    }
                    dstBatch.vertices.resize(srcBatch.sourceVertexIndices.size());
                    for (std::size_t vi = 0; vi < srcBatch.sourceVertexIndices.size(); ++vi) {
                        const std::uint32_t srcIndex = srcBatch.sourceVertexIndices[vi];
                        if (srcIndex >= mesh->vertices.size()) continue;
                        const auto& srcVertex = mesh->vertices[srcIndex];

                        IRenderBackend::WorldMeshVertex outVertex = srcBatch.gpuTemplateVertices[vi];
                        const glm::vec3 pos = transforms.resolveWorldVertexPos(
                            srcBatch.triNodeIndex, srcIndex, srcVertex);
                        outVertex.x = pos.x;
                        outVertex.y = pos.y;
                        outVertex.z = pos.z;
                        const glm::vec3 nrm = transforms.resolveModelVertexNormal(
                            srcBatch.triNodeIndex, srcIndex, srcVertex);
                        outVertex.nx = nrm.x;
                        outVertex.ny = nrm.y;
                        outVertex.nz = nrm.z;
                        const glm::vec4 tan = transforms.resolveModelVertexTangent(
                            srcBatch.triNodeIndex, srcIndex, srcVertex);
                        outVertex.tx = tan.x;
                        outVertex.ty = tan.y;
                        outVertex.tz = tan.z;
                        outVertex.tw = tan.w;
                        if (applyTintAlpha) {
                            outVertex.r *= fastTexturedTint.r;
                            outVertex.g *= fastTexturedTint.g;
                            outVertex.b *= fastTexturedTint.b;
                            outVertex.a *= fastTexturedAlpha;
                        }
                        dstBatch.vertices[vi] = outVertex;
                    }
                }
            }
            handledFastTexturedPath = true;
            bool fastPathHasGeometry = false;
            for (const auto& batch : modelIndexedBatchesPerSubmesh) {
                if (batch.hasGeometry()) {
                    fastPathHasGeometry = true;
                    break;
                }
            }
            if (!fastPathHasGeometry) {
                handledFastTexturedPath = false;
                for (auto& batch : modelIndexedBatchesPerSubmesh) {
                    batch.vertices.clear();
                    batch.indices.clear();
                    batch.sharedVertices = nullptr;
                    batch.sharedVertexCount = 0u;
                    batch.sharedIndices = nullptr;
                    batch.sharedIndexCount = 0u;
                    batch.gpuSkinning = 0u;
                    batch.skinMatrixCount = 0u;
                    batch.sharedSkinMatrices = nullptr;
                    batch.skinMatrices.clear();
                }
            }
        }

        if (unit.alive && !unit.fainting && toLowerCopy(unit.name) == "charmander") {
            const TailFireVFX::Config& tailCfg =
                game::runtime::shared_projected_scene::getTailFireFallbackCfg();
            const int tailNodeIndex = tailCfg.tailTipNodeIndex;
            if (tailNodeIndex >= 0 &&
                static_cast<std::size_t>(tailNodeIndex) < nodeGlobals.size()) {
                const glm::mat4& tailWorldM = transforms.worldMatrixForNode(tailNodeIndex);

                auto safeNorm = [](glm::vec3 v, const glm::vec3& fallback) {
                    const float len2 = glm::dot(v, v);
                    if (len2 <= 1e-10f) return fallback;
                    return v * (1.0f / std::sqrt(len2));
                };
                glm::vec3 bx = safeNorm(glm::vec3(tailWorldM[0]), glm::vec3(1.0f, 0.0f, 0.0f));
                glm::vec3 by = glm::vec3(tailWorldM[1]);
                by = by - bx * glm::dot(by, bx);
                by = safeNorm(by, glm::vec3(0.0f, 1.0f, 0.0f));
                glm::vec3 bz = safeNorm(glm::cross(bx, by), glm::vec3(0.0f, 0.0f, 1.0f));
                if (glm::dot(glm::cross(bx, by), bz) < 0.0f) {
                    bz = -bz;
                }
                const glm::mat3 tailBasis(bx, by, bz);
                glm::vec3 backDirWorld = tailBasis * tailCfg.backDir;
                backDirWorld = safeNorm(backDirWorld, glm::vec3(0.0f, 1.0f, 0.0f));

                SharedTailFireAnchor& anchor = sharedTailFireAnchors[unit.id];
                anchor.valid = true;
                anchor.pos = glm::vec3(tailWorldM[3]) + glm::vec3(0.0f, tailCfg.tailWorldYOffset, 0.0f);
                anchor.basis = tailBasis;
                anchor.backDir = backDirWorld;
                anchor.particleSizeScale =
                    std::max(0.01f, std::max(0.01f, mesh->modelScaleFactor) * resolvedScaleCorrection);
            }
        }

        if (!handledFastTexturedPath) {
            shared_projected_unit_backend_mesh_submit::TriangleSubmitter triangleSubmitter;
            triangleSubmitter.initialize(
                shared_projected_unit_backend_mesh_submit::TriangleSubmitter::Args{
                    supportsWorldTriangles3D,
                    useIndexedWorldModelPath,
                    fullIndexedMeshPath,
                    backendModelFastTexturedPathEnabled(),
                    backendModelBackfaceCullingEnabled(),
                    cameraWorldPos,
                    lightDir,
                    &projectedDebug,
                    &modelIndexedBatchesPerSubmesh,
                    &modelIndexedVertexRemap,
                    &modelDepthTris,
                    &world3DTriangles});

            if (useFastTexturedFullMeshPath &&
                modelIndexedVertexRemap.empty() &&
                !mesh->vertices.empty() &&
                !modelIndexedBatchesPerSubmesh.empty()) {
                modelIndexedVertexRemap.resize(modelIndexedBatchesPerSubmesh.size());
                for (auto& remap : modelIndexedVertexRemap) {
                    remap.assign(mesh->vertices.size(), -1);
                }
            }

            std::vector<int> triNodeIndexByTriangle(triangleCount, -1);
            for (std::size_t triIdx = 0; triIdx < triangleCount; ++triIdx) {
                int triNodeIndex =
                    (triIdx < mesh->triangleNodeIndex.size())
                        ? mesh->triangleNodeIndex[triIdx]
                        : -1;
                if (triNodeIndex < 0 &&
                    triIdx < mesh->triangleSubmesh.size() &&
                    !submeshNodeFallback.empty()) {
                    const std::uint16_t submeshIndex = mesh->triangleSubmesh[triIdx];
                    if (submeshIndex < submeshNodeFallback.size()) {
                        triNodeIndex = submeshNodeFallback[submeshIndex];
                    }
                }
                triNodeIndexByTriangle[triIdx] = triNodeIndex;
            }

            std::size_t previousTriSample = triangleCount;
            for (std::size_t sampleIdx = 0; sampleIdx < effectiveUnitTriangleBudget; ++sampleIdx) {
            std::size_t triIdx = sampleIdx;
            if (downsampleModelTriangles) {
                triIdx =
                    selectUniformTriangleIndex(sampleIdx, effectiveUnitTriangleBudget, triangleCount);
                if (triIdx == previousTriSample && triIdx + 1u < triangleCount) ++triIdx;
            }
            previousTriSample = triIdx;

            const std::size_t i = triIdx * 3u;
            const std::uint32_t i0 = mesh->indices[i + 0];
            const std::uint32_t i1 = mesh->indices[i + 1];
            const std::uint32_t i2 = mesh->indices[i + 2];
            if (i0 >= mesh->vertices.size() ||
                i1 >= mesh->vertices.size() ||
                i2 >= mesh->vertices.size()) {
                continue;
            }

            const auto& v0 = mesh->vertices[i0];
            const auto& v1 = mesh->vertices[i1];
            const auto& v2 = mesh->vertices[i2];

            const int triNodeIndex = triNodeIndexByTriangle[triIdx];

            const std::uint16_t triSubmeshIndex =
                (triIdx < mesh->triangleSubmesh.size())
                    ? mesh->triangleSubmesh[triIdx]
                    : static_cast<std::uint16_t>(0u);
            const bool texturedSubmesh =
                useIndexedWorldModelPath &&
                static_cast<std::size_t>(triSubmeshIndex) <
                    modelIndexedBatchesPerSubmesh.size() &&
                modelIndexedBatchesPerSubmesh[static_cast<std::size_t>(triSubmeshIndex)]
                        .textureRgba != nullptr &&
                modelIndexedBatchesPerSubmesh[static_cast<std::size_t>(triSubmeshIndex)]
                        .textureWidth > 0 &&
                modelIndexedBatchesPerSubmesh[static_cast<std::size_t>(triSubmeshIndex)]
                        .textureHeight > 0;
            if (useFastTexturedFullMeshPath && texturedSubmesh) {
                std::size_t fastBatchIndex = static_cast<std::size_t>(triSubmeshIndex);
                if (fastBatchIndex >= modelIndexedBatchesPerSubmesh.size()) fastBatchIndex = 0u;
                auto& fastBatch = modelIndexedBatchesPerSubmesh[fastBatchIndex];
                const bool useGpuSkinning = (fastBatch.gpuSkinning != 0u);
                const bool canReuseIndexedVertices =
                    fastBatchIndex < modelIndexedVertexRemap.size();
                const auto appendFastVertex = [&](std::uint32_t src,
                                                  const runtime::backend_model::MeshVertex& srcVertex)
                    -> std::uint32_t {
                    if (canReuseIndexedVertices &&
                        src < modelIndexedVertexRemap[fastBatchIndex].size()) {
                        int& mapped = modelIndexedVertexRemap[fastBatchIndex][src];
                        if (mapped >= 0) {
                            return static_cast<std::uint32_t>(mapped);
                        }
                        if (fastBatch.vertices.size() >=
                            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
                            return std::numeric_limits<std::uint32_t>::max();
                        }
                        const glm::vec3 pos = useGpuSkinning
                            ? transforms.resolveGpuSkinningInputPos(src, srcVertex)
                            : transforms.resolveWorldVertexPos(triNodeIndex, src, srcVertex);
                        const std::uint32_t next =
                            static_cast<std::uint32_t>(fastBatch.vertices.size());
                        IRenderBackend::WorldMeshVertex outVertex{};
                        outVertex.x = pos.x;
                        outVertex.y = pos.y;
                        outVertex.z = pos.z;
                        outVertex.u = srcVertex.uv.x;
                        outVertex.v = srcVertex.uv.y;
                        const glm::vec3 authoredVertexColor = mesh->hasVertexColor
                            ? glm::clamp(
                                glm::vec3(srcVertex.color.r, srcVertex.color.g, srcVertex.color.b),
                                0.0f,
                                1.0f)
                            : glm::vec3(1.0f);
                        const float authoredVertexAlpha = mesh->hasVertexColor
                            ? std::clamp(srcVertex.color.a, 0.0f, 1.0f)
                            : 1.0f;
                        const glm::vec3 finalVertexColor = authoredVertexColor * fastTexturedTint;
                        outVertex.r = finalVertexColor.r;
                        outVertex.g = finalVertexColor.g;
                        outVertex.b = finalVertexColor.b;
                        outVertex.a = fastTexturedAlpha * authoredVertexAlpha;
                        if (useGpuSkinning) {
                            outVertex.nx = srcVertex.normal.x;
                            outVertex.ny = srcVertex.normal.y;
                            outVertex.nz = srcVertex.normal.z;
                            outVertex.tx = srcVertex.tangent.x;
                            outVertex.ty = srcVertex.tangent.y;
                            outVertex.tz = srcVertex.tangent.z;
                            outVertex.tw = srcVertex.tangent.w;
                        } else {
                            const glm::vec3 nrm =
                                transforms.resolveModelVertexNormal(triNodeIndex, src, srcVertex);
                            outVertex.nx = nrm.x;
                            outVertex.ny = nrm.y;
                            outVertex.nz = nrm.z;
                            const glm::vec4 tan =
                                transforms.resolveModelVertexTangent(triNodeIndex, src, srcVertex);
                            outVertex.tx = tan.x;
                            outVertex.ty = tan.y;
                            outVertex.tz = tan.z;
                            outVertex.tw = tan.w;
                        }
                        if (useGpuSkinning) {
                            outVertex.joint0 = static_cast<float>(srcVertex.j0);
                            outVertex.joint1 = static_cast<float>(srcVertex.j1);
                            outVertex.joint2 = static_cast<float>(srcVertex.j2);
                            outVertex.joint3 = static_cast<float>(srcVertex.j3);
                            outVertex.weight0 = srcVertex.w0;
                            outVertex.weight1 = srcVertex.w1;
                            outVertex.weight2 = srcVertex.w2;
                            outVertex.weight3 = srcVertex.w3;
                        }
                        fastBatch.vertices.push_back(outVertex);
                        mapped = static_cast<int>(next);
                        return next;
                    }
                    if (fastBatch.vertices.size() >=
                        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
                        return std::numeric_limits<std::uint32_t>::max();
                    }
                    const glm::vec3 pos = useGpuSkinning
                        ? transforms.resolveGpuSkinningInputPos(src, srcVertex)
                        : transforms.resolveWorldVertexPos(triNodeIndex, src, srcVertex);
                    const std::uint32_t next =
                        static_cast<std::uint32_t>(fastBatch.vertices.size());
                    IRenderBackend::WorldMeshVertex outVertex{};
                    outVertex.x = pos.x;
                    outVertex.y = pos.y;
                    outVertex.z = pos.z;
                    outVertex.u = srcVertex.uv.x;
                    outVertex.v = srcVertex.uv.y;
                    const glm::vec3 authoredVertexColor = mesh->hasVertexColor
                        ? glm::clamp(
                            glm::vec3(srcVertex.color.r, srcVertex.color.g, srcVertex.color.b),
                            0.0f,
                            1.0f)
                        : glm::vec3(1.0f);
                    const float authoredVertexAlpha = mesh->hasVertexColor
                        ? std::clamp(srcVertex.color.a, 0.0f, 1.0f)
                        : 1.0f;
                    const glm::vec3 finalVertexColor = authoredVertexColor * fastTexturedTint;
                    outVertex.r = finalVertexColor.r;
                    outVertex.g = finalVertexColor.g;
                    outVertex.b = finalVertexColor.b;
                    outVertex.a = fastTexturedAlpha * authoredVertexAlpha;
                    if (useGpuSkinning) {
                        outVertex.nx = srcVertex.normal.x;
                        outVertex.ny = srcVertex.normal.y;
                        outVertex.nz = srcVertex.normal.z;
                        outVertex.tx = srcVertex.tangent.x;
                        outVertex.ty = srcVertex.tangent.y;
                        outVertex.tz = srcVertex.tangent.z;
                        outVertex.tw = srcVertex.tangent.w;
                    } else {
                        const glm::vec3 nrm =
                            transforms.resolveModelVertexNormal(triNodeIndex, src, srcVertex);
                        outVertex.nx = nrm.x;
                        outVertex.ny = nrm.y;
                        outVertex.nz = nrm.z;
                        const glm::vec4 tan =
                            transforms.resolveModelVertexTangent(triNodeIndex, src, srcVertex);
                        outVertex.tx = tan.x;
                        outVertex.ty = tan.y;
                        outVertex.tz = tan.z;
                        outVertex.tw = tan.w;
                    }
                    if (useGpuSkinning) {
                        outVertex.joint0 = static_cast<float>(srcVertex.j0);
                        outVertex.joint1 = static_cast<float>(srcVertex.j1);
                        outVertex.joint2 = static_cast<float>(srcVertex.j2);
                        outVertex.joint3 = static_cast<float>(srcVertex.j3);
                        outVertex.weight0 = srcVertex.w0;
                        outVertex.weight1 = srcVertex.w1;
                        outVertex.weight2 = srcVertex.w2;
                        outVertex.weight3 = srcVertex.w3;
                    }
                    fastBatch.vertices.push_back(outVertex);
                    return next;
                };

                const std::uint32_t outI0 = appendFastVertex(i0, v0);
                const std::uint32_t outI1 = appendFastVertex(i1, v1);
                const std::uint32_t outI2 = appendFastVertex(i2, v2);
                if (outI0 == std::numeric_limits<std::uint32_t>::max() ||
                    outI1 == std::numeric_limits<std::uint32_t>::max() ||
                    outI2 == std::numeric_limits<std::uint32_t>::max()) {
                    continue;
                }
                fastBatch.indices.push_back(outI0);
                fastBatch.indices.push_back(outI1);
                fastBatch.indices.push_back(outI2);
                continue;
            }

            const float triOpacity = (triIdx < mesh->triangleOpacity.size())
                ? mesh->triangleOpacity[triIdx]
                : 1.0f;
            // Textured indexed batches apply alpha in the pixel shader.
            // Avoid pre-multiplying with sampled triangle opacity (which would double-attenuate).
            const float alphaBase = std::clamp(modelFadeAlpha, 0.0f, 1.0f);
            const float alpha = texturedSubmesh
                ? alphaBase
                : alphaBase * std::clamp(triOpacity, 0.0f, 1.0f);
            if (alpha < 0.03f && !texturedSubmesh) continue;
            const bool triDoubleSided =
                (triIdx < mesh->triangleDoubleSided.size()) &&
                (mesh->triangleDoubleSided[triIdx] != 0u);

            glm::vec3 a(0.0f);
            glm::vec3 b(0.0f);
            glm::vec3 c(0.0f);
            glm::vec3 n0(0.0f, 1.0f, 0.0f);
            glm::vec3 n1(0.0f, 1.0f, 0.0f);
            glm::vec3 n2(0.0f, 1.0f, 0.0f);
            glm::vec4 t0(0.0f, 0.0f, 0.0f, 1.0f);
            glm::vec4 t1(0.0f, 0.0f, 0.0f, 1.0f);
            glm::vec4 t2(0.0f, 0.0f, 0.0f, 1.0f);
            if (useIndexedWorldModelPath) {
                a = transforms.resolveWorldVertexPos(triNodeIndex, i0, v0);
                b = transforms.resolveWorldVertexPos(triNodeIndex, i1, v1);
                c = transforms.resolveWorldVertexPos(triNodeIndex, i2, v2);
                n0 = transforms.resolveModelVertexNormal(triNodeIndex, i0, v0);
                n1 = transforms.resolveModelVertexNormal(triNodeIndex, i1, v1);
                n2 = transforms.resolveModelVertexNormal(triNodeIndex, i2, v2);
                t0 = transforms.resolveModelVertexTangent(triNodeIndex, i0, v0);
                t1 = transforms.resolveModelVertexTangent(triNodeIndex, i1, v1);
                t2 = transforms.resolveModelVertexTangent(triNodeIndex, i2, v2);
            } else {
                const auto sk0 = transforms.resolveWorldVertex(triNodeIndex, i0, v0);
                const auto sk1 = transforms.resolveWorldVertex(triNodeIndex, i1, v1);
                const auto sk2 = transforms.resolveWorldVertex(triNodeIndex, i2, v2);
                a = sk0.pos;
                b = sk1.pos;
                c = sk2.pos;
                n0 = sk0.normal;
                n1 = sk1.normal;
                n2 = sk2.normal;
                t0 = v0.tangent;
                t1 = v1.tangent;
                t2 = v2.tangent;
            }

            glm::vec3 baseColor0 = fallbackBase;
            glm::vec3 baseColor1 = fallbackBase;
            glm::vec3 baseColor2 = fallbackBase;
            auto resolveVertexBase = [&](std::uint32_t vi,
                                         const runtime::backend_model::MeshVertex& v) {
                if (texturedSubmesh) {
                    // For textured glTF submeshes, preserve texture albedo.
                    // Use authored vertex color only when it exists in source.
                    if (mesh->hasVertexColor) {
                        return glm::clamp(
                            glm::vec3(v.color.r, v.color.g, v.color.b), 0.0f, 1.0f);
                    }
                    return glm::vec3(1.0f);
                }
                // For backend world-lit model rendering, do NOT use cached vertexBaseColors.
                // Those are legacy precomposed/tonemapped colors and will darken/desaturate when
                // fed through the modern PBR+ACES path again.
                if (mesh->hasVertexColor) {
                    return glm::clamp(
                        glm::vec3(v.color.r, v.color.g, v.color.b), 0.0f, 1.0f);
                }
                if (triIdx < mesh->triangleSubmesh.size() &&
                    !mesh->submeshBaseColors.empty()) {
                    const std::uint16_t submeshIndex = mesh->triangleSubmesh[triIdx];
                    if (submeshIndex < mesh->submeshBaseColors.size()) {
                        const glm::vec4 subColor = mesh->submeshBaseColors[submeshIndex];
                        return glm::clamp(
                            glm::vec3(subColor.r, subColor.g, subColor.b), 0.0f, 1.0f);
                    }
                }
                (void)vi;
                return fallbackBase;
            };
            baseColor0 = resolveVertexBase(i0, v0);
            baseColor1 = resolveVertexBase(i1, v1);
            baseColor2 = resolveVertexBase(i2, v2);
            if (!strictGltfParity && captureVisualTintStrength > 0.001f) {
                const float tintAmt = std::clamp(captureVisualTintStrength, 0.0f, 1.0f);
                baseColor0 = glm::mix(baseColor0, captureTintColor, tintAmt);
                baseColor1 = glm::mix(baseColor1, captureTintColor, tintAmt);
                baseColor2 = glm::mix(baseColor2, captureTintColor, tintAmt);
            }
            triangleSubmitter.pushTriangle(
                a,
                b,
                c,
                i0,
                i1,
                i2,
                v0.uv,
                v1.uv,
                v2.uv,
                n0,
                n1,
                n2,
                t0,
                t1,
                t2,
                baseColor0,
                baseColor1,
                baseColor2,
                triSubmeshIndex,
                alpha,
                triDoubleSided);
            }
        }
        bool queuedIndexedBatch = false;
        if (useIndexedWorldModelPath && !modelIndexedBatchesPerSubmesh.empty()) {
            for (auto& batch : modelIndexedBatchesPerSubmesh) {
                if (!batch.hasGeometry()) continue;
                worldIndexedBatches.push_back(std::move(batch));
                queuedIndexedBatch = true;
            }
        }

        drewModelMesh = runtime::backend_units::didAccumulateModelGeometry(
            modelDepthCountBefore,
            modelDepthTris.size(),
            modelDepthWorldCountBefore,
            modelDepthWorldTris.size()) ||
            (world3DTriangles.size() > world3DTriangleCountBefore) ||
            queuedIndexedBatch;
    }
    out.drewModelMesh = drewModelMesh;
    return out;
}

} // namespace game::runtime::shared_projected_unit_backend_mesh


