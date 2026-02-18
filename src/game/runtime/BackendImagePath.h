#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

namespace game::runtime::backend_images {

inline bool fileExists(const std::string& path) {
    if (path.empty()) return false;
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

inline std::string normalizeNameForImage(std::string name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) || c == '-' || c == '_') {
            out.push_back(static_cast<char>(std::tolower(uc)));
            continue;
        }
        if (std::isspace(uc)) {
            out.push_back('_');
        }
    }
    return out;
}

inline std::string normalizedSlashes(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

inline std::string candidatePokemonPortraitPath(const std::string& pokemonName) {
    const std::string normalized = normalizeNameForImage(pokemonName);
    if (normalized.empty()) return {};
    return "assets/images/" + normalized + ".png";
}

inline std::string resolvePokemonPortraitPath(const std::string& explicitImagePath,
                                              const std::string& pokemonName,
                                              const std::string& fallbackPath) {
    if (!explicitImagePath.empty()) {
        const std::string normalizedExplicit = normalizedSlashes(explicitImagePath);
        if (fileExists(explicitImagePath)) return explicitImagePath;
        if (normalizedExplicit != explicitImagePath && fileExists(normalizedExplicit)) {
            return normalizedExplicit;
        }
    }

    const std::string candidate = candidatePokemonPortraitPath(pokemonName);
    if (!candidate.empty() && fileExists(candidate)) return candidate;

    if (!fallbackPath.empty()) {
        const std::string normalizedFallback = normalizedSlashes(fallbackPath);
        if (fileExists(fallbackPath)) return fallbackPath;
        if (normalizedFallback != fallbackPath && fileExists(normalizedFallback)) {
            return normalizedFallback;
        }
        return normalizedFallback;
    }

    return {};
}

} // namespace game::runtime::backend_images
