#include "game/world/GameWorld.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

#include <glm/gtc/matrix_transform.hpp>

#include "engine/render/Model.h"

namespace {

std::string lowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

glm::vec3 safeForwardXZ(const glm::vec3& v) {
    glm::vec3 f(v.x, 0.0f, v.z);
    const float len = glm::length(f);
    if (len <= 0.0001f) return glm::vec3(0.0f, 0.0f, 1.0f);
    return f / len;
}

float computeModelWorldScale(const PokemonInstance& instance) {
    return (instance.model ? instance.model->getScaleFactor() : 1.0f) *
           std::max(0.0f, instance.modelScaleCorrection) *
           std::max(0.0f, instance.speciesScale) *
           std::max(0.0f, instance.visualScale) *
           std::max(0.0f, instance.captureScale);
}

glm::mat4 buildModelInstanceTransform(const PokemonInstance& instance) {
    const float scaleFactor = computeModelWorldScale(instance);

    const glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(scaleFactor));
    const glm::mat4 rotationX =
        glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.x), glm::vec3(1, 0, 0));
    const glm::mat4 rotationY =
        glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.y), glm::vec3(0, 1, 0));
    const glm::mat4 rotationZ =
        glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.z), glm::vec3(0, 0, 1));
    const glm::vec3 renderPos = instance.position + glm::vec3(0.0f, instance.visualYOffset, 0.0f);
    const glm::mat4 translation = glm::translate(glm::mat4(1.0f), renderPos);

    return translation * rotationY * rotationX * rotationZ * scale;
}

