// src/game/vfx/TailFireVFXConfigDB.cpp
#include "TailFireVFXConfigDB.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <iostream>

static std::string trim(std::string s) {
    auto notSpace = [](unsigned char c){ return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return (char)std::tolower(c); });
    return s;
}

// Remove inline comments: "value  # comment" or "value // comment"
static std::string stripInlineComment(std::string s) {
    auto hashPos = s.find('#');
    auto slashPos = s.find("//");
    size_t cut = std::string::npos;

    if (hashPos != std::string::npos) cut = hashPos;
    if (slashPos != std::string::npos) cut = std::min(cut, slashPos);

    if (cut != std::string::npos) s = s.substr(0, cut);
    return trim(s);
}

static bool parseBool(const std::string& v, bool& out) {
    const std::string x = lower(trim(v));
    if (x == "true" || x == "1" || x == "yes" || x == "on") { out = true; return true; }
    if (x == "false" || x == "0" || x == "no" || x == "off") { out = false; return true; }
    return false;
}

static bool parseFloat(const std::string& v, float& out) {
    try { out = std::stof(trim(v)); return true; } catch (...) { return false; }
}

static bool parseInt(const std::string& v, int& out) {
    try { out = std::stoi(trim(v)); return true; } catch (...) { return false; }
}

static bool parseVec3(const std::string& v, glm::vec3& out) {
    std::stringstream ss(v);
    std::string a,b,c;
    if (!std::getline(ss,a,',')) return false;
    if (!std::getline(ss,b,',')) return false;
    if (!std::getline(ss,c,',')) return false;
    try {
        out = glm::vec3(std::stof(trim(a)), std::stof(trim(b)), std::stof(trim(c)));
        return true;
    } catch (...) { return false; }
}

TailFireVFXConfigDB& TailFireVFXConfigDB::get() {
    static TailFireVFXConfigDB inst;
    return inst;
}

bool TailFireVFXConfigDB::ensureLoaded(const std::string& path) {
    if (loaded) return true;
    loaded = true;

    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "[TailFireVFXConfigDB] No config file: " << path << "\n";
        return false;
    }

    std::string line;
    std::string currentSection;

    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty()) continue;
        if (line[0] == '#') continue;
        if (line.rfind("//", 0) == 0) continue;

        if (line.front() == '[' && line.back() == ']') {
            currentSection = lower(trim(line.substr(1, line.size() - 2)));
            entries[currentSection].has = true;
            entries[currentSection].cfg = TailFireVFX::Config{}; // defaults
            continue;
        }

        if (currentSection.empty()) continue;

        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        const std::string key = lower(trim(line.substr(0, eq)));
        std::string val = trim(line.substr(eq + 1));
        val = stripInlineComment(val);

        auto& cfg = entries[currentSection].cfg;

        if (key == "tailtipnodename") cfg.tailTipNodeName = val;
        else if (key == "tailtipnodeindex") parseInt(val, cfg.tailTipNodeIndex);
        else if (key == "tailworldyoffset") parseFloat(val, cfg.tailWorldYOffset);
        else if (key == "emitratepersec") parseFloat(val, cfg.emitRatePerSec);
        else if (key == "spawnradius") parseFloat(val, cfg.spawnRadius);
        else if (key == "backdir") parseVec3(val, cfg.backDir);
        else if (key == "acceleration") parseVec3(val, cfg.acceleration);
        else if (key == "dampingbase") parseFloat(val, cfg.dampingBase);
        else if (key == "pointscale") parseFloat(val, cfg.pointScale);
        else if (key == "useflipbook") parseBool(val, cfg.useFlipbook);
    }

    return true;
}

void TailFireVFXConfigDB::applyIfAny(const std::string& speciesLower, TailFireVFX::Config& io) const {
    auto it = entries.find(lower(speciesLower));
    if (it == entries.end() || !it->second.has) return;
    io = it->second.cfg;
}
