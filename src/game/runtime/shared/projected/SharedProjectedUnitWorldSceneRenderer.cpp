#include "game/runtime/shared/projected/SharedProjectedUnitWorldSceneRenderer.h"

#include "game/runtime/shared/projected/SharedProjectedRenderItems.h"
#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshPrep.h"
#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshSupport.h"
#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshTransforms.h"
#include "game/runtime/shared/scene/SharedWorldScene.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireMeshPlayback.h"
#include "engine/core/Environment.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <glm/gtc/type_ptr.hpp>

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

std::vector<int>& resolvedTriNodeIndexScratch() {
    static thread_local std::vector<int> scratch;
    return scratch;
}

bool batchNeedsSceneSkinning(const support::FastTexturedBatchTemplate& batchTemplate) {
    return batchTemplate.skinnedBatch || !batchTemplate.gpuJointPalette.empty();
}

std::array<float, 16> buildRigidBatchModelMatrix(
    const prep::PreparedState& prepared,
    const game::runtime::shared_backend_pose::PoseEval* scenePose,
    int resolvedTriNodeIndex) {
    std::array<float, 16> outModelMatrix = prepared.modelMatrix;
    if (!scenePose ||
        !scenePose->hasScenePose ||
        resolvedTriNodeIndex < 0 ||
        static_cast<std::size_t>(resolvedTriNodeIndex) >= scenePose->nodeGlobals.size()) {
        return outModelMatrix;
    }

    const glm::mat4 batchModel =
        prepared.modelM * scenePose->nodeGlobals[static_cast<std::size_t>(resolvedTriNodeIndex)];
    const float* batchModelData = glm::value_ptr(batchModel);
    std::copy(batchModelData, batchModelData + 16u, outModelMatrix.begin());
    return outModelMatrix;
}

std::string toLowerCopy(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (const char c : value) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

const std::vector<std::string>& worldSceneTraceTokens() {
    static const std::vector<std::string> tokens = [] {
        std::vector<std::string> out;
        const auto env = engine::env::get("PAC_TRACE_PROJECTED_WORLD_SCENE");
        if (!env.has_value()) return out;

        std::string current;
        auto flush = [&]() {
            if (current.empty()) return;
            out.push_back(toLowerCopy(current));
            current.clear();
        };
        for (const char c : *env) {
            if (c == ',' || c == ';' || std::isspace(static_cast<unsigned char>(c))) {
                flush();
            } else {
                current.push_back(c);
            }
        }
        flush();
        return out;
    }();
    return tokens;
}

const std::vector<std::string>& worldSceneDisableTokens() {
    static const std::vector<std::string> tokens = [] {
        std::vector<std::string> out;
        const auto env = engine::env::get("PAC_DISABLE_PROJECTED_WORLD_SCENE");
        if (!env.has_value()) return out;

        std::string current;
        auto flush = [&]() {
            if (current.empty()) return;
            out.push_back(toLowerCopy(current));
            current.clear();
        };
        for (const char c : *env) {
            if (c == ',' || c == ';' || std::isspace(static_cast<unsigned char>(c))) {
                flush();
            } else {
                current.push_back(c);
            }
        }
        flush();
        return out;
    }();
    return tokens;
}

const std::string& worldSceneTraceFilePath() {
    static const std::string path = []() {
        const auto env = engine::env::get("PAC_TRACE_PROJECTED_WORLD_SCENE_FILE");
        if (env.has_value() && !env->empty()) {
            return *env;
        }
        return std::string("debug_projected_world_scene_trace.log");
    }();
    return path;
}

void appendWorldSceneTraceLineImpl(std::string_view line) {
    if (line.empty()) {
        return;
    }

    std::cout << line << "\n" << std::flush;

    const std::string& path = worldSceneTraceFilePath();
    if (path.empty()) {
        return;
    }

    static std::mutex traceFileMutex;
    std::lock_guard<std::mutex> lock(traceFileMutex);
    std::error_code ec;
    const std::filesystem::path fsPath(path);
    if (!fsPath.parent_path().empty()) {
        std::filesystem::create_directories(fsPath.parent_path(), ec);
    }
    std::ofstream out(fsPath, std::ios::app);
    if (!out.is_open()) {
        return;
    }
    out << line << "\n";
    out.flush();
}

bool tokenListMatchesUnit(const std::vector<std::string>& tokens, const PokemonInstance& unit) {
    if (tokens.empty()) return false;
    const std::string unitName = toLowerCopy(unit.name);
    const std::string unitId = std::to_string(unit.id);
    for (const std::string& token : tokens) {
        if (token == "*" || token == unitName || token == unitId) {
            return true;
        }
    }
    return false;
}

bool shouldTraceWorldSceneUnit(const PokemonInstance& unit) {
    return tokenListMatchesUnit(worldSceneTraceTokens(), unit);
}

bool shouldDisableWorldSceneUnit(const PokemonInstance& unit) {
    return tokenListMatchesUnit(worldSceneDisableTokens(), unit);
}

std::uint64_t fnv1a64Append(std::uint64_t hash, const void* data, std::size_t byteCount) {
    static constexpr std::uint64_t kPrime = 1099511628211ull;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < byteCount; ++i) {
        hash ^= static_cast<std::uint64_t>(bytes[i]);
        hash *= kPrime;
    }
    return hash;
}

