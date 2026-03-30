#pragma once

#include <functional>
#include <string>

#include <glm/glm.hpp>

#include "engine/render/IRenderBackend.h"
#include "engine/render/Camera3D.h"
#include "game/PokemonInstance.h"
#include "game/preview/PreviewPokemonVisual.h"
#include "game/runtime/session/SessionRenderScratch.h"
#include "game/runtime/session/SessionTextureCache.h"
#include "game/vfx/TailFireVFX.h"

namespace game::preview {

struct PreviewTailFireBridgeArgs {
    const Camera3D* camera = nullptr;
    IRenderBackend* renderer = nullptr;
    int surfaceWidth = 0;
    int surfaceHeight = 0;
    float worldCellSize = 1.0f;
    double simNowSec = 0.0;
    const TailFireVFX::Config* fallbackConfig = nullptr;
    const PreviewPokemonVisual* visual = nullptr;
    glm::vec3 worldPos{0.0f};
    float yawDeg = 0.0f;
    PokemonSide side = PokemonSide::Player;
    game::runtime::session_texture_cache::TextureCache* backendTextureByPath = nullptr;
    game::runtime::session_render_scratch::RenderScratch* modelScratch = nullptr;
    game::runtime::session_render_scratch::RenderScratch* tailFireScratch = nullptr;
    std::function<bool(const Camera3D&,
                       int,
                       int,
                       const PreviewPokemonVisual&,
                       const glm::vec3&,
                       float,
                       PokemonSide)>
        appendModelBatches;
    std::function<game::runtime::SharedBackendTextureCacheEntry*(const std::string&, bool)>
        ensureBackendTextureLoaded;
    std::function<void(const Camera3D&,
                       int,
                       int,
                       game::runtime::session_render_scratch::RenderScratch&,
                       bool,
                       bool)>
        submitScratch;
};

void renderPreviewTailFire(const PreviewTailFireBridgeArgs& args);

} // namespace game::preview
