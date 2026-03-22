#include "game/runtime/session/SessionDebugSnapshot.h"

#include "engine/core/Environment.h"
#include "engine/core/Paths.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>

namespace game::runtime::session_debug_snapshot {

namespace {

const char* sideToToken(PokemonSide side) {
    return side == PokemonSide::Enemy ? "enemy" : "player";
}

std::string toLowerCopy(std::string s) {
    std::transform(
        s.begin(),
        s.end(),
        s.begin(),
        [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
    return s;
}

PokemonSide sideFromJson(const nlohmann::json& j) {
    if (j.is_string()) {
        const std::string token = toLowerCopy(j.get<std::string>());
        if (token == "enemy") return PokemonSide::Enemy;
        return PokemonSide::Player;
    }
    if (j.is_number_integer()) {
        return j.get<int>() == 1 ? PokemonSide::Enemy : PokemonSide::Player;
    }
    return PokemonSide::Player;
}

const char* roundPhaseToToken(RoundPhase phase) {
    switch (phase) {
        case RoundPhase::Planning: return "Planning";
        case RoundPhase::Battle: return "Battle";
        case RoundPhase::Resolution: return "Resolution";
    }
    return "Planning";
}

RoundPhase roundPhaseFromToken(const std::string& token) {
    if (token == "Battle") return RoundPhase::Battle;
    if (token == "Resolution") return RoundPhase::Resolution;
    return RoundPhase::Planning;
}

nlohmann::json encodeUnitSnapshot(const GameWorld::DebugUnitSnapshot& snap) {
    nlohmann::json j = nlohmann::json::object();
    j["name"] = snap.name;
    j["side"] = sideToToken(snap.side);
    j["level"] = snap.level;
    j["hp"] = snap.hp;
    j["energy"] = snap.energy;
    j["col"] = snap.col;
    j["row"] = snap.row;
    j["bench_slot"] = snap.benchSlot;
    j["has_position"] = snap.hasPosition;
    if (snap.hasPosition) {
        j["pos_x"] = snap.posX;
        j["pos_y"] = snap.posY;
        j["pos_z"] = snap.posZ;
    }
    j["has_rotation"] = snap.hasRotation;
    if (snap.hasRotation) {
        j["rot_x"] = snap.rotX;
        j["rot_y"] = snap.rotY;
        j["rot_z"] = snap.rotZ;
    }
    j["has_battle_start_pose"] = snap.hasBattleStartPose;
    if (snap.hasBattleStartPose) {
        j["battle_start_x"] = snap.battleStartX;
        j["battle_start_y"] = snap.battleStartY;
        j["battle_start_z"] = snap.battleStartZ;
        j["has_battle_start_rotation"] = snap.hasBattleStartRotation;
        if (snap.hasBattleStartRotation) {
            j["battle_start_rot_x"] = snap.battleStartRotX;
            j["battle_start_rot_y"] = snap.battleStartRotY;
            j["battle_start_rot_z"] = snap.battleStartRotZ;
        }
    }
    j["alive"] = snap.alive;
    j["fainting"] = snap.fainting;
    j["capture_in_progress"] = snap.captureInProgress;
    return j;
}

bool decodeUnitSnapshot(const nlohmann::json& j,
                        bool expectBench,
                        GameWorld::DebugUnitSnapshot& out,
                        std::string* outError) {
    const auto fail = [&](const std::string& msg) {
        if (outError) *outError = msg;
    };

    if (!j.is_object()) {
        fail("unit entry is not an object");
        return false;
    }

    out = GameWorld::DebugUnitSnapshot{};
    if (const auto it = j.find("name"); it != j.end() && it->is_string()) {
        out.name = it->get<std::string>();
    }
    if (out.name.empty()) {
        fail("unit entry missing name");
        return false;
    }

    if (const auto it = j.find("side"); it != j.end()) {
        out.side = sideFromJson(*it);
    }
    if (const auto it = j.find("level"); it != j.end() && it->is_number_integer()) {
        out.level = std::max(1, it->get<int>());
    }
    if (const auto it = j.find("hp"); it != j.end() && it->is_number_integer()) {
        out.hp = it->get<int>();
    }
    if (const auto it = j.find("energy"); it != j.end() && it->is_number_integer()) {
        out.energy = it->get<int>();
    }
    if (const auto it = j.find("col"); it != j.end() && it->is_number_integer()) {
        out.col = it->get<int>();
    }
    if (const auto it = j.find("row"); it != j.end() && it->is_number_integer()) {
        out.row = it->get<int>();
    }
    if (const auto it = j.find("bench_slot"); it != j.end() && it->is_number_integer()) {
        out.benchSlot = it->get<int>();
    }
    if (const auto it = j.find("has_position"); it != j.end() && it->is_boolean()) {
        out.hasPosition = it->get<bool>();
    }
    if (const auto it = j.find("pos_x"); it != j.end() && it->is_number()) {
        out.posX = it->get<float>();
        out.hasPosition = true;
    }
    if (const auto it = j.find("pos_y"); it != j.end() && it->is_number()) {
        out.posY = it->get<float>();
        out.hasPosition = true;
    }
    if (const auto it = j.find("pos_z"); it != j.end() && it->is_number()) {
        out.posZ = it->get<float>();
        out.hasPosition = true;
    }
    if (const auto it = j.find("has_rotation"); it != j.end() && it->is_boolean()) {
        out.hasRotation = it->get<bool>();
    }
    if (const auto it = j.find("rot_x"); it != j.end() && it->is_number()) {
        out.rotX = it->get<float>();
        out.hasRotation = true;
    }
    if (const auto it = j.find("rot_y"); it != j.end() && it->is_number()) {
        out.rotY = it->get<float>();
        out.hasRotation = true;
    }
    if (const auto it = j.find("rot_z"); it != j.end() && it->is_number()) {
        out.rotZ = it->get<float>();
        out.hasRotation = true;
    }
    if (const auto it = j.find("has_battle_start_pose"); it != j.end() && it->is_boolean()) {
        out.hasBattleStartPose = it->get<bool>();
    }
    if (const auto it = j.find("battle_start_x"); it != j.end() && it->is_number()) {
        out.battleStartX = it->get<float>();
        out.hasBattleStartPose = true;
    }
    if (const auto it = j.find("battle_start_y"); it != j.end() && it->is_number()) {
        out.battleStartY = it->get<float>();
        out.hasBattleStartPose = true;
    }
    if (const auto it = j.find("battle_start_z"); it != j.end() && it->is_number()) {
        out.battleStartZ = it->get<float>();
        out.hasBattleStartPose = true;
    }
    if (const auto it = j.find("has_battle_start_rotation"); it != j.end() && it->is_boolean()) {
        out.hasBattleStartRotation = it->get<bool>();
    }
    if (const auto it = j.find("battle_start_rot_x"); it != j.end() && it->is_number()) {
        out.battleStartRotX = it->get<float>();
        out.hasBattleStartRotation = true;
    }
    if (const auto it = j.find("battle_start_rot_y"); it != j.end() && it->is_number()) {
        out.battleStartRotY = it->get<float>();
        out.hasBattleStartRotation = true;
    }
    if (const auto it = j.find("battle_start_rot_z"); it != j.end() && it->is_number()) {
        out.battleStartRotZ = it->get<float>();
        out.hasBattleStartRotation = true;
    }
    if (const auto it = j.find("alive"); it != j.end() && it->is_boolean()) {
        out.alive = it->get<bool>();
    }
    if (const auto it = j.find("fainting"); it != j.end() && it->is_boolean()) {
        out.fainting = it->get<bool>();
    }
    if (const auto it = j.find("capture_in_progress"); it != j.end() && it->is_boolean()) {
        out.captureInProgress = it->get<bool>();
    }

    if (expectBench) {
        out.side = PokemonSide::Player;
    }
    return true;
}

nlohmann::json encodeStateSnapshot(const GameWorld::DebugStateSnapshot& snapshot) {
    nlohmann::json j = nlohmann::json::object();
    j["version"] = 1;
    j["money"] = snapshot.money;
    j["classic_win_streak"] = snapshot.classicWinStreak;
    j["classic_loss_streak"] = snapshot.classicLossStreak;
    j["classic_rounds_completed"] = snapshot.classicRoundsCompleted;
    j["unit_sell_rewards_enabled"] = snapshot.unitSellRewardsEnabled;

    nlohmann::json boardUnits = nlohmann::json::array();
    for (const auto& unit : snapshot.boardUnits) {
        boardUnits.push_back(encodeUnitSnapshot(unit));
    }
    j["board_units"] = std::move(boardUnits);

    nlohmann::json benchUnits = nlohmann::json::array();
    for (const auto& unit : snapshot.benchUnits) {
        benchUnits.push_back(encodeUnitSnapshot(unit));
    }
    j["bench_units"] = std::move(benchUnits);

    nlohmann::json items = nlohmann::json::object();
    for (const auto& kv : snapshot.items) {
        if (kv.first.empty()) continue;
        items[kv.first] = kv.second;
    }
    j["items"] = std::move(items);

    nlohmann::json cards = nlohmann::json::array();
    for (const auto& card : snapshot.classicShopCards) {
        nlohmann::json cj = nlohmann::json::object();
        cj["name"] = card.name;
        cj["level"] = card.level;
        cj["cost"] = card.cost;
        cards.push_back(std::move(cj));
    }
    j["classic_shop_cards"] = std::move(cards);
    return j;
}

bool decodeStateSnapshot(const nlohmann::json& j,
                         GameWorld::DebugStateSnapshot& out,
                         std::string* outError) {
    const auto fail = [&](const std::string& msg) {
        if (outError) *outError = msg;
    };

    if (!j.is_object()) {
        fail("snapshot root must be an object");
        return false;
    }

    out = GameWorld::DebugStateSnapshot{};
    if (const auto it = j.find("money"); it != j.end() && it->is_number_integer()) {
        out.money = it->get<int>();
    }
    if (const auto it = j.find("classic_win_streak"); it != j.end() && it->is_number_integer()) {
        out.classicWinStreak = std::max(0, it->get<int>());
    }
    if (const auto it = j.find("classic_loss_streak"); it != j.end() && it->is_number_integer()) {
        out.classicLossStreak = std::max(0, it->get<int>());
    }
    if (const auto it = j.find("classic_rounds_completed"); it != j.end() && it->is_number_integer()) {
        out.classicRoundsCompleted = std::max(0, it->get<int>());
    }
    if (const auto it = j.find("unit_sell_rewards_enabled"); it != j.end() && it->is_boolean()) {
        out.unitSellRewardsEnabled = it->get<bool>();
    }

    if (const auto it = j.find("items"); it != j.end() && it->is_object()) {
        for (auto itemIt = it->begin(); itemIt != it->end(); ++itemIt) {
            if (itemIt.value().is_number_integer()) {
                const int amount = itemIt.value().get<int>();
                if (!itemIt.key().empty() && amount > 0) {
                    out.items.emplace_back(itemIt.key(), amount);
                }
            }
        }
    }

    if (const auto it = j.find("classic_shop_cards"); it != j.end() && it->is_array()) {
        for (const auto& entry : *it) {
            if (!entry.is_object()) continue;
            GameWorld::ClassicShopCard card;
            if (const auto nameIt = entry.find("name"); nameIt != entry.end() && nameIt->is_string()) {
                card.name = nameIt->get<std::string>();
            }
            if (card.name.empty()) continue;
            if (const auto levelIt = entry.find("level"); levelIt != entry.end() && levelIt->is_number_integer()) {
                card.level = std::max(1, levelIt->get<int>());
            }
            if (const auto costIt = entry.find("cost"); costIt != entry.end() && costIt->is_number_integer()) {
                card.cost = std::max(0, costIt->get<int>());
            }
            out.classicShopCards.push_back(std::move(card));
        }
    }

    if (const auto it = j.find("board_units"); it != j.end() && it->is_array()) {
        out.boardUnits.reserve(it->size());
        for (const auto& entry : *it) {
            GameWorld::DebugUnitSnapshot unit;
            std::string err;
            if (!decodeUnitSnapshot(entry, false, unit, &err)) {
                fail("invalid board_units entry: " + err);
                return false;
            }
            out.boardUnits.push_back(std::move(unit));
        }
    }

    if (const auto it = j.find("bench_units"); it != j.end() && it->is_array()) {
        out.benchUnits.reserve(it->size());
        for (const auto& entry : *it) {
            GameWorld::DebugUnitSnapshot unit;
            std::string err;
            if (!decodeUnitSnapshot(entry, true, unit, &err)) {
                fail("invalid bench_units entry: " + err);
                return false;
            }
            out.benchUnits.push_back(std::move(unit));
        }
    }

    return true;
}

} // namespace

std::string snapshotPath() {
    if (const auto env = engine::env::get("PAC_DEBUG_STATE_PATH")) {
        return *env;
    }
    return engine::paths::data("config/user/debug_state_snapshot.json");
}

bool autoLoadSnapshotEnabled() {
    if (!engine::env::get("PAC_AUTO_LOAD_DEBUG_SNAPSHOT").has_value()) {
        return false;
    }
    return engine::env::flagEnabled("PAC_AUTO_LOAD_DEBUG_SNAPSHOT");
}

bool hasActiveEnemyUnits(const GameWorld::DebugStateSnapshot& snapshot) {
    for (const auto& unit : snapshot.boardUnits) {
        if (unit.side != PokemonSide::Enemy) continue;
        if (unit.alive || unit.captureInProgress || unit.hp > 0) {
            return true;
        }
    }
    return false;
}

std::string summarizeWorldSnapshot(const GameWorld::DebugStateSnapshot& snapshot) {
    std::size_t playerBoardUnits = 0;
    std::size_t enemyBoardUnits = 0;
    for (const auto& unit : snapshot.boardUnits) {
        if (unit.side == PokemonSide::Enemy) {
            ++enemyBoardUnits;
        } else {
            ++playerBoardUnits;
        }
    }

    std::ostringstream oss;
    oss << "board=" << snapshot.boardUnits.size()
        << " (player=" << playerBoardUnits
        << ", enemy=" << enemyBoardUnits
        << ") bench=" << snapshot.benchUnits.size()
        << " shop=" << snapshot.classicShopCards.size()
        << " items=" << snapshot.items.size()
        << " money=" << snapshot.money
        << " rounds=" << snapshot.classicRoundsCompleted;
    return oss.str();
}

std::string summarizeSessionSnapshot(const SessionSnapshotMetadata& session) {
    std::ostringstream oss;
    oss << "state=" << (session.stateKind.empty() ? "<none>" : session.stateKind);
    if (!session.stateScriptPath.empty()) {
        oss << " script="
            << std::filesystem::path(session.stateScriptPath).filename().string();
    }
    if (session.hasRoundPhase) {
        oss << " round=" << roundPhaseToToken(session.roundPhase);
    }
    if (session.hasCombatActive) {
        oss << " combat=" << (session.combatActive ? 1 : 0);
    }
    return oss.str();
}

std::string formatMillis(double ms) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << ms << "ms";
    return oss.str();
}

bool writeFile(const GameWorld::DebugStateSnapshot& snapshot,
               const std::string& path,
               const SessionSnapshotMetadata* session,
               std::string* outError) {
    std::error_code ec;
    const std::filesystem::path outPath(path);
    const std::filesystem::path dir = outPath.parent_path();
    if (!dir.empty()) {
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            if (outError) *outError = "create_directories failed: " + ec.message();
            return false;
        }
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        if (outError) *outError = "failed to open snapshot file for write";
        return false;
    }

