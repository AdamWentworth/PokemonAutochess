// src/game/ui/HealthBarQuery.cpp
#include "HealthBarQuery.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp> // glm::project
#include <algorithm>
#include <cmath>

#include "engine/render/Camera3D.h"
#include "game/PokemonInstance.h"
#include "game/GameConfig.h"

static int xpToNextLevel(const GameConfigData& cfg, int level) {
    if (cfg.xpLevelBase <= 0) return 0;
    const int useLevel = std::max(1, level);
    const float growth = (cfg.xpLevelGrowth > 0.0f) ? cfg.xpLevelGrowth : 1.0f;
    const float raw = static_cast<float>(cfg.xpLevelBase) * std::pow(growth, static_cast<float>(useLevel - 1));
    return std::max(1, static_cast<int>(std::round(raw)));
}

std::vector<HealthBarData> BuildHealthBarData(
    const std::vector<PokemonInstance>& boardUnits,
    const std::vector<PokemonInstance>& benchUnits,
    const Camera3D& camera,
    int screenWidth,
    int screenHeight,
    const GameConfigData& config
) {
    std::vector<HealthBarData> data;
    data.reserve(boardUnits.size() + benchUnits.size());

    glm::vec4 viewport(0.0f, 0.0f, static_cast<float>(screenWidth), static_cast<float>(screenHeight));

    auto process = [&](const PokemonInstance& instance) {
        if (!instance.alive) return;

        const glm::vec3 worldPos = instance.position + glm::vec3(0.0f, 1.0f + instance.visualYOffset, 0.0f);
        const glm::vec3 screenPos = glm::project(
            worldPos,
            camera.getViewMatrix(),
            camera.getProjectionMatrix(),
            viewport
        );

        // screenPos.z is in [0,1] when in front of camera after projection.
        if (screenPos.z > 1.0f) return;
        if (screenPos.x < 0.0f || screenPos.x > static_cast<float>(screenWidth)) return;
        if (screenPos.y < 0.0f || screenPos.y > static_cast<float>(screenHeight)) return;

        HealthBarData hb;
        hb.screenPosition = glm::vec2(screenPos.x, static_cast<float>(screenHeight) - screenPos.y);
        hb.currentHP = instance.hp;
        hb.maxHP = instance.maxHP;
        hb.currentEnergy = instance.energy;
        hb.maxEnergy     = instance.maxEnergy;
        hb.showXP        = (instance.side == PokemonSide::Player);
        hb.isEnemy       = (instance.side == PokemonSide::Enemy);
        hb.level         = std::max(1, instance.level);
        hb.currentXP     = instance.xp;
        hb.maxXP         = hb.showXP ? xpToNextLevel(config, instance.level) : 0;
        data.push_back(hb);
    };

    for (const auto& p : boardUnits) process(p);
    for (const auto& b : benchUnits) process(b);

    return data;
}
