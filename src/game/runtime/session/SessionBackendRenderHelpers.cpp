#include "game/runtime/session/SessionBackendRenderHelpers.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <string>

#include "engine/render/IRenderBackend.h"

namespace {

std::string buildRuntimeMeshTextureKeyPrefix(
    const game::runtime::backend_model::MeshData* mesh) {
    if (!mesh) return "__runtime_mesh__";
    return "__runtime_mesh__:" +
           std::to_string(static_cast<unsigned long long>(
               reinterpret_cast<std::uintptr_t>(mesh)));
}

std::string buildWorldTextureCacheKey(const std::string& key,
                                      int width,
                                      int height,
                                      int wrapS,
                                      int wrapT,
                                      bool srgb) {
    if (key.empty() || width <= 0 || height <= 0) return {};
    std::string cacheKey = key;
    cacheKey += "|";
    cacheKey += std::to_string(width);
    cacheKey += "x";
    cacheKey += std::to_string(height);
    cacheKey += "|ws=";
    cacheKey += std::to_string(wrapS);
    cacheKey += "|wt=";
    cacheKey += std::to_string(wrapT);
    cacheKey += srgb ? "|srgb" : "|lin";
    return cacheKey;
}

} // namespace

namespace game::runtime::session_backend_render_helpers {

std::string toLowerCopy(std::string s) {
    std::transform(
        s.begin(),
        s.end(),
        s.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string stripSuffix(const std::string& s, const std::string& suffix) {
    if (s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0) {
        return s.substr(0, s.size() - suffix.size());
    }
    return s;
}

std::string makeBackendCardPrewarmLabel(const std::string& texturePath) {
    std::string label = std::filesystem::path(texturePath).stem().string();
    if (label.empty()) return "Card";
    std::replace(label.begin(), label.end(), '_', ' ');
    label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
    return label;
}

int resolveBackendAnimIndexByName(const std::vector<pac_model_types::AnimationClip>& animations,
                                  const std::string& requestedName) {
    if (animations.empty() || requestedName.empty()) return -1;

    auto findExact = [&](const std::string& candidate) -> int {
        for (std::size_t i = 0; i < animations.size(); ++i) {
            if (animations[i].name == candidate) return static_cast<int>(i);
        }
        return -1;
    };
    auto findCaseInsensitive = [&](const std::string& candidate) -> int {
        if (candidate.empty()) return -1;
        const std::string needle = toLowerCopy(candidate);
        for (std::size_t i = 0; i < animations.size(); ++i) {
            if (toLowerCopy(animations[i].name) == needle) return static_cast<int>(i);
        }
        return -1;
    };

    int idx = findExact(requestedName);
    if (idx >= 0) return idx;

    const std::string noGfbanm = stripSuffix(requestedName, ".gfbanm");
    idx = findExact(noGfbanm);
    if (idx >= 0) return idx;

    const std::string noStart = stripSuffix(requestedName, "__START");
    idx = findExact(noStart);
    if (idx >= 0) return idx;

    const std::string noEnd = stripSuffix(requestedName, "__END");
    idx = findExact(noEnd);
    if (idx >= 0) return idx;

    std::string compact = stripSuffix(noGfbanm, "__START");
    compact = stripSuffix(compact, "__END");
    idx = findExact(compact);
    if (idx >= 0) return idx;

    idx = findCaseInsensitive(requestedName);
    if (idx >= 0) return idx;
    idx = findCaseInsensitive(noGfbanm);
    if (idx >= 0) return idx;
    idx = findCaseInsensitive(noStart);
    if (idx >= 0) return idx;
    idx = findCaseInsensitive(noEnd);
    if (idx >= 0) return idx;
    return findCaseInsensitive(compact);
}

int findBackendAnimIndexBySubstring(const std::vector<pac_model_types::AnimationClip>& animations,
                                    const std::vector<std::string>& needles) {
    if (animations.empty() || needles.empty()) return -1;
    for (std::size_t i = 0; i < animations.size(); ++i) {
        const std::string lowerName = toLowerCopy(animations[i].name);
        for (const std::string& needle : needles) {
            if (needle.empty()) continue;
            if (lowerName.find(toLowerCopy(needle)) != std::string::npos) {
                return static_cast<int>(i);
            }
        }
    }
    return -1;
}

std::size_t prewarmBackendWorldTexturesForMesh(
    IRenderBackend* renderer,
    const game::runtime::backend_model::MeshData* mesh) {
    if (!renderer || !mesh) return 0u;

    const std::size_t batchCount = (std::max)(
        (std::max)(
            (std::max)(mesh->submeshBaseTextures.size(), mesh->submeshNormalTextures.size()),
            (std::max)(
                mesh->submeshMetallicRoughnessTextures.size(),
                mesh->submeshOcclusionTextures.size())),
        mesh->submeshEmissiveTextures.size());
    if (batchCount == 0u) return 0u;

    const std::string keyPrefix = buildRuntimeMeshTextureKeyPrefix(mesh);
    std::size_t warmed = 0u;
    for (std::size_t si = 0; si < batchCount; ++si) {
        IRenderBackend::WorldTextureData tex{};
        std::string baseKey;
        std::string baseCacheKey;
        std::string normalKey;
        std::string normalCacheKey;
        std::string mrKey;
        std::string mrCacheKey;
        std::string occlusionKey;
        std::string occlusionCacheKey;
        std::string emissiveKey;
        std::string emissiveCacheKey;
        bool hasAnyTexture = false;

        if (si < mesh->submeshBaseTextures.size()) {
            const auto& src = mesh->submeshBaseTextures[si];
            if (src.hasPixels()) {
                baseKey = keyPrefix + "#submesh:" + std::to_string(si);
                baseCacheKey = buildWorldTextureCacheKey(
                    baseKey, src.width, src.height, src.wrapS, src.wrapT, true);
                tex.key = baseKey.c_str();
                tex.cacheKey = baseCacheKey.c_str();
                tex.rgba = src.rgba.data();
                tex.width = src.width;
                tex.height = src.height;
                tex.wrapS = src.wrapS;
                tex.wrapT = src.wrapT;
                hasAnyTexture = true;
            }
        }
        if (si < mesh->submeshNormalTextures.size()) {
            const auto& src = mesh->submeshNormalTextures[si];
            if (src.hasPixels()) {
                normalKey = keyPrefix + "#submesh_normal:" + std::to_string(si);
                normalCacheKey = buildWorldTextureCacheKey(
                    normalKey, src.width, src.height, src.wrapS, src.wrapT, false);
                tex.normalKey = normalKey.c_str();
                tex.normalCacheKey = normalCacheKey.c_str();
                tex.normalRgba = src.rgba.data();
                tex.normalWidth = src.width;
                tex.normalHeight = src.height;
                tex.normalWrapS = src.wrapS;
                tex.normalWrapT = src.wrapT;
                hasAnyTexture = true;
            }
        }
        if (si < mesh->submeshMetallicRoughnessTextures.size()) {
            const auto& src = mesh->submeshMetallicRoughnessTextures[si];
            if (src.hasPixels()) {
                mrKey = keyPrefix + "#submesh_mr:" + std::to_string(si);
                mrCacheKey = buildWorldTextureCacheKey(
                    mrKey, src.width, src.height, src.wrapS, src.wrapT, false);
                tex.metallicRoughnessKey = mrKey.c_str();
                tex.metallicRoughnessCacheKey = mrCacheKey.c_str();
                tex.metallicRoughnessRgba = src.rgba.data();
                tex.metallicRoughnessWidth = src.width;
                tex.metallicRoughnessHeight = src.height;
                tex.metallicRoughnessWrapS = src.wrapS;
                tex.metallicRoughnessWrapT = src.wrapT;
                hasAnyTexture = true;
            }
        }
        if (si < mesh->submeshOcclusionTextures.size()) {
            const auto& src = mesh->submeshOcclusionTextures[si];
            if (src.hasPixels()) {
                occlusionKey = keyPrefix + "#submesh_occ:" + std::to_string(si);
                occlusionCacheKey = buildWorldTextureCacheKey(
                    occlusionKey, src.width, src.height, src.wrapS, src.wrapT, false);
                tex.occlusionKey = occlusionKey.c_str();
                tex.occlusionCacheKey = occlusionCacheKey.c_str();
                tex.occlusionRgba = src.rgba.data();
                tex.occlusionWidth = src.width;
                tex.occlusionHeight = src.height;
                tex.occlusionWrapS = src.wrapS;
                tex.occlusionWrapT = src.wrapT;
                hasAnyTexture = true;
            }
        }
        if (si < mesh->submeshEmissiveTextures.size()) {
            const auto& src = mesh->submeshEmissiveTextures[si];
            if (src.hasPixels()) {
                emissiveKey = keyPrefix + "#submesh_emissive:" + std::to_string(si);
                emissiveCacheKey = buildWorldTextureCacheKey(
                    emissiveKey, src.width, src.height, src.wrapS, src.wrapT, true);
                tex.emissiveKey = emissiveKey.c_str();
                tex.emissiveCacheKey = emissiveCacheKey.c_str();
                tex.emissiveRgba = src.rgba.data();
                tex.emissiveWidth = src.width;
                tex.emissiveHeight = src.height;
                tex.emissiveWrapS = src.wrapS;
                tex.emissiveWrapT = src.wrapT;
                hasAnyTexture = true;
            }
        }

        if (!hasAnyTexture) continue;
        renderer->prewarmWorldTextureData(&tex);
        ++warmed;
    }

    return warmed;
}

} // namespace game::runtime::session_backend_render_helpers
