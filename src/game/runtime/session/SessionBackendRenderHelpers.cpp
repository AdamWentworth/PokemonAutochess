#include "game/runtime/session/SessionBackendRenderHelpers.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <string>

#include "engine/render/IRenderBackend.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshSupport.h"
#include "game/runtime/shared/scene/SharedWorldScene.h"

namespace {

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
    const game::runtime::render_model::MeshData* mesh) {
    if (!renderer || !mesh) return 0u;

    // Prewarm the exact world-scene material payloads that the projected unit
    // fast path will submit later. This covers fallback white/default PBR
    // textures too, which the older raw-texture-only prewarm could miss.
    const std::size_t baseBatchCount =
        (std::max<std::size_t>)(1u, mesh->submeshBaseTextures.size());
    const auto* materialCache =
        shared_projected_unit_backend_mesh_support::ensureFastTexturedMaterialTemplateCache(
            mesh,
            baseBatchCount,
            /*characterInkingEnabled=*/false,
            /*graphicsQuality=*/3);
    if (!materialCache || materialCache->materials.empty()) {
        return 0u;
    }

    std::size_t warmed = 0u;
    for (const auto& material : materialCache->materials) {
        IRenderBackend::WorldTextureData tex =
            shared_world_scene::makeWorldSceneTextureData(material);
        renderer->prewarmWorldTextureData(&tex);
        ++warmed;
    }

    return warmed;
}

} // namespace game::runtime::session_backend_render_helpers
