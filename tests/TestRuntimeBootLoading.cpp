#include <array>
#include <cmath>
#include <string>

#include <SDL2/SDL.h>

#include "engine/render/IRenderBackend.h"
#include "game/runtime/RuntimeBootLoading.h"

bool test_runtime_boot_loading_contract(std::string& outFail) {
    using game::runtime::boot_loading::buildFallbackLoadingQuads;
    using game::runtime::boot_loading::kFallbackLoadingQuadCount;
    using game::runtime::boot_loading::shouldAbortPreloadEvent;

    {
        SDL_Event event{};
        event.type = SDL_QUIT;
        if (!shouldAbortPreloadEvent(event)) {
            outFail = "shouldAbortPreloadEvent should stop preload on SDL_QUIT.";
            return false;
        }

        event.type = SDL_KEYDOWN;
        event.key.keysym.sym = SDLK_ESCAPE;
        if (!shouldAbortPreloadEvent(event)) {
            outFail = "shouldAbortPreloadEvent should stop preload on Escape.";
            return false;
        }

        event.key.keysym.sym = SDLK_RETURN;
        if (shouldAbortPreloadEvent(event)) {
            outFail = "shouldAbortPreloadEvent should ignore non-Escape keydown events.";
            return false;
        }
    }

    {
        std::array<IRenderBackend::DebugQuad, kFallbackLoadingQuadCount> quads{};
        if (buildFallbackLoadingQuads(0, 720, 0.5f, quads)) {
            outFail = "buildFallbackLoadingQuads should refuse non-positive drawable sizes.";
            return false;
        }
    }

    {
        std::array<IRenderBackend::DebugQuad, kFallbackLoadingQuadCount> quads{};
        if (!buildFallbackLoadingQuads(1280, 720, 1.5f, quads)) {
            outFail = "buildFallbackLoadingQuads should build a fallback layout for valid drawable sizes.";
            return false;
        }

        if (std::fabs(quads[0].w - 1280.0f) > 0.001f ||
            std::fabs(quads[0].h - 720.0f) > 0.001f) {
            outFail = "buildFallbackLoadingQuads should cover the full drawable area with the backdrop quad.";
            return false;
        }
        if (!(quads[1].w > 0.0f && quads[1].h > 0.0f &&
              quads[2].w < quads[1].w && quads[2].h < quads[1].h)) {
            outFail = "buildFallbackLoadingQuads should create nested panel quads.";
            return false;
        }
        if (!(quads[4].w <= quads[3].w && quads[4].w > 0.0f &&
              quads[4].h < quads[3].h)) {
            outFail = "buildFallbackLoadingQuads should clamp fill width to the loading bar bounds.";
            return false;
        }
    }

    {
        std::array<IRenderBackend::DebugQuad, kFallbackLoadingQuadCount> quads{};
        if (!buildFallbackLoadingQuads(800, 600, -1.0f, quads) ||
            std::fabs(quads[4].w) > 0.001f) {
            outFail = "buildFallbackLoadingQuads should clamp negative progress to an empty fill bar.";
            return false;
        }
    }

    return true;
}
