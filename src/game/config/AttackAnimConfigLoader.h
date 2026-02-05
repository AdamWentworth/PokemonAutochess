// AttackAnimConfigLoader.h
#pragma once

#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

// Loads per-species move -> animation clip mappings, plus optional per-move tuning.
// Intended usage: gameplay asks for clip name (species, kind, move, phase) and optional
// minimum request/cooldown seconds for that move.
//
// Base clip mapping JSON format (example):
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
//
// Optional tuning (can live in the same file OR in a smaller overrides file loaded via loadConfigMerge()):
// {
//   "bulbasaur": {
//     "fast": {
//       "vine_whip": { "_minRequestSec": 1.7 }
//     }
//   }
// }
//
// Rules:
//  - keys are case-insensitive (stored lowercase)
//  - move can be "*" wildcard
//  - getMinRequestSec uses exact move then "*" wildcard, matching getClipName
class AttackAnimConfigLoader {
public:
    static AttackAnimConfigLoader& getInstance();

    // Replaces current database on success.
    bool loadConfig(const std::string& filePath);

    // Merges/overrides entries on success (does NOT clear the existing database).
    // Useful for small "only-this-species" override files.
    bool loadConfigMerge(const std::string& filePath);

    // Returns empty string if not found.
    // kind: "fast" or "charged" (lower/upper accepted)
    // move: move name (lower/upper accepted). Use "" when you don't have one.
    // phase: "one_shot" (charged), or "start"/"loop"/"end"/"default" (fast).
    std::string getClipName(const std::string& species,
                            const std::string& kind,
                            const std::string& move,
                            const std::string& phase) const;

    // Optional tuning: minimum seconds to wait between requests for this (species, kind, move).
    // Returns 0.0f if no override is present.
    float getMinRequestSec(const std::string& species,
                           const std::string& kind,
                           const std::string& move) const;


    // Optional tuning: which animation frame should apply damage for this (species, kind, move).
    // Returns -1 if no override is present.
    int getHitFrame(const std::string& species,
                    const std::string& kind,
                    const std::string& move) const;

public:
    AttackAnimConfigLoader() = default;

private:
    static std::string toLower(std::string s);

    bool parseJsonIntoDb(const nlohmann::json& j, bool clearFirst);

    // species -> kind -> move -> phase -> clipName
    using PhaseMap = std::unordered_map<std::string, std::string>;
    using MoveMap  = std::unordered_map<std::string, PhaseMap>;
    using KindMap  = std::unordered_map<std::string, MoveMap>;

    // species -> kind -> move -> minRequestSec
    using MoveFloatMap = std::unordered_map<std::string, float>;
    using KindFloatMap = std::unordered_map<std::string, MoveFloatMap>;

    std::unordered_map<std::string, KindMap> db_;
    std::unordered_map<std::string, KindFloatMap> minReqSec_;


    // species -> kind -> move -> hitFrame
    using MoveIntMap  = std::unordered_map<std::string, int>;
    using KindIntMap  = std::unordered_map<std::string, MoveIntMap>;
    std::unordered_map<std::string, KindIntMap> hitFrame_;
};
