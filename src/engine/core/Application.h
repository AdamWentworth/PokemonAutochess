// Application.h

#pragma once

#include "../render/Renderer.h"
#include "../render/Camera3D.h"
#include "../../game/GameStateManager.h"

// Forward-declare GameWorld so we can store a pointer or instance:
class GameWorld;

class Application {
public:
    Application();
    ~Application();
    void run();
    
private:
    void init();
    void update();
    void shutdown();
    
    Renderer* renderer = nullptr;
    Camera3D* camera = nullptr;
    GameStateManager* stateManager = nullptr;

    // Instead of a single Model pointer, keep a GameWorld that can hold multiple
    GameWorld* gameWorld = nullptr;
};
