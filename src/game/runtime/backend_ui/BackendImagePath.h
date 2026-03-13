#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace game::runtime::backend_images {

inline bool fileExists(const std::string& path) {
    if (path.empty()) return false;
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

inline bool fileExistsCached(const std::string& path) {
    if (path.empty()) return false;
    static thread_local std::unordered_map<std::string, bool> cache;
    const auto it = cache.find(path);
    if (it != cache.end()) return it->second;
    const bool exists = fileExists(path);
    cache.emplace(path, exists);
    return exists;
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
    static thread_local std::unordered_map<std::string, std::string> resolvedCache;
    const std::string cacheKey =
        explicitImagePath + "\n" + pokemonName + "\n" + fallbackPath;
    const auto cached = resolvedCache.find(cacheKey);
    if (cached != resolvedCache.end()) return cached->second;

    std::string resolved;
    if (!explicitImagePath.empty()) {
        const std::string normalizedExplicit = normalizedSlashes(explicitImagePath);
        if (fileExistsCached(explicitImagePath)) {
            resolved = explicitImagePath;
            resolvedCache.emplace(cacheKey, resolved);
            return resolved;
        }
        if (normalizedExplicit != explicitImagePath && fileExistsCached(normalizedExplicit)) {
            resolved = normalizedExplicit;
            resolvedCache.emplace(cacheKey, resolved);
            return resolved;
        }
    }

    const std::string candidate = candidatePokemonPortraitPath(pokemonName);
    if (!candidate.empty() && fileExistsCached(candidate)) {
        resolved = candidate;
        resolvedCache.emplace(cacheKey, resolved);
        return resolved;
    }

    if (!fallbackPath.empty()) {
        const std::string normalizedFallback = normalizedSlashes(fallbackPath);
        if (fileExistsCached(fallbackPath)) {
            resolved = fallbackPath;
            resolvedCache.emplace(cacheKey, resolved);
            return resolved;
        }
        if (normalizedFallback != fallbackPath && fileExistsCached(normalizedFallback)) {
            resolved = normalizedFallback;
            resolvedCache.emplace(cacheKey, resolved);
            return resolved;
        }
        resolved = normalizedFallback;
        resolvedCache.emplace(cacheKey, resolved);
        return resolved;
    }

    resolved.clear();
    resolvedCache.emplace(cacheKey, resolved);
    return resolved;
}

} // namespace game::runtime::backend_images
