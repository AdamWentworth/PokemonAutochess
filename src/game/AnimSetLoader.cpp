// src/game/AnimSetLoader.cpp
#include "AnimSetLoader.h"

#include <fstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <cctype>

// Keep includes compatible with your project include layout:
#include "engine/render/Model.h"
#include "PokemonInstance.h"
#include "FlyerConfigLoader.h"

namespace AnimSet {

static std::string stripSuffix(const std::string& s, const std::string& suffix)
{
    if (s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix) {
        return s.substr(0, s.size() - suffix.size());
    }
    return s;
}

static std::string toLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });
    return s;
}

std::string animSetPathFromModelPath(const std::string& modelPath)
{
    std::filesystem::path p(modelPath);
    p.replace_extension(".animset.json");
    return p.string();
}

bool loadAnimSetJson(const std::string& animSetPath, nlohmann::json& outJson)
{
    std::ifstream f(animSetPath);
    if (!f) return false;

    try {
        f >> outJson;
    } catch (...) {
        std::cerr << "[AnimSet] Failed to parse JSON: " << animSetPath << "\n";
        return false;
    }
    return true;
}

int resolveAnimIndex(Model* model, const std::string& name)
{
    if (!model) return -1;

    int idx = model->findAnimationIndexByName(name);
    if (idx >= 0) return idx;

    idx = model->findAnimationIndexByName(stripSuffix(name, ".gfbanm"));
    if (idx >= 0) return idx;

    idx = model->findAnimationIndexByName(stripSuffix(name, "__START"));
    if (idx >= 0) return idx;

    idx = model->findAnimationIndexByName(stripSuffix(name, "__END"));
    if (idx >= 0) return idx;

    std::string tmp = stripSuffix(name, ".gfbanm");
    tmp = stripSuffix(tmp, "__START");
    tmp = stripSuffix(tmp, "__END");
    if (tmp != name) {
        idx = model->findAnimationIndexByName(tmp);
        if (idx >= 0) return idx;
    }

    return -1;
}

// (rest unchanged; see README for patch focus)

static std::string readRoleNameString(const nlohmann::json& j, const std::string& key)
{
    if (j.contains("roles") && j["roles"].is_object()) {
        const auto& r = j["roles"];
        if (r.contains(key) && r[key].is_string()) return r[key].get<std::string>();
        if (r.contains(key) && r[key].is_object()) {
            const auto& obj = r[key];
            if (obj.contains("clip") && obj["clip"].is_string()) return obj["clip"].get<std::string>();
            if (obj.contains("name") && obj["name"].is_string()) return obj["name"].get<std::string>();
        }
        return "";
    }

    if (j.contains(key) && j[key].is_string()) return j[key].get<std::string>();
    if (j.contains(key) && j[key].is_object()) {
        const auto& obj = j[key];
        if (obj.contains("clip") && obj["clip"].is_string()) return obj["clip"].get<std::string>();
        if (obj.contains("name") && obj["name"].is_string()) return obj["name"].get<std::string>();
    }

    return "";
}

static bool findClipByName(const nlohmann::json& j,
                           const std::string& wantedName,
                           nlohmann::json& outClip)
{
    if (!j.contains("clips") || !j["clips"].is_array()) return false;

    const std::string wantedLower = toLower(wantedName);

    for (const auto& c : j["clips"]) {
        if (!c.is_object()) continue;

        std::string name = c.value("gltf_name", "");
        if (name.empty()) name = c.value("export_name", "");
        if (name.empty()) continue;

        if (name == wantedName) { outClip = c; return true; }

        const std::string candLower = toLower(name);

        if (candLower == wantedLower) { outClip = c; return true; }

        if (stripSuffix(candLower, ".gfbanm") == stripSuffix(wantedLower, ".gfbanm")) {
            outClip = c;
            return true;
        }
    }

    return false;
}

static float clipDurationSeconds(const nlohmann::json& clip)
{
    if (clip.contains("duration_seconds") && clip["duration_seconds"].is_number()) {
        return clip["duration_seconds"].get<float>();
    }
    if (clip.contains("duration") && clip["duration"].is_number()) {
        return clip["duration"].get<float>();
    }
    return 0.0f;
}

