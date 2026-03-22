#pragma once

#include "game/GameWorld.h"
#include "game/systems/RoundPhase.h"

#include <string>

namespace game::runtime::session_debug_snapshot {

struct SessionSnapshotMetadata {
    std::string stateKind;
    std::string stateScriptPath;
    bool hasCombatActive = false;
    bool combatActive = false;
    bool hasRoundPhase = false;
    RoundPhase roundPhase = RoundPhase::Planning;
};

std::string snapshotPath();
bool autoLoadSnapshotEnabled();
bool hasActiveEnemyUnits(const GameWorld::DebugStateSnapshot& snapshot);
std::string summarizeWorldSnapshot(const GameWorld::DebugStateSnapshot& snapshot);
std::string summarizeSessionSnapshot(const SessionSnapshotMetadata& session);
std::string formatMillis(double ms);

bool writeFile(const GameWorld::DebugStateSnapshot& snapshot,
               const std::string& path,
               const SessionSnapshotMetadata* session,
               std::string* outError);

bool readFile(const std::string& path,
              GameWorld::DebugStateSnapshot& out,
              SessionSnapshotMetadata* outSession,
              std::string* outError);

} // namespace game::runtime::session_debug_snapshot