std::uint64_t hashPoseEval(const game::runtime::shared_backend_pose::PoseEval* scenePose) {
    if (!scenePose || !scenePose->hasScenePose || scenePose->nodeGlobals.empty()) {
        return 0ull;
    }
    std::uint64_t hash = 14695981039346656037ull;
    for (const glm::mat4& nodeGlobal : scenePose->nodeGlobals) {
        hash = fnv1a64Append(hash, glm::value_ptr(nodeGlobal), sizeof(float) * 16u);
    }
    return hash;
}

std::uint64_t hashSkinPayload(const support::GpuSkinBatchState& state) {
    if (!state.sharedSkinMatrices || state.skinMatrixCount == 0u) {
        return 0ull;
    }
    const std::size_t floatCount =
        static_cast<std::size_t>(state.skinMatrixCount) *
        static_cast<std::size_t>(state.gpuSkinningMode == 1u ? 32u : 16u);
    std::uint64_t hash = 14695981039346656037ull;
    hash = fnv1a64Append(hash, state.sharedSkinMatrices, floatCount * sizeof(float));
    hash = fnv1a64Append(hash, &state.skinMatrixCount, sizeof(state.skinMatrixCount));
    hash = fnv1a64Append(hash, &state.gpuSkinningMode, sizeof(state.gpuSkinningMode));
    return hash;
}

void traceWorldSceneFrameSummary(
    const shared_projected_unit_models::Args& args,
    const prep::PreparedState& prepared,
    std::size_t rigidBatchCount,
    std::size_t skinnedBatchCount,
    std::uint64_t batchHash,
    std::uint64_t poseHash) {
    if (!args.unit || !shouldTraceWorldSceneUnit(*args.unit)) {
        return;
    }

    std::ostringstream line;
    line
        << "[ProjectedTrace][WorldScene] unit=" << args.unit->name
        << " id=" << args.unit->id
        << " active=" << args.unit->activeAnimIndex
        << " idle=" << args.unit->animIdleIndex
        << " time=" << args.unit->animTimeSec
        << " batches=" << (rigidBatchCount + skinnedBatchCount)
        << " rigid=" << rigidBatchCount
        << " skinned=" << skinnedBatchCount
        << " poseHash=0x" << std::hex << poseHash
        << " batchHash=0x" << batchHash << std::dec
        << " sortDepth=" << prepared.indexedBatchSortDepth;
    appendWorldSceneTraceLineImpl(line.str());
}

void traceWorldSceneSkip(const shared_projected_unit_models::Args& args, const char* reason) {
    if (!args.unit || !shouldTraceWorldSceneUnit(*args.unit)) {
        return;
    }
    std::ostringstream line;
    line
        << "[ProjectedTrace][WorldScene][Skip] unit=" << args.unit->name
        << " id=" << args.unit->id
        << " active=" << args.unit->activeAnimIndex
        << " idle=" << args.unit->animIdleIndex
        << " time=" << args.unit->animTimeSec
        << " reason=" << (reason ? reason : "unknown");
    appendWorldSceneTraceLineImpl(line.str());
}

void traceWorldSceneEnter(const shared_projected_unit_models::Args& args) {
    if (!args.unit || !shouldTraceWorldSceneUnit(*args.unit)) {
        return;
    }
    std::ostringstream line;
    line
        << "[ProjectedTrace][WorldScene][Enter] unit=" << args.unit->name
        << " id=" << args.unit->id
        << " active=" << args.unit->activeAnimIndex
        << " idle=" << args.unit->animIdleIndex
        << " time=" << args.unit->animTimeSec
        << " renderer=" << (args.backendId ? args.backendId : "<null>");
    appendWorldSceneTraceLineImpl(line.str());
}

} // namespace

bool shouldTraceProjectedUnitWorldScene(const PokemonInstance& unit) {
    return shouldTraceWorldSceneUnit(unit);
}

