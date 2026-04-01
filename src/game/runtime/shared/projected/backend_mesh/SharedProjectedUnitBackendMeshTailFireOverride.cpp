#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshSupport.h"

#include "game/runtime/shared/vfx/tail_fire/SharedTailFireCoordinator.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFirePlaybackPolicy.h"

#include "engine/core/Environment.h"

#include <chrono>
#include <cctype>
#include <string>
#include <unordered_map>

namespace {

bool tailFireDebugEnvEnabled() {
    static const bool enabled = engine::env::flagEnabled("PAC_TAIL_FIRE_DEBUG");
    return enabled;
}

constexpr int kClampToEdge = 33071;

} // namespace

namespace game::runtime::shared_projected_unit_backend_mesh_support {

bool tailFireDebugShouldLogAnchor(bool runtimeModeEnabled, int unitId) {
    if (!(runtimeModeEnabled || tailFireDebugEnvEnabled())) return false;
    static std::unordered_map<int, std::chrono::steady_clock::time_point> sLastLogByUnit;
    const auto now = std::chrono::steady_clock::now();
    auto it = sLastLogByUnit.find(unitId);
    if (it != sLastLogByUnit.end() &&
        (now - it->second) < std::chrono::milliseconds(750)) {
        return false;
    }
    sLastLogByUnit[unitId] = now;
    return true;
}

bool backendUsesAuthoredTailFireMeshPlayback(const char* backendId) {
    return game::runtime::shared_tail_fire_coordinator::backendUsesAuthoredMeshPlayback(
        backendId);
}

bool backendUsesGpuClipSkinningForUnit(const char* backendId, std::string_view species) {
    return game::runtime::shared_tail_fire_coordinator::backendUsesGpuClipSkinning(
        backendId,
        species);
}

std::size_t resolveBatchBaseSubmeshIndex(
    const game::runtime::shared_world_batches::WorldIndexedBatch& batch,
    std::size_t fallback) {
    const auto parseFromKey = [](const std::string& key, std::size_t fallbackValue) {
        const std::string marker = "#submesh_geom:";
        const std::size_t pos = key.find(marker);
        if (pos == std::string::npos) return fallbackValue;
        std::size_t cur = pos + marker.size();
        std::size_t value = 0u;
        bool sawDigit = false;
        while (cur < key.size() && std::isdigit(static_cast<unsigned char>(key[cur]))) {
            sawDigit = true;
            value = value * 10u + static_cast<std::size_t>(key[cur] - '0');
            ++cur;
        }
        return sawDigit ? value : fallbackValue;
    };

    std::size_t out = parseFromKey(batch.geometryCacheKey, fallback);
    if (out != fallback) return out;
    if (batch.sharedTemplate) {
        out = parseFromKey(batch.sharedTemplate->geometryCacheKey, fallback);
    }
    return out;
}

bool applyTailFireMeshFlipbookOverride(
    const Args& args,
    const game::runtime::render_model::MeshData& mesh,
    std::vector<game::runtime::shared_world_batches::WorldIndexedBatch>& batches) {
    if (!args.unit || !args.sharedTailFireAnchors) {
        return false;
    }
    const auto* profile =
        game::runtime::shared_tail_fire_coordinator::resolvePlaybackProfile(
            args.unit->name,
            &mesh);
    if (!profile || !profile->hasFireSubmesh || profile->spec.path == nullptr) {
        return false;
    }

    if (!args.ensureBackendTextureLoaded) {
        return false;
    }
    game::runtime::SharedBackendTextureCacheEntry* atlas =
        args.ensureBackendTextureLoaded(profile->spec.path, false);
    if (!atlas || !atlas->valid || atlas->width <= 0 || atlas->height <= 0 ||
        atlas->rgba.empty()) {
        return false;
    }

    bool applied = false;
    bool suppressedForFallback = false;
    const bool useAuthoredMeshPlayback =
        game::runtime::shared_tail_fire_coordinator::backendUsesAuthoredMeshPlayback(
            args.backendId);
    for (std::size_t bi = 0; bi < batches.size(); ++bi) {
        auto& batch = batches[bi];
        const std::size_t baseSubmeshIndex = resolveBatchBaseSubmeshIndex(batch, bi);
        if (!game::runtime::shared_tail_fire_coordinator::baseSubmeshUsesAuthoredFire(
                baseSubmeshIndex,
                profile)) {
            continue;
        }

        if (!useAuthoredMeshPlayback) {
            batch.sharedTemplate = nullptr;
            batch.geometryCacheKey.clear();
            batch.vertices.clear();
            batch.indices.clear();
            batch.sharedVertices = nullptr;
            batch.sharedVertexCount = 0u;
            batch.sharedIndices = nullptr;
            batch.sharedIndexCount = 0u;
            batch.gpuSkinning = 0u;
            batch.gpuSkinningMode = 0u;
            batch.skinMatrixCount = 0u;
            batch.sharedSkinMatrices = nullptr;
            batch.skinMatrices.clear();
            suppressedForFallback = true;
            continue;
        }

        batch.sharedTemplate = nullptr;
        batch.textureKey = profile->spec.path;
        batch.textureCacheKey = profile->spec.path;
        batch.textureRgba = atlas->rgba.data();
        batch.textureWidth = atlas->width;
        batch.textureHeight = atlas->height;
        batch.textureWrapS = kClampToEdge;
        batch.textureWrapT = kClampToEdge;

        batch.normalTextureKey.clear();
        batch.normalTextureCacheKey.clear();
        batch.normalTextureRgba = nullptr;
        batch.normalTextureWidth = 0;
        batch.normalTextureHeight = 0;
        batch.normalTextureWrapS = 10497;
        batch.normalTextureWrapT = 10497;
        batch.metallicRoughnessTextureKey.clear();
        batch.metallicRoughnessTextureCacheKey.clear();
        batch.metallicRoughnessTextureRgba = nullptr;
        batch.metallicRoughnessTextureWidth = 0;
        batch.metallicRoughnessTextureHeight = 0;
        batch.occlusionTextureKey.clear();
        batch.occlusionTextureCacheKey.clear();
        batch.occlusionTextureRgba = nullptr;
        batch.occlusionTextureWidth = 0;
        batch.occlusionTextureHeight = 0;
        batch.emissiveTextureKey.clear();
        batch.emissiveTextureCacheKey.clear();
        batch.emissiveTextureRgba = nullptr;
        batch.emissiveTextureWidth = 0;
        batch.emissiveTextureHeight = 0;

        batch.materialAlphaOverride = true;
        batch.alphaMode = 1u;
        batch.blendMode = 0u;
        batch.alphaCutoff = 0.08f;
        batch.materialMode = 1u;
        batch.characterInkingEnabled = 0u;
        batch.materialTimeSec = args.materialTimeSec;
        batch.materialFlags = static_cast<float>(
            game::runtime::shared_tail_fire_playback_policy::kAuthoredFireMeshFlagBit);
        batch.materialAtlasWidth = profile->spec.atlasWidth;
        batch.materialAtlasHeight = profile->spec.atlasHeight;
        batch.materialRect0U = 0.0f;
        batch.materialRect0V = 0.0f;
        batch.materialRect0W = 1.0f;
        batch.materialRect0H = 1.0f;
        batch.materialRect1U = 0.0f;
        batch.materialRect1V = 0.0f;
        batch.materialRect1W = 1.0f;
        batch.materialRect1H = 1.0f;
        batch.materialFlipbook0Cols = profile->spec.cols;
        batch.materialFlipbook0Rows = profile->spec.rows;
        batch.materialFlipbook0Frames = profile->spec.frames;
        batch.materialFlipbook0Fps = profile->spec.fps;
        batch.materialFlipbook1Cols = profile->uvShift.x;
        batch.materialFlipbook1Rows = profile->uvShift.y;
        batch.materialFlipbook1Frames = 1.0f;
        batch.materialFlipbook1Fps = 0.0f;
        applied = true;
    }

    if (applied) {
        auto& anchor = (*args.sharedTailFireAnchors)[args.unit->id];
        anchor.valid = true;
        anchor.meshCarrierActive = true;
    }
    return applied || suppressedForFallback;
}

} // namespace game::runtime::shared_projected_unit_backend_mesh_support