static bool clipIsLoop(const nlohmann::json& clip)
{
    if (clip.contains("is_loop") && clip["is_loop"].is_boolean()) return clip["is_loop"].get<bool>();
    if (clip.contains("loop") && clip["loop"].is_boolean()) return clip["loop"].get<bool>();
    return false;
}

static std::string resolveRefToName(const nlohmann::json& j, const nlohmann::json& refVal)
{
    if (refVal.is_string()) return refVal.get<std::string>();

    if (refVal.is_number()) {
        const int id = refVal.is_number_integer() ? refVal.get<int>() : (int)refVal.get<double>();
        if (j.contains("by_id") && j["by_id"].is_object()) {
            const auto& by = j["by_id"];
            const std::string key = std::to_string(id);
            if (by.contains(key) && by[key].is_string()) return by[key].get<std::string>();
        }
    }

    return "";
}

static RolePick pickFromClipsByCategory(const nlohmann::json& j,
                                        const std::string& category,
                                        const std::vector<std::string>& preferredSubstrings,
                                        bool allowFallbackToFirst)
{
    RolePick out;

    if (!j.contains("clips") || !j["clips"].is_array()) return out;

    std::string firstName;
    float firstDur = 0.0f;
    bool firstLoop = false;

    for (const auto& clip : j["clips"]) {
        if (!clip.is_object()) continue;

        const std::string cat = clip.value("category", "");
        if (cat != category) continue;

        std::string name = clip.value("gltf_name", "");
        if (name.empty()) name = clip.value("export_name", "");
        if (name.empty()) continue;

        const float dur = clipDurationSeconds(clip);
        const bool loop = clipIsLoop(clip);

        if (firstName.empty()) {
            firstName = name;
            firstDur = dur;
            firstLoop = loop;
        }

        for (const auto& sub : preferredSubstrings) {
            if (!sub.empty() && toLower(name).find(toLower(sub)) != std::string::npos) {
                out.clipName = name;
                out.durationSec = dur;
                out.isLoop = loop;
                out.valid = true;
                return out;
            }
        }
    }

    if (allowFallbackToFirst && !firstName.empty()) {
        out.clipName = firstName;
        out.durationSec = firstDur;
        out.isLoop = firstLoop;
        out.valid = true;
    }

    return out;
}

RolePick resolveRoleClip(const nlohmann::json& j,
                         const std::string& roleKey,
                         const std::string& fallbackCategory,
                         const std::vector<std::string>& preferredSubstrings,
                         bool allowFallbackToFirst)
{
    RolePick out;

    const std::string explicitName = readRoleNameString(j, roleKey);
    if (!explicitName.empty()) {
        out.clipName = explicitName;
        nlohmann::json clip;
        if (findClipByName(j, explicitName, clip)) {
            out.durationSec = clipDurationSeconds(clip);
            out.isLoop = clipIsLoop(clip);
        }
        out.valid = true;
        return out;
    }

    out = pickFromClipsByCategory(j, fallbackCategory, preferredSubstrings, allowFallbackToFirst);
    if (out.valid) return out;

    if (j.contains("categories") && j["categories"].is_object()) {
        const auto& cats = j["categories"];
        if (cats.contains(fallbackCategory) && cats[fallbackCategory].is_array()) {
            for (const auto& refVal : cats[fallbackCategory]) {
                const std::string name = resolveRefToName(j, refVal);
                if (name.empty()) continue;

                out.clipName = name;
                nlohmann::json clip;
                if (findClipByName(j, name, clip)) {
                    out.durationSec = clipDurationSeconds(clip);
                    out.isLoop = clipIsLoop(clip);
                }
                out.valid = true;
                return out;
            }
        }
    }

    return out;
}

static void debugPrintResolved(const PokemonInstance& inst,
                               const std::string& role,
                               const std::string& clip,
                               int idx,
                               float dur)
{
    if (!inst.debugAnimLogs) return;
    std::cout << "  " << role << ": " << "'" << clip << "'" << " idx=" << idx << " dur=" << dur << "s\n";
}

