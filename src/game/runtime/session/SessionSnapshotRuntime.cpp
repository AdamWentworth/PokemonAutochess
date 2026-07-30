#include "game/runtime/session/SessionSnapshotRuntime.h"

#include "game/GameServices.h"
#include "game/GameStateManager.h"
#include "game/PhaseState.h"
#include "game/logging/LoggerUtil.h"
#include "game/state/CombatState.h"
#include "game/state/scripted/ScriptedState.h"
#include "game/systems/RoundSystem.h"
#include "engine/core/Environment.h"
#include "engine/core/ecs/World.h"

#include <chrono>
#include <memory>
#include <string>

namespace game::runtime::session_snapshot_runtime {

namespace {

using SnapshotClock = std::chrono::high_resolution_clock;

void restoreStateStack(const SessionSnapshotMetadata& session,
                       bool preferCombatState,
                       GameStateManager* stateManager,
                       GameWorld* gameWorld,
                       GameServices* services,
                       LogBus::Logger* log) {
    if (!stateManager || !gameWorld || !services) return;

    if (preferCombatState || session.stateKind == "combat") {
        if (dynamic_cast<CombatState*>(stateManager->getCurrentState())) {
            return;
        }

        std::string combatScript = session.stateScriptPath;
        if (combatScript.empty()) {
            game::log::warn(
                log,
                "[StateSnapshot] Missing combat script path; keeping current state stack.");
            return;
        }

        stateManager->clearAndPushState(std::make_unique<CombatState>(
            stateManager,
            gameWorld,
            *services,
            combatScript,
            true));
        return;
    }

    if (session.stateKind == "scripted" && !session.stateScriptPath.empty()) {
        const auto* scripted = dynamic_cast<ScriptedState*>(stateManager->getCurrentState());
        if (scripted && scripted->debugScriptPath() == session.stateScriptPath) {
            return;
        }
        stateManager->clearAndPushState(std::make_unique<ScriptedState>(
            stateManager,
            gameWorld,
            *services,
            session.stateScriptPath));
    }
}

void applyRuntimeFlags(const RuntimeFlags& flags,
                       GameWorld* gameWorld,
                       GameServices* services,
                       RoundSystem* roundSystem) {
    if (!services || !services->ecsWorld || !services->ecsWorld->alive(services->combatStateEntity)) {
        return;
    }

    if (auto* combatState = services->ecsWorld->get<game::CombatActive>(services->combatStateEntity)) {
        combatState->active = flags.combatActive;
    } else {
        services->ecsWorld->add<game::CombatActive>(
            services->combatStateEntity,
            game::CombatActive{flags.combatActive});
    }
    if (auto* roundState = services->ecsWorld->get<game::RoundState>(services->combatStateEntity)) {
        roundState->phase = flags.phase;
    } else {
        services->ecsWorld->add<game::RoundState>(
            services->combatStateEntity,
            game::RoundState{flags.phase});
    }

    if (roundSystem) {
        float timer = 30.0f;
        if (flags.phase == RoundPhase::Battle) timer = 10.0f;
        else if (flags.phase == RoundPhase::Resolution) timer = 5.0f;
        roundSystem->debugSetPhase(flags.phase, timer);
    }

    if (gameWorld) {
        gameWorld->setBoardInteractionLocked(flags.combatActive);
        if (flags.combatActive) {
            if (!gameWorld->hasBattleStartPositions()) {
                gameWorld->capturePlayerPositionsForBattle();
            }
            gameWorld->clearClassicShopCards();
            gameWorld->setUnitDropZoneLayoutHint(0, false);
        }
    }
}

} // namespace

SessionSnapshotMetadata captureSessionMetadata(GameStateManager* stateManager,
                                              const GameServices* services) {
    SessionSnapshotMetadata out;

    if (stateManager) {
        if (GameState* current = stateManager->getCurrentState()) {
            if (const auto* combat = dynamic_cast<const CombatState*>(current)) {
                out.stateKind = "combat";
                out.stateScriptPath = combat->debugScriptPath();
            } else if (const auto* scripted = dynamic_cast<const ScriptedState*>(current)) {
                out.stateKind = "scripted";
                out.stateScriptPath = scripted->debugScriptPath();
            }
        }
    }

    if (services && services->ecsWorld && services->ecsWorld->alive(services->combatStateEntity)) {
        if (const auto* combatState = services->ecsWorld->get<game::CombatActive>(services->combatStateEntity)) {
            out.hasCombatActive = true;
            out.combatActive = combatState->active;
        }
        if (const auto* roundState = services->ecsWorld->get<game::RoundState>(services->combatStateEntity)) {
            out.hasRoundPhase = true;
            out.roundPhase = roundState->phase;
        }
    }

    return out;
}

RuntimeFlags resolveRuntimeFlags(const SessionSnapshotMetadata& session,
                                 bool preferCombatState) {
    RuntimeFlags out;
    out.combatActive = preferCombatState;
    if (!preferCombatState && session.hasCombatActive) {
        out.combatActive = session.combatActive;
    }

    out.phase = out.combatActive ? RoundPhase::Battle : RoundPhase::Planning;
    if (!preferCombatState && session.hasRoundPhase) {
        out.phase = session.roundPhase;
    }

    return out;
}

void saveSnapshot(const std::string& path, const SaveOptions& options) {
    if (!options.gameWorld) {
        game::log::warn(options.log, "[StateSnapshot] Save skipped: world is not ready.");
        game::log::infoTerminalOnly(options.log, "[StateSnapshot] Save skipped: world is not ready.");
        return;
    }

    GameWorld::DebugStateSnapshot snapshot;
    if (!options.gameWorld->buildDebugStateSnapshot(snapshot)) {
        game::log::warn(options.log, "[StateSnapshot] Save failed: could not build world snapshot.");
        game::log::infoTerminalOnly(
            options.log,
            "[StateSnapshot] Save failed: could not build world snapshot.");
        return;
    }

    const SessionSnapshotMetadata session =
        captureSessionMetadata(options.stateManager, options.services);
    std::string err;
    if (!game::runtime::session_debug_snapshot::writeFile(snapshot, path, &session, &err)) {
        const std::string message =
            std::string("[StateSnapshot] Save failed: ") + err + " (" + path + ")";
        game::log::warn(options.log, message);
        game::log::infoTerminalOnly(options.log, message);
        return;
    }

    const std::string message = std::string("[StateSnapshot] Saved: ") + path;
    game::log::info(options.log, message);
    game::log::infoTerminalOnly(
        options.log,
        message + " "
            + game::runtime::session_debug_snapshot::summarizeSessionSnapshot(session) + " "
            + game::runtime::session_debug_snapshot::summarizeWorldSnapshot(snapshot));
}

void loadSnapshot(const std::string& path, const LoadOptions& options) {
    if (!options.gameWorld) {
        game::log::warn(options.log, "[StateSnapshot] Load skipped: world is not ready.");
        game::log::infoTerminalOnly(options.log, "[StateSnapshot] Load skipped: world is not ready.");
        return;
    }

    const auto loadStart = SnapshotClock::now();
    game::log::infoTerminalOnly(options.log, std::string("[StateSnapshot] Load requested: ") + path);

    GameWorld::DebugStateSnapshot snapshot;
    SessionSnapshotMetadata session;
    std::string err;

    const auto readStart = SnapshotClock::now();
    if (!game::runtime::session_debug_snapshot::readFile(path, snapshot, &session, &err)) {
        const auto readEnd = SnapshotClock::now();
        const std::string message =
            std::string("[StateSnapshot] Load failed: ") + err + " (" + path + ")";
        game::log::warn(options.log, message);
        game::log::infoTerminalOnly(
            options.log,
            message
                + " read=" + game::runtime::session_debug_snapshot::formatMillis(
                    std::chrono::duration<double, std::milli>(readEnd - readStart).count()));
        return;
    }
    const auto readEnd = SnapshotClock::now();

    if (options.resetRenderCaches) {
        options.resetRenderCaches();
    }

    const bool preferCombatState =
        game::runtime::session_debug_snapshot::hasActiveEnemyUnits(snapshot);
    const auto stateRestoreStart = SnapshotClock::now();
    restoreStateStack(
        session,
        preferCombatState,
        options.stateManager,
        options.gameWorld,
        options.services,
        options.log);
    const auto stateRestoreEnd = SnapshotClock::now();

    std::string worldErr;
    const auto worldApplyStart = SnapshotClock::now();
    const bool exact = options.gameWorld->applyDebugStateSnapshot(snapshot, &worldErr);
    const auto worldApplyEnd = SnapshotClock::now();

    if (const auto editorMode =
            engine::env::get("PAC_EDITOR_GAME_MODE");
        options.services && editorMode) {
        if (*editorMode == "classic" ||
            *editorMode == "adventure") {
            options.services->gameMode = *editorMode;
            options.gameWorld->setUnitSellRewardsEnabled(
                *editorMode == "classic");
        }
    }

    const auto flagsStart = SnapshotClock::now();
    applyRuntimeFlags(
        resolveRuntimeFlags(session, preferCombatState),
        options.gameWorld,
        options.services,
        options.roundSystem);
    const auto flagsEnd = SnapshotClock::now();

    const auto inventoryStart = SnapshotClock::now();
    if (options.refreshInventoryPanel) {
        options.refreshInventoryPanel();
    }
    const auto inventoryEnd = SnapshotClock::now();

    double prewarmIndexedMs = 0.0;
    std::size_t prewarmIndexedBatches = 0u;
    if ((!options.shouldPrewarmIndexedLayer || options.shouldPrewarmIndexedLayer()) &&
        options.prewarmIndexedLayer) {
        const auto indexedPrewarmStart = SnapshotClock::now();
        prewarmIndexedBatches = options.prewarmIndexedLayer();
        const auto indexedPrewarmEnd = SnapshotClock::now();
        prewarmIndexedMs = std::chrono::duration<double, std::milli>(
            indexedPrewarmEnd - indexedPrewarmStart).count();
    }
    const auto loadEnd = SnapshotClock::now();

    const double readMs =
        std::chrono::duration<double, std::milli>(readEnd - readStart).count();
    const double stateRestoreMs =
        std::chrono::duration<double, std::milli>(stateRestoreEnd - stateRestoreStart).count();
    const double worldApplyMs =
        std::chrono::duration<double, std::milli>(worldApplyEnd - worldApplyStart).count();
    const double flagsMs =
        std::chrono::duration<double, std::milli>(flagsEnd - flagsStart).count();
    const double inventoryMs =
        std::chrono::duration<double, std::milli>(inventoryEnd - inventoryStart).count();
    const double totalMs =
        std::chrono::duration<double, std::milli>(loadEnd - loadStart).count();

    if (!exact) {
        if (worldErr.empty()) {
            worldErr = "snapshot applied with missing units";
        }
        const std::string message =
            std::string("[StateSnapshot] Loaded with warnings: ") + worldErr;
        game::log::warn(options.log, message);
        game::log::infoTerminalOnly(options.log, message);
    }

    const std::string loadedMessage = std::string("[StateSnapshot] Loaded: ") + path;
    game::log::info(options.log, loadedMessage);
    game::log::infoTerminalOnly(
        options.log,
        loadedMessage
            + " exact=" + (exact ? "1" : "0")
            + " preferCombat=" + (preferCombatState ? std::string("1") : std::string("0"))
            + " " + game::runtime::session_debug_snapshot::summarizeSessionSnapshot(session)
            + " " + game::runtime::session_debug_snapshot::summarizeWorldSnapshot(snapshot));
    game::log::infoTerminalOnly(
        options.log,
        std::string("[StateSnapshot] Load phases: ")
            + "read=" + game::runtime::session_debug_snapshot::formatMillis(readMs)
            + " state=" + game::runtime::session_debug_snapshot::formatMillis(stateRestoreMs)
            + " apply=" + game::runtime::session_debug_snapshot::formatMillis(worldApplyMs)
            + " flags=" + game::runtime::session_debug_snapshot::formatMillis(flagsMs)
            + " inventory=" + game::runtime::session_debug_snapshot::formatMillis(inventoryMs)
            + " prewarm_indexed=" + game::runtime::session_debug_snapshot::formatMillis(prewarmIndexedMs)
            + " prewarm_batches=" + std::to_string(prewarmIndexedBatches)
            + " total=" + game::runtime::session_debug_snapshot::formatMillis(totalMs));
}

} // namespace game::runtime::session_snapshot_runtime
