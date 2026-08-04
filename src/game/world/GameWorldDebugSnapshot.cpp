#include "game/world/GameWorld.h"
#include "game/GameConfig.h"

#include <algorithm>
#include <limits>
#include <sstream>

namespace {

int clampInt(int v, int lo, int hi) {
    return std::max(lo, std::min(hi, v));
}

}  // namespace

bool GameWorld::buildDebugStateSnapshot(DebugStateSnapshot& out) const {
    out = DebugStateSnapshot{};
    out.money = money;
    out.classicWinStreak = classicWinStreak;
    out.classicLossStreak = classicLossStreak;
    out.classicRoundsCompleted = classicRoundsCompleted;
    out.unitSellRewardsEnabled = unitSellRewardsEnabled;
    out.items = listItems();
    out.classicShopCards = classicShopCards;

    out.boardUnits.reserve(pokemons.size());
    for (const auto& unit : pokemons) {
        DebugUnitSnapshot snap;
        snap.name = unit.name;
        snap.modelVariant = unit.modelVariant;
        snap.side = unit.side;
        snap.level = unit.level;
        snap.hp = unit.hp;
        snap.energy = unit.energy;
        const auto cell = worldToGrid(unit.position);
        snap.col = cell.x;
        snap.row = cell.y;
        snap.benchSlot = -1;
        snap.hasPosition = true;
        snap.posX = unit.position.x;
        snap.posY = unit.position.y;
        snap.posZ = unit.position.z;
        snap.hasRotation = true;
        snap.rotX = unit.rotation.x;
        snap.rotY = unit.rotation.y;
        snap.rotZ = unit.rotation.z;
        if (const auto it = battleStartPositions.find(unit.id); it != battleStartPositions.end()) {
            snap.hasBattleStartPose = true;
            snap.battleStartX = it->second.position.x;
            snap.battleStartY = it->second.position.y;
            snap.battleStartZ = it->second.position.z;
            snap.hasBattleStartRotation = true;
            snap.battleStartRotX = it->second.rotation.x;
            snap.battleStartRotY = it->second.rotation.y;
            snap.battleStartRotZ = it->second.rotation.z;
        }
        snap.alive = unit.alive;
        snap.fainting = unit.fainting;
        snap.captureInProgress = unit.captureInProgress;
        out.boardUnits.push_back(std::move(snap));
    }

    out.benchUnits.reserve(benchPokemons.size());
    for (std::size_t i = 0; i < benchPokemons.size(); ++i) {
        const auto& unit = benchPokemons[i];
        DebugUnitSnapshot snap;
        snap.name = unit.name;
        snap.modelVariant = unit.modelVariant;
        snap.side = PokemonSide::Player;
        snap.level = unit.level;
        snap.hp = unit.hp;
        snap.energy = unit.energy;
        snap.benchSlot = static_cast<int>(i);
        const auto cell = worldToGrid(unit.position);
        snap.col = cell.x;
        snap.row = cell.y;
        snap.hasPosition = true;
        snap.posX = unit.position.x;
        snap.posY = unit.position.y;
        snap.posZ = unit.position.z;
        snap.hasRotation = true;
        snap.rotX = unit.rotation.x;
        snap.rotY = unit.rotation.y;
        snap.rotZ = unit.rotation.z;
        if (const auto it = battleStartPositions.find(unit.id); it != battleStartPositions.end()) {
            snap.hasBattleStartPose = true;
            snap.battleStartX = it->second.position.x;
            snap.battleStartY = it->second.position.y;
            snap.battleStartZ = it->second.position.z;
            snap.hasBattleStartRotation = true;
            snap.battleStartRotX = it->second.rotation.x;
            snap.battleStartRotY = it->second.rotation.y;
            snap.battleStartRotZ = it->second.rotation.z;
        }
        snap.alive = unit.alive;
        snap.fainting = unit.fainting;
        snap.captureInProgress = unit.captureInProgress;
        out.benchUnits.push_back(std::move(snap));
    }

    return true;
}

