// src/engine/core/Application.h
#pragma once

#include <memory>
#include <string>

#include "engine/render/Camera3D.h"

// Forward decls (engine)
class Window;
class Renderer;
class BootLoadingView;

// Game-facing interface + context
class GameLoop;
struct GameContext;

/*
    Application (Engine):
    - Owns engine bootstrapping: SDL/GL window, viewport sizing, mouse scaling, fixed-timestep loop.
    - Does NOT depend on game headers.
    - Calls into the game via the GameLoop interface.
*/
class Application {
public:
    Application();
    ~Application();

    void run(GameLoop& game);

    // Engine services (read-only)
    Window*   getWindow()   const { return window.get(); }
    Renderer* getRenderer() const { return renderer.get(); }
    Camera3D* getCamera()   const { return camera.get();  }

    int getDrawableW() const { return drawableW; }
    int getDrawableH() const { return drawableH; }

    float getMouseScaleX() const { return mouseScaleX; }
    float getMouseScaleY() const { return mouseScaleY; }

    // Engine helpers used by GameContext callbacks
    void setTitle(const std::string& title);
    void swapBuffers();
    bool pumpPreloadEvents();
    void renderBootLoading(float progress01);

private:
    void initBase();
    void shutdownBase();
    void updateDrawableSizeAndViewport();
    void updateMouseScale();

    static constexpr float TIME_STEP = 1.0f / 60.0f;

private:
    std::unique_ptr<Window>   window;
    std::unique_ptr<Renderer> renderer;
    std::unique_ptr<Camera3D> camera;

    std::unique_ptr<BootLoadingView> bootLoadingView;

    int drawableW = 1280;
    int drawableH = 720;

    int windowW = 1280;
    int windowH = 720;

    float mouseScaleX = 1.0f;
    float mouseScaleY = 1.0f;
};
