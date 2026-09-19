#pragma once

#include "engine/core/EngineServices.h"

// Autochess host/session diagnostics. Shared by the standalone runner and editor preview.
struct GameFixedPerfBreakdown {
    float preUpdateMs = 0.0f;
    float updatePhaseMs = 0.0f;
    float postUpdateMs = 0.0f;
    float postOtherMs = 0.0f;
    float phaseTransitionMs = 0.0f;
    float backendHydrateMs = 0.0f;
    float cameraMs = 0.0f;
    float unitInteractionMs = 0.0f;
    float shopMs = 0.0f;
    float roundMs = 0.0f;
    float stateManagerMs = 0.0f;
    float stateUpdateMs = 0.0f;
    float stateFlushMs = 0.0f;
    float movementMs = 0.0f;
    float movementPlanMs = 0.0f;
    float movementLuaMs = 0.0f;
    float movementFlushMs = 0.0f;
    float movementAdvanceMs = 0.0f;
    float combatMs = 0.0f;
    float combatPlanMs = 0.0f;
    float combatLuaMs = 0.0f;
    float combatFlushMs = 0.0f;
    float worldMs = 0.0f;
};

struct GameRenderBuildBreakdown {
    float worldComposeMs = 0.0f;
    float worldBackdropMs = 0.0f;
    float worldVfxMs = 0.0f;
    float worldDepthFlushMs = 0.0f;
    float overlayPrepMs = 0.0f;
    float worldBackgroundMs = 0.0f;
    float worldTriangles3dMs = 0.0f;
    float worldIndexedMs = 0.0f;
    float worldSceneSubmitMs = 0.0f;
    float worldIndexedBatchSubmitMs = 0.0f;
    float worldDebugMs = 0.0f;
    float spriteMs = 0.0f;
    float uiMs = 0.0f;
    float otherMs = 0.0f;
};

enum class GameTerminalLogMode : std::uint8_t {
    Performance = 0,
    GrowlVfx = 1,
    ScratchVfx = 2,
    CombatDecision = 4,
    AnimationDecision = 5,
};

struct GameGrowlPassDebugStats {
    std::string id;
    int eid = 0;
    std::string mode;
    std::string meshPath;
    std::string texturePath;
    bool quarterTextureBake = false;
    bool linePass = false;
    float scaleMul = 1.0f;
    float alphaMul = 1.0f;
    float forwardOffset = 0.0f;
    std::uint32_t submittedBatchCount = 0u;
    std::uint32_t submittedVertexCount = 0u;
    std::uint32_t submittedIndexCount = 0u;
    int submittedTextureWidth = 0;
    int submittedTextureHeight = 0;
    float submittedTranslateX = 0.0f;
    float submittedTranslateY = 0.0f;
    float submittedTranslateZ = 0.0f;
};

struct GameGrowlDebugStats {
    bool snapshotAvailable = false;
    std::uint32_t activeRingCount = 0u;
    std::uint32_t configuredPassCount = 0u;
    std::uint32_t enabledPassCount = 0u;
    std::uint32_t meshPassCount = 0u;
    std::uint32_t linePassCount = 0u;
    std::uint32_t quarterRingPassCount = 0u;
    std::uint32_t quarterTextureBakePassCount = 0u;
    std::vector<GameGrowlPassDebugStats> activePasses;
};

struct GameScratchDebugStats {
    bool snapshotAvailable = false;
    std::uint32_t activeGlowCount = 0u;
    std::uint32_t snapshotRingCount = 0u;
    std::uint32_t configuredPassCount = 0u;
    std::uint32_t enabledPassCount = 0u;
    std::uint32_t submittedBatchCount = 0u;
    std::uint32_t submittedAlphaBatchCount = 0u;
    std::uint32_t submittedAdditiveBatchCount = 0u;
    std::uint32_t submittedPremulBatchCount = 0u;
    std::uint32_t submittedInstancedBatchCount = 0u;
    std::uint32_t submittedDynamicBatchCount = 0u;
    std::uint32_t submittedInstanceCount = 0u;
    std::uint32_t submittedVertexCount = 0u;
    std::uint32_t submittedIndexCount = 0u;
};

struct GameFramePerfStats : EngineFramePerfStats {
    std::uint32_t visibleAnimatedUnits = 0u;
    std::uint32_t particleCount = 0u;
    float projectedUnitsMs = 0.0f;
    float projectedPoseEvalMs = 0.0f;
    float projectedModelMs = 0.0f;
    float projectedModelPrepMs = 0.0f;
    float projectedModelGeometryMs = 0.0f;
    float projectedOverlayMs = 0.0f;
    std::uint32_t projectedUnitsProcessed = 0u;
    std::uint32_t projectedModelUnits = 0u;
    std::uint32_t projectedClipSkinnedUnits = 0u;
    std::uint32_t projectedSharedRigidBatches = 0u;
    std::uint32_t projectedGpuClipSkinBatches = 0u;
    std::uint32_t projectedGpuClipPaletteBatches = 0u;
    std::uint32_t projectedCpuRewriteBatches = 0u;
    std::uint32_t projectedIndexedBatchesQueued = 0u;
    GameRenderBuildBreakdown renderBreakdown{};
    GameFixedPerfBreakdown fixedBreakdown{};
};

struct GameRuntimeServices : EngineServices {
    // Updated by host loop each second (debug/perf overlay + logging).
    GameFramePerfStats framePerf;
    // Updated by runtime each frame; host loop samples and aggregates.
    std::uint32_t frameVisibleAnimatedUnits = 0u;
    std::uint32_t frameParticleCount = 0u;
    GameFixedPerfBreakdown frameFixedBreakdown{};
    float frameProjectedUnitsMs = 0.0f;
    float frameProjectedPoseEvalMs = 0.0f;
    float frameProjectedModelMs = 0.0f;
    float frameProjectedModelPrepMs = 0.0f;
    float frameProjectedModelGeometryMs = 0.0f;
    float frameProjectedOverlayMs = 0.0f;
    std::uint32_t frameProjectedUnitsProcessed = 0u;
    std::uint32_t frameProjectedModelUnits = 0u;
    std::uint32_t frameProjectedClipSkinnedUnits = 0u;
    std::uint32_t frameProjectedSharedRigidBatches = 0u;
    std::uint32_t frameProjectedGpuClipSkinBatches = 0u;
    std::uint32_t frameProjectedGpuClipPaletteBatches = 0u;
    std::uint32_t frameProjectedCpuRewriteBatches = 0u;
    std::uint32_t frameProjectedIndexedBatchesQueued = 0u;
    GameRenderBuildBreakdown frameRenderBuildBreakdown{};

    bool sessionBackdropTilesEnabled = true;
    GameTerminalLogMode terminalLogMode = GameTerminalLogMode::Performance;
    std::string bootMenuScreen;
    GameGrowlDebugStats frameGrowlDebug{};
    GameScratchDebugStats frameScratchDebug{};
};

// Generic engine hosts supply only engine services; the game owns the additional state.
inline GameRuntimeServices* bindGameRuntimeServices(
    EngineServices* host, GameRuntimeServices& fallback) {
    if (!host) return nullptr;
    if (auto* runtime = dynamic_cast<GameRuntimeServices*>(host)) return runtime;
    static_cast<EngineServices&>(fallback) = *host;
    return &fallback;
}
