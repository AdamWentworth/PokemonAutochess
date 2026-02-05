// src/engine/core/Paths.h
#pragma once

#include <cstdlib>
#include <string>
#include <string_view>

namespace engine::paths {

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

} // namespace engine::paths
