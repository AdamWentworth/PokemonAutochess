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

int animIndexCached(PokemonInstance& p, const std::string& clipName) {
    if (!p.model) return -1;
    if (clipName.empty()) return -1;

    auto it = p.animIndexCache.find(clipName);
    if (it != p.animIndexCache.end()) return it->second;

    const int idx = AnimSet::resolveAnimIndex(p.model.get(), clipName);
    p.animIndexCache[clipName] = idx;
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
// will make it apply damage only once you start the attack animation (and only while it’s active),
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
