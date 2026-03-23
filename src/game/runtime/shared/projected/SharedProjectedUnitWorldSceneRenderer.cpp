#include "game/runtime/shared/projected/SharedProjectedUnitWorldSceneRenderer.h"

#include "game/runtime/shared/projected/SharedProjectedRenderItems.h"
#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshPrep.h"
#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshSupport.h"
#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshTransforms.h"
#include "game/runtime/shared/scene/SharedWorldScene.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireMeshPlayback.h"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace support = game::runtime::shared_projected_unit_backend_mesh_support;
namespace prep = game::runtime::shared_projected_unit_backend_mesh_prep;
namespace persistent = game::runtime::shared_projected_render_items;

namespace game::runtime::shared_projected_unit_world_scene {

namespace {

std::vector<support::GpuSkinBatchState>& batchSkinStateScratch() {
    static thread_local std::vector<support::GpuSkinBatchState> scratch;
    return scratch;
}

std::vector<std::uint8_t>& batchUsesSceneSkinningScratch() {
    static thread_local std::vector<std::uint8_t> scratch;
    return scratch;
}

} // namespace

bool tryRenderProjectedUnitModelWorldScene(
    const shared_projected_unit_models::Args& args,
    shared_projected_unit_models::Result& out) {
    if (!args.renderer ||
        !args.renderer->supportsWorldSceneFastPath() ||
        !args.worldSceneRegistry ||
        !args.worldSceneFrame ||
        !args.projectedRenderItems ||
        !args.unit ||
        !args.meshForUnit ||
        !args.scenePose ||
        !args.tint ||
        !args.modelDepthTris ||
        !args.modelDepthWorldTris ||
        !args.remainingModelTrianglesBudget ||
        !args.world3DTriangles ||
        !args.backendModelTriangleLimit ||
        !args.backendModelFullMeshEnabled ||
        !args.backendModelFastTexturedPathEnabled ||
        !args.backendModelBackfaceCullingEnabled) {
        return false;
    }

    // Charmander's authored tail-fire mesh still relies on the legacy projected-model
    // path, which patches the fire submesh material/atlas and marks the mesh carrier
    // anchor. Until the world-scene path learns that same override, keep this species
    // on the proven fallback path.
    const bool tailFireMeshPlaybackSpecies =
        args.unit->alive &&
        !args.unit->fainting &&
        game::runtime::shared_tail_fire_mesh_playback::isTailFireMeshPlaybackSpecies(
            args.unit->name);
    if (tailFireMeshPlaybackSpecies) {
        return false;
    }

    using Clock = std::chrono::high_resolution_clock;
    const auto prepStart = Clock::now();

    prep::PreparedState prepared;
    if (!prep::prepareProjectedUnitBackendMeshWorldScene(args, out, prepared)) {
        if (args.perfBreakdown) {
            args.perfBreakdown->prepMs +=
                std::chrono::duration<double, std::milli>(Clock::now() - prepStart).count();
        }
        return out.skipUnit;
    }

    if (!prepared.useFastTexturedFullMeshPath ||
        !prepared.fullIndexedMeshPath ||
        !prepared.mesh ||
        !prepared.submeshNodeFallback ||
        prepared.modelIndexedBatchCount == 0u) {
        if (args.perfBreakdown) {
            args.perfBreakdown->prepMs +=
                std::chrono::duration<double, std::milli>(Clock::now() - prepStart).count();
        }
        return false;
    }

    const bool preferFullGpuSkinning =
        support::backendPrefersFullGpuSkinning(args.backendId);
    const support::FastTexturedMeshTemplateCache* fastCache =
        support::ensureFastTexturedMeshTemplateCache(
            prepared.mesh,
            *prepared.submeshNodeFallback,
            prepared.modelIndexedBatchCount,
            preferFullGpuSkinning);
    if (!fastCache || fastCache->batches.empty()) {
        if (args.perfBreakdown) {
            args.perfBreakdown->prepMs +=
                std::chrono::duration<double, std::milli>(Clock::now() - prepStart).count();
        }
        return false;
    }
    const support::FastTexturedMaterialTemplateCache* materialCache =
        support::ensureFastTexturedMaterialTemplateCache(
            prepared.mesh,
            prepared.modelIndexedBatchCount,
            args.characterInkingEnabled);
    if (!materialCache ||
        materialCache->materials.size() != prepared.modelIndexedBatchCount) {
        if (args.perfBreakdown) {
            args.perfBreakdown->prepMs +=
                std::chrono::duration<double, std::milli>(Clock::now() - prepStart).count();
        }
        return false;
    }

    IRenderBackend::WorldSceneFastPathCaps fastPathCaps{};
    (void)args.renderer->getWorldSceneFastPathCaps(fastPathCaps);

    for (std::size_t fastBatchIndex = 0; fastBatchIndex < fastCache->batches.size(); ++fastBatchIndex) {
        const auto& batchTemplate = fastCache->batches[fastBatchIndex];
        if (batchTemplate.gpuTemplateVertices.empty() ||
            batchTemplate.indices.size() < 3u ||
            batchTemplate.baseSubmeshIndex >= materialCache->materials.size()) {
            if (args.perfBreakdown) {
                args.perfBreakdown->prepMs +=
                    std::chrono::duration<double, std::milli>(Clock::now() - prepStart).count();
            }
            return false;
        }

        const auto& materialTemplate =
            materialCache->materials[batchTemplate.baseSubmeshIndex];
        if (prepared.fastTexturedAlpha < 0.999f ||
            materialTemplate.blendMode != 0u ||
            materialTemplate.materialMode != 2u ||
            materialTemplate.alphaMode == 2u) {
            if (args.perfBreakdown) {
                args.perfBreakdown->prepMs +=
                    std::chrono::duration<double, std::milli>(Clock::now() - prepStart).count();
            }
            return false;
        }
    }

    auto& batchSkinStates = batchSkinStateScratch();
    batchSkinStates.assign(fastCache->batches.size(), support::GpuSkinBatchState{});
    auto& batchUsesSceneSkinning = batchUsesSceneSkinningScratch();
    batchUsesSceneSkinning.assign(fastCache->batches.size(), 0u);
    shared_projected_unit_backend_mesh_transforms::Resolver transforms;
    bool transformsInitialized = false;
    auto& gpuSkinBatchStates = support::gpuSkinBatchStateEntries();
    gpuSkinBatchStates.clear();
    gpuSkinBatchStates.reserve(fastCache->batches.size());
    support::GpuSkinBatchStateEntry* lastGpuSkinBatchState = nullptr;

    auto ensureTransformsInitialized = [&]() {
        if (!transformsInitialized) {
            transforms.initialize(args, prepared);
            transformsInitialized = true;
        }
    };

    auto tryResolveSkinnedBatchState =
        [&](const support::FastTexturedBatchTemplate& batchTemplate,
            int resolvedTriNodeIndex,
            support::GpuSkinBatchState& outState) -> bool {
            if (!fastPathCaps.supportsSkinnedInstancing || !args.enableGpuClipSkinning) {
                return false;
            }

            ensureTransformsInitialized();
            const int skinCacheKey = transforms.gpuSkinningCacheKeyForNode(resolvedTriNodeIndex);
            if (skinCacheKey < 0) {
                return false;
            }

            const bool hasJointPalette = !batchTemplate.gpuJointPalette.empty();
            const std::size_t paletteCount = hasJointPalette
                ? std::min(batchTemplate.gpuJointPalette.size(), support::kMaxGpuSkinMatrices)
                : 0u;
            const auto matchesBatchStateKey =
                [&](const support::UnitSkinMatrixKey& key) {
                    if (key.unitId != args.unit->id ||
                        key.skinKey != skinCacheKey ||
                        key.paletteSize != static_cast<std::uint32_t>(paletteCount)) {
                        return false;
                    }
                    if (paletteCount == 0u) {
                        return true;
                    }
                    return std::memcmp(key.palette.data(),
                                       batchTemplate.gpuJointPalette.data(),
                                       paletteCount * sizeof(std::uint16_t)) == 0;
                };

            support::GpuSkinBatchStateEntry* matchedEntry = nullptr;
            if (lastGpuSkinBatchState && matchesBatchStateKey(lastGpuSkinBatchState->key)) {
                matchedEntry = lastGpuSkinBatchState;
            } else {
                auto stateIt = std::find_if(
                    gpuSkinBatchStates.begin(),
                    gpuSkinBatchStates.end(),
                    [&](const support::GpuSkinBatchStateEntry& entry) {
                        return matchesBatchStateKey(entry.key);
                    });
                if (stateIt != gpuSkinBatchStates.end()) {
                    matchedEntry = &(*stateIt);
                }
            }

            if (!matchedEntry) {
                support::UnitSkinMatrixKey batchStateKey{};
                batchStateKey.unitId = args.unit->id;
                batchStateKey.skinKey = skinCacheKey;
                batchStateKey.paletteSize = static_cast<std::uint32_t>(paletteCount);
                if (paletteCount > 0u) {
                    std::memcpy(batchStateKey.palette.data(),
                                batchTemplate.gpuJointPalette.data(),
                                paletteCount * sizeof(std::uint16_t));
                }

                support::GpuSkinBatchStateEntry newEntry{};
                newEntry.key = batchStateKey;
                auto& sharedSkinMatrices = support::unitSkinMatrices()[batchStateKey];
                if (transforms.configureGpuClipSkinningBatch(
                        resolvedTriNodeIndex,
                        hasJointPalette ? &batchTemplate.gpuJointPalette : nullptr,
                        newEntry.state.modelMatrix,
                        newEntry.state.gpuSkinningMode,
                        sharedSkinMatrices,
                        newEntry.state.skinMatrixCount)) {
                    newEntry.state.valid = true;
                    newEntry.state.sharedSkinMatrices =
                        sharedSkinMatrices.empty() ? nullptr : sharedSkinMatrices.data();
                }
                gpuSkinBatchStates.emplace_back(std::move(newEntry));
                matchedEntry = &gpuSkinBatchStates.back();
            }

            lastGpuSkinBatchState = matchedEntry;
            if (!matchedEntry->state.valid ||
                matchedEntry->state.sharedSkinMatrices == nullptr ||
                matchedEntry->state.skinMatrixCount == 0u) {
                return false;
            }

            outState = matchedEntry->state;
            return true;
        };

    for (std::size_t fastBatchIndex = 0; fastBatchIndex < fastCache->batches.size(); ++fastBatchIndex) {
        const auto& batchTemplate = fastCache->batches[fastBatchIndex];
        if (!batchTemplate.skinnedBatch) {
            continue;
        }

        int resolvedTriNodeIndex = batchTemplate.triNodeIndex;
        if (resolvedTriNodeIndex < 0 && fastCache->defaultSkinNodeIndex >= 0) {
            resolvedTriNodeIndex = fastCache->defaultSkinNodeIndex;
        }
        if (!tryResolveSkinnedBatchState(
                batchTemplate,
                resolvedTriNodeIndex,
                batchSkinStates[fastBatchIndex])) {
            if (args.perfBreakdown) {
                args.perfBreakdown->prepMs +=
                    std::chrono::duration<double, std::milli>(Clock::now() - prepStart).count();
            }
            return false;
        }
        batchUsesSceneSkinning[fastBatchIndex] = 1u;
    }

    if (args.perfBreakdown) {
        args.perfBreakdown->prepMs +=
            std::chrono::duration<double, std::milli>(Clock::now() - prepStart).count();
    }

    const auto sceneColor = prepared.fastTexturedTint;
    const float sceneAlpha = prepared.fastTexturedAlpha;

    for (std::size_t fastBatchIndex = 0; fastBatchIndex < fastCache->batches.size(); ++fastBatchIndex) {
        const auto& batchTemplate = fastCache->batches[fastBatchIndex];
        const auto& materialTemplate =
            materialCache->materials[batchTemplate.baseSubmeshIndex];

        persistent::ProjectedRenderItemKey itemKey{};
        itemKey.unitId = args.unit->id;
        itemKey.mesh = prepared.mesh;
        itemKey.itemIndex = static_cast<std::uint32_t>(fastBatchIndex);
        auto& itemEntry =
            persistent::ensureProjectedRenderItem(*args.projectedRenderItems, itemKey);
        persistent::touchProjectedRenderItem(*args.projectedRenderItems, itemEntry);

        if (!itemEntry.worldSceneObjectHandle ||
            itemEntry.worldSceneRegistryGeneration != args.worldSceneRegistry->generation) {
            const auto geometryHandle = shared_world_scene::ensureRigidGeometry(
                *args.worldSceneRegistry,
                &batchTemplate,
                batchTemplate.geometryCacheKey.c_str(),
                batchTemplate.gpuTemplateVertices.data(),
                batchTemplate.gpuTemplateVertices.size(),
                batchTemplate.indices.data(),
                batchTemplate.indices.size());
            const auto materialHandle = shared_world_scene::ensureMaterial(
                *args.worldSceneRegistry,
                &materialTemplate,
                materialTemplate);
            itemEntry.worldSceneObjectHandle = shared_world_scene::ensureRenderObject(
                *args.worldSceneRegistry,
                geometryHandle,
                materialHandle,
                shared_world_scene::PipelineVariant::OpaqueLit,
                static_cast<std::uint32_t>(fastBatchIndex),
                batchTemplate.skinnedBatch);
            itemEntry.worldSceneRegistryGeneration = args.worldSceneRegistry->generation;
        }

        IRenderBackend::WorldSceneRenderInstanceHandle instanceHandle{};
        instanceHandle.id =
            (static_cast<std::uint32_t>(args.unit->id) << 16u) ^
            static_cast<std::uint32_t>(fastBatchIndex + 1u);
        if (batchUsesSceneSkinning[fastBatchIndex] != 0u) {
            const auto& skinState = batchSkinStates[fastBatchIndex];
            shared_world_scene::appendSkinnedInstance(
                *args.worldSceneFrame,
                itemEntry.worldSceneObjectHandle,
                instanceHandle,
                skinState.modelMatrix,
                sceneColor.r,
                sceneColor.g,
                sceneColor.b,
                sceneAlpha,
                prepared.indexedBatchSortDepth,
                skinState.gpuSkinningMode,
                skinState.skinMatrixCount,
                skinState.sharedSkinMatrices);
        } else {
            shared_world_scene::appendRigidInstance(
                *args.worldSceneFrame,
                itemEntry.worldSceneObjectHandle,
                instanceHandle,
                prepared.modelMatrix,
                sceneColor.r,
                sceneColor.g,
                sceneColor.b,
                sceneAlpha,
                prepared.indexedBatchSortDepth);
        }
    }

    out.drewModelMesh = true;
    return true;
}

} // namespace game::runtime::shared_projected_unit_world_scene
