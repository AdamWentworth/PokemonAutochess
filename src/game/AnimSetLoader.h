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
//
// Landing sequence roles (Pidgey-style; all optional):
//   - "land_a"  (one-shot; transition from flight into descending)
//   - "land_b"  (loop; descending loop; variable length)
//   - "land_c"  (one-shot; finalize landing / touch down)
//
// Back-compat landing role:
//   - "land"    (single one-shot landing)
//
// Optional meta tuning (under a top-level "meta" object):
//   - "movementMode": "airborne"     (enables flight transitions)
//   - "airLiftY": number            (world-space Y offset when airborne; 0 disables code-driven lift)
//   - "takeoffSec": number          (clip-time seconds; if unknown duration)
//   - "landingSec": number          (TOTAL landing sequence clip-time seconds (A+B+C); if 0 defaults to A + one loop of B + C)
//   - "debugAnimLogs": bool         (enable/disable runtime logs; default true for pidgey)
//
// NOTE: airLiftY is purely visual; it does not affect gameplay collision/logic.
void applyAnimSetOverrides(PokemonInstance& inst, const std::string& modelPath);

// Lower-level helper (useful for debugging/tools)
bool loadAnimSetJson(const std::string& animSetPath, nlohmann::json& outJson);

RolePick resolveRoleClip(const nlohmann::json& j,
                         const std::string& roleKey,
                         const std::string& fallbackCategory,
                         const std::vector<std::string>& preferredSubstrings,
                         bool allowFallbackToFirst = true);

} // namespace AnimSet
