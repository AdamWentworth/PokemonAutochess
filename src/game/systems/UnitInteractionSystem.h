// UnitInteractionSystem.h

#pragma once

#include "../../engine/render/Camera3D.h"
#include "../../game/GameWorld.h"
#include <SDL2/SDL.h>
#include <glm/glm.hpp>

class UnitInteractionSystem {
public:
    UnitInteractionSystem(Camera3D* camera, GameWorld* world, unsigned int screenWidth, unsigned int screenHeight);

    void handleEvent(const SDL_Event& event);

private:
    glm::vec3 screenToWorld(int mouseX, int mouseY) const;

    Camera3D* camera;
    GameWorld* gameWorld;

    bool draggingUnit = false;
    int draggedIndex = -1;
    float pickRadius = 0.7f;
    unsigned int screenW;
    unsigned int screenH;
};
