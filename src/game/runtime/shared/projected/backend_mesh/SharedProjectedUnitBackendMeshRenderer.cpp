#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshRenderer.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshCachedIndexedBatches.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshFastPath.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshIndexedFinalize.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshPersistentItems.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshPrep.h"
#include "game/runtime/shared/projected/core/SharedProjectedRenderItems.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshSupport.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshTriangleLoop.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshTransforms.h"

#include "engine/render/Model.h"
#include "game/runtime/render_prep/UnitVisuals.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

namespace support = game::runtime::shared_projected_unit_backend_mesh_support;
namespace cached_indexed_batches =
    game::runtime::shared_projected_unit_backend_mesh_cached_indexed_batches;
namespace indexed_finalize = game::runtime::shared_projected_unit_backend_mesh_indexed_finalize;
namespace mesh_persistent = game::runtime::shared_projected_unit_backend_mesh_persistent;
namespace triangle_loop = game::runtime::shared_projected_unit_backend_mesh_triangle_loop;

namespace game::runtime::shared_projected_unit_backend_mesh {

std::size_t prewarmProjectedUnitBackendMeshGeometryCache(
    IRenderBackend& renderer,
    const runtime::render_model::MeshData& mesh) {
    if (!renderer.supportsWorldIndexedMeshes()) return 0u;
    if (mesh.vertices.empty() || mesh.indices.size() < 3u) return 0u;

    const std::size_t baseBatchCount =
        std::max<std::size_t>(1u, mesh.submeshBaseTextures.size());
    const std::vector<int> submeshNodeFallback = support::buildSubmeshNodeFallback(mesh);
    const bool preferFullGpuSkinning =
        support::backendPrefersFullGpuSkinning(renderer.backendId());
    const support::FastTexturedMeshTemplateCache* fastCache =
        support::ensureFastTexturedMeshTemplateCache(
            &mesh,
            submeshNodeFallback,
            baseBatchCount,
            preferFullGpuSkinning);
    if (!fastCache) return 0u;

    std::size_t warmed = 0u;
    for (std::size_t bi = 0; bi < fastCache->batches.size(); ++bi) {
        const auto& batch = fastCache->batches[bi];
        if (batch.gpuTemplateVertices.empty() || batch.indices.size() < 3u) continue;
        renderer.prewarmWorldIndexedMeshCached(
            batch.geometryCacheKey.c_str(),
            batch.gpuTemplateVertices.data(),
            batch.gpuTemplateVertices.size(),
            batch.indices.data(),
            batch.indices.size());
        ++warmed;
    }
    return warmed;
}

Result renderProjectedUnitBackendMesh(const Args& args) {
    Result out{};
    if (!args.dataDb || !args.unit || !args.pose || !args.meshForUnit || !args.scenePose ||
        !args.tint || !args.projectedDebug ||
        !args.worldIndexedBatches || !args.modelDepthTris || !args.modelDepthWorldTris ||
        !args.remainingModelTrianglesBudget || !args.world3DTriangles ||
        !args.backendModelTriangleLimit || !args.backendModelFullMeshEnabled ||
        !args.backendModelFastTexturedPathEnabled || !args.backendModelBackfaceCullingEnabled) {
        return out;
    }

    const auto& unit = *args.unit;
    const auto* meshForUnit = args.meshForUnit;
    auto* projectedRenderItems = args.projectedRenderItems;

    const float captureVisualTintStrength = args.captureVisualTintStrength;
    const float modelFadeAlpha = args.modelFadeAlpha;
    const glm::vec3 captureTintColor = args.captureTintColor;
    const glm::vec3 cameraWorldPos = args.cameraWorldPos;

    auto& worldIndexedBatches = *args.worldIndexedBatches;
    auto& modelDepthTris = *args.modelDepthTris;
    auto& modelDepthWorldTris = *args.modelDepthWorldTris;
    auto& world3DTriangles = *args.world3DTriangles;

    const bool strictGltfParity = support::strictGltfParityEnabled();
    const bool enableGpuClipSkinning = args.enableGpuClipSkinning;

    bool drewModelMesh = false;
    if (meshForUnit) {
        std::uint32_t sharedRigidBatches = 0u;
        std::uint32_t gpuClipSkinBatches = 0u;
        std::uint32_t gpuClipPaletteBatches = 0u;
        std::uint32_t cpuRewriteBatches = 0u;
        std::uint32_t indexedBatchesQueued = 0u;
        using Clock = std::chrono::high_resolution_clock;
        const auto prepStart = Clock::now();
        auto& prep = support::preparedMeshState();
        if (!shared_projected_unit_backend_mesh_prep::prepareProjectedUnitBackendMesh(args, out, prep)) {
            if (args.perfBreakdown) {
                args.perfBreakdown->prepMs +=
                    std::chrono::duration<double, std::milli>(Clock::now() - prepStart).count();
            }
            return out;
        }

        const runtime::render_model::MeshData* mesh = prep.mesh;
        const bool useIndexedWorldModelPath = prep.useIndexedWorldModelPath;
        const bool useFastTexturedFullMeshPath = prep.useFastTexturedFullMeshPath;
        const std::size_t modelDepthCountBefore = prep.modelDepthCountBefore;
        const std::size_t modelDepthWorldCountBefore = prep.modelDepthWorldCountBefore;
        const std::size_t world3DTriangleCountBefore = prep.world3DTriangleCountBefore;
        const auto& submeshNodeFallback = *prep.submeshNodeFallback;
        auto& modelIndexedBatchesPerSubmesh = prep.modelIndexedBatchesPerSubmesh;
        const auto& nodeGlobals =
            (prep.scenePose && prep.scenePose->hasScenePose) ? prep.scenePose->nodeGlobals
                                                             : mesh->bindNodeGlobals;
        const float fastTexturedAlpha = prep.fastTexturedAlpha;
        const glm::vec3& fastTexturedTint = prep.fastTexturedTint;
        const mesh_persistent::SyncContext persistentSync{
            .projectedRenderItems = projectedRenderItems,
            .mesh = mesh,
            .unitId = unit.id,
            .frameStamp = projectedRenderItems ? projectedRenderItems->currentFrameId : 0u,
        };
        bool cpuRewritePoseHashReady = false;
        std::uint64_t cpuRewritePoseHash = 0ull;
        shared_projected_unit_backend_mesh_transforms::Resolver transforms;
        transforms.initialize(args, prep);
        const auto geometryStart = Clock::now();
        if (args.perfBreakdown) {
            args.perfBreakdown->prepMs +=
                std::chrono::duration<double, std::milli>(geometryStart - prepStart).count();
        }

        bool handledFastTexturedPath = false;
        bool queuedIndexedBatch = false;
        const support::FastTexturedMeshTemplateCache* fastCachePtr = nullptr;
        std::vector<std::uint8_t> batchUsesGpuClipPalette;
        if (useFastTexturedFullMeshPath && !modelIndexedBatchesPerSubmesh.empty()) {
            const bool preferFullGpuSkinning =
                support::backendPrefersFullGpuSkinning(args.backendId);
            fastCachePtr = support::ensureFastTexturedMeshTemplateCache(
                mesh,
                submeshNodeFallback,
                modelIndexedBatchesPerSubmesh.size(),
                preferFullGpuSkinning);
        }
        if (fastCachePtr) {
            const auto directFastPathResult =
                shared_projected_unit_backend_mesh_fast_path::tryQueueDirectFastTexturedWorldBatches(
                    {
                        .unit = &unit,
                        .prep = &prep,
                        .transforms = &transforms,
                        .nodeGlobals = &nodeGlobals,
                        .fastCache = fastCachePtr,
                        .fastTexturedAlpha = fastTexturedAlpha,
                        .fastTexturedTint = fastTexturedTint,
                        .cameraWorldPos = cameraWorldPos,
                        .proxyCenter = args.proxyCenter,
                        .modelFadeAlpha = modelFadeAlpha,
                        .enableGpuClipSkinning = enableGpuClipSkinning,
                        .worldIndexedBatches = &worldIndexedBatches,
                        .modelIndexedBatchesPerSubmesh = &modelIndexedBatchesPerSubmesh,
                        .persistentSync = persistentSync,
                    });
            handledFastTexturedPath = directFastPathResult.handled;
            queuedIndexedBatch = directFastPathResult.queuedIndexedBatch;
            sharedRigidBatches += directFastPathResult.sharedRigidBatches;
            gpuClipSkinBatches += directFastPathResult.gpuClipSkinBatches;
            gpuClipPaletteBatches += directFastPathResult.gpuClipPaletteBatches;
            indexedBatchesQueued += directFastPathResult.indexedBatchesQueued;
        }
        if (!handledFastTexturedPath && fastCachePtr) {
            const auto cachedIndexedResult =
                cached_indexed_batches::buildCachedIndexedBatches(
                    {
                        .unit = &unit,
                        .mesh = mesh,
                        .prep = &prep,
                        .transforms = &transforms,
                        .nodeGlobals = &nodeGlobals,
                        .fastCache = fastCachePtr,
                        .fastTexturedAlpha = fastTexturedAlpha,
                        .fastTexturedTint = fastTexturedTint,
                        .enableGpuClipSkinning = enableGpuClipSkinning,
                        .persistentSync = persistentSync,
                        .cpuRewritePoseHashReady = &cpuRewritePoseHashReady,
                        .cpuRewritePoseHash = &cpuRewritePoseHash,
                        .modelIndexedBatchesPerSubmesh = &modelIndexedBatchesPerSubmesh,
                        .batchUsesGpuClipPalette = &batchUsesGpuClipPalette,
                    });
            handledFastTexturedPath = cachedIndexedResult.handled;
            sharedRigidBatches += cachedIndexedResult.sharedRigidBatches;
            gpuClipSkinBatches += cachedIndexedResult.gpuClipSkinBatches;
            gpuClipPaletteBatches += cachedIndexedResult.gpuClipPaletteBatches;
            cpuRewriteBatches += cachedIndexedResult.cpuRewriteBatches;
        }

        if (!handledFastTexturedPath) {
            triangle_loop::appendFallbackTriangles(
                {
                    .renderArgs = &args,
                    .prep = &prep,
                    .transforms = &transforms,
                    .strictGltfParity = strictGltfParity,
                    .enableGpuClipSkinning = enableGpuClipSkinning,
                    .captureVisualTintStrength = captureVisualTintStrength,
                    .captureTintColor = captureTintColor,
                    .modelFadeAlpha = modelFadeAlpha,
                    .cameraWorldPos = cameraWorldPos,
                });
        }
        if (useIndexedWorldModelPath && !modelIndexedBatchesPerSubmesh.empty()) {
            const auto indexedFinalizeResult =
                indexed_finalize::finalizeIndexedWorldBatches(
                    {
                        .renderArgs = &args,
                        .mesh = mesh,
                        .submeshNodeFallback = &submeshNodeFallback,
                        .fastCache = fastCachePtr,
                        .persistentSync = &persistentSync,
                        .modelIndexedBatchesPerSubmesh = &modelIndexedBatchesPerSubmesh,
                        .batchUsesGpuClipPalette = &batchUsesGpuClipPalette,
                        .worldIndexedBatches = &worldIndexedBatches,
                    });
            queuedIndexedBatch = queuedIndexedBatch || indexedFinalizeResult.queuedIndexedBatch;
            sharedRigidBatches += indexedFinalizeResult.sharedRigidBatches;
            gpuClipSkinBatches += indexedFinalizeResult.gpuClipSkinBatches;
            gpuClipPaletteBatches += indexedFinalizeResult.gpuClipPaletteBatches;
            cpuRewriteBatches += indexedFinalizeResult.cpuRewriteBatches;
            indexedBatchesQueued += indexedFinalizeResult.indexedBatchesQueued;
        }

        drewModelMesh = runtime::render_prep_units::didAccumulateModelGeometry(
            modelDepthCountBefore,
            modelDepthTris.size(),
            modelDepthWorldCountBefore,
            modelDepthWorldTris.size()) ||
            (world3DTriangles.size() > world3DTriangleCountBefore) ||
            queuedIndexedBatch;
        if (args.perfBreakdown) {
            args.perfBreakdown->geometryMs +=
                std::chrono::duration<double, std::milli>(Clock::now() - geometryStart).count();
            args.perfBreakdown->sharedRigidBatches += sharedRigidBatches;
            args.perfBreakdown->gpuClipSkinBatches += gpuClipSkinBatches;
            args.perfBreakdown->gpuClipPaletteBatches += gpuClipPaletteBatches;
            args.perfBreakdown->cpuRewriteBatches += cpuRewriteBatches;
            args.perfBreakdown->indexedBatchesQueued += indexedBatchesQueued;
        }
    }
    out.drewModelMesh = drewModelMesh;
    return out;
}

} // namespace game::runtime::shared_projected_unit_backend_mesh