    nlohmann::json root = nlohmann::json::object();
    root["world"] = encodeStateSnapshot(snapshot);
    if (session) {
        nlohmann::json js = nlohmann::json::object();
        if (!session->stateKind.empty()) {
            js["state_kind"] = session->stateKind;
        }
        if (!session->stateScriptPath.empty()) {
            js["state_script_path"] = session->stateScriptPath;
        }
        if (session->hasCombatActive) {
            js["combat_active"] = session->combatActive;
        }
        if (session->hasRoundPhase) {
            js["round_phase"] = roundPhaseToToken(session->roundPhase);
        }
        root["session"] = std::move(js);
    }

    try {
        out << root.dump(2) << "\n";
        return true;
    } catch (const std::exception& e) {
        if (outError) *outError = e.what();
        return false;
    }
}

bool readFile(const std::string& path,
              GameWorld::DebugStateSnapshot& out,
              SessionSnapshotMetadata* outSession,
              std::string* outError) {
    std::ifstream in(path);
    if (!in.is_open()) {
        if (outError) *outError = "failed to open snapshot file";
        return false;
    }

    nlohmann::json j;
    try {
        in >> j;
    } catch (const std::exception& e) {
        if (outError) *outError = std::string("failed to parse snapshot JSON: ") + e.what();
        return false;
    }

    const nlohmann::json* worldNode = &j;
    if (j.is_object()) {
        if (const auto it = j.find("world"); it != j.end() && it->is_object()) {
            worldNode = &(*it);
        }
    }

    if (!decodeStateSnapshot(*worldNode, out, outError)) {
        return false;
    }

    if (outSession) {
        *outSession = SessionSnapshotMetadata{};
        if (j.is_object()) {
            const auto it = j.find("session");
            if (it != j.end() && it->is_object()) {
                if (const auto kindIt = it->find("state_kind"); kindIt != it->end() && kindIt->is_string()) {
                    outSession->stateKind = kindIt->get<std::string>();
                }
                if (const auto scriptIt = it->find("state_script_path"); scriptIt != it->end() && scriptIt->is_string()) {
                    outSession->stateScriptPath = scriptIt->get<std::string>();
                }
                if (const auto combatIt = it->find("combat_active"); combatIt != it->end() && combatIt->is_boolean()) {
                    outSession->hasCombatActive = true;
                    outSession->combatActive = combatIt->get<bool>();
                }
                if (const auto phaseIt = it->find("round_phase"); phaseIt != it->end() && phaseIt->is_string()) {
                    outSession->hasRoundPhase = true;
                    outSession->roundPhase = roundPhaseFromToken(phaseIt->get<std::string>());
                }
            }
        }
    }
    return true;
}

} // namespace game::runtime::session_debug_snapshot
