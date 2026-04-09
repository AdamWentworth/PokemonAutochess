#pragma once

#include "engine/render/Camera3D.h"
#include "engine/render/IRenderBackend.h"
#include "game/config/GameDataDb.h"
#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "game/runtime/session/SessionRenderScratch.h"
#include "game/runtime/session/SessionWorldBackdrop.h"
#include "game/runtime/shared/backend/SharedBackendTextureCache.h"
#include "game/runtime/shared/ui/SharedUnitHudBatches.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

class GameWorld;

namespace game::runtime::session_projected_world_view {

struct Args {
    IRenderBackend* renderer = nullptr;
    GameWorld* gameWorld = nullptr;
    Camera3D* camera = nullptr;
    const ::GameDataDb* dataDb = nullptr;
    session_render_scratch::RenderScratch* scratch = nullptr;
    std::unordered_map<std::string, SharedBackendTextureCacheEntry>* backendTextureByPath = nullptr;

    shared_unit_hud::Config sharedUnitHudCfg{};

    bool supportsWorldTriangles3D = false;
    bool supportsWorldIndexedMeshes = false;
    bool enableBackdropTiles = true;
    bool characterInkingEnabled = false;
    int graphicsQuality = 3;
    bool enableGpuClipSkinning = false;
    bool allowPortraitFallback = false;
    bool forcePortraitOverlay = false;
    bool useLegacyGrowlWaveVfx = false;
    bool useLegacyParticleVfxSnapshotBridge = false;
    bool useExactTailFireCpuPath = false;
    bool tailFireDebugEnabled = false;
    session_world_backdrop::ArenaBackdropTheme backdropTheme =
        session_world_backdrop::ArenaBackdropTheme::Default;

    int drawableW = 0;
    int drawableH = 0;
    int rows = 0;
    int cols = 0;
    int benchSlots = 0;

    float minDim = 0.0f;
    float boardX = 0.0f;
    float boardY = 0.0f;
    float boardW = 0.0f;
    float boardH = 0.0f;
    float cellW = 0.0f;
    float cellH = 0.0f;
    double simNowSec = 0.0;
    const session_world_backdrop::Route1BackdropTuningState* route1BackdropTuning = nullptr;

    std::function<runtime::render_model::MeshData*(const std::string&)> ensureBackendMeshLoaded;
    std::function<SharedBackendTextureCacheEntry*(const std::string&, bool)> ensureBackendTextureLoaded;
};

struct Result {
    bool hasWorldViewProj = false;
    std::array<float, 16> worldViewProj{};
    std::array<float, 3> cameraWorldPos{0.0f, 7.0f, 9.0f};
    std::array<float, 3> cameraForward{0.0f, -0.6139406f, -0.7893522f};
    std::array<float, 3> cameraTarget{0.0f, -1.0f, 0.0f};

    std::uint32_t visibleAnimatedUnits = 0u;
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
    float worldBackdropComposeMs = 0.0f;
    float worldVfxBridgeMs = 0.0f;
    float worldDepthFlushMs = 0.0f;
};

Result appendProjectedWorldView(const Args& args);

} // namespace game::runtime::session_projected_world_view
