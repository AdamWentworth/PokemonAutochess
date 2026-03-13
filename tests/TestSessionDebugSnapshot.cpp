#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>

#include "game/runtime/session/SessionDebugSnapshot.h"

namespace {

std::optional<std::string> readRawEnv(const char* name) {
    if (name == nullptr || *name == '\0') return std::nullopt;

#if defined(_MSC_VER)
    char* raw = nullptr;
    std::size_t len = 0;
    if (_dupenv_s(&raw, &len, name) != 0 || raw == nullptr) return std::nullopt;
    std::unique_ptr<char, decltype(&std::free)> holder(raw, &std::free);
    return std::string(holder.get());
#else
    const char* raw = std::getenv(name);
    if (raw == nullptr) return std::nullopt;
    return std::string(raw);
#endif
}

bool setEnvVar(const char* name, const char* value) {
    if (name == nullptr || *name == '\0') return false;
#if defined(_MSC_VER)
    return _putenv_s(name, value == nullptr ? "" : value) == 0;
#else
    if (value == nullptr) return unsetenv(name) == 0;
    return setenv(name, value, 1) == 0;
#endif
}

struct ScopedEnvVar {
    explicit ScopedEnvVar(std::string key)
        : name(std::move(key))
        , previous(readRawEnv(name.c_str())) {}

    ~ScopedEnvVar() {
        if (previous.has_value()) {
            setEnvVar(name.c_str(), previous->c_str());
        } else {
            setEnvVar(name.c_str(), nullptr);
        }
    }

    std::string name;
    std::optional<std::string> previous;
};

} // namespace

