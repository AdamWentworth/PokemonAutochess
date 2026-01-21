// src/game/AnimSetLoader.h
#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

class Model;
struct PokemonInstance;

namespace AnimSet {

struct RolePick {
    std::string clipName;       // glTF animation name to find
    float durationSec = 0.0f;   // optional; 0 if unknown
    bool isLoop = false;
    bool valid = false;
};

// Same folder as model, same stem, extension replaced with ".animset.json"
std::string animSetPathFromModelPath(const std::string& modelPath);

// Resolve a model animation index by name with fallbacks (e.g. stripping ".gfbanm")
int resolveAnimIndex(Model* model, const std::string& name);

// Main entry: reads the .animset.json and applies idle/move/attack1 indices to the instance.
// Safe to call even if animset is missing or malformed (it will keep defaults).
void applyAnimSetOverrides(PokemonInstance& inst, const std::string& modelPath);

// Lower-level helper (useful for debugging/tools)
bool loadAnimSetJson(const std::string& animSetPath, nlohmann::json& outJson);
RolePick resolveRoleClip(const nlohmann::json& j,
                         const std::string& roleKey,
                         const std::string& fallbackCategory,
                         const std::vector<std::string>& preferredSubstrings);

} // namespace AnimSet
