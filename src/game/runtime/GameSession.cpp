#include "game/runtime/GameSession.h"

// Heavy includes live here (not in headers).
#include <iostream>
#include <string>
#include <utility>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <random>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stb_image.h>

#include "engine/core/GameContext.h"
#include "engine/core/EngineServices.h"
#include "engine/core/Paths.h"
#include "engine/core/Random.h"
#include "engine/core/Services.h"
#include "engine/core/TimeSources.h"
#include "engine/core/Environment.h"
#include "engine/core/ecs/Entity.h"
#include "engine/input/InputEvent.h"

#include "engine/render/Camera3D.h"
#include "engine/render/IRenderBackend.h"
#include "engine/render/Model.h"

#include "engine/core/ecs/Scheduler.h"
#include "engine/core/ecs/World.h"
#include "engine/utils/ResourceManager.h"

#include "game/GameWorld.h"
#include "game/GameStateManager.h"
#include "game/runtime/routes/BackendRenderPolicy.h"
#include "game/runtime/routes/RenderFlowDecisions.h"
#include "game/runtime/routes/StartupRenderRoutePolicy.h"
#include "game/runtime/BackendDebugText.h"
#include "game/runtime/routes/GameServiceRenderRoutes.h"
#include "game/runtime/BackendInventoryOverlay.h"
#include "game/runtime/BackendInventoryPanel.h"
#include "game/runtime/BackendInputSlots.h"
#include "game/runtime/BackendStatusText.h"
#include "game/runtime/BackendUiScale.h"
#include "game/runtime/BackendHudFormatting.h"
#include "game/runtime/BackendWorldProjection.h"
#include "game/runtime/BackendWorldProxyGeometry.h"
#include "game/runtime/backend_model_cache/BackendModelCache.h"
#include "game/runtime/BackendMaterialShading.h"
#include "game/runtime/BackendProceduralPose.h"
#include "game/runtime/BackendUnitVisuals.h"
#include "game/runtime/shared/backend/SharedBackendTextureCache.h"
#include "game/runtime/shared/backend/SharedBackendPoseEval.h"
#include "game/runtime/shared/capture/SharedCapturePresentation.h"
#include "game/runtime/shared/capture/SharedCaptureModelBridge.h"
#include "game/runtime/shared/world/SharedBoardGridBatches.h"
#include "game/runtime/shared/projected/SharedProjectedDebugVfx.h"
#include "game/runtime/shared/ui/SharedBackendDebugViewOverlay.h"
#include "game/runtime/shared/projected/SharedProjectedWorldSceneHelpers.h"
#include "game/runtime/shared/projected/SharedProjectedUnitRenderer.h"
#include "game/runtime/shared/vfx/particles/SharedParticleBillboardBatches.h"
#include "game/runtime/shared/vfx/particles/SharedParticleSnapshotBillboards.h"
#include "game/runtime/shared/vfx/particles/SharedParticleVfxBridgeDispatch.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireFallbackEmitter.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireExactGpuBatches.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireAtlasHelpers.h"
#include "game/runtime/shared/vfx/growl/SharedGrowlVfxHelpers.h"
#include "game/runtime/shared/vfx/growl/SharedGrowlWaveBridge.h"
#include "game/runtime/shared/vfx/growl/SharedGrowlWaveBatches.h"
#include "game/runtime/shared/ui/SharedUnitHudBatches.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"
#include "game/GameServices.h"
#include "game/GameConfig.h"
#include "game/runtime/GameUpdateGraph.h"
#include "game/ui/UIViewport.h"
#include "game/ui/ShopLayout.h"

#include "game/config/GameDataDb.h"
#include "game/config/AnimSetLoader.h"
#include "game/assets/DevAssetStore.h"
#include "game/assets/PackedAssetStore.h"
#include "game/ecs/RoundState.h"
#include "game/ecs/CombatActive.h"

#include "game/systems/CameraSystem.h"
#include "game/systems/UnitInteractionSystem.h"
#include "game/systems/RoundSystem.h"
#include "game/systems/MovementSystem.h"
#include "game/systems/CombatSystem.h"
#include "game/systems/ShopSystem.h"
#include "game/systems/LegacySystemAdapters.h"

#include "game/state/scripted/ScriptedState.h"
#include "game/logging/LogBus.h"
#include "game/logging/LoggerUtil.h"
#include "game/scripting/ScriptEventBus.h"
#include "game/world/MoveImpactRouting.h"

