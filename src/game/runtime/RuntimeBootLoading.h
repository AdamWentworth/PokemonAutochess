#pragma once

#include <array>

#include <SDL2/SDL.h>

#include "engine/render/IRenderBackend.h"

namespace game::runtime::boot_loading {

constexpr std::size_t kFallbackLoadingQuadCount = 5;

bool shouldAbortPreloadEvent(const SDL_Event& event);

bool buildFallbackLoadingQuads(
    int drawableW,
    int drawableH,
    float progress01,
    std::array<IRenderBackend::DebugQuad, kFallbackLoadingQuadCount>& out);

} // namespace game::runtime::boot_loading
