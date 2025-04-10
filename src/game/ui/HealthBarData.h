// game/ui/HealthBarData.h
#pragma once
#include <glm/glm.hpp>

struct HealthBarData {
    glm::vec2 screenPosition;
    int currentHP;
    int maxHP;
};