namespace {
std::size_t backendModelTriangleLimit() {
    static const std::size_t limit = []() -> std::size_t {
        constexpr std::size_t kDefault = 42000u;
        constexpr std::size_t kMin = 512u;
        constexpr std::size_t kMax = 360000u;
        const auto env = engine::env::get("PAC_BACKEND_MODEL_TRI_LIMIT");
        if (!env.has_value()) return kDefault;
        try {
            const std::size_t parsed = static_cast<std::size_t>(std::stoull(*env));
            return std::clamp(parsed, kMin, kMax);
        } catch (...) {
            return kDefault;
        }
    }();
    return limit;
}

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

std::size_t backendModelTriangleFrameBudget() {
    static const std::size_t budget = []() -> std::size_t {
        constexpr std::size_t kDefault = 126000u;
        constexpr std::size_t kMin = 1024u;
        constexpr std::size_t kMax = 720000u;
        const auto env = engine::env::get("PAC_BACKEND_MODEL_TRI_FRAME_BUDGET");
        if (!env.has_value()) return kDefault;
        try {
            const std::size_t parsed = static_cast<std::size_t>(std::stoull(*env));
            return std::clamp(parsed, kMin, kMax);
        } catch (...) {
            return kDefault;
        }
    }();
    return budget;
}

bool backendModelBackfaceCullingEnabled() {
    static const bool enabled = []() -> bool {
        const auto env = engine::env::get("PAC_BACKEND_MODEL_CULL");
        if (!env.has_value()) return false;
        const std::string raw = *env;
        if (raw == "0" || raw == "false" || raw == "FALSE" || raw == "off" || raw == "OFF") {
            return false;
        }
        return true;
    }();
    return enabled;
}

bool backendWorldPortraitFallbackEnabled() {
    static const bool enabled = []() -> bool {
        const auto env = engine::env::get("PAC_BACKEND_WORLD_PORTRAITS");
        if (!env.has_value()) return true;
        const std::string raw = *env;
        if (raw == "0" || raw == "false" || raw == "FALSE" || raw == "off" || raw == "OFF") {
            return false;
        }
        return true;
    }();
    return enabled;
}

bool backendWorldPortraitOverlayForced() {
    static const bool enabled = []() -> bool {
        const auto env = engine::env::get("PAC_BACKEND_WORLD_PORTRAIT_OVERLAY");
        if (!env.has_value()) return false;
        const std::string raw = *env;
        if (raw == "0" || raw == "false" || raw == "FALSE" || raw == "off" || raw == "OFF") {
            return false;
        }
        return true;
    }();
    return enabled;
}

bool backendPreloadModelCacheEnabled() {
    static const bool enabled = []() -> bool {
        const auto env = engine::env::get("PAC_BACKEND_PRELOAD_MODELS");
        if (!env.has_value()) return true;
        const std::string raw = *env;
        if (raw == "0" || raw == "false" || raw == "FALSE" || raw == "off" || raw == "OFF") {
            return false;
        }
        return true;
    }();
    return enabled;
}

bool backendModelFullMeshEnabled() {
    static const bool enabled = []() -> bool {
        const auto env = engine::env::get("PAC_BACKEND_MODEL_FULL_MESH");
        if (!env.has_value()) return true;
        const std::string raw = *env;
        if (raw == "0" || raw == "false" || raw == "FALSE" || raw == "off" || raw == "OFF") {
            return false;
        }
        return true;
    }();
    return enabled;
}

bool backendModelVerboseLoggingEnabled() {
    static const bool enabled = []() -> bool {
        const auto env = engine::env::get("PAC_BACKEND_MODEL_VERBOSE");
        if (!env.has_value()) return false;
        const std::string raw = *env;
        if (raw == "0" || raw == "false" || raw == "FALSE" || raw == "off" || raw == "OFF") {
            return false;
        }
        return true;
    }();
    return enabled;
}

bool backendModelFastTexturedPathEnabled() {
    static const bool enabled = []() -> bool {
        const auto env = engine::env::get("PAC_BACKEND_MODEL_FAST_TEXTURED");
        if (!env.has_value()) return true;
        const std::string raw = *env;
        if (raw == "0" || raw == "false" || raw == "FALSE" || raw == "off" || raw == "OFF") {
            return false;
        }
        return true;
    }();
    return enabled;
}

bool backendGpuClipSkinningEnabled(const IRenderBackend* renderer) {
    static const bool requested = []() -> bool {
        const auto env = engine::env::get("PAC_BACKEND_GPU_CLIP_SKINNING");
        if (!env.has_value()) return true;
        const std::string raw = *env;
        if (raw == "0" || raw == "false" || raw == "FALSE" || raw == "off" || raw == "OFF") {
            return false;
        }
        return true;
    }();
    if (!requested || !renderer) return false;
    const char* backendId = renderer->backendId();
    if (!backendId) return false;
    return toLowerCopy(backendId) == "opengl";
}

bool backendUseLegacyGrowlWaveVfxEnabled() {
    static const bool enabled = []() -> bool {
        const auto env = engine::env::get("PAC_BACKEND_GROWL_LEGACY_VFX");
        if (!env.has_value()) return true;
        const std::string raw = *env;
        if (raw == "0" || raw == "false" || raw == "FALSE" || raw == "off" || raw == "OFF") {
            return false;
        }
        return true;
    }();
    return enabled;
}

bool backendUseLegacyParticleVfxSnapshotBridgeEnabled() {
    static const bool enabled = []() -> bool {
        const auto env = engine::env::get("PAC_BACKEND_PARTICLE_LEGACY_VFX");
        if (!env.has_value()) return true;
        const std::string raw = *env;
        if (raw == "0" || raw == "false" || raw == "FALSE" || raw == "off" || raw == "OFF") {
            return false;
        }
        return true;
    }();
    return enabled;
}

bool backendUseExactTailFireCpuPathEnabled() {
    static const bool enabled = []() -> bool {
        const auto env = engine::env::get("PAC_BACKEND_TAIL_FIRE_EXACT_CPU");
        if (!env.has_value()) return false;
        const std::string raw = *env;
        if (raw == "0" || raw == "false" || raw == "FALSE" || raw == "off" || raw == "OFF") {
            return false;
        }
        return true;
    }();
    return enabled;
}

float hash01(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    const std::uint32_t v = x >> 8;
    return static_cast<float>(v) * (1.0f / 16777216.0f);
}

glm::quat rotationFromToSafe(const glm::vec3& from, const glm::vec3& to) {
    glm::vec3 a = from;
    glm::vec3 b = to;
    const float la = glm::length(a);
    const float lb = glm::length(b);
    if (la <= 0.0001f || lb <= 0.0001f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    a /= la;
    b /= lb;
    const float d = glm::clamp(glm::dot(a, b), -1.0f, 1.0f);
    if (d > 0.9999f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    if (d < -0.9999f) {
        glm::vec3 ortho = glm::cross(a, glm::vec3(1.0f, 0.0f, 0.0f));
        if (glm::dot(ortho, ortho) <= 0.0001f) ortho = glm::cross(a, glm::vec3(0.0f, 1.0f, 0.0f));
        ortho = glm::normalize(ortho);
        return glm::angleAxis(3.14159265f, ortho);
    }
    const glm::vec3 axis = glm::normalize(glm::cross(a, b));
    return glm::angleAxis(std::acos(d), axis);
}

std::size_t selectUniformTriangleIndex(std::size_t sampleIndex,
                                       std::size_t sampleCount,
                                       std::size_t triangleCount) {
    if (sampleCount == 0u || triangleCount == 0u) return 0u;
    if (sampleCount >= triangleCount) return std::min(sampleIndex, triangleCount - 1u);
    const double span = static_cast<double>(triangleCount) / static_cast<double>(sampleCount);
    const double center = (static_cast<double>(sampleIndex) + 0.5) * span;
    const std::size_t tri =
        std::min(triangleCount - 1u, static_cast<std::size_t>(std::floor(center)));
    return tri;
}
} // namespace

namespace game {

struct GameSession::Impl {
    // Pointers (engine-owned)
    Camera3D* camera = nullptr;
    IRenderBackend* renderer = nullptr;
    EngineServices* engineServices = nullptr;

    // Injected db (owned; loader instances).
    GameDataDb dataDb;

    // Game-owned logger instance (no file-scope globals).
    LogBus::Logger log;
    ScriptEventBus scriptEvents;
    std::unique_ptr<engine::IAssetStore> assetStore;
    engine::XorShift32 rng;
    engine::ManualTimeSource timeSource;

    // Owned config (loaded once per session).
    GameConfigData config;

    // v1: thread config/db/log into states without singletons.
    std::unique_ptr<GameServices> services;
    ui::UIViewport viewport;

    // ECS runtime core services + world.
    engine::CoreServices coreServices;
    engine::ecs::World ecsWorld;
    engine::ecs::Entity roundPhaseEntity{};

    // Owned state
    std::unique_ptr<GameStateManager> stateManager;
    std::unique_ptr<GameWorld>        gameWorld;
    engine::ecs::Scheduler scheduler;
    GameUpdateGraph updateGraph;

    runtime::render::RenderRoutes startupRoutes{};
    bool allowBackendMenuBackdrop = false;
    bool showPerfOverlay = false;
    bool devPauseWorld = false;
    int devPauseStepTicks = 0;

    static constexpr std::size_t kBackendInventoryVisibleCount = 6;
    runtime::backend_inventory_panel::PanelState backendInventoryPanel;
    struct BackendMeshCacheEntry {
        bool attemptedLoad = false;
        bool reportedFailure = false;
        runtime::backend_model::MeshData mesh;
        std::string error;
    };
    std::unordered_map<std::string, BackendMeshCacheEntry> backendMeshByModelPath;
    struct BackendAnimRoleEntry {
        bool attemptedResolve = false;
        int idleIndex = -1;
        int moveIndex = -1;
        int attackIndex = -1;
        int groundIdleIndex = -1;
        int airIdleIndex = -1;
        int takeoffIndex = -1;
        int landIndex = -1;
        int landAIndex = -1;
        int landBIndex = -1;
        int landCIndex = -1;
        int faintIndex = -1;
        float attackDurationSec = 0.0f;
        float faintDurationSec = 0.0f;
        bool usesAirLocomotion = false;
        float airLiftY = 0.0f;
        float takeoffSec = 0.0f;
        float landingSec = 0.0f;
    };
    std::unordered_map<std::string, BackendAnimRoleEntry> backendAnimByModelPath;
    using BackendTextureCacheEntry = game::runtime::SharedBackendTextureCacheEntry;
    std::unordered_map<std::string, BackendTextureCacheEntry> backendTextureByPath;

    std::shared_ptr<CameraSystem>           cameraSystem;
    std::shared_ptr<UnitInteractionSystem>  unitSystem;
    ShopSystem*                             shopSystem = nullptr;


    Impl(GameContext& ctx, GameDataDb db)
        : dataDb(std::move(db))
        , ecsWorld(&coreServices) {
        init(ctx);
    }

    bool hasActiveRenderBackend() const {
        if (services) return services->renderEnabled;
        return startupRoutes.hasRenderer;
    }

    bool usesBackendGameRenderPath() const {
        if (services) return services->usesBackendGameRenderPath();
        return startupRoutes.usesBackendRenderPath();
    }

    bool usesBackendGameUiPath() const {
        if (services) return services->usesBackendGameUiPath();
        return startupRoutes.usesBackendUiPath();
    }

    runtime::render::RenderRoutes activeRenderRoutes() const {
        if (services) {
            return runtime::render::routesFromServices(*services);
        }
        return startupRoutes;
    }

    runtime::render::FrameRenderFlow currentFrameFlow(bool renderWorldRequested) const {
        return runtime::render::decideFrameRenderFlow(
            activeRenderRoutes(),
            renderWorldRequested,
            allowBackendMenuBackdrop);
    }

    runtime::backend_model::MeshData* ensureBackendMeshLoaded(const std::string& modelPath) {
        auto& cacheEntry = backendMeshByModelPath[modelPath];
        if (!cacheEntry.attemptedLoad) {
            cacheEntry.attemptedLoad = true;
            std::string err;
            if (!runtime::backend_model::loadMeshFromCache(modelPath, cacheEntry.mesh, &err)) {
                cacheEntry.error = std::move(err);
                cacheEntry.mesh = {};
            }
        }

        if (!cacheEntry.error.empty()) {
            if (!cacheEntry.reportedFailure) {
                std::cout << "[Render][ModelCache] Unable to render model '" << modelPath
                          << "' (" << cacheEntry.error << ")\n";
                cacheEntry.reportedFailure = true;
            }
            return nullptr;
        }
        if (cacheEntry.mesh.vertices.empty() || cacheEntry.mesh.indices.empty()) {
            return nullptr;
        }
        return &cacheEntry.mesh;
    }

    BackendTextureCacheEntry* ensureBackendTextureLoaded(const std::string& texturePath,
                                                         bool flipVertical = false) {
        if (backendTextureByPath.empty()) {
            backendTextureByPath.reserve(64u);
        }
        const std::string key = texturePath.empty()
            ? "__white__"
            : ((flipVertical ? "__flipv__:" : "__noflip__:" ) + texturePath);
        auto& cacheEntry = backendTextureByPath[key];
        if (cacheEntry.attemptedLoad) {
            return cacheEntry.valid ? &cacheEntry : nullptr;
        }

        cacheEntry.attemptedLoad = true;
        cacheEntry.valid = false;
        cacheEntry.width = 0;
        cacheEntry.height = 0;
        cacheEntry.rgba.clear();

        if (texturePath.empty()) {
            cacheEntry.width = 1;
            cacheEntry.height = 1;
            cacheEntry.rgba = {255u, 255u, 255u, 255u};
            cacheEntry.valid = true;
            return &cacheEntry;
        }

        if (texturePath.rfind("__proc:", 0) == 0) {
            const std::string procId = texturePath.substr(7);
            const int width = 64;
            const int height = 64;
            cacheEntry.width = width;
            cacheEntry.height = height;
            cacheEntry.rgba.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 0u);

            auto putPixel = [&](int x, int y, float alpha) {
                alpha = std::clamp(alpha, 0.0f, 1.0f);
                const std::size_t idx =
                    (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                     static_cast<std::size_t>(x)) *
                    4u;
                cacheEntry.rgba[idx + 0] = 255u;
                cacheEntry.rgba[idx + 1] = 255u;
                cacheEntry.rgba[idx + 2] = 255u;
                cacheEntry.rgba[idx + 3] = static_cast<unsigned char>(std::round(alpha * 255.0f));
            };

            auto smooth = [](float e0, float e1, float x) {
                const float t = std::clamp((x - e0) / std::max(0.0001f, (e1 - e0)), 0.0f, 1.0f);
                return t * t * (3.0f - 2.0f * t);
            };

            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const float fx = ((static_cast<float>(x) + 0.5f) / static_cast<float>(width)) * 2.0f - 1.0f;
                    const float fy = ((static_cast<float>(y) + 0.5f) / static_cast<float>(height)) * 2.0f - 1.0f;
                    const float r = std::sqrt(fx * fx + fy * fy);
                    float alpha = 0.0f;

                    if (procId == "soft_circle" || procId == "dot") {
                        alpha = 1.0f - smooth(0.55f, 1.0f, r);
                    } else if (procId == "plus") {
                        const float h = (1.0f - smooth(0.18f, 0.26f, std::fabs(fy))) *
                                        (1.0f - smooth(0.78f, 0.98f, std::fabs(fx)));
                        const float v = (1.0f - smooth(0.18f, 0.26f, std::fabs(fx))) *
                                        (1.0f - smooth(0.78f, 0.98f, std::fabs(fy)));
                        alpha = std::max(h, v);
                    } else if (procId == "leaf" || procId == "seed") {
                        float px = fx;
                        float py = fy * 1.12f + 0.03f;
                        const float t = std::clamp((py + 1.0f) * 0.5f, 0.0f, 1.0f);
                        const float widthScale = (procId == "seed")
                            ? std::max(0.20f, (0.85f - 0.60f * t))
                            : std::max(0.20f, (0.95f - 0.70f * t));
                        const float d = std::sqrt((px / widthScale) * (px / widthScale) + py * py);
                        alpha = 1.0f - smooth(0.82f, 1.02f, d);
                        alpha *= smooth(-1.0f, -0.68f, py);
                    } else if (procId == "starburst") {
                        const float ang = std::atan2(fy, fx);
                        const float spikes = std::pow(std::fabs(std::sin(ang * 11.0f)), 0.75f);
                        const float core = 1.0f - smooth(0.0f, 0.74f, r);
                        const float streak = (1.0f - smooth(0.0f, 0.92f, r)) * spikes;
                        alpha = std::max(core * 0.9f, streak);
                    } else if (procId == "claw") {
                        const float ca = std::cos(-0.60f);
                        const float sa = std::sin(-0.60f);
                        const float qx = ca * fx - sa * fy;
                        const float qy = sa * fx + ca * fy;
                        auto stroke = [&](float xOff, float halfLen, float width0) {
                            const float lx = std::fabs(qx - xOff);
                            const float ly = std::fabs(qy);
                            const float tipT = std::clamp(ly / std::max(0.0001f, halfLen), 0.0f, 1.0f);
                            const float tipNarrow = 1.0f - smooth(0.58f, 1.0f, tipT) * 0.96f;
                            const float localWidth = width0 * tipNarrow;
                            const float core = 1.0f - smooth(localWidth, localWidth + 0.02f, lx);
                            const float lenMask = 1.0f - smooth(halfLen, halfLen + 0.05f, ly);
                            return core * lenMask;
                        };
                        const float s1 = stroke(-0.34f, 0.90f, 0.075f);
                        const float s2 = stroke(0.00f, 0.90f, 0.075f);
                        const float s3 = stroke(0.34f, 0.90f, 0.075f);
                        alpha = std::max(s1, std::max(s2, s3));
                    } else if (procId == "swoosh") {
                        const float band = std::fabs(fy - 0.6f * fx);
                        const float arc = (1.0f - smooth(0.0f, 0.36f, band)) * (1.0f - smooth(0.32f, 1.0f, r));
                        const float core = 1.0f - smooth(0.0f, 0.62f, r);
                        alpha = std::max(arc, core * 0.35f);
                    } else {
                        alpha = 1.0f - smooth(0.55f, 1.0f, r);
                    }

                    putPixel(x, y, alpha);
                }
            }

            cacheEntry.valid = true;
            return &cacheEntry;
        }

        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_set_flip_vertically_on_load(flipVertical ? 1 : 0);
        unsigned char* pixels = stbi_load(texturePath.c_str(), &width, &height, &channels, 4);
        if (!pixels) {
            const std::string dataPath = engine::paths::data(texturePath);
            if (dataPath != texturePath) {
                stbi_set_flip_vertically_on_load(flipVertical ? 1 : 0);
                pixels = stbi_load(dataPath.c_str(), &width, &height, &channels, 4);
            }
        }
        stbi_set_flip_vertically_on_load(false);
        if (!pixels || width <= 0 || height <= 0) {
            if (pixels) stbi_image_free(pixels);
            return nullptr;
        }

        const std::size_t rgbaSize = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
        cacheEntry.rgba.resize(rgbaSize);
        std::memcpy(cacheEntry.rgba.data(), pixels, rgbaSize);
        stbi_image_free(pixels);
        cacheEntry.width = width;
        cacheEntry.height = height;
        cacheEntry.valid = true;
        return &cacheEntry;
    }

