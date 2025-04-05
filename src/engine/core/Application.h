// Application.h

#pragma once

#include "../render/Renderer.h"
#include "../render/Camera3D.h"
#include "../../game/GameStateManager.h"

// Forward-declare GameWorld so we can store a pointer or instance:
class GameWorld;
class Window; // Forward declaration of our new Window class

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
    GameWorld* gameWorld = nullptr;

    // Use our new Window class
    Window* window = nullptr;
};
