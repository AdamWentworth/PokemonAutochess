// AttackAnimConfigLoader.cpp
#include "AttackAnimConfigLoader.h"

#include "game/logging/LoggerUtil.h"
#include "game/logging/DebugTrace.h"
#include "game/config/JsonFile.h"

#include <algorithm>
#include <cctype>
#include <string>

std::string AttackAnimConfigLoader::toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });
    return s;
}

bool AttackAnimConfigLoader::loadConfig(const std::string& filePath,
                                        LogBus::Logger* logger,
                                        const engine::IAssetStore* store) {
    nlohmann::json j;
    if (!ConfigIO::loadJsonFile(filePath, j, "AttackAnimConfigLoader", /*silentIfMissing=*/false, logger, store)) {
        return false;
    }
    return parseJsonIntoDb(j, /*clearFirst=*/true, logger);
}

bool AttackAnimConfigLoader::loadConfigMerge(const std::string& filePath,
                                             LogBus::Logger* logger,
                                             const engine::IAssetStore* store) {
    // Overrides are optional; do not spam logs if missing.
    nlohmann::json j;
    if (!ConfigIO::loadJsonFile(filePath, j, "AttackAnimConfigLoader", /*silentIfMissing=*/true, logger, store)) {
        return false;
    }
    return parseJsonIntoDb(j, /*clearFirst=*/false, logger);
}

bool AttackAnimConfigLoader::parseJsonIntoDb(const nlohmann::json& j, bool clearFirst, LogBus::Logger* logger) {
    if (clearFirst) {
        db_.clear();
        minReqSec_.clear();
        hitFrame_.clear();
    }

    if (!j.is_object()) {
        game::log::error(logger, "[AttackAnimConfigLoader] Root must be an object");
        return false;
    }

    const std::string kMinReq = "_minrequestsec";
    const std::string kHitFrame = "_hitframe";

    for (auto itSpecies = j.begin(); itSpecies != j.end(); ++itSpecies) {
        const std::string species = toLower(itSpecies.key());
        const auto& speciesObj = itSpecies.value();
        if (!speciesObj.is_object()) continue;

        for (auto itKind = speciesObj.begin(); itKind != speciesObj.end(); ++itKind) {
            const std::string kind = toLower(itKind.key());
            const auto& kindObj = itKind.value();
            if (!kindObj.is_object()) continue;

            for (auto itMove = kindObj.begin(); itMove != kindObj.end(); ++itMove) {
                const std::string move = toLower(itMove.key());
                const auto& phasesObj = itMove.value();
                if (!phasesObj.is_object()) continue;

                // Phase clips + optional tuning at the move level.
                for (auto itPhase = phasesObj.begin(); itPhase != phasesObj.end(); ++itPhase) {
                    const std::string phaseKeyLower = toLower(itPhase.key());
                    const auto& phaseVal = itPhase.value();

                    if (phaseKeyLower == kMinReq) {
                        if (phaseVal.is_number()) {
                            minReqSec_[species][kind][move] = phaseVal.get<float>();
                        }
                        continue;
                    }
                    if (phaseKeyLower == kHitFrame) {
                        if (phaseVal.is_number_integer()) {
                            hitFrame_[species][kind][move] = phaseVal.get<int>();
                        }
                        continue;
                    }

                    if (!phaseVal.is_string()) continue;

                    db_[species][kind][move][phaseKeyLower] = phaseVal.get<std::string>();

                    if (DebugTrace::combat(species, move)) {
                        game::log::infoTerminalOnly(logger, std::string("[AttackAnimDB] ") + species + ":" + kind + ":" + move +
                            " -> " + phaseKeyLower + " = " + db_[species][kind][move][phaseKeyLower]);
                    }
                }
            }
        }
    }

    return true;
}

std::string AttackAnimConfigLoader::getClipName(const std::string& species,
                                               const std::string& kind,
                                               const std::string& move,
                                               const std::string& phase,
                                               LogBus::Logger* logger) const
{
    const std::string s = toLower(species);
    const std::string k = toLower(kind);
    const std::string m = toLower(move);
    const std::string p = toLower(phase);

    const bool traceCombat = DebugTrace::combat(s, m);
    auto trlog = [&](const std::string& msg){
        if (traceCombat) game::log::infoTerminalOnly(logger, std::string("[TRACE_ANIMCFG] ") + msg);
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

        itP = pm.find("default");
        if (itP != pm.end()) return itP->second;

        return "";
    };

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
                                              const std::string& move,
                                              LogBus::Logger* logger) const
{
    const std::string s = toLower(species);
    const std::string k = toLower(kind);
    const std::string m = toLower(move);

    const bool traceCombat = DebugTrace::combat(s, m);
    auto trlog = [&](const std::string& msg){
        if (traceCombat) game::log::infoTerminalOnly(logger, std::string("[TRACE_ANIMCFG] ") + msg);
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
