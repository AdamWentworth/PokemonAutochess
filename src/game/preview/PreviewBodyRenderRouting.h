#pragma once

#include <string_view>

#include "game/runtime/shared/vfx/tail_fire/SharedTailFireCoordinator.h"

namespace game::preview {

struct PreviewBodyRenderRouting {
    bool buildProjectedScratch = false;
    bool allowProjectedBody = false;
    bool tailFireMeshPlaybackSpecies = false;
};

inline PreviewBodyRenderRouting resolvePreviewBodyRenderRouting(
    std::string_view species,
    bool exactClipMotionPreview) {
    PreviewBodyRenderRouting routing{};
    routing.tailFireMeshPlaybackSpecies =
        game::runtime::shared_tail_fire_coordinator::speciesUsesTailFireMeshPlayback(species);
    routing.allowProjectedBody = !exactClipMotionPreview;
    routing.buildProjectedScratch =
        routing.allowProjectedBody || routing.tailFireMeshPlaybackSpecies;
    return routing;
}

} // namespace game::preview
