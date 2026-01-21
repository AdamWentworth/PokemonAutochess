// src/game/AnimSetLoader.cpp
#include "AnimSetLoader.h"

#include <fstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <cctype>

// Keep includes compatible with your project include layout:
#include "./engine/render/Model.h"
#include "PokemonInstance.h"

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

static std::string readRoleNameString(const nlohmann::json& j, const std::string& key)
{
    // supports:
    //   { "roles": { "idle": "..." } }
    //   { "roles": { "idle": { "clip": "..." } } }
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

    // supports:
    //   { "idle": "..." }
    //   { "idle": { "clip": "..." } }
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

        if (name == wantedName) {
            outClip = c;
            return true;
        }

        const std::string candLower = toLower(name);

        if (candLower == wantedLower) {
            outClip = c;
            return true;
        }

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
    if (refVal.is_string()) {
        return refVal.get<std::string>();
    }

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
                                        const std::vector<std::string>& preferredSubstrings)
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

    if (!firstName.empty()) {
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
                         const std::vector<std::string>& preferredSubstrings)
{
    RolePick out;

    // 1) Explicit role mapping
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

    // 2) Scan clips array by category
    out = pickFromClipsByCategory(j, fallbackCategory, preferredSubstrings);
    if (out.valid) return out;

    // 3) categories bucket in animset export (may contain id refs)
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

void applyAnimSetOverrides(PokemonInstance& inst, const std::string& modelPath)
{
    const int fallbackLoop = (inst.model && inst.model->getAnimationCount() > 0) ? 0 : -1;

    inst.animIdleIndex     = fallbackLoop;
    inst.animMoveIndex     = fallbackLoop;
    inst.animAttack1Index  = fallbackLoop;
    inst.activeAnimIndex   = fallbackLoop;
    inst.attackDurationSec = 0.0f;

    if (!inst.model) return;

    const std::string animSetPath = animSetPathFromModelPath(modelPath);

    nlohmann::json j;
    if (!loadAnimSetJson(animSetPath, j)) {
        return; // animset optional
    }

    const RolePick idlePick = resolveRoleClip(j, "idle",    "idle",   {"battlewait", "defaultwait", "idle", "wait"});
    const RolePick movePick = resolveRoleClip(j, "move",    "move",   {"walk", "run", "dash", "move"});
    const RolePick atkPick  = resolveRoleClip(j, "attack1", "attack", {"attack01", "attack1", "attack"});

    if (idlePick.valid && !idlePick.clipName.empty()) {
        const int idx = resolveAnimIndex(inst.model.get(), idlePick.clipName);
        if (idx >= 0) inst.animIdleIndex = idx;
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

    inst.activeAnimIndex = inst.animIdleIndex;
}

} // namespace AnimSet
