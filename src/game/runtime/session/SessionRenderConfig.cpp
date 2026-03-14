#include "game/runtime/session/SessionRenderConfig.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>

#include "engine/core/Environment.h"
#include "engine/render/IRenderBackend.h"

namespace {

struct CacheState {
    std::optional<std::size_t> triangleLimit;
    std::optional<std::size_t> triangleFrameBudget;
    std::optional<bool> modelBackfaceCulling;
    std::optional<bool> worldPortraitFallback;
    std::optional<bool> worldPortraitOverlayForced;
    std::optional<bool> preloadModelCache;
    std::optional<bool> prewarmAnimRoles;
    std::optional<bool> prewarmModelTextures;
    std::optional<bool> prewarmModelGeometry;
    std::optional<bool> snapshotPrewarmRestoreRender;
    std::optional<bool> modelFullMesh;
    std::optional<bool> modelVerboseLogging;
    std::optional<bool> modelFastTexturedPath;
    std::optional<bool> uiSpritePrewarm;
    std::optional<bool> worldLayerPrewarm;
    std::optional<bool> growlVfxPrewarm;
    std::optional<bool> particleVfxPrewarm;
    std::optional<bool> gpuClipSkinningGlobal;
    std::optional<bool> gpuClipSkinningOpenGl;
    std::optional<bool> gpuClipSkinningD3d12;
    std::optional<bool> gpuClipSkinningOther;
    std::optional<bool> legacyGrowlWaveVfx;
    std::optional<bool> legacyParticleVfxSnapshotBridge;
    std::optional<bool> exactTailFireCpuPath;
    std::optional<bool> prewarmLegacyTailFirePremul;
};

CacheState gCache;

bool parseEnvFlag(const char* key, bool defaultValue) {
    const auto env = engine::env::get(key);
    if (!env.has_value()) return defaultValue;

    const std::string raw = *env;
    if (raw == "0" || raw == "false" || raw == "FALSE" || raw == "off" || raw == "OFF") {
        return false;
    }
    return true;
}

std::string toLowerCopy(std::string s) {
    std::transform(
        s.begin(),
        s.end(),
        s.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool cachedFlag(std::optional<bool>& slot, const char* key, bool defaultValue) {
    if (!slot.has_value()) {
        slot = parseEnvFlag(key, defaultValue);
    }
    return *slot;
}

} // namespace

namespace game::runtime::session_render_config {

std::size_t backendModelTriangleLimit() {
    if (!gCache.triangleLimit.has_value()) {
        constexpr std::size_t kDefault = 42000u;
        constexpr std::size_t kMin = 512u;
        constexpr std::size_t kMax = 360000u;
        const auto env = engine::env::get("PAC_BACKEND_MODEL_TRI_LIMIT");
        if (!env.has_value()) {
            gCache.triangleLimit = kDefault;
        } else {
            try {
                const std::size_t parsed = static_cast<std::size_t>(std::stoull(*env));
                gCache.triangleLimit = std::clamp(parsed, kMin, kMax);
            } catch (...) {
                gCache.triangleLimit = kDefault;
            }
        }
    }
    return *gCache.triangleLimit;
}

std::size_t backendModelTriangleFrameBudget() {
    if (!gCache.triangleFrameBudget.has_value()) {
        constexpr std::size_t kDefault = 126000u;
        constexpr std::size_t kMin = 1024u;
        constexpr std::size_t kMax = 720000u;
        const auto env = engine::env::get("PAC_BACKEND_MODEL_TRI_FRAME_BUDGET");
        if (!env.has_value()) {
            gCache.triangleFrameBudget = kDefault;
        } else {
            try {
                const std::size_t parsed = static_cast<std::size_t>(std::stoull(*env));
                gCache.triangleFrameBudget = std::clamp(parsed, kMin, kMax);
            } catch (...) {
                gCache.triangleFrameBudget = kDefault;
            }
        }
    }
    return *gCache.triangleFrameBudget;
}

bool backendModelBackfaceCullingEnabled() {
    return cachedFlag(gCache.modelBackfaceCulling, "PAC_BACKEND_MODEL_CULL", false);
}

bool backendWorldPortraitFallbackEnabled() {
    return cachedFlag(gCache.worldPortraitFallback, "PAC_BACKEND_WORLD_PORTRAITS", true);
}

bool backendWorldPortraitOverlayForced() {
    return cachedFlag(gCache.worldPortraitOverlayForced, "PAC_BACKEND_WORLD_PORTRAIT_OVERLAY", false);
}

bool backendPreloadModelCacheEnabled() {
    return cachedFlag(gCache.preloadModelCache, "PAC_BACKEND_PRELOAD_MODELS", true);
}

bool backendPrewarmAnimRolesEnabled() {
    return cachedFlag(gCache.prewarmAnimRoles, "PAC_BACKEND_PREWARM_ANIM_ROLES", true);
}

bool backendPrewarmModelTexturesEnabled() {
    return cachedFlag(gCache.prewarmModelTextures, "PAC_BACKEND_PREWARM_MODEL_TEXTURES", true);
}

bool backendPrewarmModelGeometryEnabled() {
    return cachedFlag(gCache.prewarmModelGeometry, "PAC_BACKEND_PREWARM_MODEL_GEOMETRY", true);
}

bool snapshotPrewarmRestoreRenderEnabled() {
    return cachedFlag(gCache.snapshotPrewarmRestoreRender, "PAC_STATE_SNAPSHOT_PREWARM_RENDER", true);
}

bool backendModelFullMeshEnabled() {
    return cachedFlag(gCache.modelFullMesh, "PAC_BACKEND_MODEL_FULL_MESH", true);
}

bool backendModelVerboseLoggingEnabled() {
    return cachedFlag(gCache.modelVerboseLogging, "PAC_BACKEND_MODEL_VERBOSE", false);
}

bool backendModelFastTexturedPathEnabled() {
    return cachedFlag(gCache.modelFastTexturedPath, "PAC_BACKEND_MODEL_FAST_TEXTURED", true);
}

bool backendUiSpritePrewarmEnabled() {
    return cachedFlag(gCache.uiSpritePrewarm, "PAC_BACKEND_PREWARM_UI_SPRITES", true);
}

bool backendWorldLayerPrewarmEnabled() {
    return cachedFlag(gCache.worldLayerPrewarm, "PAC_BACKEND_PREWARM_WORLD_LAYER", true);
}

bool backendPrewarmGrowlVfxEnabled() {
    return cachedFlag(gCache.growlVfxPrewarm, "PAC_BACKEND_PREWARM_GROWL_VFX", true);
}

bool backendPrewarmParticleVfxEnabled() {
    return cachedFlag(gCache.particleVfxPrewarm, "PAC_BACKEND_PREWARM_PARTICLE_VFX", true);
}

bool backendGpuClipSkinningEnabled(const IRenderBackend* renderer) {
    if (!renderer) return false;
    if (!cachedFlag(gCache.gpuClipSkinningGlobal, "PAC_BACKEND_GPU_CLIP_SKINNING", true)) return false;

    const std::string backendId = toLowerCopy(renderer->backendId());
    if (backendId == "opengl") {
        return cachedFlag(gCache.gpuClipSkinningOpenGl, "PAC_BACKEND_GPU_CLIP_SKINNING_OPENGL", true);
    }
    if (backendId == "d3d12") {
        return cachedFlag(gCache.gpuClipSkinningD3d12, "PAC_BACKEND_GPU_CLIP_SKINNING_D3D12", true);
    }
    return cachedFlag(gCache.gpuClipSkinningOther, "PAC_BACKEND_GPU_CLIP_SKINNING_OTHER", false);
}

bool backendUseLegacyGrowlWaveVfxEnabled() {
    return cachedFlag(gCache.legacyGrowlWaveVfx, "PAC_BACKEND_GROWL_LEGACY_VFX", true);
}

bool backendUseLegacyParticleVfxSnapshotBridgeEnabled() {
    return cachedFlag(gCache.legacyParticleVfxSnapshotBridge, "PAC_BACKEND_PARTICLE_LEGACY_VFX", true);
}

bool backendUseExactTailFireCpuPathEnabled() {
    return cachedFlag(gCache.exactTailFireCpuPath, "PAC_BACKEND_TAIL_FIRE_EXACT_CPU", false);
}

bool backendPrewarmLegacyTailFirePremulEnabled() {
    return cachedFlag(gCache.prewarmLegacyTailFirePremul, "PAC_BACKEND_TAIL_FIRE_PREWARM_PREMUL", false);
}

void resetForTests() {
    gCache = {};
}

} // namespace game::runtime::session_render_config
