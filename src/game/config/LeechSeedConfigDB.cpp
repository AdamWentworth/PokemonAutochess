// src/game/config/LeechSeedConfigDB.cpp
#include "LeechSeedConfigDB.h"

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

static std::string stripInlineComment(std::string s) {
    auto hashPos = s.find('#');
    auto slashPos = s.find("//");
    size_t cut = std::string::npos;

    if (hashPos != std::string::npos) cut = hashPos;
    if (slashPos != std::string::npos) cut = std::min(cut, slashPos);

    if (cut != std::string::npos) s = s.substr(0, cut);
    return trim(s);
}

static bool parseFloat(const std::string& v, float& out) {
    try { out = std::stof(trim(v)); return true; }
    catch (...) { return false; }
}

static bool parseInt(const std::string& v, int& out) {
    try { out = std::stoi(trim(v)); return true; }
    catch (...) { return false; }
}

LeechSeedConfigDB& LeechSeedConfigDB::get() {
    static LeechSeedConfigDB inst;
    return inst;
}

bool LeechSeedConfigDB::ensureLoaded(const std::string& path) {
    if (loaded) return true;
    loaded = true;

    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "[LeechSeedConfigDB] No config file: " << path << "\n";
        return false;
    }

    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty()) continue;
        if (line[0] == '#') continue;
        if (line.rfind("//", 0) == 0) continue;

        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        const std::string key = lower(trim(line.substr(0, eq)));
        std::string val = trim(line.substr(eq + 1));
        val = stripInlineComment(val);

        if (key == "sappercent") parseFloat(val, cfg.sapPercent);
        else if (key == "tickintervalsec") parseFloat(val, cfg.tickIntervalSec);
        else if (key == "durationsec") parseFloat(val, cfg.durationSec);
        else if (key == "minsap") parseInt(val, cfg.minSap);
        else if (key == "healmultiplier") parseFloat(val, cfg.healMultiplier);
    }

    return true;
}
