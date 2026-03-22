#pragma once

#include "game/runtime/shared/projected/SharedProjectedUnitModelRenderer.h"

namespace game::runtime::shared_projected_unit_world_scene {

bool tryRenderProjectedUnitModelWorldScene(
    const shared_projected_unit_models::Args& args,
    shared_projected_unit_models::Result& out);

} // namespace game::runtime::shared_projected_unit_world_scene
