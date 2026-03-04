#pragma once

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace game::logging::flow {

inline const auto g_traceStart = std::chrono::steady_clock::now();
inline std::uint64_t g_menuClickSeq = 0;
inline std::uint64_t g_starterClickSeq = 0;
inline double g_lastMenuClickMs = -1.0;
inline double g_lastStarterClickMs = -1.0;

inline double nowMs() {
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration<double, std::milli>(now - g_traceStart);
    return elapsed.count();
}

inline std::string formatMs(double valueMs) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << valueMs << "ms";
    return oss.str();
}

inline std::string sinceMarkerMs(double nowMsValue, double markerMs) {
    if (markerMs < 0.0) return "n/a";
    return formatMs(nowMsValue - markerMs);
}

inline void log(const std::string& stage, const std::string& detail) {
    std::cout << "[FlowTrace] t=" << formatMs(nowMs()) << " " << stage;
    if (!detail.empty()) {
        std::cout << " " << detail;
    }
    std::cout << "\n";
}

inline bool isStartActionEntry(const std::string& entryId) {
    return entryId == "start_game" ||
           entryId == "new_game_classic" ||
           entryId == "new_game_adventure";
}

inline void noteMenuActionClick(const std::string& entryId, const std::string& scriptPath) {
    const double now = nowMs();
    g_lastMenuClickMs = now;
    ++g_menuClickSeq;
    log("menu_click",
        "seq=" + std::to_string(g_menuClickSeq) +
        " entry=" + entryId +
        " script=" + scriptPath);
}

inline void noteStartNewGameQueued(const std::string& mode) {
    const double now = nowMs();
    log("start_new_game_queued",
        "mode=" + mode +
        " since_menu_click=" + sinceMarkerMs(now, g_lastMenuClickMs));
}

inline void noteStartNewGameApplyBegin(const std::string& mode) {
    const double now = nowMs();
    log("start_new_game_apply_begin",
        "mode=" + mode +
        " since_menu_click=" + sinceMarkerMs(now, g_lastMenuClickMs));
}

inline void noteStarterStateEntered(const std::string& scriptPath) {
    const double now = nowMs();
    log("starter_state_entered",
        "script=" + scriptPath +
        " since_menu_click=" + sinceMarkerMs(now, g_lastMenuClickMs));
}

inline void noteStarterCardClick(const std::string& pokemonName) {
    const double now = nowMs();
    g_lastStarterClickMs = now;
    ++g_starterClickSeq;
    log("starter_click",
        "seq=" + std::to_string(g_starterClickSeq) +
        " pokemon=" + pokemonName);
}

inline void notePlacementStateEntered(const std::string& starterName) {
    const double now = nowMs();
    log("placement_state_entered",
        "starter=" + starterName +
        " since_starter_click=" + sinceMarkerMs(now, g_lastStarterClickMs));
}

}  // namespace game::logging::flow

