#include "game/world/GameWorld.h"

void GameWorld::emitClawSwipeImpact(const PokemonInstance& target,
                                    const glm::vec3& forward,
                                    bool metallic) {
    if (!clawSwipeVfxInitialized) {
        ClawSwipeVFX::Config defaultConfig;
        clawSwipeVfx.setConfig(defaultConfig);
        clawSwipeVfxInitialized = true;
    }

    const glm::vec3 base = target.position + glm::vec3(0.0f, target.visualYOffset, 0.0f);
    clawSwipeVfx.emitAt(base, forward, metallic);
}

void GameWorld::emitAquaSwooshImpact(const PokemonInstance& target,
                                     const PokemonInstance* attacker,
                                     const glm::vec3& forward,
                                     AquaSwooshVFX::Style style,
                                     bool originFromAttacker) {
    if (!aquaSwooshVfxInitialized) {
        AquaSwooshVFX::Config defaultConfig;
        aquaSwooshVfx.setConfig(defaultConfig);
        aquaSwooshVfxInitialized = true;
    }

    const glm::vec3 base =
        (originFromAttacker && attacker)
            ? (attacker->position + glm::vec3(0.0f, attacker->visualYOffset, 0.0f))
            : (target.position + glm::vec3(0.0f, target.visualYOffset, 0.0f));
    aquaSwooshVfx.emitAt(base, forward, style);
}
