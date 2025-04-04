// Application.h

#pragma once

#include "../render/Renderer.h"
#include "../render/Camera3D.h"
#include "../../game/GameStateManager.h"

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
    };
