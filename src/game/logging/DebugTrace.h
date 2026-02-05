// src/game/logging/DebugTrace.h
#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <cstdlib>
#include <cctype>

namespace DebugTrace {

// Match rules are read from environment once (lazy):
//   PAC_TRACE_ALL=1                      -> enable all traces
//   PAC_TRACE_COMBAT="unit:move,..."     -> enable combat traces with optional wildcards
// Token syntax for PAC_TRACE_COMBAT:
//   "unit:move" where either side can be "*" (wildcard)
// Examples:
//   PAC_TRACE_COMBAT="bulbasaur:vine_whip"
//   PAC_TRACE_COMBAT="*:vine_whip"
//   PAC_TRACE_COMBAT="bulbasaur:*"
//   PAC_TRACE_COMBAT="*:*,pikachu:thunder_shock"
struct Rule {
    std::string unit;
    std::string move;
};

inline std::string toLower(std::string_view sv) {
    std::string out;
    out.reserve(sv.size());
    for (char c : sv) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

inline std::vector<Rule>& rulesCombat() {
    static std::vector<Rule> rules;
    static bool loaded = false;
    if (loaded) return rules;
    loaded = true;

    const char* env = std::getenv("PAC_TRACE_COMBAT");
    if (!env || !*env) return rules;

    std::string s(env);
    auto pushTok = [&](std::string tok) {
        // trim
        auto l = tok.find_first_not_of(" \t\r\n");
        auto r = tok.find_last_not_of(" \t\r\n");
        if (l == std::string::npos) return;
        tok = tok.substr(l, r - l + 1);

        // split unit:move (either can be omitted -> wildcard)
        std::string unit="*";
        std::string move="*";
        auto colon = tok.find(':');
        if (colon == std::string::npos) {
            // allow shorthand: "bulbasaur" => unit match only
            unit = tok;
        } else {
            unit = tok.substr(0, colon);
            move = tok.substr(colon + 1);
            if (unit.empty()) unit = "*";
            if (move.empty()) move = "*";
        }
        rules.push_back({toLower(unit), toLower(move)});
    };

    std::string cur;
    for (char c : s) {
        if (c == ',' || c == ';' || std::isspace(static_cast<unsigned char>(c))) {
            if (!cur.empty()) { pushTok(cur); cur.clear(); }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) pushTok(cur);

    return rules;
}

inline bool traceAll() {
    const char* all = std::getenv("PAC_TRACE_ALL");
    return all && *all && std::string_view(all) != "0";
}

inline bool matchOne(std::string_view value, const std::string& patLower) {
    if (patLower == "*" || patLower.empty()) return true;
    return toLower(value) == patLower;
}

inline bool combat(std::string_view unitName, std::string_view moveName) {
    if (traceAll()) return true;
    const auto& rs = rulesCombat();
    if (rs.empty()) return false;

    for (const auto& r : rs) {
        if (matchOne(unitName, r.unit) && matchOne(moveName, r.move)) return true;
    }
    return false;
}

} // namespace DebugTrace
