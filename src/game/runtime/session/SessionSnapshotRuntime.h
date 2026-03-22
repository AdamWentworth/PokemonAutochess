#pragma once

#include "game/runtime/session/SessionDebugSnapshot.h"
#include "game/systems/RoundPhase.h"

#include <cstddef>
#include <functional>
#include <string>

class GameStateManager;
class GameWorld;
class RoundSystem;
struct GameServices;
namespace LogBus { class Logger; }

namespace game::runtime::session_snapshot_runtime {

using SessionSnapshotMetadata = session_debug_snapshot::SessionSnapshotMetadata;

struct RuntimeFlags {
    bool combatActive = false;
    RoundPhase phase = RoundPhase::Planning;
};

struct SaveOptions {
    GameStateManager* stateManager = nullptr;
    GameWorld* gameWorld = nullptr;
    const GameServices* services = nullptr;
    LogBus::Logger* log = nullptr;
};

struct LoadOptions {
    GameStateManager* stateManager = nullptr;
    GameWorld* gameWorld = nullptr;
    GameServices* services = nullptr;
    RoundSystem* roundSystem = nullptr;
    LogBus::Logger* log = nullptr;
    std::function<void()> refreshInventoryPanel;
    std::function<void()> resetRenderCaches;
    std::function<bool()> shouldPrewarmIndexedLayer;
    std::function<std::size_t()> prewarmIndexedLayer;
};

SessionSnapshotMetadata captureSessionMetadata(GameStateManager* stateManager,
                                              const GameServices* services);

RuntimeFlags resolveRuntimeFlags(const SessionSnapshotMetadata& session,
                                 bool preferCombatState);

void saveSnapshot(const std::string& path, const SaveOptions& options);

void loadSnapshot(const std::string& path, const LoadOptions& options);

} // namespace game::runtime::session_snapshot_runtime
