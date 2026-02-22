// src/game/scripting/LuaBindings.cpp
#include <glm/glm.hpp>
#include "engine/render/Model.h"
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

#include "LuaBindings.h"

#include "game/GameWorld.h"
#include "game/PokemonInstance.h"
#include "game/GameStateManager.h"
#include "game/GameConfig.h"

#include "game/animation/FlightLocomotion.h"
#include "game/animation/AttackAnimDebug.h"

#include "game/config/PokemonConfigLoader.h"
#include "game/config/MovesConfigLoader.h"
#include "game/config/AttackAnimConfigLoader.h"
#include "game/config/AnimSetLoader.h"

#include "game/state/ScriptedState.h"

#include "game/logging/LogBus.h"
#include "game/logging/DebugTrace.h"

#include "LuaBindings_Internal.h"

std::string toLowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });
    return s;
}

namespace {
std::string stripSuffix(const std::string& s, const std::string& suffix) {
    if (!suffix.empty() && s.size() >= suffix.size()) {
        const std::string tail = s.substr(s.size() - suffix.size());
        if (toLowerCopy(tail) == toLowerCopy(suffix)) {
            return s.substr(0, s.size() - suffix.size());
        }
    }
    return s;
}
}  // namespace

int animIndexCached(PokemonInstance& p, const std::string& clipName) {
    if (clipName.empty()) return -1;

    const auto findCached = [&](const std::string& key) -> int {
        if (key.empty()) return -1;
        auto it = p.animIndexCache.find(key);
        if (it != p.animIndexCache.end()) return it->second;
        const std::string lower = toLowerCopy(key);
        it = p.animIndexCache.find(lower);
        if (it != p.animIndexCache.end()) return it->second;
        return -1;
    };
    const auto cacheAlias = [&](const std::string& key, int idx) {
        if (idx < 0 || key.empty()) return;
        p.animIndexCache[key] = idx;
        p.animIndexCache[toLowerCopy(key)] = idx;
    };

    const std::string noGfbanm = stripSuffix(clipName, ".gfbanm");
    const std::string noStart = stripSuffix(clipName, "__START");
    const std::string noEnd = stripSuffix(clipName, "__END");
    std::string compact = stripSuffix(noGfbanm, "__START");
    compact = stripSuffix(compact, "__END");

    int idx = findCached(clipName);
    if (idx >= 0) return idx;
    idx = findCached(noGfbanm);
    if (idx >= 0) return idx;
    idx = findCached(noStart);
    if (idx >= 0) return idx;
    idx = findCached(noEnd);
    if (idx >= 0) return idx;
    idx = findCached(compact);
    if (idx >= 0) return idx;

    if (!p.model) return -1;

    idx = AnimSet::resolveAnimIndex(p.model.get(), clipName);
    cacheAlias(clipName, idx);
    cacheAlias(noGfbanm, idx);
    cacheAlias(noStart, idx);
    cacheAlias(noEnd, idx);
    cacheAlias(compact, idx);
    return idx;
}
// Helper
PokemonSide sideFromString(const std::string& s) {
    if (s == "Enemy" || s == "enemy") return PokemonSide::Enemy;
    return PokemonSide::Player;
}

// Grid helpers
glm::vec3 gridToWorld(const GameConfigData& c, int col, int row) {
    float boardOriginX = -((c.cols * c.cellSize) / 2.0f) + c.cellSize * 0.5f;
    float boardOriginZ = -((c.rows * c.cellSize) / 2.0f) + c.cellSize * 0.5f;
    return { boardOriginX + col * c.cellSize, 0.0f, boardOriginZ + row * c.cellSize };
}
glm::ivec2 worldToGrid(const GameConfigData& c, const glm::vec3& pos) {
    float boardOriginX = -((c.cols * c.cellSize) / 2.0f) + c.cellSize * 0.5f;
    float boardOriginZ = -((c.rows * c.cellSize) / 2.0f) + c.cellSize * 0.5f;
    int col = static_cast<int>(std::round((pos.x - boardOriginX) / c.cellSize));
    int row = static_cast<int>(std::round((pos.z - boardOriginZ) / c.cellSize));
    return { col, row };
}

// ============================================================================
// IMPORTANT GAMEPLAY CHANGE (outgoing damage gating):
//
// - Receiving damage: unchanged (targets always lose HP when this function decides to apply it).
// - Applying damage: ONLY occurs if the attacker is actually playing its attack animation
//   (i.e., attackTimerSec > 0 and activeAnimIndex == currently-selected attack clip).
//
// This prevents "ghost" hits during takeoff/landing/other cosmetic animations.
//
// NOTE: This assumes your combat loop calls world_apply_damage at the time it *wants* to
// deal damage. If the loop calls it continuously every tick while in range, this gating
// will make it apply damage only once you start the attack animation (and only while it's active),
// but you should still make sure the combat logic has a cooldown / one-shot trigger.
//
// ADDITIONAL CHANGE (anim state correctness):
// - Do NOT switch attack clips mid-cycle.
// - Fast attacks: always use the configured loop clip (phase=loop). Start/end are ignored.
// - Charged attacks: keep using the configured one_shot/default clip.
// - Disable legacy fast-attack chaining state (chainedFastMove/fastChainTimerSec).
// ============================================================================


bool attackerIsInAttackAnimation(const PokemonInstance& A) {
    if (!A.alive) return false;
    if (A.attackTimerSec <= 0.0f) return false;

    // Use the currently-selected attack clip (start/loop/end/one_shot), not hard-coded attack1.
    const int idx = (A.currentAttackAnimIndex >= 0) ? A.currentAttackAnimIndex : A.animAttack1Index;
    if (idx < 0) return false;

    return (A.activeAnimIndex == idx);
}
