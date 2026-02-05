// AttackAnimConfigLoader.cpp
#include "AttackAnimConfigLoader.h"

#include "game/logging/LogBus.h"
#include "game/logging/DebugTrace.h"  // env-driven trace rules (PAC_TRACE_ALL / PAC_TRACE_COMBAT)

#include <fstream>
#include <iostream>
#include <algorithm>
#include <cctype>

AttackAnimConfigLoader& AttackAnimConfigLoader::getInstance() {
    static AttackAnimConfigLoader inst;
    return inst;
}

std::string AttackAnimConfigLoader::toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });
    return s;
}

bool AttackAnimConfigLoader::loadConfig(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        std::cerr << "[AttackAnimConfigLoader] Failed to open: " << filePath << "\n";
        return false;
    }

    nlohmann::json j;
    try {
        file >> j;
    } catch (...) {
        std::cerr << "[AttackAnimConfigLoader] Failed to parse JSON: " << filePath << "\n";
        return false;
    }

    return parseJsonIntoDb(j, /*clearFirst=*/true);
}

bool AttackAnimConfigLoader::loadConfigMerge(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        // Overrides are optional; don't spam logs.
        return false;
    }

    nlohmann::json j;
    try {
        file >> j;
    } catch (...) {
        std::cerr << "[AttackAnimConfigLoader] Failed to parse JSON: " << filePath << "\n";
        return false;
    }

    return parseJsonIntoDb(j, /*clearFirst=*/false);
}

bool AttackAnimConfigLoader::parseJsonIntoDb(const nlohmann::json& j, bool clearFirst) {
    if (clearFirst) {
        db_.clear();
        minReqSec_.clear();
        hitFrame_.clear();
    }

    if (!j.is_object()) {
        std::cerr << "[AttackAnimConfigLoader] Root must be an object\n";
        return false;
    }

    // Supported per-move tuning keys (lowercase).
    // Kept underscore-prefixed to avoid collisions with phases like "start"/"loop"/"end".
    const std::string kMinReq = "_minrequestsec";
    const std::string kHitFrame = "_hitframe";

    for (auto itSpecies = j.begin(); itSpecies != j.end(); ++itSpecies) {
        const std::string species = toLower(itSpecies.key());
        const auto& speciesObj = itSpecies.value();
        if (!speciesObj.is_object()) continue;

        for (auto itKind = speciesObj.begin(); itKind != speciesObj.end(); ++itKind) {
            const std::string kind = toLower(itKind.key()); // fast/charged
            const auto& kindObj = itKind.value();
            if (!kindObj.is_object()) continue;

            for (auto itMove = kindObj.begin(); itMove != kindObj.end(); ++itMove) {
                const std::string move = toLower(itMove.key()); // move name or "*"
                const auto& moveObj = itMove.value();
                if (!moveObj.is_object()) continue;

                PhaseMap phaseMap;

                // Parse both phase->clip entries and optional underscore-prefixed tuning keys.
                for (auto itEntry = moveObj.begin(); itEntry != moveObj.end(); ++itEntry) {
                    const std::string keyLower = toLower(itEntry.key());
                    const auto& v = itEntry.value();

                    if (v.is_string()) {
                        phaseMap[keyLower] = v.get<std::string>();
                        continue;
                    }

                    if (keyLower == kMinReq && v.is_number()) {
                        const float vv = v.get<float>();
                        if (vv > 0.0f) {
                            minReqSec_[species][kind][move] = vv;
                        }
                        continue;
                    }

                    if (keyLower == kHitFrame && (v.is_number_integer() || v.is_number_float())) {
                        const int hf = v.get<int>();
                        if (hf > 0) {
                            hitFrame_[species][kind][move] = hf;
                        }
                        continue;
                    }
                }

                if (!phaseMap.empty()) {
                    db_[species][kind][move] = std::move(phaseMap);
                }
            }
        }
    }

    std::cout << "[AttackAnimConfigLoader] Loaded species: " << db_.size() << "\n";
    return true;
}

