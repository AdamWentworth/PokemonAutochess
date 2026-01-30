// AttackAnimConfigLoader.h
#pragma once

#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

// Loads per-species move -> animation clip mappings.
// Intended usage: gameplay asks for clip name for (species, kind, move, phase).
//
// JSON format (example):
// {
//   "bulbasaur": {
//     "charged": {
//       "*": { "one_shot": "pm0001_00_00_00450_rangeattack01.gfbanm" }
//     },
//     "fast": {
//       "vine_whip": {
//         "start": "pm0001_00_00_00460_rangeattack02_start.gfbanm",
//         "loop":  "pm0001_00_00_00461_rangeattack02_loop.gfbanm",
//         "end":   "pm0001_00_00_00462_rangeattack02_end.gfbanm"
//       }
//     }
//   }
// }
class AttackAnimConfigLoader {
public:
    static AttackAnimConfigLoader& getInstance();

    bool loadConfig(const std::string& filePath);

    // Returns empty string if not found.
    // kind: "fast" or "charged" (lower/upper accepted)
    // move: move name (lower/upper accepted). Use "" when you don't have one.
    // phase: "one_shot" (charged), or "start"/"loop"/"end"/"default" (fast).
    std::string getClipName(const std::string& species,
                            const std::string& kind,
                            const std::string& move,
                            const std::string& phase) const;

private:
    AttackAnimConfigLoader() = default;

    static std::string toLower(std::string s);

    // species -> kind -> move -> phase -> clipName
    using PhaseMap = std::unordered_map<std::string, std::string>;
    using MoveMap  = std::unordered_map<std::string, PhaseMap>;
    using KindMap  = std::unordered_map<std::string, MoveMap>;

    std::unordered_map<std::string, KindMap> db_;
};