    BackendAnimRoleEntry& ensureBackendAnimRoles(const std::string& modelPath,
                                                 const runtime::backend_model::MeshData* mesh) {
        auto& entry = backendAnimByModelPath[modelPath];
        if (entry.attemptedResolve) return entry;
        entry.attemptedResolve = true;
        if (!mesh) return entry;

        const int fallbackLoop = mesh->animations.empty() ? -1 : 0;
        entry.idleIndex = fallbackLoop;
        entry.moveIndex = fallbackLoop;
        entry.attackIndex = fallbackLoop;
        entry.groundIdleIndex = fallbackLoop;
        entry.airIdleIndex = fallbackLoop;

        nlohmann::json animSetJson;
        if (AnimSet::loadAnimSetJson(AnimSet::animSetPathFromModelPath(modelPath), animSetJson)) {
            const auto idlePick = AnimSet::resolveRoleClip(
                animSetJson, "idle", "idle", {"battlewait", "defaultwait", "idle", "wait"}, true);
            const auto movePick = AnimSet::resolveRoleClip(
                animSetJson, "move", "move", {"run", "dash", "move"}, true);

            auto attackPick = AnimSet::resolveRoleClip(
                animSetJson, "attack1", "attack", {"attack01", "attack1", "attack"}, true);
            if (!attackPick.valid || attackPick.clipName.empty()) {
                attackPick =
                    AnimSet::resolveRoleClip(animSetJson, "attack1", "misc", {"buturi", "ba20_buturi", "ba20"}, false);
            }

            auto faintPick = AnimSet::resolveRoleClip(
                animSetJson, "faint", "status", {"down01_start", "down_start", "down01", "down"}, true);
            if (!faintPick.valid || faintPick.clipName.empty()) {
                faintPick = AnimSet::resolveRoleClip(
                    animSetJson, "down", "status", {"down01_start", "down_start", "down01", "down"}, true);
            }

            const auto groundIdlePick = AnimSet::resolveRoleClip(
                animSetJson, "ground_idle", "idle", {"ba10_wait", "battlewait", "ba10", "wait", "idle"}, true);
            const auto airIdlePick = AnimSet::resolveRoleClip(
                animSetJson, "air_idle", "idle", {"fi01_wait", "fly", "air", "hover"}, true);
            const auto takeoffPick = AnimSet::resolveRoleClip(
                animSetJson, "takeoff", "misc", {"take_flight", "takeflight", "takeoff"}, false);
            const auto landPick = AnimSet::resolveRoleClip(
                animSetJson, "land", "misc", {"land"}, false);
            const auto landAPick = AnimSet::resolveRoleClip(
                animSetJson, "land_a", "misc", {"landa"}, false);
            const auto landBPick = AnimSet::resolveRoleClip(
                animSetJson, "land_b", "misc", {"landb"}, false);
            const auto landCPick = AnimSet::resolveRoleClip(
                animSetJson, "land_c", "misc", {"landc"}, false);

            auto resolvePick = [&](const AnimSet::RolePick& pick) -> int {
                if (!pick.valid || pick.clipName.empty()) return -1;
                return resolveBackendAnimIndexByName(mesh->animations, pick.clipName);
            };

            const int idleIdx = resolvePick(idlePick);
            if (idleIdx >= 0) entry.idleIndex = idleIdx;

            const int moveIdx = resolvePick(movePick);
            if (moveIdx >= 0) entry.moveIndex = moveIdx;

            const int attackIdx = resolvePick(attackPick);
            if (attackIdx >= 0) {
                entry.attackIndex = attackIdx;
                entry.attackDurationSec = attackPick.durationSec;
            }

            const int faintIdx = resolvePick(faintPick);
            if (faintIdx >= 0) {
                entry.faintIndex = faintIdx;
                entry.faintDurationSec = faintPick.durationSec;
            }

            const int groundIdleIdx = resolvePick(groundIdlePick);
            if (groundIdleIdx >= 0) entry.groundIdleIndex = groundIdleIdx;
            const int airIdleIdx = resolvePick(airIdlePick);
            if (airIdleIdx >= 0) entry.airIdleIndex = airIdleIdx;
            entry.takeoffIndex = resolvePick(takeoffPick);
            entry.landIndex = resolvePick(landPick);
            entry.landAIndex = resolvePick(landAPick);
            entry.landBIndex = resolvePick(landBPick);
            entry.landCIndex = resolvePick(landCPick);

            if (animSetJson.contains("meta") && animSetJson["meta"].is_object()) {
                const auto& meta = animSetJson["meta"];
                if (meta.contains("movementMode") && meta["movementMode"].is_string()) {
                    const std::string mode = toLowerCopy(meta["movementMode"].get<std::string>());
                    entry.usesAirLocomotion =
                        (mode == "airborne" || mode == "air" || mode == "flying" || mode == "fly");
                }
                if (meta.contains("airLiftY") && meta["airLiftY"].is_number()) {
                    entry.airLiftY = meta["airLiftY"].get<float>();
                }
                if (meta.contains("takeoffSec") && meta["takeoffSec"].is_number()) {
                    entry.takeoffSec = meta["takeoffSec"].get<float>();
                }
                if (meta.contains("landingSec") && meta["landingSec"].is_number()) {
                    entry.landingSec = meta["landingSec"].get<float>();
                }
            }
        }

        if (entry.idleIndex < 0) {
            entry.idleIndex = findBackendAnimIndexBySubstring(mesh->animations, {"wait", "idle", "ba10"});
        }
        if (entry.moveIndex < 0) {
            entry.moveIndex = findBackendAnimIndexBySubstring(mesh->animations, {"move", "run", "walk", "fly"});
        }
        if (entry.attackIndex < 0) {
            entry.attackIndex =
                findBackendAnimIndexBySubstring(mesh->animations, {"attack", "ba20", "buturi", "strike"});
        }
        if (entry.faintIndex < 0) {
            entry.faintIndex = findBackendAnimIndexBySubstring(mesh->animations, {"down", "faint", "death", "ko"});
        }

        if (entry.groundIdleIndex < 0) entry.groundIdleIndex = entry.idleIndex;
        if (entry.airIdleIndex < 0) entry.airIdleIndex = entry.idleIndex;

        if (entry.attackDurationSec <= 0.0f &&
            entry.attackIndex >= 0 &&
            static_cast<std::size_t>(entry.attackIndex) < mesh->animations.size()) {
            entry.attackDurationSec = mesh->animations[static_cast<std::size_t>(entry.attackIndex)].durationSec;
        }
        if (entry.faintDurationSec <= 0.0f &&
            entry.faintIndex >= 0 &&
            static_cast<std::size_t>(entry.faintIndex) < mesh->animations.size()) {
            entry.faintDurationSec = mesh->animations[static_cast<std::size_t>(entry.faintIndex)].durationSec;
        }

        const bool hasTakeoff = entry.takeoffIndex >= 0;
        const bool hasSeqLanding = (entry.landCIndex >= 0) && (entry.landAIndex >= 0 || entry.landBIndex >= 0);
        const bool hasSingleLanding = entry.landIndex >= 0;
        const bool hasDistinctLand =
            hasSeqLanding || (hasTakeoff && hasSingleLanding && entry.takeoffIndex != entry.landIndex);
        if (hasTakeoff && hasDistinctLand) {
            entry.usesAirLocomotion = true;
        }

        return entry;
    }

