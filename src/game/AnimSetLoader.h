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

// Main entry: reads the .animset.json and applies indices to the instance.
// Safe to call even if animset is missing or malformed (it will keep defaults).
//
// Supported role keys (all optional):
//   - "idle"    (ground/battle idle)
//   - "move"    (movement locomotion)
//   - "attack1" (one-shot attack)
//
// Optional flight roles (visual-only; used by small fliers like Pidgey):
//   - "ground_idle" (idle on the ground; falls back to "idle")
//   - "air_idle"    (idle/hover in the air; falls back to "idle")
//   - "takeoff"     (one-shot; take flight)
//   - "land"        (one-shot; landing)
//
// Optional meta tuning (under a top-level "meta" object):
//   - "movementMode": "airborne"  (enables flight transitions)
//   - "flightHeight": number      (render-time Y offset when airborne)
//   - "takeoffSec": number        (fallback takeoff time if clip duration is unknown)
//   - "landingSec": number        (fallback landing time if clip duration is unknown)

void applyAnimSetOverrides(PokemonInstance& inst, const std::string& modelPath);

// Lower-level helper (useful for debugging/tools)
bool loadAnimSetJson(const std::string& animSetPath, nlohmann::json& outJson);
RolePick resolveRoleClip(const nlohmann::json& j,
                         const std::string& roleKey,
                         const std::string& fallbackCategory,
                         const std::vector<std::string>& preferredSubstrings,
                         bool allowFallbackToFirst = true);

} // namespace AnimSet


