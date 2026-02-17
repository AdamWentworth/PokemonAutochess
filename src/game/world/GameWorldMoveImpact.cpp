#include "game/world/GameWorld.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

#include "engine/render/Model.h"
#include "game/world/MoveImpactMath.h"
#include "game/world/MoveImpactRouting.h"

namespace {

std::string lowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool isMetalClawMove(std::string_view moveLower) {
    return moveLower == "metal_claw";
}

bool isTailWhipMove(std::string_view moveLower) {
    return moveLower == "tail_whip";
}

AquaSwooshVFX::Style aquaSwooshStyleForMove(std::string_view moveLower) {
    if (moveLower == "bubble") return AquaSwooshVFX::Style::Bubble;
    if (moveLower == "water_gun") return AquaSwooshVFX::Style::WaterGun;
    return AquaSwooshVFX::Style::TailWhip;
}

}  // namespace

void GameWorld::emitGrassImpactAt(const PokemonInstance& target) {
    if (!renderEnabled) return;
    if (!grassImpactVfxInitialized) {
        GrassImpactVFX::Config c;  // defaults
        grassImpactVfx.setConfig(c);
        grassImpactVfxInitialized = true;
    }

    const glm::vec3 base = target.position + glm::vec3(0.0f, target.visualYOffset, 0.0f);
    grassImpactVfx.emitAt(base);
}

void GameWorld::emitTackleImpactAt(const PokemonInstance& target, const PokemonInstance* attacker) {
    if (!renderEnabled) return;
    if (!tackleImpactVfxInitialized) {
        TackleImpactVFX::Config c;  // defaults
        tackleImpactVfx.setConfig(c);
        tackleImpactVfxInitialized = true;
    }

    glm::vec3 base = target.position + glm::vec3(0.0f, target.visualYOffset, 0.0f);

    if (attacker && target.model && target.model->hasBounds()) {
        glm::vec3 from = attacker->position + glm::vec3(0.0f, attacker->visualYOffset, 0.0f);
        glm::vec3 dir = from - base;
        dir.y = 0.0f;
        const float len = glm::length(dir);
        if (len > 0.0001f) {
            dir /= len;
            const float scale = computeModelWorldScaleForMoveImpact(target);
            const float radius = target.model->getBoundsRadiusHorizontal();
            const float edge = radius * scale * tackleImpactVfx.getConfig().impactEdgeOffset;
            base += dir * edge;
        }
    }

    tackleImpactVfx.emitAt(base);
}

void GameWorld::emitMoveImpactByName(const std::string& moveName,
                                     const PokemonInstance& target,
                                     const PokemonInstance* attacker) {
    if (!renderEnabled) return;

    const std::string move = lowerCopy(moveName);
    if (move.empty()) return;
    const MoveImpactRoute route = classifyMoveImpactRoute(move);
    if (route == MoveImpactRoute::None) return;

    auto makeForward = [&]() -> glm::vec3 {
        if (attacker) {
            glm::vec3 d = target.position - attacker->position;
            d.y = 0.0f;
            const float len = glm::length(d);
            if (len > 0.0001f) return d / len;
        }
        return glm::vec3(0.0f, 0.0f, 1.0f);
    };

    switch (route) {
    case MoveImpactRoute::Tackle: {
        emitTackleImpactAt(target, attacker);
        return;
    }

    case MoveImpactRoute::GrassImpact: {
        emitGrassImpactAt(target);
        return;
    }

    case MoveImpactRoute::GrowlSoundRings: {
        emitGrowlImpact(target, attacker, makeForward());
        return;
    }

    case MoveImpactRoute::ClawSwipe: {
        if (!clawSwipeVfxInitialized) {
            ClawSwipeVFX::Config c;
            clawSwipeVfx.setConfig(c);
            clawSwipeVfxInitialized = true;
        }
        const bool metallic = isMetalClawMove(move);
        const glm::vec3 base = target.position + glm::vec3(0.0f, target.visualYOffset, 0.0f);
        clawSwipeVfx.emitAt(base, makeForward(), metallic);
        return;
    }

    case MoveImpactRoute::AquaSwoosh: {
        if (!aquaSwooshVfxInitialized) {
            AquaSwooshVFX::Config c;
            aquaSwooshVfx.setConfig(c);
            aquaSwooshVfxInitialized = true;
        }
        const AquaSwooshVFX::Style style = aquaSwooshStyleForMove(move);

        const glm::vec3 base =
            (isTailWhipMove(move) && attacker)
                ? (attacker->position + glm::vec3(0.0f, attacker->visualYOffset, 0.0f))
                : (target.position + glm::vec3(0.0f, target.visualYOffset, 0.0f));
        aquaSwooshVfx.emitAt(base, makeForward(), style);
        return;
    }

    case MoveImpactRoute::None:
    default:
        return;
    }
}