    void hydrateBackendUnitAnimationAndScale() {
        if (!usesBackendGameRenderPath() || !gameWorld) return;

        auto hydrate = [&](PokemonInstance& unit) {
            const PokemonStats* stats = dataDb.pokemon.getStats(unit.name);
            if (!stats || stats->model.empty()) return;

            const std::string modelPath = "assets/models/" + stats->model;
            runtime::backend_model::MeshData* mesh = ensureBackendMeshLoaded(modelPath);
            if (!mesh) return;

            if (!unit.model) {
                if (unit.animIndexCacheSourceModelPath != modelPath) {
                    unit.animIndexCache.clear();
                    unit.animIndexCacheSourceModelPath = modelPath;
                }
                if (unit.backendAnimDurationsSourceModelPath != modelPath ||
                    unit.backendAnimDurationsSec.size() != mesh->animations.size()) {
                    unit.backendAnimDurationsSec.assign(mesh->animations.size(), 0.0f);
                    for (std::size_t i = 0; i < mesh->animations.size(); ++i) {
                        unit.backendAnimDurationsSec[i] =
                            std::max(0.0f, mesh->animations[i].durationSec);
                    }
                    unit.backendAnimDurationsSourceModelPath = modelPath;
                }

                const auto cacheAlias = [&](const std::string& clipName, int idx) {
                    if (clipName.empty() || idx < 0) return;
                    unit.animIndexCache[clipName] = idx;
                    unit.animIndexCache[toLowerCopy(clipName)] = idx;
                };

                for (std::size_t i = 0; i < mesh->animations.size(); ++i) {
                    const int idx = static_cast<int>(i);
                    const std::string& raw = mesh->animations[i].name;
                    cacheAlias(raw, idx);
                    const std::string noGfbanm = stripSuffix(raw, ".gfbanm");
                    cacheAlias(noGfbanm, idx);
                    const std::string noStart = stripSuffix(raw, "__START");
                    cacheAlias(noStart, idx);
                    const std::string noEnd = stripSuffix(raw, "__END");
                    cacheAlias(noEnd, idx);
                    std::string compact = stripSuffix(noGfbanm, "__START");
                    compact = stripSuffix(compact, "__END");
                    cacheAlias(compact, idx);
                }
            } else {
                unit.animIndexCacheSourceModelPath.clear();
                unit.backendAnimDurationsSourceModelPath.clear();
                unit.backendAnimDurationsSec.clear();
            }

            BackendAnimRoleEntry& roles = ensureBackendAnimRoles(modelPath, mesh);

            if (unit.animIdleIndex < 0) unit.animIdleIndex = roles.idleIndex;
            if (unit.animMoveIndex < 0) unit.animMoveIndex = roles.moveIndex;
            if (unit.animAttack1Index < 0) unit.animAttack1Index = roles.attackIndex;
            if (unit.animFaintIndex < 0) unit.animFaintIndex = roles.faintIndex;
            if (unit.animGroundIdleIndex < 0) unit.animGroundIdleIndex = roles.groundIdleIndex;
            if (unit.animAirIdleIndex < 0) unit.animAirIdleIndex = roles.airIdleIndex;
            if (unit.animTakeoffIndex < 0) unit.animTakeoffIndex = roles.takeoffIndex;
            if (unit.animLandIndex < 0) unit.animLandIndex = roles.landIndex;
            if (unit.animLandAIndex < 0) unit.animLandAIndex = roles.landAIndex;
            if (unit.animLandBIndex < 0) unit.animLandBIndex = roles.landBIndex;
            if (unit.animLandCIndex < 0) unit.animLandCIndex = roles.landCIndex;

            if (unit.attackDurationSec <= 0.0f && roles.attackDurationSec > 0.0f) {
                unit.attackDurationSec = roles.attackDurationSec;
            }
            if (unit.faintAnimDurationSec <= 0.0f && roles.faintDurationSec > 0.0f) {
                unit.faintAnimDurationSec = roles.faintDurationSec;
            }

            const bool speciesListedFlyer = dataDb.flyers.isFlyer(unit.name);
            if ((roles.usesAirLocomotion || speciesListedFlyer) && !unit.usesAirLocomotion) {
                unit.usesAirLocomotion = true;
            }
            if (unit.usesAirLocomotion) {
                if (unit.airLiftY <= 0.0f && roles.airLiftY > 0.0f) unit.airLiftY = roles.airLiftY;
                if (unit.takeoffSec <= 0.0f && roles.takeoffSec > 0.0f) unit.takeoffSec = roles.takeoffSec;
                if (unit.landingSec <= 0.0f && roles.landingSec > 0.0f) unit.landingSec = roles.landingSec;
                if (const auto* d = dataDb.flyers.getAirLocomotionDefaults(unit.name)) {
                    if (unit.airLiftY <= 0.0f && d->airLiftY.has_value()) {
                        unit.airLiftY = *d->airLiftY;
                    }
                    if (unit.takeoffSec <= 0.0f && d->takeoffSec.has_value()) {
                        unit.takeoffSec = *d->takeoffSec;
                    }
                    if (unit.landingSec <= 0.0f && d->landingSec.has_value()) {
                        unit.landingSec = *d->landingSec;
                    }
                    if (d->takeoffAnimSpeed.has_value()) {
                        unit.takeoffAnimSpeed = *d->takeoffAnimSpeed;
                    }
                    if (d->landAnimSpeed.has_value()) {
                        unit.landAnimSpeed = *d->landAnimSpeed;
                    }
                }
            }

            if (unit.activeAnimIndex < 0) {
                unit.activeAnimIndex = unit.isMoving ? unit.animMoveIndex : unit.animIdleIndex;
            }
            if (unit.activeAnimIndex < 0 && !mesh->animations.empty()) {
                unit.activeAnimIndex = 0;
            }
            if (unit.currentAttackAnimIndex < 0) {
                unit.currentAttackAnimIndex = unit.animAttack1Index;
            }

            const std::string scaleMode = toLowerCopy(stats->modelScaleMode);
            if (!unit.model && scaleMode != "normalized") {
                const float importerScale = std::max(0.0f, mesh->modelScaleFactor);
                if (importerScale > 1e-6f) {
                    unit.modelScaleCorrection = 1.0f / importerScale;
                }
            }
        };

        for (auto& unit : gameWorld->getPokemons()) {
            hydrate(unit);
        }
        for (auto& unit : gameWorld->getBenchPokemons()) {
            hydrate(unit);
        }
    }