void appendProjectedUnitWorldSceneTraceLine(std::string_view line) {
    appendWorldSceneTraceLineImpl(line);
}

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

    traceWorldSceneEnter(args);

    if (shouldDisableWorldSceneUnit(*args.unit)) {
        traceWorldSceneSkip(args, "disabled_by_env");
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
        traceWorldSceneSkip(args, "prepare_failed");
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
        traceWorldSceneSkip(args, "prepared_not_world_scene_eligible");
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
        traceWorldSceneSkip(args, "fast_cache_empty");
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
        traceWorldSceneSkip(args, "material_cache_mismatch");
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
            traceWorldSceneSkip(args, "batch_validation_failed");
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
            traceWorldSceneSkip(args, "material_validation_failed");
            return false;
        }
    }

    auto& batchSkinStates = batchSkinStateScratch();
    batchSkinStates.assign(fastCache->batches.size(), support::GpuSkinBatchState{});
    auto& batchUsesSceneSkinning = batchUsesSceneSkinningScratch();
    batchUsesSceneSkinning.assign(fastCache->batches.size(), 0u);
    auto& resolvedTriNodeIndices = resolvedTriNodeIndexScratch();
    resolvedTriNodeIndices.assign(fastCache->batches.size(), -1);
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
        int resolvedTriNodeIndex = batchTemplate.triNodeIndex;
        if (resolvedTriNodeIndex < 0 && fastCache->defaultSkinNodeIndex >= 0) {
            resolvedTriNodeIndex = fastCache->defaultSkinNodeIndex;
        }
        resolvedTriNodeIndices[fastBatchIndex] = resolvedTriNodeIndex;

        const bool needsSceneSkinning = batchNeedsSceneSkinning(batchTemplate);
        if (!tryResolveSkinnedBatchState(
                batchTemplate,
                resolvedTriNodeIndex,
                batchSkinStates[fastBatchIndex])) {
            if (needsSceneSkinning) {
                if (args.perfBreakdown) {
                    args.perfBreakdown->prepMs +=
                        std::chrono::duration<double, std::milli>(Clock::now() - prepStart).count();
                }
                traceWorldSceneSkip(args, "skinned_batch_state_unavailable");
                return false;
            }
            continue;
        }
        batchUsesSceneSkinning[fastBatchIndex] = 1u;
    }

    if (args.perfBreakdown) {
        args.perfBreakdown->prepMs +=
            std::chrono::duration<double, std::milli>(Clock::now() - prepStart).count();
    }

    const auto sceneColor = prepared.fastTexturedTint;
    const float sceneAlpha = prepared.fastTexturedAlpha;
    const std::uint64_t poseHash = hashPoseEval(args.scenePose);
    std::uint64_t batchHash = 14695981039346656037ull;
    std::size_t rigidBatchCount = 0u;
    std::size_t skinnedBatchCount = 0u;

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
            const bool needsSceneSkinning = batchNeedsSceneSkinning(batchTemplate);
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
                needsSceneSkinning);
            itemEntry.worldSceneRegistryGeneration = args.worldSceneRegistry->generation;
        }

        IRenderBackend::WorldSceneRenderInstanceHandle instanceHandle{};
        instanceHandle.id =
            (static_cast<std::uint32_t>(args.unit->id) << 16u) ^
            static_cast<std::uint32_t>(fastBatchIndex + 1u);
        if (batchUsesSceneSkinning[fastBatchIndex] != 0u) {
            const auto& skinState = batchSkinStates[fastBatchIndex];
            const std::uint64_t skinHash = hashSkinPayload(skinState);
            batchHash = fnv1a64Append(batchHash, &skinHash, sizeof(skinHash));
            batchHash = fnv1a64Append(batchHash, &batchTemplate.baseSubmeshIndex, sizeof(batchTemplate.baseSubmeshIndex));
            ++skinnedBatchCount;
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
            const std::array<float, 16> batchModelMatrix = buildRigidBatchModelMatrix(
                prepared,
                args.scenePose,
                resolvedTriNodeIndices[fastBatchIndex]);
            batchHash = fnv1a64Append(batchHash, batchModelMatrix.data(), batchModelMatrix.size() * sizeof(float));
            batchHash = fnv1a64Append(batchHash, &batchTemplate.baseSubmeshIndex, sizeof(batchTemplate.baseSubmeshIndex));
            ++rigidBatchCount;
            shared_world_scene::appendRigidInstance(
                *args.worldSceneFrame,
                itemEntry.worldSceneObjectHandle,
                instanceHandle,
                batchModelMatrix,
                sceneColor.r,
                sceneColor.g,
                sceneColor.b,
                sceneAlpha,
                prepared.indexedBatchSortDepth);
        }
    }

    traceWorldSceneFrameSummary(
        args,
        prepared,
        rigidBatchCount,
        skinnedBatchCount,
        batchHash,
        poseHash);
    out.drewModelMesh = true;
    return true;
}

} // namespace game::runtime::shared_projected_unit_world_scene
