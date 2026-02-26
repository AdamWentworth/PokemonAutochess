#pragma once

#include "engine/render/IRenderBackend.h"
#include "game/PokemonInstance.h"
#include "game/runtime/BackendWorldProxyGeometry.h"
#include "game/runtime/shared/SharedProjectedDebugVfx.h"
#include "game/runtime/shared/SharedUnitHudBatches.h"
#include "game/world/GameWorld.h"

#include <vector>

#include <glm/glm.hpp>

namespace game::runtime::shared_projected_unit_overlays {

struct Args {
    const PokemonInstance* unit = nullptr;
    bool drewModelMesh = false;
    bool allowPortraitFallback = false;
    bool forcePortraitOverlay = false;
    bool useLegacyGrowlWaveVfx = false;
    bool useLegacyParticleVfxSnapshotBridge = false;
    GameWorld* gameWorld = nullptr;

    shared_projected_debug::ProjectedDebugVfxBuilder* projectedDebug = nullptr;
    std::vector<IRenderBackend::DebugQuad>* worldQuads = nullptr;
    std::vector<IRenderBackend::DebugLine>* lines = nullptr;
    std::vector<IRenderBackend::DebugLine>* textLines = nullptr;
    std::vector<IRenderBackend::DebugSprite>* sprites = nullptr;
    const shared_unit_hud::Config* sharedUnitHudCfg = nullptr;

    float cx = 0.0f;
    float cy = 0.0f;
    float unitSize = 0.0f;
    float minDim = 0.0f;
    float cellPx = 0.0f;
    float lineThickness = 1.0f;
    float worldCellSize = 1.0f;
    float animYaw = 0.0f;
    glm::vec3 proxyCenter{0.0f};
    backend_proxy::UnitProxyExtents extents{};
};

void appendProjectedUnitOverlays(const Args& args);

} // namespace game::runtime::shared_projected_unit_overlays