    void init(GameContext& ctx) {
        camera = ctx.camera;
        renderer = ctx.renderer;
        engineServices = ctx.services;
        const bool hasBackend = (ctx.renderer != nullptr) && (ctx.camera != nullptr);
        startupRoutes = runtime::render::selectStartupRenderRoutes(hasBackend);
        if (engine::env::get("PAC_BACKEND_MENU_BACKDROP").has_value()) {
            allowBackendMenuBackdrop = engine::env::flagEnabled("PAC_BACKEND_MENU_BACKDROP");
        }
        if (engine::env::get("PAC_SHOW_PERF_OVERLAY").has_value()) {
            showPerfOverlay = engine::env::flagEnabled("PAC_SHOW_PERF_OVERLAY");
        }
        viewport.set(ctx.drawableW, ctx.drawableH);

        const std::string packPath = engine::paths::dataPack();
        if (!packPath.empty()) {
            auto pack = std::make_unique<assets::PackedAssetStore>();
            std::string err;
            if (pack->open(packPath, &err)) {
                assetStore = std::move(pack);
                game::log::info(&log, std::string("[Init] Using packed data bundle: ") + packPath);
            } else {
                game::log::warn(&log, std::string("[Init] Failed to open pack: ") + packPath +
                    (err.empty() ? "" : (" (" + err + ")")));
            }
        }
        if (!assetStore) {
            auto dev = std::make_unique<assets::DevAssetStore>(engine::paths::dataRoot());
            assetStore = std::move(dev);
        }

        {
            std::uint32_t seed = 0;
            bool hasSeed = false;
            if (const auto v = engine::env::get("PAC_RANDOM_SEED")) {
                try {
                    seed = static_cast<std::uint32_t>(std::stoul(*v));
                    hasSeed = true;
                } catch (...) {
                    game::log::warn(&log, std::string("[Init] Invalid PAC_RANDOM_SEED value: ") + *v);
                }
            }
            if (!hasSeed) {
                std::random_device rd;
                seed = (static_cast<std::uint32_t>(rd()) << 16) ^ static_cast<std::uint32_t>(rd());
            }
            rng.reseed(seed);
            game::log::info(&log, std::string("[Init] RNG seed: ") + std::to_string(seed));
        }

        roundPhaseEntity = ecsWorld.create();
        ecsWorld.add<game::CombatActive>(roundPhaseEntity, game::CombatActive{false});

        config = GameConfig::load(&log, assetStore.get());
        services = std::make_unique<GameServices>(config, dataDb, log, scriptEvents, *assetStore, rng, timeSource,
                                                  &ecsWorld, roundPhaseEntity, &viewport, startupRoutes.hasRenderer);
        services->renderer = renderer;
        services->applyVideoMode = ctx.applyVideoMode;
        services->requestQuit = ctx.requestQuit;
        if (ctx.services) {
            services->requestedRendererBackend = ctx.services->requestedRendererBackend;
            services->activeRendererBackend = ctx.services->activeRendererBackend;
            services->rendererBackendFallback = ctx.services->rendererBackendFallback;
            services->gpuVendor = ctx.services->gpuVendor;
            services->gpuRenderer = ctx.services->gpuRenderer;
            services->availableGpuAdapters = ctx.services->availableGpuAdapters;
            services->preferredGpuAdapter = ctx.services->preferredGpuAdapter;
            services->gpuDiscrete = ctx.services->gpuDiscrete;
            services->requireDiscreteGpu = ctx.services->requireDiscreteGpu;
            services->bootMenuScreen = ctx.services->bootMenuScreen;
        }
        if (ctx.queryVideoMode) {
            services->queryVideoMode = [q = ctx.queryVideoMode]() {
                auto vm = q();
                GameServices::VideoMode out;
                out.width = vm.width;
                out.height = vm.height;
                out.fullscreen = vm.fullscreen;
                return out;
            };
        }
        coreServices.rng = &services->rng;
        coreServices.time = &services->time;

        // World
        gameWorld = std::make_unique<GameWorld>(config);
        gameWorld->setRenderEnabled(hasActiveRenderBackend());
        gameWorld->setLogger(&log);
        gameWorld->setRng(&services->rng);
        if (ctx.services) gameWorld->setResources(ctx.services->resources);
        gameWorld->setData(&dataDb);

        // State stack
        stateManager = std::make_unique<GameStateManager>();

        // Systems
        if (camera) {
            cameraSystem = std::make_shared<CameraSystem>(camera, *services);
            unitSystem   = std::make_shared<UnitInteractionSystem>(camera, gameWorld.get(), ctx.drawableW, ctx.drawableH);
        }
        using Phase = engine::ecs::Scheduler::Phase;

        if (cameraSystem) scheduler.add(std::make_unique<game::UpdatableSystemAdapter>(cameraSystem.get()), Phase::Update);
        if (unitSystem)   scheduler.add(std::make_unique<game::UpdatableSystemAdapter>(unitSystem.get()), Phase::Update);
        auto shopSystemImpl = std::make_unique<ShopSystem>(services->rng);
        shopSystem = shopSystemImpl.get();
        scheduler.add(std::move(shopSystemImpl), Phase::Update);

        auto roundSystem = std::make_unique<RoundSystem>(*services, roundPhaseEntity);
        ecsWorld.add<game::RoundState>(roundPhaseEntity, game::RoundState{ roundSystem->getCurrentPhase() });
        scheduler.add(std::move(roundSystem), Phase::Update);

        if (auto* stateMgr = stateManager.get()) {
            scheduler.add(std::make_unique<game::CallbackSystemAdapter>(
                [stateMgr](float dt) { stateMgr->update(dt); }
            ), Phase::PostUpdate);
        }
        if (auto* worldPtr = gameWorld.get()) {
            auto movementSystem = std::make_unique<MovementSystem>(worldPtr, *services, roundPhaseEntity);
            scheduler.add(std::move(movementSystem), Phase::PostUpdate);

            auto combatSystem = std::make_unique<CombatSystem>(worldPtr, *services, roundPhaseEntity);
            scheduler.add(std::move(combatSystem), Phase::PostUpdate);
        }
        if (auto* worldPtr = gameWorld.get()) {
            scheduler.add(std::make_unique<game::CallbackSystemAdapter>(
                [worldPtr](float dt) { worldPtr->update(dt); }
            ), Phase::PostUpdate);
        }

        updateGraph.configure({
            &scheduler,
            &ecsWorld,
            roundPhaseEntity,
            shopSystem,
            &log,
            &scriptEvents
        });

        std::cout << "[Init] Shared gameplay render path: using backend model cache loader.\n";
        if (backendPreloadModelCacheEnabled()) {
            std::cout << "[Init] Shared gameplay render path: preloading backend model cache...\n";
            const bool verboseModelCacheLog = backendModelVerboseLoggingEnabled();
            std::vector<std::string> modelPathsToPreload;
            modelPathsToPreload.reserve(dataDb.pokemon.all().size());
            std::unordered_set<std::string> seenModelPaths;
            seenModelPaths.reserve(dataDb.pokemon.all().size());
            for (const auto& [name, stats] : dataDb.pokemon.all()) {
                (void)name;
                if (stats.model.empty()) continue;
                const std::string modelPath = "assets/models/" + stats.model;
                if (seenModelPaths.insert(modelPath).second) {
                    modelPathsToPreload.push_back(modelPath);
                }
            }
            // Shared capture uses pokeball.glb as well; preload it by default so first-use
            // capture interactions avoid cache/rebuild cost in active gameplay.
            if (!engine::env::equals("PAC_BACKEND_PRELOAD_CAPTURE_POKEBALL", "0")) {
                const std::string sharedCapturePokeballPath = "assets/models/pokeball.glb";
                if (seenModelPaths.insert(sharedCapturePokeballPath).second) {
                    modelPathsToPreload.push_back(sharedCapturePokeballPath);
                }
            }

            if (ctx.setTitle) ctx.setTitle("PokemonAutochess - Loading.");
            if (ctx.renderBootLoading) ctx.renderBootLoading(0.0f);
            bool preloadInterrupted = false;
            if (ctx.pumpPreloadEvents && !ctx.pumpPreloadEvents()) {
                if (ctx.requestQuit) ctx.requestQuit();
                preloadInterrupted = true;
            }

            std::size_t loaded = 0u;
            std::size_t failed = 0u;
            std::vector<std::string> failedSamples;
            failedSamples.reserve(8);
            const std::size_t totalModels = modelPathsToPreload.size();
            for (std::size_t i = 0; i < totalModels; ++i) {
                const std::string& modelPath = modelPathsToPreload[i];
                if (ctx.setTitle) {
                    ctx.setTitle(
                        std::string("PokemonAutochess - Loading ") +
                        std::to_string(i + 1u) + "/" + std::to_string(totalModels) + "  " + modelPath);
                }
                if (ctx.pumpPreloadEvents && !ctx.pumpPreloadEvents()) {
                    if (ctx.requestQuit) ctx.requestQuit();
                    preloadInterrupted = true;
                    break;
                }

                auto& cacheEntry = backendMeshByModelPath[modelPath];
                if (cacheEntry.attemptedLoad) {
                    const float progress = totalModels > 0u
                        ? static_cast<float>(i + 1u) / static_cast<float>(totalModels)
                        : 1.0f;
                    if (ctx.renderBootLoading) ctx.renderBootLoading(progress);
                    continue;
                }
                cacheEntry.attemptedLoad = true;
                std::string err;
                if (!runtime::backend_model::loadMeshFromCache(modelPath, cacheEntry.mesh, &err)) {
                    cacheEntry.error = std::move(err);
                    cacheEntry.mesh = {};
                    ++failed;
                    if (failedSamples.size() < 8u) {
                        failedSamples.push_back(modelPath + " (" + cacheEntry.error + ")");
                    }
                    if (verboseModelCacheLog) {
                        std::cout << "[Init][ModelCache][MISS] " << modelPath
                                  << " reason=" << cacheEntry.error << "\n";
                    }
                } else {
                    ++loaded;
                    if (verboseModelCacheLog) {
                        std::cout << "[Init][ModelCache][OK] " << modelPath
                                  << " vtx=" << cacheEntry.mesh.vertices.size()
                                  << " idx=" << cacheEntry.mesh.indices.size()
                                  << " submesh=" << cacheEntry.mesh.submeshBaseTextures.size() << "\n";
                    }
                }
                const float progress = totalModels > 0u
                    ? static_cast<float>(i + 1u) / static_cast<float>(totalModels)
                    : 1.0f;
                if (ctx.renderBootLoading) ctx.renderBootLoading(progress);
            }
            std::cout << "[Init] Backend model cache preload complete: loaded=" << loaded
                      << " failed=" << failed << "\n";
            if (failed > 0u && !failedSamples.empty() && !verboseModelCacheLog) {
                std::cout << "[Init][ModelCache] Sample failures:\n";
                for (const std::string& item : failedSamples) {
                    std::cout << "  - " << item << "\n";
                }
                std::cout << "[Init][ModelCache] Set PAC_BACKEND_MODEL_VERBOSE=1 for full per-model cache logs.\n";
            }
            if (preloadInterrupted) {
                std::cout << "[Init][ModelCache] Preload interrupted by window close or quit request.\n";
            }
            if (ctx.setTitle) ctx.setTitle("Pokemon Autochess");
            if (ctx.pumpPreloadEvents) ctx.pumpPreloadEvents();
        } else {
            std::cout << "[Init] Shared gameplay render path: backend model cache preload disabled.\n";
        }

        // OpenGL shared route renders capture pokeball via the OpenGL Model path (ResourceManager),
        // not the backend mesh cache. Prewarm up front to avoid first-use hitching.
        if (usesBackendGameRenderPath() &&
            renderer &&
            renderer->backendId() &&
            std::string(renderer->backendId()) == "opengl" &&
            engineServices &&
            engineServices->resources) {
            (void)engineServices->resources->getModel("assets/models/pokeball.glb");
        }

        stateManager->pushState(std::make_unique<ScriptedState>(
            stateManager.get(),
            gameWorld.get(),
            *services,
            engine::paths::data("scripts/states/main_menu.lua")
        ));

        if (ctx.setTitle) ctx.setTitle("Pokemon Autochess");
        std::cout << "[Init] Game initialized.\n";
    }

    bool selectBackendInventoryItem(const std::string& itemId) {
        if (!gameWorld || itemId.empty()) return false;
        if (gameWorld->getSelectedItem() == itemId) return true;
        gameWorld->setSelectedItem(itemId);
        log.catchInfo("Selected " + runtime::hud::humanizeToken(itemId) + ". Click a target.");
        return true;
    }

    bool clearBackendInventorySelection() {
        if (!gameWorld) return false;
        if (gameWorld->getSelectedItem().empty()) return false;
        gameWorld->setSelectedItem("");
        log.catchInfo("Cleared selected item.");
        return true;
    }

    void refreshBackendInventoryFromWorld() {
        if (!gameWorld) {
            backendInventoryPanel = {};
            return;
        }

        runtime::backend_inventory_panel::refreshPanelState(
            backendInventoryPanel,
            gameWorld->listItems(),
            kBackendInventoryVisibleCount,
            gameWorld->getSelectedItem());
    }

    bool applyBackendInventoryOffsetDelta(int delta) {
        if (delta == 0 || !gameWorld) return false;

        refreshBackendInventoryFromWorld();
        return runtime::backend_inventory_panel::applyOffsetDelta(
            backendInventoryPanel,
            delta,
            kBackendInventoryVisibleCount,
            gameWorld->getSelectedItem());
    }

    bool handleBackendInventoryUiInput(const InputEvent& event) {
        if (event.type == InputEvent::Type::KeyDown && !event.repeat) {
            const int offsetDelta = runtime::backend_input::inventoryOffsetDeltaFromKey(
                event.keyId,
                static_cast<int>(kBackendInventoryVisibleCount));
            if (applyBackendInventoryOffsetDelta(offsetDelta)) {
                return true; // consume nav key when inventory paging changed.
            }

            refreshBackendInventoryFromWorld();
            const int slot = runtime::backend_input::slotFromNumberKey(event.keyId);
            const auto itemId = runtime::backend_inventory_panel::visibleItemForSlot(
                backendInventoryPanel,
                slot);
            if (itemId && selectBackendInventoryItem(*itemId)) {
                return true; // consume key to avoid accidental board interactions.
            }
            return false;
        }

        if (event.type == InputEvent::Type::MouseWheel) {
            const int wheelDelta = runtime::backend_inventory_panel::offsetDeltaFromWheel(event.wheelY);
            return applyBackendInventoryOffsetDelta(wheelDelta);
        }

        if (event.type != InputEvent::Type::MouseDown ||
            event.mouseButtonId != InputEvent::MouseButton::Left) {
            return false;
        }

        const float mx = static_cast<float>(event.mouseX);
        const float my = static_cast<float>(event.mouseY);
        const auto* hit = runtime::backend_inventory_panel::findHit(backendInventoryPanel, mx, my);
        if (!hit) return false;

        if (hit->action == runtime::backend_inventory_panel::HitAction::ClearSelection) {
            clearBackendInventorySelection();
            return true;
        }
        if (hit->action == runtime::backend_inventory_panel::HitAction::ScrollOffset) {
            applyBackendInventoryOffsetDelta(hit->offsetDelta);
            return true;
        }
        if (selectBackendInventoryItem(hit->itemId)) {
            return true;
        }
        return false;
    }

