// src/engine/core/Paths.h
#pragma once

#include <cstdlib>
#include <string>
#include <string_view>

namespace engine::paths {

// ---------------- Assets ----------------
//
// Asset root can be overridden with env var PAC_ASSET_ROOT.
// Default is "assets".
inline std::string assetRoot() {
    if (const char* v = std::getenv("PAC_ASSET_ROOT")) {
        if (*v) return std::string(v);
    }
    return "assets";
}

// Join asset root with a relative path (uses forward slashes).
inline std::string asset(std::string_view rel) {
    std::string root = assetRoot();
    if (!root.empty() && (root.back() == '/' || root.back() == '\\')) root.pop_back();

    std::string r(rel);
    while (!r.empty() && (r.front() == '/' || r.front() == '\\')) r.erase(r.begin());

    return root + "/" + r;
}

// ---------------- Data (repo/runtime root) ----------------
//
// Data root can be overridden with env var PAC_DATA_ROOT.
// Default is "." (current working directory).
//
// Use this for non-asset runtime files that you ship alongside the exe
// (scripts/, config/, etc.) when they are not under assets/.
inline std::string dataRoot() {
    if (const char* v = std::getenv("PAC_DATA_ROOT")) {
        if (*v) return std::string(v);
    }
    return ".";
}

// Optional packed data bundle (scripts/config). Empty if not set.
inline std::string dataPack() {
    if (const char* v = std::getenv("PAC_DATA_PACK")) {
        if (*v) return std::string(v);
    }
    return "";
}

// Join data root with a relative path (uses forward slashes).
inline std::string data(std::string_view rel) {
    std::string root = dataRoot();
    if (!root.empty() && (root.back() == '/' || root.back() == '\\')) root.pop_back();

    std::string r(rel);
    while (!r.empty() && (r.front() == '/' || r.front() == '\\')) r.erase(r.begin());

    return root + "/" + r;
}

} // namespace engine::paths
