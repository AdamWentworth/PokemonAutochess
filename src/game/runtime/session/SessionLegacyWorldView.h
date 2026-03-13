#pragma once

#include "game/runtime/session/SessionRenderScratch.h"
#include "game/runtime/shared/ui/SharedUnitHudBatches.h"

class GameWorld;

namespace game::runtime::session_legacy_world_view {

struct Args {
    bool renderWorld = false;
    GameWorld* gameWorld = nullptr;
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
    shared_unit_hud::Config sharedUnitHudCfg;
};

struct Result {
    std::uint32_t visibleAnimatedUnits = 0u;
};

Result appendLegacyWorldView(const Args& args,
                             session_render_scratch::RenderScratch& scratch);

} // namespace game::runtime::session_legacy_world_view