    void handleEvent(const InputEvent& event) {
        if (event.type == InputEvent::Type::Resize) {
            viewport.set(event.drawableW, event.drawableH);
            if (unitSystem) {
                unitSystem->setScreenSize(
                    static_cast<unsigned int>(std::max(1, event.drawableW)),
                    static_cast<unsigned int>(std::max(1, event.drawableH)));
            }
        }

        if (event.type == InputEvent::Type::KeyDown && !event.repeat) {
            if (event.keyId == InputEvent::Key::P) {
                devPauseWorld = !devPauseWorld;
                devPauseStepTicks = 0;
                game::log::info(
                    &log,
                    devPauseWorld
                        ? "[DevPause] ON (P resumes, O steps one frame)"
                        : "[DevPause] OFF");
                return;
            }
            if (event.keyId == InputEvent::Key::O && devPauseWorld) {
                devPauseStepTicks = 1;
                game::log::info(&log, "[DevPause] Step 1 frame");
                return;
            }
        }

        bool renderWorldForInput = true;
        if (stateManager) {
            if (auto* state = stateManager->getCurrentState()) {
                renderWorldForInput = state->shouldRenderWorld();
            }
        }

        if (event.type == InputEvent::Type::KeyDown &&
            event.keyId == InputEvent::Key::Escape &&
            !event.repeat) {
            if (renderWorldForInput && stateManager) {
                stateManager->pushState(std::make_unique<ScriptedState>(
                    stateManager.get(),
                    gameWorld.get(),
                    *services,
                    engine::paths::data("scripts/states/main_menu.lua")
                ));
                return;
            }
        }

        if (renderWorldForInput &&
            event.type == InputEvent::Type::KeyDown &&
            !event.repeat &&
            runtime::backend_input::isClearSelectionKey(event.keyId)) {
            if (clearBackendInventorySelection()) {
                return; // consume key so gameplay actions do not fire simultaneously.
            }
        }

        if (renderWorldForInput && usesBackendGameUiPath()) {
            if (handleBackendInventoryUiInput(event)) {
                return;
            }
        }
        if (renderWorldForInput && cameraSystem) cameraSystem->handleInput(event);
        if (renderWorldForInput && unitSystem)   unitSystem->handleInput(event);
        if (stateManager) stateManager->handleInput(event);
    }

    void fixedUpdate(float dt) {
        if (devPauseWorld && devPauseStepTicks <= 0) {
            return;
        }
        timeSource.advance(dt);
        if (usesBackendGameRenderPath()) {
            hydrateBackendUnitAnimationAndScale();
        }
        updateGraph.tick(dt);
        if (devPauseWorld && devPauseStepTicks > 0) {
            --devPauseStepTicks;
        }
    }

