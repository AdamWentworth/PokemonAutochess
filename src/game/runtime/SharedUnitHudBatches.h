#pragma once

#include "engine/render/IRenderBackend.h"
#include "game/PokemonInstance.h"

#include <vector>

namespace game::runtime::shared_unit_hud {

struct Config {
    int xpLevelBase = 0;
    float xpLevelGrowth = 1.0f;
};

void appendLegacyUnitHud(std::vector<IRenderBackend::DebugQuad>& worldQuads,
                         std::vector<IRenderBackend::DebugLine>& lines,
                         std::vector<IRenderBackend::DebugLine>& textLines,
                         const Config& config,
                         const PokemonInstance& unit,
                         float screenX,
                         float screenY,
                         float cellPx);

} // namespace game::runtime::shared_unit_hud

