#pragma once

#include "game/runtime/shared/projected/SharedProjectedUnitModelRenderer.h"

#include <string_view>

namespace game::runtime::shared_projected_unit_world_scene {

bool shouldTraceProjectedUnitWorldScene(const PokemonInstance& unit);
void appendProjectedUnitWorldSceneTraceLine(std::string_view line);

bool tryRenderProjectedUnitModelWorldScene(
    const shared_projected_unit_models::Args& args,
    shared_projected_unit_models::Result& out);

} // namespace game::runtime::shared_projected_unit_world_scene