std::string AttackAnimConfigLoader::getClipName(const std::string& species,
                                                const std::string& kind,
                                                const std::string& move,
                                                const std::string& phase) const
{
    const std::string s = toLower(species);
    const std::string k = toLower(kind);
    const std::string m = toLower(move);
    const std::string p = toLower(phase);

    // Env-driven trace gating (matches DebugTrace.h token rules).
    const bool traceCombat = DebugTrace::combat(s, m);
    auto trlog = [&](const std::string& msg){
        if (traceCombat) LogBus::infoTerminalOnly(std::string("[TRACE_ANIMCFG] ") + msg);
    };

    auto itS = db_.find(s);
    if (itS == db_.end()) return "";

    auto itK = itS->second.find(k);
    if (itK == itS->second.end()) return "";

    const MoveMap& mm = itK->second;

    auto lookup = [&](const std::string& moveKey, const std::string& phaseKey) -> std::string {
        auto itM = mm.find(moveKey);
        if (itM == mm.end()) return "";
        const PhaseMap& pm = itM->second;

        auto itP = pm.find(phaseKey);
        if (itP != pm.end()) return itP->second;

        // fallback to "default" phase if present
        itP = pm.find("default");
        if (itP != pm.end()) return itP->second;

        return "";
    };

    // Prefer exact move, then "*" wildcard.
    if (!m.empty()) {
        std::string out = lookup(m, p);
        if (traceCombat) {
            trlog(std::string("getClipName species=") + s +
                  " kind=" + k +
                  " move=" + m +
                  " phase=" + p +
                  " -> exact=" + (out.empty() ? std::string("<empty>") : out));
        }
        if (!out.empty()) return out;
    }

    std::string out = lookup("*", p);
    if (traceCombat) {
        trlog(std::string("getClipName species=") + s +
              " kind=" + k +
              " move=" + m +
              " phase=" + p +
              " -> wildcard=" + (out.empty() ? std::string("<empty>") : out));
    }
    return out;
}

float AttackAnimConfigLoader::getMinRequestSec(const std::string& species,
                                               const std::string& kind,
                                               const std::string& move) const
{
    const std::string s = toLower(species);
    const std::string k = toLower(kind);
    const std::string m = toLower(move);

    // Env-driven trace gating (matches DebugTrace.h token rules).
    const bool traceCombat = DebugTrace::combat(s, m);
    auto trlog = [&](const std::string& msg){
        if (traceCombat) LogBus::infoTerminalOnly(std::string("[TRACE_ANIMCFG] ") + msg);
    };

    auto itS = minReqSec_.find(s);
    if (itS == minReqSec_.end()) return 0.0f;

    auto itK = itS->second.find(k);
    if (itK == itS->second.end()) return 0.0f;

    const MoveFloatMap& mm = itK->second;

    auto lookup = [&](const std::string& moveKey) -> float {
        auto itM = mm.find(moveKey);
        if (itM == mm.end()) return 0.0f;
        return itM->second;
    };

    if (!m.empty()) {
        float v = lookup(m);
        if (v > 0.0f) return v;
    }

    const float out = lookup("*");
    if (traceCombat) {
        trlog(std::string("getMinRequestSec species=") + s +
              " kind=" + k +
              " move=" + m +
              " -> " + std::to_string(out));
    }
    return out;
}

int AttackAnimConfigLoader::getHitFrame(const std::string& species,
                                       const std::string& kind,
                                       const std::string& move) const
{
    const std::string s = toLower(species);
    const std::string k = toLower(kind);
    const std::string m = toLower(move);

    auto itS = hitFrame_.find(s);
    if (itS == hitFrame_.end()) return -1;

    auto itK = itS->second.find(k);
    if (itK == itS->second.end()) return -1;

    const MoveIntMap& mm = itK->second;

    auto lookup = [&](const std::string& moveKey) -> int {
        auto itM = mm.find(moveKey);
        if (itM == mm.end()) return -1;
        return itM->second;
    };

    if (!m.empty()) {
        int v = lookup(m);
        if (v > 0) return v;
    }

    return lookup("*");
}
