// src/engine/core/GameContext.h
#pragma once

#include <functional>
#include <string>

// Forward-declare engine services
class Renderer;
class Camera3D;
struct EngineServices;

/*
    GameContext:
    - The *stable* engine-facing API exposed to the game layer.
    - Keeps the game from depending on Application directly.
    - Add fields here deliberately (treat as public engine/game contract).
*/
struct GameContext {
    // Core render services (owned by engine)
    Renderer* renderer = nullptr;
    Camera3D* camera   = nullptr;


    // Optional engine service bundle (owned by engine; may be null)
    EngineServices* services = nullptr;
    // Current drawable size (framebuffer size)
    int drawableW = 1280;
    int drawableH = 720;

    // Minimal engine callbacks (kept intentionally small)
    std::function<void(const std::string&)> setTitle;
    std::function<void()>                  swapBuffers;

    // Request application shutdown (safe to call during init/preload)
    std::function<void()>                  requestQuit;

    // Loading / long task helpers
    std::function<bool()>                  pumpPreloadEvents;
    std::function<void(float /*progress01*/)> renderBootLoading;
};