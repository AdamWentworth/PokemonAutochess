#include "game/world/GameWorld.h"

#include <algorithm>
#include <cctype>
#include <string>

#include "game/config/GameDataDb.h"
#include "game/world/MoveImpactMath.h"
#include "game/world/MoveImpactRouting.h"

namespace {

std::string lowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string resolveBackendModelPathLocal(const PokemonInstance& unit, const GameDataDb* data) {
    if (!unit.backendModelPath.empty()) return unit.backendModelPath;
    if (!unit.animIndexCacheSourceModelPath.empty()) return unit.animIndexCacheSourceModelPath;
    if (!unit.backendAnimDurationsSourceModelPath.empty()) {
        return unit.backendAnimDurationsSourceModelPath;
    }
    if (!data) return {};

    const PokemonStats* stats = data->pokemon.getStats(unit.name);
    if (!stats || stats->resolveModel(unit.modelVariant).empty()) return {};
    return "assets/models/" + stats->resolveModel(unit.modelVariant);
}

const game::runtime::render_model::MeshData* tryResolveImpactMeshLocal(const PokemonInstance& unit,
                                                                       const GameDataDb* data) {
    const std::string modelPath = resolveBackendModelPathLocal(unit, data);
    if (modelPath.empty()) return nullptr;
    return resolveMoveImpactMeshForModelPath(modelPath);
}

constexpr AquaSwooshVFX::Style toAquaSwooshStyle(AquaImpactStyle style) {
    if (style == AquaImpactStyle::Bubble) return AquaSwooshVFX::Style::Bubble;
    if (style == AquaImpactStyle::WaterGun) return AquaSwooshVFX::Style::WaterGun;
    return AquaSwooshVFX::Style::TailWhip;
}

}  // namespace

void GameWorld::emitGrassImpactAt(const PokemonInstance& target) {
    if (!grassImpactVfxInitialized) {
        GrassImpactVFX::Config c;  // defaults
        grassImpactVfx.setConfig(c);
        grassImpactVfxInitialized = true;
    }

    const glm::vec3 base = target.position + glm::vec3(0.0f, target.visualYOffset, 0.0f);
    grassImpactVfx.emitAt(base);
}

void GameWorld::emitTackleImpactAt(const PokemonInstance& target, const PokemonInstance* attacker) {
    if (!tackleSmokeVfxInitialized) {
        TackleSmokeVFX::Config configData = TackleSmokeVFX::makeGameplayConfig();
        tackleSmokeVfx.setConfig(configData);
        tackleSmokeVfxInitialized = true;
    }

    const auto* targetMesh = tryResolveImpactMeshLocal(target, data);
    const auto* attackerMesh = attacker ? tryResolveImpactMeshLocal(*attacker, data) : nullptr;
    const MoveImpactSurfacePoint impact =
        computeTargetSurfaceImpactPoint(target, attacker, targetMesh, attackerMesh);
    tackleSmokeVfx.emitAt(impact.position, impact.forward);
}

void GameWorld::emitMoveImpactByName(const std::string& moveName,
                                     const PokemonInstance& target,
                                     const PokemonInstance* attacker) {
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
        emitClawSwipeImpact(target, attacker, makeForward(), isMetalClawImpactMove(move));
        return;
    }

    case MoveImpactRoute::AquaSwoosh: {
        const AquaSwooshVFX::Style style = toAquaSwooshStyle(classifyAquaImpactStyle(move));
        const bool originFromAttacker = isTailWhipImpactMove(move);
        emitAquaSwooshImpact(target, attacker, makeForward(), style, originFromAttacker);
        return;
    }

    case MoveImpactRoute::None:
    default:
        return;
    }
}
