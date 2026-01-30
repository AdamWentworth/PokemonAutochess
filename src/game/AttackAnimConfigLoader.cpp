// AttackAnimConfigLoader.cpp
#include "AttackAnimConfigLoader.h"

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

    db_.clear();

    if (!j.is_object()) {
        std::cerr << "[AttackAnimConfigLoader] Root must be an object: " << filePath << "\n";
        return false;
    }

    for (auto itSpecies = j.begin(); itSpecies != j.end(); ++itSpecies) {
        const std::string species = toLower(itSpecies.key());
        const auto& speciesObj = itSpecies.value();
        if (!speciesObj.is_object()) continue;

        KindMap kindMap;

        for (auto itKind = speciesObj.begin(); itKind != speciesObj.end(); ++itKind) {
            const std::string kind = toLower(itKind.key()); // fast/charged
            const auto& kindObj = itKind.value();
            if (!kindObj.is_object()) continue;

            MoveMap moveMap;

            for (auto itMove = kindObj.begin(); itMove != kindObj.end(); ++itMove) {
                const std::string move = toLower(itMove.key()); // move name or "*"
                const auto& moveObj = itMove.value();
                if (!moveObj.is_object()) continue;

                PhaseMap phaseMap;
                for (auto itPhase = moveObj.begin(); itPhase != moveObj.end(); ++itPhase) {
                    const std::string phase = toLower(itPhase.key());
                    const auto& v = itPhase.value();
                    if (v.is_string()) {
                        phaseMap[phase] = v.get<std::string>();
                    }
                }

                if (!phaseMap.empty()) moveMap[move] = std::move(phaseMap);
            }

            if (!moveMap.empty()) kindMap[kind] = std::move(moveMap);
        }

        if (!kindMap.empty()) db_[species] = std::move(kindMap);
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
        if (!out.empty()) return out;
    }

    std::string out = lookup("*", p);
    return out;
}