bool test_session_debug_snapshot_contract(std::string& outFail) {
    using game::runtime::session_debug_snapshot::SessionSnapshotMetadata;

    {
        ScopedEnvVar guard("PAC_DEBUG_STATE_PATH");
        const std::filesystem::path custom =
            std::filesystem::temp_directory_path() / "pac_snapshot_contract" / "state.json";
        setEnvVar("PAC_DEBUG_STATE_PATH", custom.string().c_str());
        if (game::runtime::session_debug_snapshot::snapshotPath() != custom.string()) {
            outFail = "snapshotPath should honor PAC_DEBUG_STATE_PATH when set.";
            return false;
        }
    }

    {
        GameWorld::DebugStateSnapshot snapshot;
        snapshot.money = 17;
        snapshot.classicRoundsCompleted = 3;
        snapshot.items.emplace_back("potion", 2);
        snapshot.classicShopCards.push_back({"charmander", 5, 4});
        snapshot.boardUnits.push_back(GameWorld::DebugUnitSnapshot{
            .name = "pikachu",
            .side = PokemonSide::Player,
        });
        snapshot.boardUnits.push_back(GameWorld::DebugUnitSnapshot{
            .name = "rattata",
            .side = PokemonSide::Enemy,
            .hp = 12,
            .alive = false,
            .captureInProgress = false,
        });

        SessionSnapshotMetadata session;
        session.stateKind = "combat";
        session.stateScriptPath = "scripts/states/combat.lua";
        session.hasRoundPhase = true;
        session.roundPhase = RoundPhase::Battle;
        session.hasCombatActive = true;
        session.combatActive = true;

        const std::string worldSummary =
            game::runtime::session_debug_snapshot::summarizeWorldSnapshot(snapshot);
        const std::string sessionSummary =
            game::runtime::session_debug_snapshot::summarizeSessionSnapshot(session);
        if (worldSummary.find("board=2 (player=1, enemy=1)") == std::string::npos ||
            worldSummary.find("money=17") == std::string::npos ||
            sessionSummary.find("state=combat") == std::string::npos ||
            sessionSummary.find("script=combat.lua") == std::string::npos ||
            sessionSummary.find("round=Battle") == std::string::npos ||
            sessionSummary.find("combat=1") == std::string::npos) {
            outFail = "snapshot summaries should include board, money, script, round, and combat metadata.";
            return false;
        }

        if (!game::runtime::session_debug_snapshot::hasActiveEnemyUnits(snapshot)) {
            outFail = "hasActiveEnemyUnits should treat enemy units with positive HP as active.";
            return false;
        }
        if (game::runtime::session_debug_snapshot::formatMillis(12.345) != "12.35ms") {
            outFail = "formatMillis should round to two decimal places.";
            return false;
        }
    }

    {
        const std::filesystem::path root =
            std::filesystem::temp_directory_path() / "pac_session_debug_snapshot";
        std::filesystem::remove_all(root);
        const std::filesystem::path path = root / "debug_state_snapshot.json";

        GameWorld::DebugStateSnapshot snapshot;
        snapshot.money = 42;
        snapshot.classicWinStreak = 2;
        snapshot.classicLossStreak = 1;
        snapshot.classicRoundsCompleted = 5;
        snapshot.unitSellRewardsEnabled = false;
        snapshot.items.emplace_back("pokeball", 3);
        snapshot.classicShopCards.push_back({"bulbasaur", 2, 3});
        snapshot.boardUnits.push_back(GameWorld::DebugUnitSnapshot{
            .name = "charmander",
            .side = PokemonSide::Enemy,
            .level = 5,
            .hp = 120,
            .energy = 30,
            .col = 1,
            .row = 2,
            .hasPosition = true,
            .posX = 1.0f,
            .posY = 2.0f,
            .posZ = 3.0f,
            .hasRotation = true,
            .rotY = 90.0f,
            .alive = true,
        });
        snapshot.benchUnits.push_back(GameWorld::DebugUnitSnapshot{
            .name = "squirtle",
            .side = PokemonSide::Enemy,
            .benchSlot = 3,
            .alive = true,
        });

        SessionSnapshotMetadata session;
        session.stateKind = "scripted";
        session.stateScriptPath = "scripts/states/starter.lua";
        session.hasCombatActive = true;
        session.combatActive = false;
        session.hasRoundPhase = true;
        session.roundPhase = RoundPhase::Planning;

        std::string err;
        if (!game::runtime::session_debug_snapshot::writeFile(
                snapshot,
                path.string(),
                &session,
                &err)) {
            outFail = "writeFile should persist a valid snapshot file.";
            return false;
        }

        GameWorld::DebugStateSnapshot loaded;
        SessionSnapshotMetadata loadedSession;
        if (!game::runtime::session_debug_snapshot::readFile(
                path.string(),
                loaded,
                &loadedSession,
                &err)) {
            outFail = "readFile should restore a snapshot written by writeFile.";
            return false;
        }

        if (loaded.money != 42 ||
            loaded.classicWinStreak != 2 ||
            loaded.classicLossStreak != 1 ||
            loaded.classicRoundsCompleted != 5 ||
            loaded.unitSellRewardsEnabled ||
            loaded.items.size() != 1u ||
            loaded.classicShopCards.size() != 1u ||
            loaded.boardUnits.size() != 1u ||
            loaded.benchUnits.size() != 1u) {
            outFail = "readFile should preserve core world snapshot fields.";
            return false;
        }

        if (loaded.boardUnits.front().name != "charmander" ||
            loaded.boardUnits.front().side != PokemonSide::Enemy ||
            !loaded.boardUnits.front().hasPosition ||
            !loaded.boardUnits.front().hasRotation ||
            loaded.benchUnits.front().side != PokemonSide::Player) {
            outFail = "readFile should preserve board-unit data and force bench units to the player side.";
            return false;
        }

        if (loadedSession.stateKind != "scripted" ||
            loadedSession.stateScriptPath != "scripts/states/starter.lua" ||
            !loadedSession.hasCombatActive ||
            loadedSession.combatActive ||
            !loadedSession.hasRoundPhase ||
            loadedSession.roundPhase != RoundPhase::Planning) {
            outFail = "readFile should restore session metadata alongside the world snapshot.";
            return false;
        }
    }

    {
        const std::filesystem::path root =
            std::filesystem::temp_directory_path() / "pac_session_debug_snapshot_invalid";
        std::filesystem::remove_all(root);
        const std::filesystem::path path = root / "invalid.json";
        std::filesystem::create_directories(root);
        std::ofstream out(path);
        out << "{\"world\":{\"board_units\":[{\"side\":\"enemy\"}]}}";
        out.close();

        GameWorld::DebugStateSnapshot snapshot;
        std::string err;
        if (game::runtime::session_debug_snapshot::readFile(path.string(), snapshot, nullptr, &err) ||
            err.find("invalid board_units entry") == std::string::npos) {
            outFail = "readFile should report invalid unit snapshot entries.";
            return false;
        }
    }

    return true;
}