bool GameWorld::applyDebugStateSnapshot(const DebugStateSnapshot& in, std::string* outError) {
    auto setError = [&](const std::string& msg) {
        if (outError) *outError = msg;
    };

    if (!data) {
        setError("GameDataDb is not initialized.");
        return false;
    }

    battleStartPositions.clear();
    resetForNewGame(in.money);

    classicWinStreak = std::max(0, in.classicWinStreak);
    classicLossStreak = std::max(0, in.classicLossStreak);
    classicRoundsCompleted = std::max(0, in.classicRoundsCompleted);
    unitSellRewardsEnabled = in.unitSellRewardsEnabled;
    setClassicShopCards(in.classicShopCards);

    items.clear();
    for (const auto& kv : in.items) {
        if (kv.first.empty() || kv.second <= 0) continue;
        items[kv.first] = kv.second;
    }

    int failedBoard = 0;
    int failedBench = 0;

    for (const auto& snap : in.boardUnits) {
        if (snap.name.empty()) {
            ++failedBoard;
            continue;
        }

        PokemonInstance inst;
        if (!buildPokemonInstance(snap.name, snap.side, snap.level, inst, snap.modelVariant)) {
            ++failedBoard;
            continue;
        }

        if (snap.hasPosition) {
            inst.position = conformPositionToGround(
                glm::vec3(snap.posX, snap.posY, snap.posZ));
        } else {
            inst.position = gridToWorld(snap.col, snap.row);
        }
        if (snap.hasRotation) {
            inst.rotation = glm::vec3(snap.rotX, snap.rotY, snap.rotZ);
        }
        inst.hp = clampInt(snap.hp, 0, std::max(0, inst.maxHP));
        inst.energy = clampInt(snap.energy, 0, std::max(0, inst.maxEnergy));
        inst.alive = snap.alive && inst.hp > 0;
        if (!inst.alive) inst.hp = 0;
        inst.fainting = snap.fainting;
        inst.captureInProgress = snap.captureInProgress;
        const int newId = inst.id;
        pokemons.push_back(std::move(inst));
        if (snap.hasBattleStartPose) {
            BattleStartPose pose;
            pose.position = glm::vec3(snap.battleStartX, snap.battleStartY, snap.battleStartZ);
            if (snap.hasBattleStartRotation) {
                pose.rotation = glm::vec3(snap.battleStartRotX, snap.battleStartRotY, snap.battleStartRotZ);
            } else {
                pose.rotation = glm::vec3(0.0f, inst.side == PokemonSide::Player ? 180.0f : 0.0f, 0.0f);
            }
            battleStartPositions[newId] = pose;
        }
    }

    std::vector<DebugUnitSnapshot> bench = in.benchUnits;
    std::stable_sort(bench.begin(), bench.end(), [](const DebugUnitSnapshot& a, const DebugUnitSnapshot& b) {
        const int sa = (a.benchSlot >= 0) ? a.benchSlot : std::numeric_limits<int>::max();
        const int sb = (b.benchSlot >= 0) ? b.benchSlot : std::numeric_limits<int>::max();
        return sa < sb;
    });

    const int benchSlots = std::max(1, config.benchSlots);
    int nextAutoSlot = 0;
    for (const auto& snap : bench) {
        if (snap.name.empty()) {
            ++failedBench;
            continue;
        }

        PokemonInstance inst;
        if (!buildPokemonInstance(snap.name, PokemonSide::Player, snap.level, inst, snap.modelVariant)) {
            ++failedBench;
            continue;
        }

        const int requested = (snap.benchSlot >= 0) ? snap.benchSlot : nextAutoSlot;
        const int slot = clampInt(requested, 0, benchSlots - 1);
        nextAutoSlot = std::max(nextAutoSlot, slot + 1);

        if (snap.hasPosition) {
            inst.position = conformPositionToGround(
                glm::vec3(snap.posX, snap.posY, snap.posZ));
        } else {
            inst.position = benchSlotToWorld(slot, getBoardCellSize());
        }
        if (snap.hasRotation) {
            inst.rotation = glm::vec3(snap.rotX, snap.rotY, snap.rotZ);
        }
        inst.hp = clampInt(snap.hp, 0, std::max(0, inst.maxHP));
        inst.energy = clampInt(snap.energy, 0, std::max(0, inst.maxEnergy));
        inst.alive = snap.alive && inst.hp > 0;
        if (!inst.alive) inst.hp = 0;
        inst.fainting = snap.fainting;
        inst.captureInProgress = snap.captureInProgress;
        const int newId = inst.id;
        benchPokemons.push_back(std::move(inst));
        if (snap.hasBattleStartPose) {
            BattleStartPose pose;
            pose.position = glm::vec3(snap.battleStartX, snap.battleStartY, snap.battleStartZ);
            if (snap.hasBattleStartRotation) {
                pose.rotation = glm::vec3(snap.battleStartRotX, snap.battleStartRotY, snap.battleStartRotZ);
            } else {
                pose.rotation = glm::vec3(0.0f, 180.0f, 0.0f);
            }
            battleStartPositions[newId] = pose;
        }
    }

    reconcileBoardScaleFromRoster();
    conformPokemonToGround();

    if (failedBoard > 0 || failedBench > 0) {
        std::ostringstream oss;
        oss << "Loaded with missing units (board=" << failedBoard
            << ", bench=" << failedBench << ").";
        setError(oss.str());
        return false;
    }

    return true;
}
