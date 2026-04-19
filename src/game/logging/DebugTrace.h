// src/game/logging/DebugTrace.h
#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <cctype>

#include "engine/core/Environment.h"

namespace DebugTrace {

// Match rules are read from environment once (lazy):
//   PAC_TRACE_ALL=1                      -> enable all traces
//   PAC_TRACE_COMBAT="unit:move,..."     -> enable combat traces with optional wildcards
//   PAC_TRACE_ANIM=1                     -> enable movement/animation traces for all units
//   PAC_TRACE_ANIM="unit:move,..."       -> enable movement/animation traces with optional wildcards
//   PAC_TRACE_ANIM_TICKS=1               -> include noisy per-fixed-tick movement samples
// Token syntax for PAC_TRACE_COMBAT / PAC_TRACE_ANIM:
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

struct RuleSet {
    bool all = false;
    std::vector<Rule> rules;
};

inline std::string toLower(std::string_view sv) {
    std::string out;
    out.reserve(sv.size());
    for (char c : sv) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

inline bool isAllToken(std::string_view value) {
    const std::string lower = toLower(value);
    return lower == "1" || lower == "true" || lower == "yes" || lower == "on" ||
           lower == "*" || lower == "*:*" || lower == "all";
}

inline RuleSet parseRuleSetFromEnv(const char* envName) {
    RuleSet out;
    const auto env = engine::env::get(envName);
    if (!env.has_value()) return out;

    std::string s(*env);
    auto l = s.find_first_not_of(" \t\r\n");
    auto r = s.find_last_not_of(" \t\r\n");
    if (l == std::string::npos) return out;
    s = s.substr(l, r - l + 1);

    if (isAllToken(s)) {
        out.all = true;
        return out;
    }

    auto pushTok = [&](std::string tok) {
        // trim
        auto tl = tok.find_first_not_of(" \t\r\n");
        auto tr = tok.find_last_not_of(" \t\r\n");
        if (tl == std::string::npos) return;
        tok = tok.substr(tl, tr - tl + 1);
        if (isAllToken(tok)) {
            out.all = true;
            return;
        }

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
        out.rules.push_back({toLower(unit), toLower(move)});
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

    return out;
}

inline RuleSet& rulesCombat() {
    static RuleSet rules;
    static bool loaded = false;
    if (loaded) return rules;
    loaded = true;
    rules = parseRuleSetFromEnv("PAC_TRACE_COMBAT");
    return rules;
}

inline RuleSet& rulesAnim() {
    static RuleSet rules;
    static bool loaded = false;
    if (loaded) return rules;
    loaded = true;
    rules = parseRuleSetFromEnv("PAC_TRACE_ANIM");
    return rules;
}

inline bool traceAll() {
    return engine::env::truthyNonZero("PAC_TRACE_ALL");
}

inline bool matchOne(std::string_view value, const std::string& patLower) {
    if (patLower == "*" || patLower.empty()) return true;
    return toLower(value) == patLower;
}

inline bool combat(std::string_view unitName, std::string_view moveName) {
    if (traceAll()) return true;
    const auto& rs = rulesCombat();
    if (rs.all) return true;
    if (rs.rules.empty()) return false;

    for (const auto& r : rs.rules) {
        if (matchOne(unitName, r.unit) && matchOne(moveName, r.move)) return true;
    }
    return false;
}

inline bool anim(std::string_view unitName, std::string_view moveName = {}) {
    if (traceAll()) return true;
    const auto& rs = rulesAnim();
    if (rs.all) return true;
    if (rs.rules.empty()) return false;

    for (const auto& r : rs.rules) {
        if (matchOne(unitName, r.unit) && matchOne(moveName, r.move)) return true;
    }
    return false;
}

inline bool animTicks() {
    return engine::env::flagEnabled("PAC_TRACE_ANIM_TICKS");
}

} // namespace DebugTrace