template <size_t N>
bool tryResolveAnimatedNodeWorld(const PokemonInstance& unit,
                                 const std::array<const char*, N>& nodeNames,
                                 glm::vec3& outWorldPos) {
    if (!unit.model) return false;

    const glm::mat4 instanceM = buildModelInstanceTransform(unit);
    const int activeAnim = (unit.activeAnimIndex >= 0) ? unit.activeAnimIndex : unit.animIdleIndex;
    const int idleAnim = unit.animIdleIndex;

    auto tryAnim = [&](int animIndex) -> bool {
        if (animIndex < 0) return false;
        for (const char* nodeName : nodeNames) {
            if (!nodeName || !nodeName[0]) continue;
            glm::mat4 nodeGlobal(1.0f);
            if (!unit.model->getNodeGlobalTransformByName(unit.animTimeSec, animIndex, nodeName, nodeGlobal)) {
                continue;
            }
            const glm::mat4 nodeWorld = instanceM * nodeGlobal;
            outWorldPos = glm::vec3(nodeWorld[3]);
            return true;
        }
        return false;
    };

    if (tryAnim(activeAnim)) return true;
    if (idleAnim != activeAnim && tryAnim(idleAnim)) return true;
    return false;
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
            const float scale = computeModelWorldScale(target);
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

    if (move == "tackle") {
        emitTackleImpactAt(target, attacker);
        return;
    }

    if (move == "vine_whip" || move == "leech_seed") {
        emitGrassImpactAt(target);
        return;
    }

    auto makeForward = [&]() -> glm::vec3 {
        if (attacker) {
            glm::vec3 d = target.position - attacker->position;
            d.y = 0.0f;
            const float len = glm::length(d);
            if (len > 0.0001f) return d / len;
        }
        return glm::vec3(0.0f, 0.0f, 1.0f);
    };

    if (move == "growl") {
        if (!growlWaveVfxInitialized) {
            GrowlWaveVFX::Config c;
            c.spawnForwardOffset = 0.0f;
            c.spawnHeightOffset = 0.0f;
            growlWaveVfx.setConfig(c);
            growlWaveVfxInitialized = true;
        }

        const glm::vec3 forward = makeForward();
        glm::vec3 origin =
            attacker ? (attacker->position + glm::vec3(0.0f, attacker->visualYOffset, 0.0f))
                     : (target.position + glm::vec3(0.0f, target.visualYOffset, 0.0f));

        if (attacker) {
            const glm::vec3 fwdXZ = safeForwardXZ(forward);
            const glm::vec3 renderPos = attacker->position + glm::vec3(0.0f, attacker->visualYOffset, 0.0f);
            const float worldScale = computeModelWorldScale(*attacker);

            // Stable fallback near mouth/head area in world-space.
            glm::vec3 fallbackOrigin = renderPos + glm::vec3(0.0f, 0.14f, 0.0f);
            fallbackOrigin += fwdXZ * 0.10f;

            static constexpr std::array<const char*, 12> kGrowlNodeCandidates = {
                "EffMouth01", "effmouth01", "mouth", "Mouth", "head", "Head",
                "jaw", "Jaw", "Nose", "nose", "neck", "Neck"};

            glm::vec3 mouthWorld(0.0f);
            bool resolvedFromNode = false;
            if (tryResolveAnimatedNodeWorld(*attacker, kGrowlNodeCandidates, mouthWorld)) {
                origin = mouthWorld;
                resolvedFromNode = true;
            } else {
                origin = fallbackOrigin;
            }

            // Safety checks: reject pathological node transforms that place the origin
            // underground or behind the caster (seen on some rigs/clips).
            if (resolvedFromNode) {
                const glm::vec3 planarDelta =
                    glm::vec3(origin.x - renderPos.x, 0.0f, origin.z - renderPos.z);
                const float planarDist2 = glm::dot(planarDelta, planarDelta);
                const float maxPlanar = std::max(0.30f, getBoardCellSize() * 0.9f);
                const bool tooLow = origin.y < (renderPos.y + 0.04f);
                const bool tooFar = planarDist2 > (maxPlanar * maxPlanar);
                const bool behind = glm::dot(planarDelta, fwdXZ) < -0.05f;
                if (tooLow || tooFar || behind) {
                    origin = fallbackOrigin;
                }
            }

            const std::string species = lowerCopy(attacker->name);
            float speciesGrowlYOffset = 0.0f;
            float speciesForwardBonus = 0.0f;
            if (species == "bulbasaur") {
                // Bulbasaur mouth sits slightly lower than generic anchors.
                speciesGrowlYOffset = -0.01f;
                speciesForwardBonus = 0.03f;
            }

            float forwardPush = 0.08f + speciesForwardBonus;
            if (attacker->model && attacker->model->hasBounds()) {
                const float r = attacker->model->getBoundsRadiusHorizontal() * worldScale;
                forwardPush = std::clamp(r * 0.38f + speciesForwardBonus, 0.08f, 0.18f);
            }

            origin += glm::vec3(0.0f, speciesGrowlYOffset, 0.0f);
            origin += fwdXZ * forwardPush;

            // Final vertical guardrail against extreme node-space values.
            const float minGrowlY = renderPos.y + 0.06f;
            const float maxGrowlY = renderPos.y + std::max(0.28f, getBoardCellSize() * 0.42f);
            origin.y = std::clamp(origin.y, minGrowlY, maxGrowlY);
        }

        growlWaveVfx.emitFrom(origin, forward, hasLastViewMatrix ? &lastViewMatrix : nullptr);
        return;
    }

    if (move == "scratch" || move == "metal_claw") {
        if (!clawSwipeVfxInitialized) {
            ClawSwipeVFX::Config c;
            clawSwipeVfx.setConfig(c);
            clawSwipeVfxInitialized = true;
        }
        const bool metallic = (move == "metal_claw");
        const glm::vec3 base = target.position + glm::vec3(0.0f, target.visualYOffset, 0.0f);
        clawSwipeVfx.emitAt(base, makeForward(), metallic);
        return;
    }

    if (move == "tail_whip" || move == "bubble" || move == "water_gun") {
        if (!aquaSwooshVfxInitialized) {
            AquaSwooshVFX::Config c;
            aquaSwooshVfx.setConfig(c);
            aquaSwooshVfxInitialized = true;
        }
        AquaSwooshVFX::Style style = AquaSwooshVFX::Style::TailWhip;
        if (move == "bubble") style = AquaSwooshVFX::Style::Bubble;
        if (move == "water_gun") style = AquaSwooshVFX::Style::WaterGun;

        const glm::vec3 base =
            ((move == "tail_whip") && attacker)
                ? (attacker->position + glm::vec3(0.0f, attacker->visualYOffset, 0.0f))
                : (target.position + glm::vec3(0.0f, target.visualYOffset, 0.0f));
        aquaSwooshVfx.emitAt(base, makeForward(), style);
        return;
    }
}