    void renderBackendDebugView(int drawableW, int drawableH, bool renderWorld) {
        if (!renderer || drawableW <= 0 || drawableH <= 0) return;
        const bool useLegacyGrowlWaveVfx = backendUseLegacyGrowlWaveVfxEnabled();
        const bool useLegacyParticleVfxSnapshotBridge = backendUseLegacyParticleVfxSnapshotBridgeEnabled();

        runtime::backend_inventory_panel::clearHitRegions(backendInventoryPanel);

        using WorldIndexedBatch = runtime::shared_world_batches::WorldIndexedBatch;
        struct BackendUnitLabel {
            float x = 0.0f;
            float y = 0.0f;
            std::string text;
            glm::vec3 color{1.0f, 1.0f, 1.0f};
        };
        struct BackendRenderScratch {
            std::vector<IRenderBackend::DebugQuad> worldBackgroundQuads;
            std::vector<IRenderBackend::DebugQuad> worldQuads;
            std::vector<IRenderBackend::DebugTriangle> worldTriangles;
            std::vector<IRenderBackend::WorldTriangle> world3DTriangles;
            std::vector<WorldIndexedBatch> worldIndexedBatches;
            std::vector<IRenderBackend::DebugQuad> overlayQuads;
            std::vector<IRenderBackend::DebugLine> lines;
            std::vector<IRenderBackend::DebugLine> textLines;
            std::vector<IRenderBackend::DebugSprite> sprites;
            std::vector<BackendUnitLabel> unitLabels;
        };
        static thread_local BackendRenderScratch scratch;

        auto& worldBackgroundQuads = scratch.worldBackgroundQuads;
        auto& worldQuads = scratch.worldQuads;
        auto& worldTriangles = scratch.worldTriangles;
        auto& world3DTriangles = scratch.world3DTriangles;
        auto& worldIndexedBatches = scratch.worldIndexedBatches;
        auto& overlayQuads = scratch.overlayQuads;
        auto& lines = scratch.lines;
        auto& textLines = scratch.textLines;
        auto& sprites = scratch.sprites;
        auto& unitLabels = scratch.unitLabels;

        worldBackgroundQuads.clear();
        worldQuads.clear();
        worldTriangles.clear();
        world3DTriangles.clear();
        worldIndexedBatches.clear();
        overlayQuads.clear();
        lines.clear();
        textLines.clear();
        sprites.clear();
        unitLabels.clear();

        if (worldBackgroundQuads.capacity() < 1024u) worldBackgroundQuads.reserve(1024u);
        if (worldQuads.capacity() < 1024u) worldQuads.reserve(1024u);
        if (worldTriangles.capacity() < 4096u) worldTriangles.reserve(4096u);
        if (world3DTriangles.capacity() < 120000u) world3DTriangles.reserve(120000u);
        if (worldIndexedBatches.capacity() < 64u) worldIndexedBatches.reserve(64u);
        if (overlayQuads.capacity() < 1024u) overlayQuads.reserve(1024u);
        if (lines.capacity() < 512u) lines.reserve(512u);
        if (textLines.capacity() < 8192u) textLines.reserve(8192u);
        if (sprites.capacity() < 256u) sprites.reserve(256u);
        if (unitLabels.capacity() < 64u) unitLabels.reserve(64u);
        std::uint32_t visibleAnimatedUnitsThisFrame = 0u;
        std::uint32_t particleCountThisFrame = 0u;
        float projectedUnitsMsThisFrame = 0.0f;
        float projectedPoseEvalMsThisFrame = 0.0f;
        float projectedModelMsThisFrame = 0.0f;
        float projectedOverlayMsThisFrame = 0.0f;
        std::uint32_t projectedUnitsProcessedThisFrame = 0u;
        std::uint32_t projectedModelUnitsThisFrame = 0u;
        std::uint32_t projectedClipSkinnedUnitsThisFrame = 0u;

        const bool supportsWorldTriangles3D = renderer->supportsWorldTriangles3D();
        const bool supportsWorldIndexedMeshes = renderer->supportsWorldIndexedMeshes();
        const bool allowPortraitFallback = backendWorldPortraitFallbackEnabled();
        const bool forcePortraitOverlay = backendWorldPortraitOverlayForced();
        float worldViewProj[16] = {};
        bool hasWorldViewProj = false;
        const runtime::shared_unit_hud::Config sharedUnitHudCfg{
            config.xpLevelBase,
            config.xpLevelGrowth};

        const int rows = std::max(1, config.rows);
        const int cols = std::max(1, config.cols);
        const float minDim = static_cast<float>(std::min(drawableW, drawableH));
        const float uiScale = runtime::backend_ui::viewportScale(drawableW, drawableH);
        const float edgePad = runtime::backend_ui::edgePad(drawableW, drawableH);
        const float lineStep = runtime::backend_ui::lineStep(drawableW, drawableH);
        const float boardW = std::max(240.0f, minDim * 0.78f);
        const float boardH = std::max(180.0f, minDim * 0.58f);
        const float boardX = (static_cast<float>(drawableW) - boardW) * 0.5f;
        const float boardY = (static_cast<float>(drawableH) - boardH) * 0.5f;
        const float cellW = boardW / static_cast<float>(cols);
        const float cellH = boardH / static_cast<float>(rows);

        const runtime::render::RenderRoutes routes = activeRenderRoutes();
        const bool showWorldBackdrop = runtime::render::shouldRenderBackendWorldBackdrop(
            routes,
            renderWorld,
            allowBackendMenuBackdrop);
        if (showWorldBackdrop) {
            const bool useProjectedWorldLayout = renderWorld && gameWorld && (camera != nullptr);
            if (useProjectedWorldLayout) {
                const float worldCellSize = std::max(0.05f, gameWorld->getBoardCellSize());
                const runtime::backendview::BoardBounds boardBounds =
                    runtime::backendview::computeBoardBounds(cols, rows, worldCellSize);
                const float boardMinX = boardBounds.minX;
                const float boardMinZ = boardBounds.minZ;
                const float boardMaxX = boardBounds.maxX;
                const float boardMaxZ = boardBounds.maxZ;

                const glm::mat4 view = camera->getViewMatrix();
                const glm::mat4 proj = camera->getProjectionMatrix();
                const glm::mat4 viewProj = proj * view;
                const glm::mat4 invViewProj = glm::inverse(viewProj);
                if (supportsWorldTriangles3D) {
                    const float* vp = glm::value_ptr(viewProj);
                    std::copy(vp, vp + 16, worldViewProj);
                    hasWorldViewProj = true;
                }
                const glm::mat4 invView = glm::inverse(view);
                glm::vec3 cameraWorldPos(invView[3].x, invView[3].y, invView[3].z);
                if (!std::isfinite(cameraWorldPos.x) ||
                    !std::isfinite(cameraWorldPos.y) ||
                    !std::isfinite(cameraWorldPos.z)) {
                    cameraWorldPos = glm::vec3(0.0f, 6.0f, -6.0f);
                }
                const glm::vec4 screenViewport(
                    0.0f,
                    0.0f,
                    static_cast<float>(drawableW),
                    static_cast<float>(drawableH));
                const float line = std::max(1.0f, minDim * 0.0019f);

                game::runtime::shared_projected_debug::ProjectedDebugVfxBuilder projectedDebug(
                    supportsWorldTriangles3D,
                    view,
                    proj,
                    drawableH,
                    screenViewport,
                    worldTriangles,
                    world3DTriangles,
                    lines);
                using SharedTailFireAnchor = game::runtime::shared_tail_fire_fallback::Anchor;
                std::unordered_map<int, SharedTailFireAnchor> sharedTailFireAnchors;
                sharedTailFireAnchors.reserve(16u);
                using DepthTri = game::runtime::shared_projected_scene::DepthTri;
                using DepthWorldTri = game::runtime::shared_projected_scene::DepthWorldTri;
                auto modelDepthBuffers =
                    game::runtime::shared_projected_scene::acquireModelDepthBuffers(12000u);
                auto& modelDepthTris = modelDepthBuffers.modelDepthTris;
                auto& modelDepthWorldTris = modelDepthBuffers.modelDepthWorldTris;
                std::size_t remainingModelTrianglesBudget = backendModelTriangleFrameBudget();
                runtime::shared_projected_units::PerfStats projectedUnitPerf{};

                {
                    const game::runtime::shared_board_grid::Config boardGridCfg =
                        game::runtime::shared_projected_scene::makeBoardGridConfig(
                            supportsWorldTriangles3D,
                            rows,
                            cols,
                            config.benchSlots,
                            worldCellSize,
                            boardMinX,
                            boardMinZ,
                            boardMaxX,
                            boardMaxZ,
                            boardX,
                            boardY,
                            boardW,
                            boardH,
                            cellW,
                            cellH,
                            line);
                    game::runtime::shared_projected_scene::appendBoardAndBench(
                        boardGridCfg,
                        worldTriangles,
                        world3DTriangles,
                        worldBackgroundQuads,
                        lines,
                        projectedDebug);
                }
                const float boardSurfaceY = 0.006f;

                using BackendPoseEval = game::runtime::shared_backend_pose::PoseEval;

                runtime::shared_capture::SnapshotCache sharedCaptureAttemptCache =
                    game::runtime::shared_projected_scene::makeSharedCaptureSnapshotCache(8u);
                runtime::shared_projected_units::Args projectedUnitArgs;
                projectedUnitArgs.dataDb = &dataDb;
                projectedUnitArgs.gameWorld = gameWorld.get();
                projectedUnitArgs.worldCellSize = worldCellSize;
                projectedUnitArgs.minDim = minDim;
                projectedUnitArgs.boardSurfaceY = boardSurfaceY;
                projectedUnitArgs.lineThickness = line;
                projectedUnitArgs.supportsWorldTriangles3D = supportsWorldTriangles3D;
                projectedUnitArgs.supportsWorldIndexedMeshes = supportsWorldIndexedMeshes;
                projectedUnitArgs.enableGpuClipSkinning = backendGpuClipSkinningEnabled(renderer);
                projectedUnitArgs.hasWorldViewProj = hasWorldViewProj;
                projectedUnitArgs.allowPortraitFallback = allowPortraitFallback;
                projectedUnitArgs.forcePortraitOverlay = forcePortraitOverlay;
                projectedUnitArgs.useLegacyGrowlWaveVfx = useLegacyGrowlWaveVfx;
                projectedUnitArgs.useLegacyParticleVfxSnapshotBridge =
                    useLegacyParticleVfxSnapshotBridge;
                projectedUnitArgs.worldViewProj = hasWorldViewProj ? worldViewProj : nullptr;
                projectedUnitArgs.drawableW = drawableW;
                projectedUnitArgs.drawableH = drawableH;
                projectedUnitArgs.cameraWorldPos = cameraWorldPos;
                projectedUnitArgs.projectedDebug = &projectedDebug;
                projectedUnitArgs.sharedCaptureAttemptCache = &sharedCaptureAttemptCache;
                projectedUnitArgs.sharedTailFireAnchors = &sharedTailFireAnchors;
                projectedUnitArgs.worldIndexedBatches = &worldIndexedBatches;
                projectedUnitArgs.backendTextureByPath = &backendTextureByPath;
                projectedUnitArgs.modelDepthTris = &modelDepthTris;
                projectedUnitArgs.modelDepthWorldTris = &modelDepthWorldTris;
                projectedUnitArgs.remainingModelTrianglesBudget = &remainingModelTrianglesBudget;
                projectedUnitArgs.worldQuads = &worldQuads;
                projectedUnitArgs.lines = &lines;
                projectedUnitArgs.textLines = &textLines;
                projectedUnitArgs.sprites = &sprites;
                projectedUnitArgs.worldTriangles = &worldTriangles;
                projectedUnitArgs.world3DTriangles = &world3DTriangles;
                projectedUnitArgs.visibleAnimatedUnitCount = &visibleAnimatedUnitsThisFrame;
                projectedUnitArgs.sharedUnitHudCfg = &sharedUnitHudCfg;
                projectedUnitArgs.resolveModelMesh = [&](const PokemonInstance& unit)
                    -> const runtime::backend_model::MeshData* {
                    return game::runtime::shared_projected_scene::resolveModelMesh(
                        unit,
                        dataDb,
                        [&](const std::string& modelPath) {
                            return ensureBackendMeshLoaded(modelPath);
                        });
                };
                projectedUnitArgs.ensureBackendTextureLoaded =
                    [&](const std::string& texturePath, bool flipVertical) {
                        return ensureBackendTextureLoaded(texturePath, flipVertical);
                    };
                projectedUnitArgs.backendModelTriangleLimit = [&]() {
                    return backendModelTriangleLimit();
                };
                projectedUnitArgs.backendModelFullMeshEnabled = [&]() {
                    return backendModelFullMeshEnabled();
                };
                projectedUnitArgs.backendModelFastTexturedPathEnabled = [&]() {
                    return backendModelFastTexturedPathEnabled();
                };
                projectedUnitArgs.backendModelBackfaceCullingEnabled = [&]() {
                    return backendModelBackfaceCullingEnabled();
                };
                projectedUnitArgs.getTailFireFallbackCfg = [&]() -> const TailFireVFX::Config& {
                    return game::runtime::shared_projected_scene::getTailFireFallbackCfg();
                };
                projectedUnitArgs.perfStats = &projectedUnitPerf;

                (void)sharedCaptureAttemptCache.refresh(gameWorld.get());
                game::runtime::shared_projected_units::drawProjectedUnits(
                    projectedUnitArgs, gameWorld->getPokemons());
                game::runtime::shared_projected_units::drawProjectedUnits(
                    projectedUnitArgs, gameWorld->getBenchPokemons());
                (void)game::runtime::shared_projected_scene::appendSharedCaptureAttemptModelsIfNeededForProjectedWorld(
                    renderer, gameWorld.get(), supportsWorldIndexedMeshes, hasWorldViewProj, drawableW,
                    drawableH, worldCellSize, worldViewProj, cameraWorldPos, sharedCaptureAttemptCache,
                    worldIndexedBatches, backendTextureByPath,
                    [&](const std::string& path) { return ensureBackendMeshLoaded(path); },
                    [&](const std::string& path) { return ensureBackendTextureLoaded(path); });
                game::runtime::shared_projected_scene::appendSharedProjectedVfxBridgesSession(
                    useLegacyParticleVfxSnapshotBridge, useLegacyGrowlWaveVfx, supportsWorldIndexedMeshes,
                    hasWorldViewProj, backendUseExactTailFireCpuPathEnabled(), gameWorld.get(), viewProj,
                    invViewProj, cameraWorldPos, drawableW, drawableH, worldCellSize, timeSource.nowSeconds(),
                    line, sharedTailFireAnchors, backendTextureByPath, worldIndexedBatches, projectedDebug,
                    [&](const std::string& meshPath) { return ensureBackendMeshLoaded(meshPath); },
                    [&](const std::string& texturePath, bool flipVertical) {
                        return ensureBackendTextureLoaded(texturePath, flipVertical);
                    });
                game::runtime::shared_projected_scene::flushModelDepthBuffers(
                    modelDepthTris,
                    modelDepthWorldTris,
                    worldTriangles,
                    world3DTriangles);
                projectedUnitsMsThisFrame = static_cast<float>(projectedUnitPerf.totalMs);
                projectedPoseEvalMsThisFrame = static_cast<float>(projectedUnitPerf.poseEvalMs);
                projectedModelMsThisFrame = static_cast<float>(projectedUnitPerf.modelRenderMs);
                projectedOverlayMsThisFrame = static_cast<float>(projectedUnitPerf.overlayMs);
                projectedUnitsProcessedThisFrame = projectedUnitPerf.unitsProcessed;
                projectedModelUnitsThisFrame = projectedUnitPerf.modelUnits;
                projectedClipSkinnedUnitsThisFrame = projectedUnitPerf.clipSkinnedUnits;
            } else {
            IRenderBackend::DebugQuad boardBg;
            boardBg.x = boardX;
            boardBg.y = boardY;
            boardBg.w = boardW;
            boardBg.h = boardH;
            boardBg.r = renderWorld ? 0.08f : 0.07f;
            boardBg.g = renderWorld ? 0.09f : 0.08f;
            boardBg.b = renderWorld ? 0.10f : 0.09f;
            boardBg.a = renderWorld ? 1.0f : 0.90f;
            worldBackgroundQuads.push_back(boardBg);

            const float line = std::max(1.0f, minDim * 0.002f);
            for (int r = 0; r < rows; ++r) {
                for (int c = 0; c < cols; ++c) {
                    IRenderBackend::DebugQuad cell;
                    cell.x = boardX + cellW * static_cast<float>(c);
                    cell.y = boardY + cellH * static_cast<float>(r);
                    cell.w = cellW;
                    cell.h = cellH;
                    const bool darkCell = ((r + c) % 2) == 0;
                    if (darkCell) {
                        cell.r = renderWorld ? 0.08f : 0.07f;
                        cell.g = renderWorld ? 0.09f : 0.08f;
                        cell.b = renderWorld ? 0.10f : 0.09f;
                        cell.a = renderWorld ? 0.24f : 0.20f;
                    } else {
                        cell.r = renderWorld ? 0.11f : 0.09f;
                        cell.g = renderWorld ? 0.12f : 0.10f;
                        cell.b = renderWorld ? 0.13f : 0.11f;
                        cell.a = renderWorld ? 0.18f : 0.14f;
                    }
                    worldBackgroundQuads.push_back(cell);
                }
            }

            for (int c = 0; c <= cols; ++c) {
                IRenderBackend::DebugLine vLine;
                vLine.x1 = boardX + cellW * static_cast<float>(c);
                vLine.y1 = boardY;
                vLine.x2 = vLine.x1;
                vLine.y2 = boardY + boardH;
                vLine.thickness = line;
                vLine.r = renderWorld ? 0.74f : 0.62f;
                vLine.g = renderWorld ? 0.75f : 0.63f;
                vLine.b = renderWorld ? 0.77f : 0.65f;
                vLine.a = 1.0f;
                lines.push_back(vLine);
            }
            for (int r = 0; r <= rows; ++r) {
                IRenderBackend::DebugLine hLine;
                hLine.x1 = boardX;
                hLine.y1 = boardY + cellH * static_cast<float>(r);
                hLine.x2 = boardX + boardW;
                hLine.y2 = hLine.y1;
                hLine.thickness = line;
                hLine.r = renderWorld ? 0.74f : 0.62f;
                hLine.g = renderWorld ? 0.75f : 0.63f;
                hLine.b = renderWorld ? 0.77f : 0.65f;
                hLine.a = 1.0f;
                lines.push_back(hLine);
            }

            if (renderWorld && gameWorld) {
                const float worldCellSize = gameWorld->getBoardCellSize();
                const auto& units = gameWorld->getPokemons();
                for (const auto& unit : units) {
                    if (!unit.alive && !unit.captureInProgress && !unit.fainting) continue;
                    const auto uv = runtime::backendview::worldToBoardUv(
                        unit.position.x,
                        unit.position.z,
                        cols,
                        rows,
                        worldCellSize);
                    if (uv.first < 0.0f || uv.first > 1.0f || uv.second < 0.0f || uv.second > 1.0f) continue;
                    ++visibleAnimatedUnitsThisFrame;

                    IRenderBackend::DebugQuad u;
                    const float centerX = boardX + uv.first * boardW;
                    const float centerY = boardY + uv.second * boardH;
                    u.w = cellW * 0.60f;
                    u.h = cellH * 0.60f;
                    u.x = centerX - u.w * 0.5f;
                    u.y = centerY - u.h * 0.5f;
                    runtime::backend_units::applyWorldUnitTint(u, unit);

                    const std::string unitImagePath =
                        runtime::backend_units::resolveWorldUnitImagePath(unit.name);
                    IRenderBackend::DebugSprite unitSprite =
                        runtime::backend_units::makeWorldUnitSprite(
                            centerX,
                            centerY,
                            cellW,
                            cellH,
                            unitImagePath,
                            unit.alive ? 0.96f : 0.70f);
                    const bool hasUnitSprite = !unitSprite.texturePath.empty();
                    if (runtime::backend_units::shouldRenderTintUnderPortrait(hasUnitSprite)) {
                        worldQuads.push_back(u);
                    }
                    if (hasUnitSprite) {
                        sprites.push_back(std::move(unitSprite));
                    }

                    const float hudCellPx = std::clamp(minDim * 0.070f, 38.0f, 58.0f);
                    if (unit.alive) {
                        runtime::shared_unit_hud::appendLegacyUnitHud(
                            worldQuads,
                            lines,
                            textLines,
                            sharedUnitHudCfg,
                            unit,
                            centerX,
                            centerY,
                            hudCellPx);
                    }
                }

                const auto& benchUnits = gameWorld->getBenchPokemons();
                {
                    const int benchSlots = std::max(1, config.benchSlots);
                    const float benchGap = std::max(12.0f, minDim * 0.02f);
                    const float benchH = std::max(26.0f, minDim * 0.085f);
                    const float benchW = std::max(160.0f, std::min(boardW, static_cast<float>(drawableW) - 40.0f));
                    const float benchX = (static_cast<float>(drawableW) - benchW) * 0.5f;
                    const float desiredBenchY = boardY + boardH + benchGap;
                    const float benchY = std::min(desiredBenchY, static_cast<float>(drawableH) - benchH - 24.0f);

                    const bool benchOverlapsBoard = (benchY <= boardY + boardH + 3.0f);
                    IRenderBackend::DebugQuad benchBg;
                    benchBg.x = benchX;
                    benchBg.y = benchY;
                    benchBg.w = benchW;
                    benchBg.h = benchH;
                    benchBg.r = 0.09f;
                    benchBg.g = 0.12f;
                    benchBg.b = 0.15f;
                    benchBg.a = benchOverlapsBoard ? 0.90f : 0.96f;
                    worldQuads.push_back(benchBg);

                    const float benchCellW = benchW / static_cast<float>(benchSlots);
                    const float benchLineThickness = std::max(1.0f, line * 0.95f);
                    for (int slot = 0; slot < benchSlots; ++slot) {
                        IRenderBackend::DebugQuad cellBg;
                        cellBg.x = benchX + benchCellW * static_cast<float>(slot);
                        cellBg.y = benchY;
                        cellBg.w = benchCellW;
                        cellBg.h = benchH;
                        const bool dark = (slot % 2) == 0;
                        cellBg.r = dark ? 0.10f : 0.13f;
                        cellBg.g = dark ? 0.13f : 0.16f;
                        cellBg.b = dark ? 0.17f : 0.20f;
                        cellBg.a = benchOverlapsBoard ? 0.14f : 0.20f;
                        worldQuads.push_back(cellBg);
                    }

                    for (int slot = 0; slot <= benchSlots; ++slot) {
                        IRenderBackend::DebugLine slotLine;
                        slotLine.x1 = benchX + benchCellW * static_cast<float>(slot);
                        slotLine.y1 = benchY;
                        slotLine.x2 = slotLine.x1;
                        slotLine.y2 = benchY + benchH;
                        slotLine.thickness = benchLineThickness;
                        slotLine.r = 0.58f;
                        slotLine.g = 0.66f;
                        slotLine.b = 0.74f;
                        slotLine.a = 0.96f;
                        lines.push_back(slotLine);
                    }

                    IRenderBackend::DebugLine top;
                    top.x1 = benchX;
                    top.y1 = benchY;
                    top.x2 = benchX + benchW;
                    top.y2 = benchY;
                    top.thickness = benchLineThickness;
                    top.r = 0.64f;
                    top.g = 0.71f;
                    top.b = 0.79f;
                    top.a = 0.98f;
                    lines.push_back(top);

                    IRenderBackend::DebugLine bottom = top;
                    bottom.y1 = benchY + benchH;
                    bottom.y2 = benchY + benchH;
                    lines.push_back(bottom);

                    for (const auto& unit : benchUnits) {
                        ++visibleAnimatedUnitsThisFrame;
                        const int slot = runtime::backendview::worldToBenchSlot(
                            unit.position.x,
                            benchSlots,
                            worldCellSize);
                        IRenderBackend::DebugQuad benchUnit;
                        benchUnit.x = benchX + benchCellW * static_cast<float>(slot) + benchCellW * 0.20f;
                        benchUnit.y = benchY + benchH * 0.20f;
                        benchUnit.w = benchCellW * 0.60f;
                        benchUnit.h = benchH * 0.60f;
                        benchUnit.r = 0.34f;
                        benchUnit.g = 0.73f;
                        benchUnit.b = 0.96f;
                        benchUnit.a = 0.24f;

                        const std::string benchImagePath =
                            runtime::backend_units::resolveWorldUnitImagePath(unit.name);
                        IRenderBackend::DebugSprite benchSprite =
                            runtime::backend_units::makeBenchUnitSprite(
                                benchUnit.x,
                                benchUnit.y,
                                benchUnit.w,
                                benchUnit.h,
                                benchImagePath,
                                0.92f);
                        const bool hasBenchSprite = !benchSprite.texturePath.empty();
                        if (runtime::backend_units::shouldRenderTintUnderPortrait(hasBenchSprite)) {
                            worldQuads.push_back(benchUnit);
                        }
                        if (hasBenchSprite) {
                            sprites.push_back(std::move(benchSprite));
                        }
                    }
                }
            }
            }
        }
        if (renderWorld && gameWorld) {
            particleCountThisFrame = gameWorld->countActiveParticleVfx();
        }
        if (engineServices) {
            engineServices->frameVisibleAnimatedUnits = visibleAnimatedUnitsThisFrame;
            engineServices->frameParticleCount = particleCountThisFrame;
            engineServices->frameProjectedUnitsMs = projectedUnitsMsThisFrame;
            engineServices->frameProjectedPoseEvalMs = projectedPoseEvalMsThisFrame;
            engineServices->frameProjectedModelMs = projectedModelMsThisFrame;
            engineServices->frameProjectedOverlayMs = projectedOverlayMsThisFrame;
            engineServices->frameProjectedUnitsProcessed = projectedUnitsProcessedThisFrame;
            engineServices->frameProjectedModelUnits = projectedModelUnitsThisFrame;
            engineServices->frameProjectedClipSkinnedUnits = projectedClipSkinnedUnitsThisFrame;
        }

        runtime::shared_backend_debug_view::ComposeAndSubmitArgs overlayArgs;
        overlayArgs.renderer = renderer;
        overlayArgs.engineServices = engineServices;
        overlayArgs.services = services.get();
        overlayArgs.gameWorld = gameWorld.get();
        overlayArgs.camera = camera;
        overlayArgs.ecsWorld = &ecsWorld;
        overlayArgs.roundPhaseEntity = roundPhaseEntity;
        overlayArgs.log = &log;
        overlayArgs.backendInventoryPanel = &backendInventoryPanel;
        overlayArgs.refreshBackendInventoryFromWorld = [&]() { refreshBackendInventoryFromWorld(); };
        overlayArgs.showPerfOverlay = showPerfOverlay;
        overlayArgs.renderWorld = renderWorld;
        overlayArgs.hasWorldViewProj = hasWorldViewProj;
        overlayArgs.supportsWorldTriangles3D = supportsWorldTriangles3D;
        overlayArgs.supportsWorldIndexedMeshes = supportsWorldIndexedMeshes;
        overlayArgs.drawableW = drawableW;
        overlayArgs.drawableH = drawableH;
        overlayArgs.edgePad = edgePad;
        overlayArgs.lineStep = lineStep;
        overlayArgs.uiScale = uiScale;
        overlayArgs.worldViewProj = hasWorldViewProj ? worldViewProj : nullptr;
        overlayArgs.worldBackgroundQuads = &worldBackgroundQuads;
        overlayArgs.worldQuads = &worldQuads;
        overlayArgs.worldTriangles = &worldTriangles;
        overlayArgs.world3DTriangles = &world3DTriangles;
        overlayArgs.worldIndexedBatches = &worldIndexedBatches;
        overlayArgs.overlayQuads = &overlayQuads;
        overlayArgs.lines = &lines;
        overlayArgs.textLines = &textLines;
        overlayArgs.sprites = &sprites;
        runtime::shared_backend_debug_view::composeAndSubmit(overlayArgs);
    }

