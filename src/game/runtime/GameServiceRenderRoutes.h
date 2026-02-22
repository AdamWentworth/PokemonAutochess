#pragma once

#include "game/GameServices.h"
#include "game/runtime/RenderRoutes.h"

namespace game::runtime::render {

inline RenderRoutes routesFromServices(const GameServices& services) {
    return makeRenderRoutes(
        services.renderEnabled,
        services.usesLegacyGameRenderPath(),
        services.usesLegacyGameUiPath());
}

} // namespace game::runtime::render