void applyAnimSetOverrides(PokemonInstance& inst, const std::string& modelPath)
{
    const int fallbackLoop = (inst.model && inst.model->getAnimationCount() > 0) ? 0 : -1;

    inst.animIdleIndex     = fallbackLoop;
    inst.animMoveIndex     = fallbackLoop;
    inst.animAttack1Index  = fallbackLoop;
    inst.activeAnimIndex   = fallbackLoop;
    inst.attackDurationSec = 0.0f;

    inst.animFps          = 24.0f;

    inst.usesAirLocomotion = false;
    inst.animGroundIdleIndex = inst.animIdleIndex;
    inst.animAirIdleIndex    = inst.animIdleIndex;
    inst.animTakeoffIndex    = -1;

    inst.animLandIndex       = -1;
    inst.animLandAIndex      = -1;
    inst.animLandBIndex      = -1;
    inst.animLandCIndex      = -1;

    inst.airState            = AirLocomotionState::Grounded;
    inst.airStateTimeSec     = 0.0f;
    inst.visualYOffset       = 0.0f;
    inst.airLiftY            = 0.0f;

    inst.wasMovingLastFrame  = inst.isMoving;
    inst.pendingAttackAfterLanding = false;
    inst.queuedAttackDurationSec   = 0.0f;
    inst.landingLoopTargetSec = 0.0f;

    inst.debugAnimLogs = false;

    if (!inst.model) return;

    const std::string animSetPath = animSetPathFromModelPath(modelPath);

    nlohmann::json j;
    if (!loadAnimSetJson(animSetPath, j)) {
        return;
    }

    if (j.contains("fps") && j["fps"].is_number()) {
        inst.animFps = j["fps"].get<float>();
        if (inst.animFps <= 0.0f) inst.animFps = 24.0f;
    }

    bool metaAirborne = false;
    bool metaAirLiftSpecified = false;
    bool metaDebugSpecified = false;
    if (j.contains("meta") && j["meta"].is_object()) {
        const auto& meta = j["meta"];

        if (meta.contains("movementMode") && meta["movementMode"].is_string()) {
            const std::string mm = toLower(meta["movementMode"].get<std::string>());
            if (mm == "airborne" || mm == "air" || mm == "flying" || mm == "fly") {
                metaAirborne = true;
            }
        }

        if (meta.contains("airLiftY") && meta["airLiftY"].is_number()) {
            inst.airLiftY = meta["airLiftY"].get<float>();
            metaAirLiftSpecified = true;
        }

        if (meta.contains("takeoffSec") && meta["takeoffSec"].is_number()) {
            inst.takeoffSec = meta["takeoffSec"].get<float>();
        }
        if (meta.contains("landingSec") && meta["landingSec"].is_number()) {
            inst.landingSec = meta["landingSec"].get<float>();
        }

        if (meta.contains("debugAnimLogs") && meta["debugAnimLogs"].is_boolean()) {
            inst.debugAnimLogs = meta["debugAnimLogs"].get<bool>();
            metaDebugSpecified = true;
        }
    }
    const RolePick idlePick = resolveRoleClip(j, "idle", "idle", {"battlewait", "defaultwait", "idle", "wait"}, true);
    const RolePick movePick = resolveRoleClip(j, "move", "move", {"run", "dash", "move"}, true);

    RolePick atkPick  = resolveRoleClip(j, "attack1", "attack", {"attack01", "attack1", "attack"}, true);
    if (!atkPick.valid || atkPick.clipName.empty()) {
        atkPick = resolveRoleClip(j, "attack1", "misc", {"buturi", "ba20_buturi", "ba20"}, false);
    }

    const RolePick groundIdlePick = resolveRoleClip(j, "ground_idle", "idle", {"ba10_wait", "battlewait", "ba10", "wait", "idle"}, true);
    const RolePick airIdlePick    = resolveRoleClip(j, "air_idle",    "idle", {"fi01_wait", "fly", "air", "hover"}, true);
    const RolePick takeoffPick    = resolveRoleClip(j, "takeoff",     "misc", {"take_flight", "takeflight", "takeoff"}, false);

    const RolePick landAPick      = resolveRoleClip(j, "land_a", "misc", {"landa"}, false);
    const RolePick landBPick      = resolveRoleClip(j, "land_b", "misc", {"landb"}, false);
    const RolePick landCPick      = resolveRoleClip(j, "land_c", "misc", {"landc"}, false);

    const RolePick landPick       = resolveRoleClip(j, "land",   "misc", {"land"}, false);

    if (idlePick.valid && !idlePick.clipName.empty()) {
        const int idx = resolveAnimIndex(inst.model.get(), idlePick.clipName);
        if (idx >= 0) inst.animIdleIndex = idx;
    }

    if (groundIdlePick.valid && !groundIdlePick.clipName.empty()) {
        const int idx = resolveAnimIndex(inst.model.get(), groundIdlePick.clipName);
        if (idx >= 0) inst.animGroundIdleIndex = idx;
    } else {
        inst.animGroundIdleIndex = inst.animIdleIndex;
    }

    if (airIdlePick.valid && !airIdlePick.clipName.empty()) {
        const int idx = resolveAnimIndex(inst.model.get(), airIdlePick.clipName);
        if (idx >= 0) inst.animAirIdleIndex = idx;
    } else {
        inst.animAirIdleIndex = inst.animIdleIndex;
    }

    if (takeoffPick.valid && !takeoffPick.clipName.empty()) {
        inst.animTakeoffIndex = resolveAnimIndex(inst.model.get(), takeoffPick.clipName);
    }

    if (landAPick.valid && !landAPick.clipName.empty()) {
        inst.animLandAIndex = resolveAnimIndex(inst.model.get(), landAPick.clipName);
    }
    if (landBPick.valid && !landBPick.clipName.empty()) {
        inst.animLandBIndex = resolveAnimIndex(inst.model.get(), landBPick.clipName);
    }
    if (landCPick.valid && !landCPick.clipName.empty()) {
        inst.animLandCIndex = resolveAnimIndex(inst.model.get(), landCPick.clipName);
    }

    if (landPick.valid && !landPick.clipName.empty()) {
        inst.animLandIndex = resolveAnimIndex(inst.model.get(), landPick.clipName);
    }

    if (movePick.valid && !movePick.clipName.empty()) {
        const int idx = resolveAnimIndex(inst.model.get(), movePick.clipName);
        if (idx >= 0) inst.animMoveIndex = idx;
    }

    if (atkPick.valid && !atkPick.clipName.empty()) {
        const int idx = resolveAnimIndex(inst.model.get(), atkPick.clipName);
        if (idx >= 0) {
            inst.animAttack1Index = idx;
            inst.attackDurationSec = atkPick.durationSec;
            if (inst.attackDurationSec <= 0.0f) {
                inst.attackDurationSec = inst.model->getAnimationDurationSec(idx);
            }
        }
    }

    const bool hasTakeoff = (inst.animTakeoffIndex >= 0);
    const bool hasSeqLanding = (inst.animLandCIndex >= 0) && (inst.animLandAIndex >= 0 || inst.animLandBIndex >= 0);
    const bool hasSingleLanding = (inst.animLandIndex >= 0);

    const bool hasDistinctLand = hasSeqLanding ||
        (hasTakeoff && hasSingleLanding && inst.animTakeoffIndex != inst.animLandIndex);

    // Enable visual-only flight when:
    //  - animset meta explicitly declares airborne, OR
    //  - this species is listed as a flyer, OR
    //  - the animset provides takeoff + a distinct landing sequence.
    const bool allowFlight =
        metaAirborne ||
        FlyerConfigLoader::getInstance().isFlyer(inst.name) ||
        (hasTakeoff && hasDistinctLand);
    if (allowFlight) {
        inst.usesAirLocomotion = true;
    } else {
        inst.usesAirLocomotion = false;
        inst.animTakeoffIndex = -1;
        inst.animLandIndex = -1;
        inst.animLandAIndex = -1;
        inst.animLandBIndex = -1;
        inst.animLandCIndex = -1;
        inst.airLiftY = 0.0f;
        inst.takeoffSec = 0.0f;
        inst.landingSec = 0.0f;
    }

    // Optional species defaults (config-driven) for air locomotion tuning.
    // Animset meta remains the highest priority for the same fields.
    if (inst.usesAirLocomotion) {
        if (const auto* d = FlyerConfigLoader::getInstance().getAirLocomotionDefaults(inst.name)) {
            if (!metaAirLiftSpecified && d->airLiftY.has_value()) {
                inst.airLiftY = *d->airLiftY;
            }
            if (inst.takeoffSec <= 0.0f && d->takeoffSec.has_value()) {
                inst.takeoffSec = *d->takeoffSec;
            }
            if (inst.landingSec <= 0.0f && d->landingSec.has_value()) {
                inst.landingSec = *d->landingSec;
            }
            if (d->takeoffAnimSpeed.has_value()) {
                inst.takeoffAnimSpeed = *d->takeoffAnimSpeed;
            }
            if (d->landAnimSpeed.has_value()) {
                inst.landAnimSpeed = *d->landAnimSpeed;
            }
            if (!metaDebugSpecified && d->debugAnimLogs.has_value()) {
                inst.debugAnimLogs = *d->debugAnimLogs;
            }
        }
    }


    inst.activeAnimIndex = inst.usesAirLocomotion ? inst.animGroundIdleIndex : inst.animIdleIndex;

    if (inst.debugAnimLogs) {
        std::cout << "[AnimDebug] " << inst.name << " (ID " << inst.id << ") animset resolved:\n";
        debugPrintResolved(inst, "idle",        idlePick.clipName, inst.animIdleIndex, inst.model->getAnimationDurationSec(inst.animIdleIndex));
        debugPrintResolved(inst, "move",        movePick.clipName, inst.animMoveIndex, inst.model->getAnimationDurationSec(inst.animMoveIndex));
        debugPrintResolved(inst, "attack1",     atkPick.clipName,  inst.animAttack1Index, inst.attackDurationSec > 0.0f ? inst.attackDurationSec : inst.model->getAnimationDurationSec(inst.animAttack1Index));
        debugPrintResolved(inst, "ground_idle", groundIdlePick.clipName, inst.animGroundIdleIndex, inst.model->getAnimationDurationSec(inst.animGroundIdleIndex));
        debugPrintResolved(inst, "air_idle",    airIdlePick.clipName,    inst.animAirIdleIndex, inst.model->getAnimationDurationSec(inst.animAirIdleIndex));
        debugPrintResolved(inst, "takeoff",     takeoffPick.clipName,    inst.animTakeoffIndex, inst.model->getAnimationDurationSec(inst.animTakeoffIndex));
        debugPrintResolved(inst, "land_a",      landAPick.clipName,      inst.animLandAIndex, inst.model->getAnimationDurationSec(inst.animLandAIndex));
        debugPrintResolved(inst, "land_b",      landBPick.clipName,      inst.animLandBIndex, inst.model->getAnimationDurationSec(inst.animLandBIndex));
        debugPrintResolved(inst, "land_c",      landCPick.clipName,      inst.animLandCIndex, inst.model->getAnimationDurationSec(inst.animLandCIndex));
        debugPrintResolved(inst, "land",        landPick.clipName,       inst.animLandIndex, inst.model->getAnimationDurationSec(inst.animLandIndex));

        std::cout << "  usesAirLocomotion=" << (inst.usesAirLocomotion ? "true" : "false")
                  << " airLiftY=" << inst.airLiftY
                  << " takeoffSec=" << inst.takeoffSec
                  << " landingSec=" << inst.landingSec
                  << " takeoffAnimSpeed=" << inst.takeoffAnimSpeed
                  << " landAnimSpeed=" << inst.landAnimSpeed
                  << "\n";
    }
}

} // namespace AnimSet