    void renderWorldLayer(int drawableW, int drawableH, bool renderWorld) {
        const runtime::render::RenderRoutes routes = activeRenderRoutes();
        if (routes.usesBackendRenderPath()) {
            renderBackendDebugView(drawableW, drawableH, renderWorld);
        }
    }

    void renderStateLayer() {
        if (stateManager) {
            stateManager->render();
        }
    }

    void renderFrameFromFlow(const runtime::render::FrameRenderFlow& flow,
                             int drawableW,
                             int drawableH,
                             bool renderWorld) {
        if (flow.renderWorldLayer) {
            renderWorldLayer(drawableW, drawableH, renderWorld);
        }
        if (flow.renderStateLayer) {
            renderStateLayer();
        }
    }

    void render(int drawableW, int drawableH) {
        viewport.set(drawableW, drawableH);
        if (unitSystem) {
            unitSystem->setScreenSize(
                static_cast<unsigned int>(std::max(1, drawableW)),
                static_cast<unsigned int>(std::max(1, drawableH)));
        }

        bool renderWorld = true;
        if (stateManager) {
            if (auto* state = stateManager->getCurrentState()) {
                renderWorld = state->shouldRenderWorld();
            }
        }

        const auto flow = currentFrameFlow(renderWorld);
        renderFrameFromFlow(flow, drawableW, drawableH, renderWorld);
    }

    void shutdown() {
        std::cout << "[Shutdown] Game.\n";

        log.attach(nullptr);
        log.attachCatchFeed(nullptr);
        log.attachEconomyFeed(nullptr);
        shopSystem = nullptr;
        unitSystem.reset();
        cameraSystem.reset();

        stateManager.reset();
        gameWorld.reset();

        scheduler.clear();

        std::cout << "[Shutdown] Game done.\n";
    }
};

GameSession::GameSession(GameContext& ctx, GameDataDb db)
    : impl_(std::make_unique<Impl>(ctx, std::move(db))) {}

GameSession::~GameSession() = default;

GameSession::GameSession(GameSession&&) noexcept = default;
GameSession& GameSession::operator=(GameSession&&) noexcept = default;

void GameSession::handleEvent(const InputEvent& event) { impl_->handleEvent(event); }
void GameSession::fixedUpdate(float dt) { impl_->fixedUpdate(dt); }
void GameSession::render(int drawableW, int drawableH) { impl_->render(drawableW, drawableH); }
void GameSession::shutdown() { impl_->shutdown(); }

} // namespace game

