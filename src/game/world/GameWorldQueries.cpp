#include "game/GameConfig.h"
#include "game/world/GameWorld.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "game/config/GameDataDb.h"
#include "game/config/PokemonConfigLoader.h"

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

}  // namespace

std::string GameWorld::resolveEvolutionLineRoot(const std::string& species) const {
    std::string current = lower(species);
    if (current.empty() || !data) return current;

    std::unordered_set<std::string> visited;
    visited.reserve(8);

    for (int i = 0; i < 16; ++i) {
        if (!visited.insert(current).second) break;
        std::string prev;
        if (!data->evolution.getPreEvolution(current, prev)) break;
        if (prev.empty()) break;
        current = prev;
    }
    return current;
}

const std::vector<GameWorld::TypeLineCount>& GameWorld::getPlayerTypeLineCountsCached() const {
    if (cachedPlayerTypeLineRevision_ == overlayRosterRevision_) {
        return cachedPlayerTypeLines_;
    }

    cachedPlayerTypeLines_.clear();
    if (!data) {
        cachedPlayerTypeLineRevision_ = overlayRosterRevision_;
        return cachedPlayerTypeLines_;
    }

    std::unordered_map<std::string, std::unordered_set<std::string>> typeToRoots;
    typeToRoots.reserve(16);

    auto ingest = [&](const PokemonInstance& unit) {
        if (unit.side != PokemonSide::Player) return;

        const PokemonStats* stats = data->pokemon.getStats(unit.name);
        if (!stats) return;

        const std::string root = resolveEvolutionLineRoot(unit.name);
        if (root.empty()) return;

        // Avoid duplicate type entries from malformed config.
        std::unordered_set<std::string> unitTypes;
        unitTypes.reserve(stats->types.size());
        for (const auto& rawType : stats->types) {
            const std::string t = lower(rawType);
            if (t.empty()) continue;
            if (!unitTypes.insert(t).second) continue;
            typeToRoots[t].insert(root);
        }
    };

    for (const auto& u : pokemons) ingest(u);
    for (const auto& u : benchPokemons) ingest(u);

    cachedPlayerTypeLines_.reserve(typeToRoots.size());
    for (const auto& kv : typeToRoots) {
        const int count = static_cast<int>(kv.second.size());
        if (count <= 0) continue;
        cachedPlayerTypeLines_.push_back(TypeLineCount{kv.first, count});
    }

    std::sort(
        cachedPlayerTypeLines_.begin(),
        cachedPlayerTypeLines_.end(),
        [](const TypeLineCount& a, const TypeLineCount& b) {
        if (a.uniqueLineCount != b.uniqueLineCount) return a.uniqueLineCount > b.uniqueLineCount;
        return a.type < b.type;
    });

    cachedPlayerTypeLineRevision_ = overlayRosterRevision_;
    return cachedPlayerTypeLines_;
}

std::vector<GameWorld::TypeLineCount> GameWorld::getPlayerTypeLineCounts() const {
    return getPlayerTypeLineCountsCached();
}

glm::vec3 GameWorld::getNearestEnemyPosition(const PokemonInstance& unit) const {
    float closestDist = std::numeric_limits<float>::max();
    glm::vec3 closestPos = unit.position;

    for (const auto& other : pokemons) {
        if (!other.alive || other.captureInProgress || other.side == unit.side) continue;
        if (!combatMap().canPerceive(combatActor(unit), combatActor(other))) continue;
        const float d = glm::distance(unit.position, other.position);
        if (d < closestDist) {
            closestDist = d;
            closestPos = other.position;
        }
    }

    return closestPos;
}

game::arena::CombatMapView GameWorld::combatMap() const {
    return {config.cols, config.rows, combatMapRules_.get()};
}

game::arena::Actor GameWorld::combatActor(const PokemonInstance &unit) const {
    const auto cell = worldToGrid(unit.position);
    const auto centre = gridToWorld(cell.x, cell.y);
    const float size = std::max(config.cellSize, 0.0001f);
    return {unit.id, static_cast<int>(unit.side), {cell.x, cell.y}, unit.traversalCapabilities,
            unit.ledgeJump.active(), unit.airState == AirLocomotionState::Grounded,
            unit.coverRevealRemainingSec > 0.0f || unit.attackTimerSec > 0.0f || unit.captureInProgress,
            (unit.position.x - centre.x) / size, (unit.position.z - centre.z) / size};
}

bool GameWorld::canTeamPerceive(PokemonSide team, const PokemonInstance &target) const {
    if (target.side == team) return true;
    const auto map = combatMap();
    const auto actor = combatActor(target);
    // Open terrain and attack reveals remain visible even without a spotter.
    game::arena::Actor outside;
    outside.team = static_cast<int>(team);
    outside.grounded = false;
    if (map.canPerceive(outside, actor)) return true;
    for (const auto &observer : pokemons) {
        if (observer.side == team && observer.alive && !observer.fainting && !observer.captureInProgress &&
            map.canPerceive(combatActor(observer), actor)) return true;
    }
    return false;
}

bool GameWorld::isVisibleToPlayer(const PokemonInstance &unit) const {
    return showConcealedUnits_ || canTeamPerceive(PokemonSide::Player, unit);
}
