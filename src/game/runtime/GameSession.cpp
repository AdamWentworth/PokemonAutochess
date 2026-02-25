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

#include "engine/render/BoardRenderer.h"
#include "engine/render/Camera3D.h"
#include "engine/render/IRenderBackend.h"
#include "engine/render/Model.h"

#include "engine/ui/UIManager.h"
#include "engine/ui/BattleFeed.h"
#include "engine/ui/HealthBarRenderer.h"
#include "engine/ui/TextRenderer.h"

#include "engine/core/ecs/Scheduler.h"
#include "engine/core/ecs/World.h"
#include "engine/utils/ResourceManager.h"

#include "game/GameWorld.h"
#include "game/GameStateManager.h"
#include "game/runtime/GamePreload.h"
#include "game/runtime/BackendRenderPolicy.h"
#include "game/runtime/RenderFlowDecisions.h"
#include "game/runtime/BackendDebugText.h"
#include "game/runtime/GameServiceRenderRoutes.h"
#include "game/runtime/BackendInventoryOverlay.h"
#include "game/runtime/BackendInventoryPanel.h"
#include "game/runtime/BackendInputSlots.h"
#include "game/runtime/BackendStatusText.h"
#include "game/runtime/BackendUiScale.h"
#include "game/runtime/BackendHudFormatting.h"
#include "game/runtime/BackendWorldProjection.h"
#include "game/runtime/BackendWorldProxyGeometry.h"
#include "game/runtime/BackendModelCache.h"
#include "game/runtime/BackendMaterialShading.h"
#include "game/runtime/BackendProceduralPose.h"
#include "game/runtime/BackendUnitVisuals.h"
#include "game/GameServices.h"
#include "game/GameConfig.h"
#include "game/runtime/GameUpdateGraph.h"
#include "game/ui/UIViewport.h"
#include "game/ui/ItemInventoryUI.h"
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

#include "game/state/ScriptedState.h"
#include "game/logging/LogBus.h"
#include "game/logging/LoggerUtil.h"
#include "game/scripting/ScriptEventBus.h"
#include "game/world/MoveImpactRouting.h"

namespace {
std::string trimDebugLine(std::string s, std::size_t maxChars) {
    if (s.size() <= maxChars) return s;
    if (maxChars <= 3) return s.substr(0, maxChars);
    return s.substr(0, maxChars - 3) + "...";
}

struct BackendItemAtlasIcon {
    const char* id;
    int row;
    int col;
};

const BackendItemAtlasIcon* findBackendItemAtlasIcon(const std::string& id) {
    static const BackendItemAtlasIcon kIcons[] = {
        {"pokeball", 1, 4},
        {"potion", 2, 4},
        {"burn_heal", 2, 6},
        {"antidote", 2, 5},
        {"paralyze_heal", 2, 9},
    };
    for (const auto& icon : kIcons) {
        if (id == icon.id) return &icon;
    }
    return nullptr;
}

glm::vec2 backendItemAtlasUvMin(int row, int col) {
    constexpr int kCols = 13;
    constexpr int kRows = 14;
    constexpr float kPadU = 0.08f;
    constexpr float kPadV = 0.08f;
    const int c = std::max(1, col);
    const int r = std::max(1, row);
    float u0 = static_cast<float>(c - 1) / static_cast<float>(kCols);
    float v0 = static_cast<float>(r - 1) / static_cast<float>(kRows);
    u0 += (kPadU / static_cast<float>(kCols));
    v0 += (kPadV / static_cast<float>(kRows));
    return {u0, v0};
}

glm::vec2 backendItemAtlasUvMax(int row, int col) {
    constexpr int kCols = 13;
    constexpr int kRows = 14;
    constexpr float kPadURight = 0.06f;
    constexpr float kPadVBottom = 0.06f;
    const int c = std::max(1, col);
    const int r = std::max(1, row);
    float u1 = static_cast<float>(c) / static_cast<float>(kCols);
    float v1 = static_cast<float>(r) / static_cast<float>(kRows);
    u1 -= (kPadURight / static_cast<float>(kCols));
    v1 -= (kPadVBottom / static_cast<float>(kRows));
    return {u1, v1};
}

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

struct SharedCaptureSnapshotCache {
    std::vector<GameWorld::CaptureAttemptRenderSnapshot> snaps;
    std::unordered_map<int, std::size_t> byTargetId;

    bool refresh(const GameWorld* gameWorld) {
        snaps.clear();
        byTargetId.clear();
        if (!gameWorld) return false;
        if (!gameWorld->buildCaptureAttemptRenderSnapshots(snaps)) return false;
        byTargetId.reserve(snaps.size());
        for (std::size_t i = 0; i < snaps.size(); ++i) {
            const auto& snap = snaps[i];
            if (snap.targetId < 0) continue;
            byTargetId[snap.targetId] = i;
        }
        return !snaps.empty();
    }

    const GameWorld::CaptureAttemptRenderSnapshot* findByTarget(int targetId) const {
        const auto it = byTargetId.find(targetId);
        if (it == byTargetId.end()) return nullptr;
        if (it->second >= snaps.size()) return nullptr;
        return &snaps[it->second];
    }
};

float sharedCaptureBallClipTimeSec(const GameWorld::CaptureAttemptRenderSnapshot& snap, float clipDurationSec) {
    if (clipDurationSec <= 0.0f) return 0.0f;
    if (snap.phase != 1) return 0.0f; // Absorb only; keep closed during throw/shake/resolve.
    return std::clamp(snap.absorbNorm01, 0.0f, 1.0f) * clipDurationSec;
}

glm::mat4 buildCaptureBallModelMatrix(const glm::vec3& pos, float yawDeg, float uniformScale) {
    const glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(std::max(0.0f, uniformScale)));
    const glm::mat4 rotationY =
        glm::rotate(glm::mat4(1.0f), glm::radians(yawDeg), glm::vec3(0, 1, 0));
    const glm::mat4 translation = glm::translate(glm::mat4(1.0f), pos);
    return translation * rotationY * scale;
}

int findPokeballAnimIndex(const std::shared_ptr<Model>& model) {
    if (!model) return -1;
    int animIndex = model->findAnimationIndexByName("Hinge_TopAction");
    if (animIndex < 0 && model->getAnimationCount() > 0) animIndex = 0;
    return animIndex;
}

int findPokeballAnimIndex(const game::runtime::backend_model::MeshData& mesh) {
    if (mesh.animations.empty()) return -1;
    for (std::size_t ai = 0; ai < mesh.animations.size(); ++ai) {
        if (mesh.animations[ai].name == "Hinge_TopAction") return static_cast<int>(ai);
    }
    return 0;
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
    std::unique_ptr<BoardRenderer>    board;
    std::unique_ptr<BattleFeed>       battleFeed;
    std::unique_ptr<BattleFeed>       catchFeed;
    std::unique_ptr<BattleFeed>       economyFeed;
    std::unique_ptr<TextRenderer>     typeBonusText;
    ItemInventoryUI                  itemInventoryUI;

    HealthBarRenderer healthBarRenderer;
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
    struct BackendTextureCacheEntry {
        bool attemptedLoad = false;
        bool valid = false;
        int width = 0;
        int height = 0;
        std::vector<unsigned char> rgba;
    };
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

    bool usesLegacyGameRenderPath() const {
        if (services) return services->usesLegacyGameRenderPath();
        return startupRoutes.usesLegacyRenderPath();
    }

    bool usesBackendGameRenderPath() const {
        if (services) return services->usesBackendGameRenderPath();
        return startupRoutes.usesBackendRenderPath();
    }

    bool usesLegacyGameUiPath() const {
        if (services) return services->usesLegacyGameUiPath();
        return startupRoutes.usesLegacyUiPath();
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

            if (roles.usesAirLocomotion && !unit.usesAirLocomotion) {
                unit.usesAirLocomotion = true;
            }
            if (unit.usesAirLocomotion) {
                if (unit.airLiftY <= 0.0f && roles.airLiftY > 0.0f) unit.airLiftY = roles.airLiftY;
                if (unit.takeoffSec <= 0.0f && roles.takeoffSec > 0.0f) unit.takeoffSec = roles.takeoffSec;
                if (unit.landingSec <= 0.0f && roles.landingSec > 0.0f) unit.landingSec = roles.landingSec;
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
        const bool prefersLegacyRenderPath = hasBackend && ctx.renderer->prefersLegacyGameRenderPath();
        const bool prefersLegacyUiPath = hasBackend && ctx.renderer->prefersLegacyGameUiPath();
        startupRoutes = runtime::render::makeRenderRoutes(
            hasBackend,
            prefersLegacyRenderPath,
            prefersLegacyUiPath);
        if (ctx.services) {
            const std::string requestedBackend = toLowerCopy(ctx.services->requestedRendererBackend);
            if (requestedBackend == "opengl_shared" && hasBackend &&
                ctx.renderer->backendId() &&
                toLowerCopy(ctx.renderer->backendId()) == "opengl") {
                startupRoutes.legacyRenderPath = false;
                startupRoutes.legacyUiPath = false;
                std::cout << "[Renderer] OpenGL shared-contract mode enabled via Display preference "
                          << "(renderer_backend=opengl_shared).\n";
            }
        }
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
        services->legacyGameRenderPath = startupRoutes.legacyRenderPath;
        services->legacyGameUiPath = startupRoutes.legacyUiPath;
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

        // Board visuals
        if (usesLegacyGameRenderPath()) {
            board = std::make_unique<BoardRenderer>(config.rows, config.cols, config.cellSize);
        }

        // World
        gameWorld = std::make_unique<GameWorld>(config);
        gameWorld->setRenderEnabled(hasActiveRenderBackend());
        gameWorld->setLegacyModelRenderPathEnabled(usesLegacyGameRenderPath());
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


        if (usesLegacyGameRenderPath()) {
            if (ctx.services && ctx.services->shaders) {
                healthBarRenderer.init(*ctx.services->shaders);
            } else {
                healthBarRenderer.init();
            }
            const int levelFontSize = std::max(20, config.fontSize / 2);
            healthBarRenderer.setFont(config.fontPath, levelFontSize,
                                      ctx.services ? ctx.services->shaders : nullptr);

            // Battle feed + logger (instance-based)
            battleFeed = std::make_unique<BattleFeed>(config.fontPath, config.fontSize);
            battleFeed->setAlignRight(true);
            battleFeed->setBaseScale(0.40f);
            log.attach(battleFeed.get());
            log.setEchoToStdout(true);

            // Catch feed (right side)
            catchFeed = std::make_unique<BattleFeed>(config.fontPath, config.fontSize);
            catchFeed->setAlignRight(false);
            catchFeed->setWrapWidth(320.f);
            catchFeed->setMaxLines(5);
            catchFeed->setBaseScale(0.38f);
            catchFeed->setPadding(16.f, 16.f);
            log.attachCatchFeed(catchFeed.get());

            // Classic economy feed (bottom-right, mode-specific)
            economyFeed = std::make_unique<BattleFeed>(config.fontPath, config.fontSize);
            economyFeed->setAlignRight(false);
            economyFeed->setWrapWidth(320.f);
            economyFeed->setMaxLines(4);
            economyFeed->setBaseScale(0.36f);
            economyFeed->setPadding(16.f, 16.f);
            log.attachEconomyFeed(economyFeed.get());

            itemInventoryUI.init(config.fontPath, config.fontSize);
            typeBonusText = std::make_unique<TextRenderer>(config.fontPath, std::max(18, config.fontSize / 2));
        }

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
        if (auto* feed = battleFeed.get()) {
            scheduler.add(std::make_unique<game::CallbackSystemAdapter>(
                [feed](float dt) { feed->update(dt); }
            ), Phase::PostUpdate);
        }
        if (auto* feed = catchFeed.get()) {
            scheduler.add(std::make_unique<game::CallbackSystemAdapter>(
                [feed](float dt) { feed->update(dt); }
            ), Phase::PostUpdate);
        }
        if (auto* feed = economyFeed.get()) {
            scheduler.add(std::make_unique<game::CallbackSystemAdapter>(
                [feed](float dt) { feed->update(dt); }
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

        // Preload common models (uses the db's pokemon loader).
        if (usesLegacyGameRenderPath()) {
            game::preload::preloadCommonModels(ctx, dataDb.pokemon, "PokemonAutochess");
        } else {
            std::cout << "[Init] Non-OpenGL render path: using backend model cache loader (OpenGL ModelStartupLog is not used).\n";
            if (backendPreloadModelCacheEnabled()) {
                std::cout << "[Init] Non-OpenGL render path: preloading backend model cache...\n";
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
                std::cout << "[Init] Non-OpenGL render path: backend model cache preload disabled.\n";
            }
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

    bool handleLegacyInventoryUiInput(const InputEvent& event) {
        if (event.type == InputEvent::Type::MouseWheel) {
            itemInventoryUI.handleScroll(event.wheelY, viewport.height);
            return false;
        }

        if (event.type != InputEvent::Type::MouseDown ||
            event.mouseButtonId != InputEvent::MouseButton::Left) {
            return false;
        }

        if (auto clicked = itemInventoryUI.handleMouseClick(event.mouseX, event.mouseY)) {
            if (gameWorld) {
                gameWorld->setSelectedItem(*clicked);
                log.catchInfo("Selected " + runtime::hud::humanizeToken(*clicked) + ". Click a target.");
            }
            return true; // consume click (avoid dragging/other UI)
        }
        return false;
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
        if (renderWorldForInput && usesLegacyGameUiPath()) {
            if (handleLegacyInventoryUiInput(event)) {
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

        struct WorldIndexedBatch {
            std::vector<IRenderBackend::WorldMeshVertex> vertices;
            std::vector<std::uint32_t> indices;
            std::string textureKey;
            std::vector<unsigned char> ownedTextureRgba;
            const unsigned char* textureRgba = nullptr;
            int textureWidth = 0;
            int textureHeight = 0;
            int textureWrapS = 10497;
            int textureWrapT = 10497;
            std::uint8_t alphaMode = 0u;
            std::uint8_t blendMode = 0u;
            std::uint8_t materialMode = 0u;
            float alphaCutoff = 0.5f;
            float sortDepth = 0.0f;
            float materialTimeSec = 0.0f;
            float materialFlags = 0.0f;
            float materialAtlasWidth = 0.0f;
            float materialAtlasHeight = 0.0f;
            float materialRect0U = 0.0f;
            float materialRect0V = 0.0f;
            float materialRect0W = 1.0f;
            float materialRect0H = 1.0f;
            float materialRect1U = 0.0f;
            float materialRect1V = 0.0f;
            float materialRect1W = 1.0f;
            float materialRect1H = 1.0f;
            float materialFlipbook0Cols = 1.0f;
            float materialFlipbook0Rows = 1.0f;
            float materialFlipbook0Frames = 1.0f;
            float materialFlipbook0Fps = 0.0f;
            float materialFlipbook1Cols = 1.0f;
            float materialFlipbook1Rows = 1.0f;
            float materialFlipbook1Frames = 1.0f;
            float materialFlipbook1Fps = 0.0f;
        };
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

        const bool supportsWorldTriangles3D = renderer->supportsWorldTriangles3D();
        const bool supportsWorldIndexedMeshes = renderer->supportsWorldIndexedMeshes();
        const bool allowPortraitFallback = backendWorldPortraitFallbackEnabled();
        const bool forcePortraitOverlay = backendWorldPortraitOverlayForced();
        float worldViewProj[16] = {};
        bool hasWorldViewProj = false;
        const auto xpToNextLevel = [&](int level) {
            if (config.xpLevelBase <= 0) return 0;
            const int useLevel = std::max(1, level);
            const float growth = (config.xpLevelGrowth > 0.0f) ? config.xpLevelGrowth : 1.0f;
            const float raw =
                static_cast<float>(config.xpLevelBase) * std::pow(growth, static_cast<float>(useLevel - 1));
            return std::max(1, static_cast<int>(std::round(raw)));
        };
        const auto appendRingArc = [&](const glm::vec2& center,
                                       float innerR,
                                       float outerR,
                                       float startRad,
                                       float endRad,
                                       float r,
                                       float g,
                                       float b,
                                       float a) {
            const float arc = endRad - startRad;
            if (std::abs(arc) < 0.0001f) return;
            const float full = 6.2831853f;
            const int segments = std::max(
                6, static_cast<int>(std::ceil(std::abs(arc) / full * 32.0f)));
            const float thickness = std::max(1.0f, outerR - innerR);
            for (int i = 0; i < segments; ++i) {
                const float t0 = static_cast<float>(i) / static_cast<float>(segments);
                const float t1 = static_cast<float>(i + 1) / static_cast<float>(segments);
                const float a0 = startRad + arc * t0;
                const float a1 = startRad + arc * t1;
                IRenderBackend::DebugLine lineSeg;
                lineSeg.x1 = center.x + std::cos(a0) * (innerR + outerR) * 0.5f;
                lineSeg.y1 = center.y + std::sin(a0) * (innerR + outerR) * 0.5f;
                lineSeg.x2 = center.x + std::cos(a1) * (innerR + outerR) * 0.5f;
                lineSeg.y2 = center.y + std::sin(a1) * (innerR + outerR) * 0.5f;
                lineSeg.thickness = thickness;
                lineSeg.r = r;
                lineSeg.g = g;
                lineSeg.b = b;
                lineSeg.a = a;
                lines.push_back(lineSeg);
            }
        };
        const auto appendLegacyUnitHud = [&](const PokemonInstance& unit,
                                             float screenX,
                                             float screenY,
                                             float cellPx) {
            const float safeCellPx = std::max(8.0f, cellPx);
            const float hudScale = 1.20f; // Keep shared HUD ~20% larger to match legacy readability.
            const float hudPx = safeCellPx * hudScale;
            const float width = hudPx * 0.45f;
            const float hpH = hudPx * 0.07f;
            const float enH = hudPx * 0.06f;
            const float yOffset = hudPx * 0.82f;
            const float gap = hudPx * 0.03f;

            const float ringOuter = hudPx * 0.155f;
            const float ringInner = hudPx * 0.135f;
            const float ringGap = hudPx * 0.035f;
            const float ringPad = ringOuter + ringGap;
            const float leftExtent = ringOuter + ringPad;

            glm::vec2 pos(screenX, screenY);
            pos.x -= (width - leftExtent) * 0.5f;
            pos.y -= yOffset;

            const float hpRatio = std::clamp(
                static_cast<float>(std::max(0, unit.hp)) /
                    static_cast<float>(std::max(1, unit.maxHP)),
                0.0f,
                1.0f);
            const float energyRatio = std::clamp(
                static_cast<float>(std::max(0, unit.energy)) /
                    static_cast<float>(std::max(1, unit.maxEnergy)),
                0.0f,
                1.0f);

            IRenderBackend::DebugQuad hpBg;
            hpBg.x = pos.x;
            hpBg.y = pos.y;
            hpBg.w = width;
            hpBg.h = hpH;
            hpBg.r = 0.3f;
            hpBg.g = 0.3f;
            hpBg.b = 0.3f;
            hpBg.a = 1.0f;
            worldQuads.push_back(hpBg);

            IRenderBackend::DebugQuad hpFg = hpBg;
            hpFg.w = width * hpRatio;
            if (unit.side == PokemonSide::Enemy) {
                hpFg.r = 1.0f;
                hpFg.g = 0.0f;
                hpFg.b = 0.0f;
            } else {
                hpFg.r = 0.0f;
                hpFg.g = 1.0f;
                hpFg.b = 0.0f;
            }
            worldQuads.push_back(hpFg);

            IRenderBackend::DebugQuad energyBg = hpBg;
            energyBg.y = pos.y + hpH + gap;
            energyBg.h = enH;
            energyBg.r = 0.25f;
            energyBg.g = 0.25f;
            energyBg.b = 0.25f;
            worldQuads.push_back(energyBg);

            IRenderBackend::DebugQuad energyFg = energyBg;
            energyFg.w = width * energyRatio;
            energyFg.r = 0.95f;
            energyFg.g = 0.65f;
            energyFg.b = 0.20f;
            worldQuads.push_back(energyFg);

            const float barH = hpH + gap + enH;
            const glm::vec2 levelCenter(pos.x - ringPad, pos.y + barH * 0.5f - hudPx * 0.02f);
            const bool showXP = (unit.side == PokemonSide::Player);
            const int maxXP = showXP ? xpToNextLevel(unit.level) : 0;
            if (showXP && maxXP > 0) {
                const float xFrac = std::clamp(
                    static_cast<float>(std::max(0, unit.xp)) / static_cast<float>(std::max(1, maxXP)),
                    0.0f,
                    1.0f);
                const float start = -1.5707963f;
                appendRingArc(levelCenter,
                              ringInner,
                              ringOuter,
                              start,
                              start + 6.2831853f,
                              0.20f,
                              0.20f,
                              0.20f,
                              1.0f);
                appendRingArc(levelCenter,
                              ringInner,
                              ringOuter,
                              start,
                              start + xFrac * 6.2831853f,
                              0.20f,
                              0.55f,
                              1.0f,
                              1.0f);
            }

            const std::string levelText = std::to_string(std::max(1, unit.level));
            const float baseLevelH = std::max(1.0f, runtime::backend_text::measureTextHeight(levelText, 1.0f));
            const float levelScale = std::clamp((ringInner * 1.55f) / baseLevelH, 0.72f, 1.05f);
            const runtime::backend_text::TextBounds levelBounds =
                runtime::backend_text::measureTextBounds(levelText, levelScale);
            const float levelCenterOffsetX = levelBounds.valid
                ? (levelBounds.minX + levelBounds.maxX) * 0.5f
                : (std::max(1.0f, runtime::backend_text::measureTextWidth(levelText, levelScale)) * 0.5f);
            const float levelCenterOffsetY = levelBounds.valid
                ? (levelBounds.minY + levelBounds.maxY) * 0.5f
                : (std::max(1.0f, runtime::backend_text::measureTextHeight(levelText, levelScale)) * 0.5f);
            const float textX = levelCenter.x - levelCenterOffsetX;
            const float textY = levelCenter.y - levelCenterOffsetY;
            runtime::backend_text::appendTextLines(
                textLines,
                textX,
                textY,
                levelText,
                levelScale,
                1.0f,
                1.0f,
                1.0f,
                1.0f,
                0.88f);
        };

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

                const auto projectWorld = [&](const glm::vec3& worldPos,
                                              float& outX,
                                              float& outY,
                                              float& outZ) {
                    const glm::vec3 p = glm::project(worldPos, view, proj, screenViewport);
                    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
                    outX = p.x;
                    outY = static_cast<float>(drawableH) - p.y;
                    outZ = p.z;
                    return true;
                };

                const float line = std::max(1.0f, minDim * 0.0019f);
                const auto appendWorldTriangle = [&](const glm::vec3& a,
                                                     const glm::vec3& b,
                                                     const glm::vec3& c,
                                                     float r,
                                                     float g,
                                                     float bl,
                                                     float alpha) {
                    if (!supportsWorldTriangles3D) return;
                    IRenderBackend::WorldTriangle tri;
                    tri.x1 = a.x;
                    tri.y1 = a.y;
                    tri.z1 = a.z;
                    tri.x2 = b.x;
                    tri.y2 = b.y;
                    tri.z2 = b.z;
                    tri.x3 = c.x;
                    tri.y3 = c.y;
                    tri.z3 = c.z;
                    tri.r = r;
                    tri.g = g;
                    tri.b = bl;
                    tri.a = alpha;
                    world3DTriangles.push_back(tri);
                };
                const auto appendWorldQuad = [&](const glm::vec3& a,
                                                 const glm::vec3& b,
                                                 const glm::vec3& c,
                                                 const glm::vec3& d,
                                                 float r,
                                                 float g,
                                                 float bl,
                                                 float alpha) {
                    appendWorldTriangle(a, b, c, r, g, bl, alpha);
                    appendWorldTriangle(a, c, d, r, g, bl, alpha);
                };
                const auto appendProjectedTriangle = [&](const glm::vec3& a,
                                                         const glm::vec3& b,
                                                         const glm::vec3& c,
                                                         float r,
                                                         float g,
                                                         float bl,
                                                         float alpha) {
                    float x1 = 0.0f;
                    float y1 = 0.0f;
                    float z1 = 0.0f;
                    float x2 = 0.0f;
                    float y2 = 0.0f;
                    float z2 = 0.0f;
                    float x3 = 0.0f;
                    float y3 = 0.0f;
                    float z3 = 0.0f;
                    if (!projectWorld(a, x1, y1, z1) ||
                        !projectWorld(b, x2, y2, z2) ||
                        !projectWorld(c, x3, y3, z3)) {
                        return;
                    }
                    if ((z1 < 0.0f || z1 > 1.0f) &&
                        (z2 < 0.0f || z2 > 1.0f) &&
                        (z3 < 0.0f || z3 > 1.0f)) {
                        return;
                    }
                    IRenderBackend::DebugTriangle tri;
                    tri.x1 = x1;
                    tri.y1 = y1;
                    tri.x2 = x2;
                    tri.y2 = y2;
                    tri.x3 = x3;
                    tri.y3 = y3;
                    tri.r = r;
                    tri.g = g;
                    tri.b = bl;
                    tri.a = alpha;
                    worldTriangles.push_back(tri);
                };
                const auto appendProjectedQuad = [&](const glm::vec3& a,
                                                     const glm::vec3& b,
                                                     const glm::vec3& c,
                                                     const glm::vec3& d,
                                                     float r,
                                                     float g,
                                                     float bl,
                                                     float alpha) {
                    appendProjectedTriangle(a, b, c, r, g, bl, alpha);
                    appendProjectedTriangle(a, c, d, r, g, bl, alpha);
                };
                const auto appendProjectedLine = [&](const glm::vec3& a,
                                                     const glm::vec3& b,
                                                     float r,
                                                     float g,
                                                     float bl,
                                                     float alpha,
                                                     float thickness) {
                    float x1 = 0.0f;
                    float y1 = 0.0f;
                    float z1 = 0.0f;
                    float x2 = 0.0f;
                    float y2 = 0.0f;
                    float z2 = 0.0f;
                    if (!projectWorld(a, x1, y1, z1) || !projectWorld(b, x2, y2, z2)) return;
                    if ((z1 < 0.0f || z1 > 1.0f) && (z2 < 0.0f || z2 > 1.0f)) return;
                    IRenderBackend::DebugLine l;
                    l.x1 = x1;
                    l.y1 = y1;
                    l.x2 = x2;
                    l.y2 = y2;
                    l.thickness = thickness;
                    l.r = r;
                    l.g = g;
                    l.b = bl;
                    l.a = alpha;
                    lines.push_back(l);
                };
                const auto safeNormalize3 = [&](const glm::vec3& v, const glm::vec3& fallback) {
                    const float lenSq = glm::dot(v, v);
                    if (lenSq > 1e-9f) return glm::normalize(v);
                    return fallback;
                };
                const auto appendProjectedRing = [&](const glm::vec3& center,
                                                     float radius,
                                                     float r,
                                                     float g,
                                                     float bl,
                                                     float alpha,
                                                     float thickness,
                                                     int segments = 14) {
                    const int safeSegments = std::max(8, segments);
                    for (int seg = 0; seg < safeSegments; ++seg) {
                        const float t0 =
                            (static_cast<float>(seg) / static_cast<float>(safeSegments)) * 6.2831853f;
                        const float t1 =
                            (static_cast<float>(seg + 1) / static_cast<float>(safeSegments)) * 6.2831853f;
                        const glm::vec3 p0 = center + glm::vec3(std::cos(t0) * radius, 0.0f, std::sin(t0) * radius);
                        const glm::vec3 p1 = center + glm::vec3(std::cos(t1) * radius, 0.0f, std::sin(t1) * radius);
                        appendProjectedLine(p0, p1, r, g, bl, alpha, thickness);
                    }
                };
                const auto appendProjectedBurst = [&](const glm::vec3& center,
                                                      const glm::vec3& forward,
                                                      float radius,
                                                      float r,
                                                      float g,
                                                      float bl,
                                                      float alpha,
                                                      float thickness,
                                                      int spokes = 8) {
                    const int safeSpokes = std::max(4, spokes);
                    const glm::vec3 up(0.0f, 1.0f, 0.0f);
                    const glm::vec3 fwd = safeNormalize3(forward, glm::vec3(0.0f, 0.0f, 1.0f));
                    const glm::vec3 right = safeNormalize3(glm::cross(up, fwd), glm::vec3(1.0f, 0.0f, 0.0f));
                    for (int i = 0; i < safeSpokes; ++i) {
                        const float t = (static_cast<float>(i) / static_cast<float>(safeSpokes)) * 6.2831853f;
                        const glm::vec3 dir = safeNormalize3(
                            right * std::cos(t) + fwd * std::sin(t) + up * 0.25f,
                            fwd);
                        appendProjectedLine(
                            center,
                            center + dir * radius,
                            r,
                            g,
                            bl,
                            alpha,
                            thickness);
                    }
                };
                const auto appendProjectedTailFire = [&](const PokemonInstance& unit,
                                                         const glm::vec3& center,
                                                         const game::runtime::backend_proxy::UnitProxyExtents& extents,
                                                         float yawDeg,
                                                         float thickness) {
                    const std::string species = toLowerCopy(unit.name);
                    if (species != "charmander") return;
                    if (!unit.alive || unit.fainting) return;

                    const glm::vec3 up(0.0f, 1.0f, 0.0f);
                    const glm::vec3 fwd = game::runtime::backend_proxy::yawForward(yawDeg);
                    const glm::vec3 right = game::runtime::backend_proxy::yawRight(yawDeg);
                    const glm::vec3 tailBase =
                        center - fwd * std::max(0.03f, extents.halfDepth * 0.95f) +
                        up * std::max(0.02f, extents.height * 0.22f);
                    const float flameHeight = std::max(0.05f, extents.height * 0.26f);
                    const float flameRadius = std::max(0.015f, extents.halfWidth * 0.16f);
                    const float pulse = 0.5f + 0.5f * std::sin(unit.animTimeSec * 13.0f + static_cast<float>(unit.id) * 0.93f);

                    appendProjectedLine(
                        tailBase,
                        tailBase + up * flameHeight * (0.90f + pulse * 0.35f),
                        1.00f,
                        0.62f,
                        0.20f,
                        0.92f,
                        std::max(1.0f, thickness * 1.25f));
                    appendProjectedLine(
                        tailBase + right * flameRadius * 0.35f,
                        tailBase + right * flameRadius * 0.10f + up * flameHeight * (0.65f + pulse * 0.25f),
                        1.00f,
                        0.88f,
                        0.38f,
                        0.88f,
                        std::max(1.0f, thickness * 1.05f));
                    appendProjectedLine(
                        tailBase - right * flameRadius * 0.30f,
                        tailBase - right * flameRadius * 0.08f + up * flameHeight * (0.58f + pulse * 0.22f),
                        1.00f,
                        0.80f,
                        0.32f,
                        0.82f,
                        std::max(1.0f, thickness * 1.0f));

                    const glm::vec3 tip = tailBase + up * flameHeight * (0.88f + pulse * 0.30f);
                    appendProjectedRing(
                        tip,
                        flameRadius * (0.45f + pulse * 0.20f),
                        1.00f,
                        0.66f,
                        0.22f,
                        0.70f,
                        std::max(1.0f, thickness * 0.95f),
                        10);
                };
                const auto appendProjectedLeechDrain = [&](const PokemonInstance& target,
                                                           float worldY,
                                                           float thickness) {
                    if (!target.leechSeeded || target.leechSeedSourceId < 0) return;
                    if (!gameWorld) return;
                    const PokemonInstance* source = gameWorld->findUnitById(target.leechSeedSourceId);
                    if (!source || !source->alive) return;
                    const glm::vec3 from =
                        target.position + glm::vec3(0.0f, std::max(0.08f, worldY) + target.visualYOffset, 0.0f);
                    const glm::vec3 to =
                        source->position + glm::vec3(0.0f, std::max(0.08f, worldY) + source->visualYOffset, 0.0f);
                    const float phase = std::fmod(target.animTimeSec * 1.8f + static_cast<float>(target.id) * 0.21f, 1.0f);
                    const int segments = 6;
                    for (int i = 0; i < segments; ++i) {
                        const float t0 = std::fmod(phase + static_cast<float>(i) / static_cast<float>(segments), 1.0f);
                        const float t1 = std::min(1.0f, t0 + 0.10f);
                        const glm::vec3 p0 = glm::mix(from, to, t0);
                        const glm::vec3 p1 = glm::mix(from, to, t1);
                        appendProjectedLine(
                            p0,
                            p1,
                            0.42f,
                            0.94f,
                            0.34f,
                            0.86f,
                            std::max(1.0f, thickness * 1.05f));
                    }
                };
                const auto appendSharedGrowlWaveVfx = [&]() {
                    if (!useLegacyGrowlWaveVfx) return;
                    if (!supportsWorldIndexedMeshes || !hasWorldViewProj) return;
                    if (!gameWorld) return;

                    GrowlWaveVFX::RenderSnapshot growlSnapshot;
                    if (!gameWorld->buildGrowlWaveSnapshot(growlSnapshot)) return;
                    if (growlSnapshot.drawPasses.empty() || growlSnapshot.rings.empty()) return;

                    const glm::vec3 defaultMeshForward =
                        (glm::dot(growlSnapshot.config.meshForwardAxis, growlSnapshot.config.meshForwardAxis) <= 0.0001f)
                            ? glm::vec3(0.0f, 1.0f, 0.0f)
                            : glm::normalize(growlSnapshot.config.meshForwardAxis);
                    const float fadeStart = glm::clamp(growlSnapshot.config.fadeStart, 0.0f, 1.0f);
                    const auto growlClamp01 = [](float v) {
                        return std::clamp(v, 0.0f, 1.0f);
                    };
                    const auto growlU8 = [&](float v) -> std::uint8_t {
                        return static_cast<std::uint8_t>(std::clamp<int>(
                            static_cast<int>(std::lround(growlClamp01(v) * 255.0f)), 0, 255));
                    };
                    const auto growlTevMixU8Scalar = [&](float a, float b, float t) {
                        const float a8 = std::floor(growlClamp01(a) * 255.0f + 0.5f);
                        const float b8 = std::floor(growlClamp01(b) * 255.0f + 0.5f);
                        const float t8 = std::floor(growlClamp01(t) * 255.0f + 0.5f);
                        const float tc = t8 + std::floor(t8 / 128.0f);
                        const float out8 = std::floor((a8 * 256.0f + (b8 - a8) * tc + 128.0f) / 256.0f);
                        return growlClamp01(out8 / 255.0f);
                    };
                    const auto growlAlpha6bit = [&](float a) {
                        return growlClamp01(std::floor(growlClamp01(a) * 63.0f + 0.5f) / 63.0f);
                    };

                    struct GrowlTevState {
                        glm::vec3 c0{1.0f, 1.0f, 1.0f};
                        glm::vec3 c1{0.0f, 0.0f, 0.0f};
                        glm::vec3 k0{1.0f, 1.0f, 1.0f};
                        float k1a = 1.0f;
                    };

                    const auto resolveGrowlTevState = [&](const GrowlWaveVFX::Config::DrawPass& pass) {
                        GrowlTevState tev;
                        tev.c0 = pass.overrideTev ? pass.tevC0 : growlSnapshot.config.tevC0;
                        tev.c1 = pass.overrideTev ? pass.tevC1 : growlSnapshot.config.tevC1;
                        tev.k0 = pass.overrideTev ? pass.tevK0 : growlSnapshot.config.tevK0;
                        tev.k1a = pass.overrideTev ? pass.tevK1A : growlSnapshot.config.tevK1A;
                        tev.c0 = glm::clamp(tev.c0, glm::vec3(0.0f), glm::vec3(1.0f));
                        tev.c1 = glm::clamp(tev.c1, glm::vec3(0.0f), glm::vec3(1.0f));
                        tev.k0 = glm::clamp(tev.k0, glm::vec3(0.0f), glm::vec3(1.0f));
                        tev.k1a = growlClamp01(tev.k1a);
                        return tev;
                    };

                    const auto effectiveGrowlFragPath = [&](const GrowlWaveVFX::Config::DrawPass& pass) {
                        return toLowerCopy(
                            pass.fragShaderPath.empty() ? growlSnapshot.config.fragShaderPath : pass.fragShaderPath);
                    };

                    const auto isGrowlLinePass = [&](const GrowlWaveVFX::Config::DrawPass& pass) {
                        return effectiveGrowlFragPath(pass).find("growl_line_shared") != std::string::npos;
                    };

                    const auto isGrowlQuarterRingPass = [&](const GrowlWaveVFX::Config::DrawPass& pass) {
                        if (pass.textureQuarterRing) return true;
                        return effectiveGrowlFragPath(pass).find("growl_quarter_ring_shared") != std::string::npos;
                    };

                    const auto resolveGrowlSharedTexture =
                        [&](const GrowlWaveVFX::Config::DrawPass& pass,
                            const GrowlTevState& tev) -> BackendTextureCacheEntry* {
                            if (isGrowlLinePass(pass) || pass.texturePath.empty()) {
                                return ensureBackendTextureLoaded("");
                            }

                            BackendTextureCacheEntry* rawTex = ensureBackendTextureLoaded(pass.texturePath);
                            if (!rawTex || !rawTex->valid || rawTex->rgba.empty() ||
                                rawTex->width <= 0 || rawTex->height <= 0) {
                                return nullptr;
                            }

                            const bool quarterPass = isGrowlQuarterRingPass(pass);
                            if (backendTextureByPath.empty()) backendTextureByPath.reserve(64u);
                            const std::string bakedKey =
                                std::string("__growl_baked:") + pass.id + ":" +
                                (quarterPass ? "q:" : "m:") +
                                (pass.texturePath.empty() ? std::string("__white__") : pass.texturePath);
                            auto& baked = backendTextureByPath[bakedKey];
                            if (baked.attemptedLoad) {
                                return baked.valid ? &baked : nullptr;
                            }

                            baked.attemptedLoad = true;
                            baked.valid = false;
                            baked.width = rawTex->width;
                            baked.height = rawTex->height;
                            baked.rgba.clear();
                            baked.rgba.resize(rawTex->rgba.size(), 0u);

                            const glm::vec3 tint = glm::clamp(pass.tintColor, glm::vec3(0.0f), glm::vec3(1.0f));
                            for (std::size_t i = 0; i + 3u < rawTex->rgba.size(); i += 4u) {
                                const float tr = static_cast<float>(rawTex->rgba[i + 0u]) / 255.0f;
                                const float tg = static_cast<float>(rawTex->rgba[i + 1u]) / 255.0f;
                                const float tb = static_cast<float>(rawTex->rgba[i + 2u]) / 255.0f;
                                const float ta = static_cast<float>(rawTex->rgba[i + 3u]) / 255.0f;

                                glm::vec3 rgb(1.0f);
                                float alpha = ta;
                                if (quarterPass) {
                                    rgb = glm::vec3(
                                        growlTevMixU8Scalar(tev.c1.r, tev.c0.r, tr),
                                        growlTevMixU8Scalar(tev.c1.g, tev.c0.g, tg),
                                        growlTevMixU8Scalar(tev.c1.b, tev.c0.b, tb));
                                    rgb *= tint;
                                    alpha = growlAlpha6bit(ta * tev.k1a);
                                } else {
                                    const glm::vec3 tevInput = pass.useAlphaMaskForColor
                                        ? glm::vec3(ta, ta, ta)
                                        : glm::vec3(tr, tg, tb);
                                    const glm::vec3 stage1 = glm::mix(tev.c1, tev.k0, tevInput);
                                    rgb = tint * (tev.c0 * stage1);
                                    alpha = ta;
                                }

                                rgb = glm::clamp(rgb, glm::vec3(0.0f), glm::vec3(1.0f));
                                baked.rgba[i + 0u] = growlU8(rgb.r);
                                baked.rgba[i + 1u] = growlU8(rgb.g);
                                baked.rgba[i + 2u] = growlU8(rgb.b);
                                baked.rgba[i + 3u] = growlU8(alpha);
                            }

                            baked.valid = true;
                            return &baked;
                        };

                    const auto appendTransformedMesh =
                        [&](WorldIndexedBatch& batch,
                            const runtime::backend_model::MeshData& mesh,
                            const glm::mat4& world,
                            const glm::vec4& color,
                            bool quantizeLineAlpha,
                            float lineTevK1A) {
                            if (mesh.vertices.empty() || mesh.indices.size() < 3u) return;
                            const std::uint32_t baseVertex =
                                static_cast<std::uint32_t>(batch.vertices.size());
                            batch.vertices.reserve(batch.vertices.size() + mesh.vertices.size());
                            for (const auto& src : mesh.vertices) {
                                const glm::vec4 wp = world * glm::vec4(src.position, 1.0f);
                                IRenderBackend::WorldMeshVertex vtx;
                                vtx.x = wp.x;
                                vtx.y = wp.y;
                                vtx.z = wp.z;
                                vtx.u = src.uv.x;
                                vtx.v = src.uv.y;
                                vtx.r = color.r;
                                vtx.g = color.g;
                                vtx.b = color.b;
                                const float srcAlpha = std::clamp(src.color.a, 0.0f, 1.0f);
                                if (quantizeLineAlpha) {
                                    const float alpha255 =
                                        std::clamp(srcAlpha * std::clamp(lineTevK1A, 0.0f, 1.0f) * 255.0f,
                                                   0.0f,
                                                   255.0f);
                                    const float quantized = std::floor(alpha255 * 0.25f) / 63.0f;
                                    vtx.a = std::clamp(color.a * quantized, 0.0f, 1.0f);
                                } else {
                                    vtx.a = std::clamp(color.a * srcAlpha, 0.0f, 1.0f);
                                }
                                batch.vertices.push_back(vtx);
                            }
                            batch.indices.reserve(batch.indices.size() + mesh.indices.size());
                            for (std::uint32_t idx : mesh.indices) {
                                batch.indices.push_back(baseVertex + idx);
                            }
                        };

                    const auto appendQuarterRing =
                        [&](WorldIndexedBatch& batch,
                            const glm::mat4& world,
                            const glm::vec4& color) {
                            static constexpr glm::vec3 kPositions[4] = {
                                glm::vec3(0.0f, 0.0f, 0.0f),
                                glm::vec3(1.0f, 0.0f, 0.0f),
                                glm::vec3(0.0f, 0.0f, 1.0f),
                                glm::vec3(1.0f, 0.0f, 1.0f),
                            };
                            static constexpr glm::vec2 kUvs[4] = {
                                glm::vec2(1.0f, 1.0f),
                                glm::vec2(0.0f, 1.0f),
                                glm::vec2(1.0f, 0.0f),
                                glm::vec2(0.0f, 0.0f),
                            };
                            static constexpr std::uint32_t kIndices[6] = {0u, 1u, 2u, 2u, 1u, 3u};
                            const std::uint32_t baseVertex =
                                static_cast<std::uint32_t>(batch.vertices.size());
                            batch.vertices.reserve(batch.vertices.size() + 4u);
                            batch.indices.reserve(batch.indices.size() + 6u);
                            for (int i = 0; i < 4; ++i) {
                                const glm::vec4 wp = world * glm::vec4(kPositions[i], 1.0f);
                                IRenderBackend::WorldMeshVertex vtx;
                                vtx.x = wp.x;
                                vtx.y = wp.y;
                                vtx.z = wp.z;
                                vtx.u = kUvs[i].x;
                                vtx.v = kUvs[i].y;
                                vtx.r = color.r;
                                vtx.g = color.g;
                                vtx.b = color.b;
                                vtx.a = color.a;
                                batch.vertices.push_back(vtx);
                            }
                            for (std::uint32_t idx : kIndices) {
                                batch.indices.push_back(baseVertex + idx);
                            }
                        };

                    for (const auto& pass : growlSnapshot.drawPasses) {
                        if (!pass.enabled) continue;
                        const GrowlTevState passTev = resolveGrowlTevState(pass);
                        const bool drawLinePass = isGrowlLinePass(pass);

                        const bool drawQuarterRing = pass.textureQuarterRing;
                        const runtime::backend_model::MeshData* passMesh = nullptr;
                        if (!drawQuarterRing) {
                            if (pass.meshPath.empty()) continue;
                            passMesh = ensureBackendMeshLoaded(pass.meshPath);
                            if (!passMesh) continue;
                        }

                        BackendTextureCacheEntry* tex = resolveGrowlSharedTexture(pass, passTev);
                        if (!tex) tex = ensureBackendTextureLoaded("");
                        if (!tex || !tex->valid || tex->rgba.empty()) continue;

                        WorldIndexedBatch batch;
                        batch.textureKey =
                            std::string("growl:") + pass.id + ":" +
                            (pass.texturePath.empty() ? std::string("__white__") : pass.texturePath);
                        batch.textureRgba = tex->rgba.data();
                        batch.textureWidth = tex->width;
                        batch.textureHeight = tex->height;
                        batch.textureWrapS = 10497;
                        batch.textureWrapT = 10497;
                        batch.alphaMode = 2u;
                        batch.blendMode = 1u; // Legacy growl passes use additive blending.
                        batch.alphaCutoff = 0.0f;

                        const glm::vec3 passMeshForwardAxis = pass.overrideMeshForwardAxis
                            ? pass.meshForwardAxis
                            : defaultMeshForward;
                        const glm::vec3 meshForwardLocal =
                            (glm::dot(passMeshForwardAxis, passMeshForwardAxis) <= 0.0001f)
                                ? glm::vec3(0.0f, 1.0f, 0.0f)
                                : glm::normalize(passMeshForwardAxis);
                        const glm::vec3 meshForwardAxisWeight = meshForwardLocal * meshForwardLocal;
                        const glm::vec3 passTint = drawLinePass
                            ? glm::clamp(passTev.c0 * glm::clamp(pass.tintColor, glm::vec3(0.0f), glm::vec3(1.0f)),
                                         glm::vec3(0.0f),
                                         glm::vec3(1.0f))
                            : glm::vec3(1.0f, 1.0f, 1.0f);

                        float sortDepth = 0.0f;
                        bool hasGeometry = false;

                        for (const auto& ring : growlSnapshot.rings) {
                            const float life = std::max(0.0001f, ring.lifeSec);
                            const float age01 = glm::clamp(ring.ageSec / life, 0.0f, 1.0f);
                            const float scale =
                                glm::mix(ring.startScale, ring.endScale, age01) * std::max(0.0f, pass.scaleMul);
                            if (scale <= 0.0001f) continue;

                            const glm::vec3 ringForward = safeNormalize3(ring.forward, glm::vec3(0.0f, 0.0f, 1.0f));
                            glm::vec3 right = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), ringForward);
                            right = safeNormalize3(right, glm::vec3(1.0f, 0.0f, 0.0f));
                            glm::vec3 up = glm::cross(ringForward, right);
                            up = safeNormalize3(up, glm::vec3(0.0f, 1.0f, 0.0f));

                            std::vector<glm::vec3> localDirectionsFallback;
                            const std::vector<glm::vec3>* localDirections = &pass.directionsLocal;
                            if (localDirections->empty()) {
                                localDirectionsFallback.push_back(
                                    pass.overrideDirection ? pass.directionLocal : glm::vec3(0.0f, 0.0f, 1.0f));
                                localDirections = &localDirectionsFallback;
                            }
                            if (localDirections->empty()) continue;

                            float fade = 1.0f;
                            if (age01 > fadeStart) {
                                const float t = (age01 - fadeStart) / std::max(0.0001f, (1.0f - fadeStart));
                                fade = 1.0f - glm::clamp(t, 0.0f, 1.0f);
                            }
                            if (fade <= 0.001f) continue;

                            const float radiusMul = std::max(0.0f, pass.radiusMul);
                            const float thicknessMul = std::max(0.0f, pass.thicknessMul);
                            const glm::vec3 axisScale =
                                glm::vec3(radiusMul) + (thicknessMul - radiusMul) * meshForwardAxisWeight;
                            const glm::vec3 finalScale = glm::vec3(scale) * axisScale;

                            for (std::size_t dirIndex = 0; dirIndex < localDirections->size(); ++dirIndex) {
                                glm::vec3 localDirBasisRaw = (*localDirections)[dirIndex];
                                if (glm::dot(localDirBasisRaw, localDirBasisRaw) <= 0.000001f) continue;

                                if (pass.directionSpacingJitterDeg > 0.0001f && localDirections->size() > 1u) {
                                    const glm::vec2 baseXY(localDirBasisRaw.x, localDirBasisRaw.y);
                                    const float xyLen = glm::length(baseXY);
                                    if (xyLen > 0.0001f) {
                                        const float baseAngle = std::atan2(baseXY.y, baseXY.x);
                                        const std::uint32_t passSalt =
                                            static_cast<std::uint32_t>(pass.eid) * 0x9e3779b9u;
                                        const std::uint32_t dirSalt =
                                            static_cast<std::uint32_t>(dirIndex) * 0x85ebca6bu;
                                        const float noise =
                                            hash01(ring.randomSeed ^ passSalt ^ dirSalt ^ 0x68e31da4u);
                                        const float delta =
                                            glm::radians(pass.directionSpacingJitterDeg) * (noise * 2.0f - 1.0f);
                                        const float angle = baseAngle + delta;
                                        localDirBasisRaw.x = std::cos(angle) * xyLen;
                                        localDirBasisRaw.y = std::sin(angle) * xyLen;
                                    }
                                }

                                float lineAlphaMul = std::max(0.0f, pass.alphaMul);
                                if (pass.lineAlphaMax > pass.lineAlphaMin + 0.0001f) {
                                    const std::uint32_t passSalt =
                                        static_cast<std::uint32_t>(pass.eid) * 0x9e3779b9u;
                                    const std::uint32_t dirSalt =
                                        static_cast<std::uint32_t>(dirIndex) * 0x85ebca6bu;
                                    const float noise =
                                        hash01(ring.randomSeed ^ passSalt ^ dirSalt ^ 0x4f1bbcdcu);
                                    lineAlphaMul *= glm::mix(pass.lineAlphaMin, pass.lineAlphaMax, noise);
                                }
                                float passAlphaScale = std::clamp(fade * lineAlphaMul, 0.0f, 1.0f);
                                if (!drawQuarterRing && !drawLinePass) {
                                    passAlphaScale *= passTev.k1a;
                                }
                                const float passAlpha = std::clamp(passAlphaScale, 0.0f, 1.0f);
                                if (passAlpha <= 0.001f) continue;

                                const glm::vec3 localDir = glm::normalize(localDirBasisRaw);
                                const glm::vec3 worldDir = right * localDir.x + up * localDir.y + ringForward * localDir.z;
                                if (glm::dot(worldDir, worldDir) <= 0.000001f) continue;

                                const glm::vec3 passForward = glm::normalize(worldDir);
                                const glm::quat passRot = rotationFromToSafe(meshForwardLocal, passForward);
                                const float radialRadius = pass.heightOffset * std::max(0.0f, pass.startRadiusMul);
                                const glm::vec3 radialStartOffset =
                                    (right * localDirBasisRaw.x + up * localDirBasisRaw.y) * radialRadius;
                                const glm::vec3 passPos =
                                    ring.pos + passForward * pass.forwardOffset + radialStartOffset;
                                const float distSq = glm::dot(passPos - cameraWorldPos, passPos - cameraWorldPos);
                                sortDepth = std::max(sortDepth, distSq);

                                const glm::vec4 color(passTint, passAlpha);
                                if (drawQuarterRing) {
                                    const int quarterCount = std::max(1, pass.quarterCount);
                                    for (int i = 0; i < quarterCount; ++i) {
                                        const float quarterDeg =
                                            pass.quarterStartDeg + pass.quarterStepDeg * static_cast<float>(i);
                                        const glm::quat quarterRot =
                                            glm::angleAxis(glm::radians(quarterDeg), meshForwardLocal);
                                        const glm::mat4 world =
                                            glm::translate(glm::mat4(1.0f), passPos) *
                                            glm::mat4_cast(passRot * quarterRot) *
                                            glm::scale(glm::mat4(1.0f), finalScale);
                                        appendQuarterRing(batch, world, color);
                                        hasGeometry = true;
                                    }
                                } else if (passMesh) {
                                    const glm::mat4 world =
                                        glm::translate(glm::mat4(1.0f), passPos) *
                                        glm::mat4_cast(passRot) *
                                        glm::scale(glm::mat4(1.0f), finalScale);
                                    appendTransformedMesh(
                                        batch,
                                        *passMesh,
                                        world,
                                        color,
                                        drawLinePass,
                                        passTev.k1a);
                                    hasGeometry = true;
                                }
                            }
                        }

                        if (hasGeometry && !batch.vertices.empty() && !batch.indices.empty()) {
                            batch.sortDepth = sortDepth;
                            worldIndexedBatches.push_back(std::move(batch));
                        }
                    }
                };
                struct SharedTailFireAnchor {
                    bool valid = false;
                    glm::vec3 pos{0.0f};
                    glm::mat3 basis{1.0f};
                    glm::vec3 backDir{0.0f, 1.0f, 0.0f};
                    float particleSizeScale = 1.0f;
                };
                std::unordered_map<int, SharedTailFireAnchor> sharedTailFireAnchors;
                sharedTailFireAnchors.reserve(16u);
                const auto getSharedTailFireFallbackCfg = [&]() -> const TailFireVFX::Config& {
                    static TailFireVFX::Config sTailFireFallbackCfg{};
                    static bool sTailFireFallbackCfgLoaded = false;
                    if (!sTailFireFallbackCfgLoaded) {
                        TailFireVFX::Config cfg;
                        TailFireVFXConfigDB::get().ensureLoaded();
                        TailFireVFXConfigDB::get().applyIfAny("charmander", cfg);
                        sTailFireFallbackCfg = cfg;
                        sTailFireFallbackCfgLoaded = true;
                    }
                    return sTailFireFallbackCfg;
                };
                const auto appendSharedCaptureAttemptVfx = [&]() {
                    if (!gameWorld) return;
                    if (!renderWorld) return;

                    std::vector<GameWorld::CaptureAttemptRenderSnapshot> captureSnaps;
                    if (!gameWorld->buildCaptureAttemptRenderSnapshots(captureSnaps)) return;

                    const BackendItemAtlasIcon* icon = findBackendItemAtlasIcon("pokeball");
                    if (!icon) return;
                    const glm::vec2 uvMin = backendItemAtlasUvMin(icon->row, icon->col);
                    const glm::vec2 uvMax = backendItemAtlasUvMax(icon->row, icon->col);
                    const std::string atlasPath = "assets/images/items_atlas.png";

                    for (const auto& snap : captureSnaps) {
                        float sx = 0.0f;
                        float sy = 0.0f;
                        float sz = 0.0f;
                        if (!projectWorld(snap.ballPos, sx, sy, sz)) continue;
                        if (sz < 0.0f || sz > 1.0f) continue;

                        const float worldRadius = std::max(0.03f, worldCellSize * 0.17f * std::max(0.05f, snap.ballScale));
                        float sx2 = 0.0f;
                        float sy2 = 0.0f;
                        float sz2 = 0.0f;
                        float ballPx = std::max(12.0f, line * 8.0f);
                        if (projectWorld(snap.ballPos + glm::vec3(worldRadius, 0.0f, 0.0f), sx2, sy2, sz2)) {
                            ballPx = std::max(ballPx, std::abs(sx2 - sx) * 2.0f);
                        }
                        ballPx = std::clamp(ballPx, 12.0f, 96.0f);

                        IRenderBackend::DebugSprite ball;
                        ball.x = sx - ballPx * 0.5f;
                        ball.y = sy - ballPx * 0.5f;
                        ball.w = ballPx;
                        ball.h = ballPx;
                        ball.u0 = uvMin.x;
                        ball.v0 = uvMin.y;
                        ball.u1 = uvMax.x;
                        ball.v1 = uvMax.y;
                        ball.r = 1.0f;
                        ball.g = 1.0f;
                        ball.b = 1.0f;
                        ball.a = std::clamp(0.82f + (snap.phase == 3 && snap.success ? 0.12f : 0.0f), 0.0f, 1.0f);
                        ball.texturePath = atlasPath;
                        sprites.push_back(std::move(ball));

                        // Screen-space seam line to make yaw/roll visible on the 2D icon.
                        const float seamAng = glm::radians(snap.ballYawDeg + (snap.phase == 2 ? std::sin(snap.phaseTimeSec * 18.0f) * 8.0f : 0.0f));
                        IRenderBackend::DebugLine seam;
                        seam.x1 = sx - std::cos(seamAng) * (ballPx * 0.28f);
                        seam.y1 = sy - std::sin(seamAng) * (ballPx * 0.18f);
                        seam.x2 = sx + std::cos(seamAng) * (ballPx * 0.28f);
                        seam.y2 = sy + std::sin(seamAng) * (ballPx * 0.18f);
                        seam.thickness = std::max(1.0f, ballPx * 0.06f);
                        seam.r = 0.10f;
                        seam.g = 0.10f;
                        seam.b = 0.10f;
                        seam.a = 0.72f;
                        lines.push_back(seam);

                        if (snap.phase == 2) {
                            const float pulse = 0.5f + 0.5f * std::sin(snap.phaseTimeSec * 16.0f);
                            appendProjectedRing(
                                snap.ballPos + glm::vec3(0.0f, 0.03f, 0.0f),
                                std::max(0.03f, worldRadius * (0.9f + pulse * 0.35f)),
                                0.96f, 0.94f, 0.86f, 0.48f + pulse * 0.18f,
                                std::max(1.0f, line * 0.9f),
                                10);
                        } else if (snap.phase == 3) {
                            if (snap.success) {
                                const float t = std::clamp(snap.phaseTimeSec / 0.35f, 0.0f, 1.0f);
                                appendProjectedRing(
                                    snap.ballPos + glm::vec3(0.0f, 0.04f, 0.0f),
                                    std::max(0.03f, worldRadius * (1.0f + t * 1.2f)),
                                    1.00f, 0.86f, 0.28f, (1.0f - t) * 0.65f,
                                    std::max(1.0f, line * 1.0f),
                                    12);
                            } else {
                                const float t = std::clamp(snap.phaseTimeSec / 0.35f, 0.0f, 1.0f);
                                appendProjectedRing(
                                    snap.ballPos + glm::vec3(0.0f, 0.04f, 0.0f),
                                    std::max(0.03f, worldRadius * (0.9f + t * 0.9f)),
                                    1.00f, 0.42f, 0.30f, (1.0f - t) * 0.58f,
                                    std::max(1.0f, line * 0.95f),
                                    12);
                            }
                        }
                    }
                };
                const auto appendSharedParticleVfx = [&]() {
                    if (!useLegacyParticleVfxSnapshotBridge) return;
                    if (!supportsWorldIndexedMeshes || !hasWorldViewProj) return;
                    if (!gameWorld) return;

                    GameWorld::ParticleVfxSnapshots vfxSnapshots;
                    (void)gameWorld->buildParticleVfxSnapshots(vfxSnapshots);

                    const auto toBackendBlendMode =
                        [](ParticleSystem::BlendMode mode) -> std::uint8_t {
                            switch (mode) {
                            case ParticleSystem::BlendMode::Additive:
                                return 1u;
                            case ParticleSystem::BlendMode::Premultiplied:
                                return 2u;
                            case ParticleSystem::BlendMode::Alpha:
                            default:
                                return 0u;
                            }
                        };

                    const auto safeUnprojectClip =
                        [&](const glm::vec4& clipPos, glm::vec3& outWorld) -> bool {
                            const glm::vec4 world = invViewProj * clipPos;
                            if (!std::isfinite(world.x) || !std::isfinite(world.y) ||
                                !std::isfinite(world.z) || !std::isfinite(world.w)) {
                                return false;
                            }
                            if (std::fabs(world.w) <= 0.000001f) return false;
                            outWorld = glm::vec3(world) / world.w;
                            return std::isfinite(outWorld.x) && std::isfinite(outWorld.y) &&
                                   std::isfinite(outWorld.z);
                        };

                    struct ParticleVisualStyle {
                        std::string texturePath;
                        glm::vec3 color{1.0f, 1.0f, 1.0f};
                        float alpha = 1.0f;
                    };

                    const auto resolveParticleStyle =
                        [&](const ParticleSystem::RenderSnapshot& snapshot,
                            const ParticleSystem::Particle& particle,
                            float age01) -> ParticleVisualStyle {
                            ParticleVisualStyle style;
                            style.texturePath = "__proc:soft_circle";
                            const std::string frag = toLowerCopy(snapshot.shaderFragPath);
                            const float seed = std::clamp(particle.seed, 0.0f, 1.0f);
                            const float fadeInFast = std::clamp(age01 / 0.12f, 0.0f, 1.0f);
                            const float fadeOutLate = 1.0f - std::clamp((age01 - 0.70f) / 0.30f, 0.0f, 1.0f);
                            const float lifeFade = std::clamp(1.0f - age01, 0.0f, 1.0f);

                            if (snapshot.useFlipbook && !snapshot.flipbookPath.empty()) {
                                style.texturePath = snapshot.flipbookPath;
                                style.color = glm::vec3(1.0f);
                                style.alpha = std::clamp((0.18f + lifeFade * 0.95f), 0.0f, 1.0f);
                                return style;
                            }

                            if (frag.find("leaf_impact") != std::string::npos) {
                                style.texturePath = "__proc:leaf";
                                style.color = glm::mix(glm::vec3(0.12f, 0.45f, 0.15f),
                                                       glm::vec3(0.50f, 0.88f, 0.36f),
                                                       0.45f + 0.35f * seed);
                                style.alpha = std::pow(lifeFade, 0.65f);
                            } else if (frag.find("splat_impact") != std::string::npos) {
                                style.texturePath = "__proc:starburst";
                                style.color = glm::mix(glm::vec3(1.00f, 0.96f, 0.92f),
                                                       glm::vec3(0.98f, 0.78f, 0.70f),
                                                       0.55f + 0.25f * seed);
                                const float fadeIn = std::clamp(age01 / 0.06f, 0.0f, 1.0f);
                                const float fadeOut = 1.0f - std::clamp((age01 - 0.35f) / 0.65f, 0.0f, 1.0f);
                                style.alpha = 0.80f * fadeIn * fadeOut;
                            } else if (frag.find("impact_spark") != std::string::npos) {
                                style.texturePath = "__proc:dot";
                                style.color = glm::mix(glm::vec3(0.96f, 0.72f, 0.66f),
                                                       glm::vec3(0.98f, 0.92f, 0.88f),
                                                       seed);
                                style.alpha = 0.70f * fadeInFast *
                                              (1.0f - std::clamp((age01 - 0.55f) / 0.45f, 0.0f, 1.0f));
                            } else if (frag.find("claw_swipe") != std::string::npos) {
                                style.texturePath = "__proc:claw";
                                const bool metallic = seed >= 0.55f;
                                style.color = metallic ? glm::vec3(0.88f, 0.92f, 0.98f)
                                                       : glm::vec3(0.96f, 0.96f, 0.96f);
                                const float outStart = metallic ? 0.60f : 0.78f;
                                style.alpha = (metallic ? 0.95f : 1.10f) *
                                              std::clamp(age01 / (metallic ? 0.06f : 0.04f), 0.0f, 1.0f) *
                                              (1.0f - std::clamp((age01 - outStart) /
                                                                 std::max(0.01f, 1.0f - outStart),
                                                                 0.0f,
                                                                 1.0f));
                            } else if (frag.find("aqua_swoosh") != std::string::npos) {
                                style.texturePath = "__proc:swoosh";
                                if (seed < 0.40f) {
                                    style.color = glm::vec3(0.58f, 0.92f, 1.00f);
                                } else if (seed < 0.75f) {
                                    style.color = glm::vec3(0.70f, 0.95f, 1.00f);
                                } else {
                                    style.color = glm::vec3(0.52f, 0.82f, 1.00f);
                                }
                                style.alpha = 0.95f * fadeInFast * fadeOutLate;
                            } else if (frag.find("seed_projectile") != std::string::npos) {
                                style.texturePath = "__proc:seed";
                                style.color = glm::mix(glm::vec3(0.50f, 0.40f, 0.14f),
                                                       glm::vec3(0.96f, 0.86f, 0.34f),
                                                       0.50f + 0.20f * seed);
                                style.alpha = std::pow(lifeFade, 0.65f);
                            } else if (frag.find("leech_drain_dot") != std::string::npos) {
                                style.texturePath = "__proc:dot";
                                style.color = glm::mix(glm::vec3(0.10f, 0.50f, 0.18f),
                                                       glm::vec3(0.45f, 0.95f, 0.40f),
                                                       0.65f);
                                style.alpha = fadeInFast * fadeOutLate;
                            } else if (frag.find("heal_plus") != std::string::npos) {
                                style.texturePath = "__proc:plus";
                                style.color = glm::mix(glm::vec3(0.10f, 0.55f, 0.18f),
                                                       glm::vec3(0.35f, 0.95f, 0.45f),
                                                       lifeFade);
                                style.alpha = std::clamp(age01 / 0.18f, 0.0f, 1.0f) * fadeOutLate;
                            } else {
                                style.texturePath = "__proc:soft_circle";
                                style.color = glm::vec3(1.0f);
                                style.alpha = fadeInFast * fadeOutLate;
                            }

                            style.alpha = std::clamp(style.alpha, 0.0f, 1.0f);
                            style.color = glm::clamp(style.color, glm::vec3(0.0f), glm::vec3(1.0f));
                            return style;
                        };

                    const auto hashFrac01 = [](float x) {
                        const float s = std::sin(x * 12.9898f) * 43758.5453f;
                        return s - std::floor(s);
                    };
                    const auto tailFireRampOrangeRed = [](float age01) {
                        const glm::vec3 hot(1.05f, 0.42f, 0.18f);
                        const glm::vec3 mid(0.90f, 0.30f, 0.14f);
                        const glm::vec3 cool(0.58f, 0.16f, 0.10f);
                        glm::vec3 c = glm::mix(hot, mid, glm::smoothstep(0.0f, 0.65f, age01));
                        c = glm::mix(c, cool, glm::smoothstep(0.55f, 1.0f, age01));
                        c *= 0.70f;
                        const float l = glm::dot(c, glm::vec3(0.2126f, 0.7152f, 0.0722f));
                        c = glm::mix(glm::vec3(l), c, 0.95f);
                        return glm::clamp(c, glm::vec3(0.0f), glm::vec3(1.0f));
                    };
                    const auto resolveTailFirePremulAtlas =
                        [&](const std::string& atlasPath) -> BackendTextureCacheEntry* {
                            if (atlasPath.empty()) return nullptr;
                            // Legacy ParticleSystem loads VFX flipbooks with stb vertical flip enabled.
                            // Match that texture orientation here so the shared fire_tail UV logic aligns.
                            BackendTextureCacheEntry* src = ensureBackendTextureLoaded(atlasPath, true);
                            if (!src || !src->valid || src->rgba.empty() || src->width <= 0 || src->height <= 0) {
                                return nullptr;
                            }
                            const std::string key = std::string("__tailfire_premul:") + atlasPath;
                            if (backendTextureByPath.empty()) backendTextureByPath.reserve(64u);
                            auto& baked = backendTextureByPath[key];
                            if (baked.attemptedLoad) return baked.valid ? &baked : nullptr;

                            baked.attemptedLoad = true;
                            baked.valid = false;
                            baked.width = src->width;
                            baked.height = src->height;
                            baked.rgba.assign(src->rgba.size(), 0u);

                            for (std::size_t i = 0; i + 3u < src->rgba.size(); i += 4u) {
                                const float r = static_cast<float>(src->rgba[i + 0u]) / 255.0f;
                                const float g = static_cast<float>(src->rgba[i + 1u]) / 255.0f;
                                const float b = static_cast<float>(src->rgba[i + 2u]) / 255.0f;
                                const float a = static_cast<float>(src->rgba[i + 3u]) / 255.0f;
                                // Shared world textured path multiplies texture * vertex color, so keep the atlas
                                // color as-is and only premultiply by texture alpha here (avoid double darkening).
                                glm::vec3 rgb = glm::vec3(r, g, b) * a;
                                baked.rgba[i + 0u] = static_cast<unsigned char>(
                                    std::clamp<int>(static_cast<int>(std::lround(rgb.r * 255.0f)), 0, 255));
                                baked.rgba[i + 1u] = static_cast<unsigned char>(
                                    std::clamp<int>(static_cast<int>(std::lround(rgb.g * 255.0f)), 0, 255));
                                baked.rgba[i + 2u] = static_cast<unsigned char>(
                                    std::clamp<int>(static_cast<int>(std::lround(rgb.b * 255.0f)), 0, 255));
                                baked.rgba[i + 3u] = src->rgba[i + 3u];
                            }

                            baked.valid = true;
                            return &baked;
                        };

                    struct TailFireCombinedAtlasInfo {
                        BackendTextureCacheEntry* atlas = nullptr;
                        std::string cacheKey;
                        glm::vec4 rect0{0.0f, 0.0f, 1.0f, 1.0f};
                        glm::vec4 rect1{0.0f, 0.0f, 1.0f, 1.0f};
                        bool hasSecondary = false;
                    };

                    const auto resolveTailFireCombinedAtlas =
                        [&](const ParticleSystem::RenderSnapshot& snapshot) -> TailFireCombinedAtlasInfo {
                            TailFireCombinedAtlasInfo out;
                            if (!snapshot.useFlipbook || snapshot.flipbookPath.empty()) return out;
                            BackendTextureCacheEntry* primaryRaw = ensureBackendTextureLoaded(snapshot.flipbookPath, true);
                            if (!primaryRaw || !primaryRaw->valid || primaryRaw->rgba.empty() ||
                                primaryRaw->width <= 0 || primaryRaw->height <= 0) {
                                return out;
                            }
                            BackendTextureCacheEntry* secondaryRaw = nullptr;
                            if (snapshot.useSecondaryFlipbook && !snapshot.flipbookPath2.empty()) {
                                secondaryRaw = ensureBackendTextureLoaded(snapshot.flipbookPath2, true);
                                if (!(secondaryRaw && secondaryRaw->valid && !secondaryRaw->rgba.empty() &&
                                      secondaryRaw->width > 0 && secondaryRaw->height > 0)) {
                                    secondaryRaw = nullptr;
                                }
                            }

                            out.hasSecondary = (secondaryRaw != nullptr);
                            out.cacheKey = std::string("__tailfire_combined_exact__:") +
                                           snapshot.flipbookPath +
                                           "|" +
                                           (secondaryRaw ? snapshot.flipbookPath2 : std::string());
                            if (backendTextureByPath.empty()) backendTextureByPath.reserve(64u);
                            auto& combined = backendTextureByPath[out.cacheKey];
                            if (!combined.attemptedLoad) {
                                combined.attemptedLoad = true;
                                combined.valid = false;
                                const int gutter = out.hasSecondary ? 2 : 0;
                                const int atlasW = std::max(1, primaryRaw->width + gutter +
                                                                  (out.hasSecondary ? secondaryRaw->width : 0));
                                const int atlasH = std::max(
                                    1,
                                    std::max(primaryRaw->height, out.hasSecondary ? secondaryRaw->height : 0));
                                combined.width = atlasW;
                                combined.height = atlasH;
                                combined.rgba.assign(static_cast<std::size_t>(atlasW) *
                                                         static_cast<std::size_t>(atlasH) * 4u,
                                                     0u);

                                auto blit = [&](const BackendTextureCacheEntry& src, int dstX, int dstY) {
                                    for (int y = 0; y < src.height; ++y) {
                                        if (dstY + y < 0 || dstY + y >= atlasH) continue;
                                        const std::size_t srcRowBytes =
                                            static_cast<std::size_t>(src.width) * 4u;
                                        const std::size_t srcIdx =
                                            static_cast<std::size_t>(y) *
                                            static_cast<std::size_t>(src.width) * 4u;
                                        const std::size_t dstIdx =
                                            (static_cast<std::size_t>(dstY + y) *
                                                 static_cast<std::size_t>(atlasW) +
                                             static_cast<std::size_t>(dstX)) *
                                            4u;
                                        if (srcIdx + srcRowBytes <= src.rgba.size() &&
                                            dstIdx + srcRowBytes <= combined.rgba.size()) {
                                            std::memcpy(combined.rgba.data() + dstIdx,
                                                        src.rgba.data() + srcIdx,
                                                        srcRowBytes);
                                        }
                                    }
                                };
                                blit(*primaryRaw, 0, 0);
                                if (out.hasSecondary) {
                                    blit(*secondaryRaw, primaryRaw->width + gutter, 0);
                                }
                                combined.valid = true;
                            }

                            if (!combined.valid || combined.rgba.empty() || combined.width <= 0 || combined.height <= 0) {
                                return {};
                            }

                            out.atlas = &combined;
                            const float invW = 1.0f / static_cast<float>(std::max(1, combined.width));
                            const float invH = 1.0f / static_cast<float>(std::max(1, combined.height));
                            out.rect0 = glm::vec4(
                                0.0f,
                                0.0f,
                                static_cast<float>(primaryRaw->width) * invW,
                                static_cast<float>(primaryRaw->height) * invH);
                            if (out.hasSecondary) {
                                const int gutter = 2;
                                out.rect1 = glm::vec4(
                                    static_cast<float>(primaryRaw->width + gutter) * invW,
                                    0.0f,
                                    static_cast<float>(secondaryRaw->width) * invW,
                                    static_cast<float>(secondaryRaw->height) * invH);
                            } else {
                                out.rect1 = out.rect0;
                            }
                            return out;
                        };

                    const auto appendTailFireExactGpuBatch =
                        [&](const char* label, const ParticleSystem::RenderSnapshot& snapshot) -> bool {
                            if (!label || snapshot.particles.empty()) return false;
                            if (!snapshot.useFlipbook || snapshot.flipbookPath.empty()) return false;

                            TailFireCombinedAtlasInfo atlasInfo = resolveTailFireCombinedAtlas(snapshot);
                            if (!atlasInfo.atlas || !atlasInfo.atlas->valid || atlasInfo.atlas->rgba.empty()) {
                                return false;
                            }

                            WorldIndexedBatch batch;
                            batch.textureKey = std::string("particle:") + label + ":tail_fire_exact_gpu:" + atlasInfo.cacheKey;
                            batch.textureRgba = atlasInfo.atlas->rgba.data();
                            batch.textureWidth = atlasInfo.atlas->width;
                            batch.textureHeight = atlasInfo.atlas->height;
                            batch.textureWrapS = 33071; // clamp
                            batch.textureWrapT = 33071; // clamp
                            batch.alphaMode = 2u;
                            batch.blendMode = toBackendBlendMode(snapshot.renderSettings.blend);
                            batch.materialMode = 1u; // exact fire_tail in backend shader
                            batch.alphaCutoff = 0.0f;
                            batch.sortDepth = 0.0f;
                            batch.materialTimeSec = snapshot.timeSec;
                            batch.materialFlags = 1.0f + (atlasInfo.hasSecondary ? 2.0f : 0.0f);
                            batch.materialAtlasWidth = static_cast<float>(batch.textureWidth);
                            batch.materialAtlasHeight = static_cast<float>(batch.textureHeight);
                            batch.materialRect0U = atlasInfo.rect0.x;
                            batch.materialRect0V = atlasInfo.rect0.y;
                            batch.materialRect0W = atlasInfo.rect0.z;
                            batch.materialRect0H = atlasInfo.rect0.w;
                            batch.materialRect1U = atlasInfo.rect1.x;
                            batch.materialRect1V = atlasInfo.rect1.y;
                            batch.materialRect1W = atlasInfo.rect1.z;
                            batch.materialRect1H = atlasInfo.rect1.w;
                            batch.materialFlipbook0Cols = static_cast<float>(std::max(1, snapshot.flipbookCols));
                            batch.materialFlipbook0Rows = static_cast<float>(std::max(1, snapshot.flipbookRows));
                            batch.materialFlipbook0Frames = static_cast<float>(std::max(1, snapshot.flipbookFrames));
                            batch.materialFlipbook0Fps = std::max(0.0f, snapshot.flipbookFps);
                            batch.materialFlipbook1Cols = static_cast<float>(std::max(1, snapshot.flipbookCols2));
                            batch.materialFlipbook1Rows = static_cast<float>(std::max(1, snapshot.flipbookRows2));
                            batch.materialFlipbook1Frames = static_cast<float>(std::max(1, snapshot.flipbookFrames2));
                            batch.materialFlipbook1Fps = std::max(0.0f, snapshot.flipbookFps2);
                            batch.vertices.reserve(snapshot.particles.size() * 4u);
                            batch.indices.reserve(snapshot.particles.size() * 6u);

                            bool appendedAny = false;
                            for (const auto& particle : snapshot.particles) {
                                const float maxLife = std::max(0.0001f, particle.maxLifeSec);
                                float age01 = 1.0f - (particle.lifeSec / maxLife);
                                age01 = std::clamp(age01, 0.0f, 1.0f);

                                const glm::vec4 clip = viewProj * glm::vec4(particle.pos, 1.0f);
                                if (!std::isfinite(clip.x) || !std::isfinite(clip.y) ||
                                    !std::isfinite(clip.z) || !std::isfinite(clip.w)) {
                                    continue;
                                }
                                if (clip.w <= 0.0001f) continue;
                                const float ndcZ = clip.z / clip.w;
                                if (!std::isfinite(ndcZ) || ndcZ < -1.2f || ndcZ > 1.2f) continue;

                                const float pxSize = std::clamp(
                                    particle.sizePx * snapshot.pointScale / std::max(0.0001f, clip.w),
                                    3.0f,
                                    160.0f);
                                const float halfNdcX = pxSize / std::max(1, drawableW);
                                const float halfNdcY = pxSize / std::max(1, drawableH);
                                if (halfNdcX <= 0.000001f || halfNdcY <= 0.000001f) continue;

                                const float ndcX = clip.x / clip.w;
                                const float ndcY = clip.y / clip.w;
                                glm::vec3 corners[4];
                                if (!safeUnprojectClip(
                                        glm::vec4((ndcX - halfNdcX) * clip.w, (ndcY - halfNdcY) * clip.w, clip.z, clip.w),
                                        corners[0]) ||
                                    !safeUnprojectClip(
                                        glm::vec4((ndcX + halfNdcX) * clip.w, (ndcY - halfNdcY) * clip.w, clip.z, clip.w),
                                        corners[1]) ||
                                    !safeUnprojectClip(
                                        glm::vec4((ndcX + halfNdcX) * clip.w, (ndcY + halfNdcY) * clip.w, clip.z, clip.w),
                                        corners[2]) ||
                                    !safeUnprojectClip(
                                        glm::vec4((ndcX - halfNdcX) * clip.w, (ndcY + halfNdcY) * clip.w, clip.z, clip.w),
                                        corners[3])) {
                                    continue;
                                }

                                const std::uint32_t baseVertex = static_cast<std::uint32_t>(batch.vertices.size());
                                const float seed = std::clamp(particle.seed, 0.0f, 1.0f);
                                const auto pushVertex = [&](const glm::vec3& p, float u, float v) {
                                    IRenderBackend::WorldMeshVertex vtx;
                                    vtx.x = p.x;
                                    vtx.y = p.y;
                                    vtx.z = p.z;
                                    vtx.u = u;
                                    vtx.v = v;
                                    vtx.r = age01;
                                    vtx.g = seed;
                                    vtx.b = 1.0f;
                                    vtx.a = 1.0f;
                                    batch.vertices.push_back(vtx);
                                };
                                pushVertex(corners[0], 0.0f, 0.0f);
                                pushVertex(corners[1], 1.0f, 0.0f);
                                pushVertex(corners[2], 1.0f, 1.0f);
                                pushVertex(corners[3], 0.0f, 1.0f);
                                batch.indices.push_back(baseVertex + 0u);
                                batch.indices.push_back(baseVertex + 1u);
                                batch.indices.push_back(baseVertex + 2u);
                                batch.indices.push_back(baseVertex + 0u);
                                batch.indices.push_back(baseVertex + 2u);
                                batch.indices.push_back(baseVertex + 3u);
                                const float distSq =
                                    glm::dot(cameraWorldPos - particle.pos, cameraWorldPos - particle.pos);
                                batch.sortDepth = std::max(batch.sortDepth, distSq);
                                appendedAny = true;
                            }

                            if (appendedAny && !batch.vertices.empty() && !batch.indices.empty()) {
                                worldIndexedBatches.push_back(std::move(batch));
                                return true;
                            }
                            return false;
                        };

                    const auto appendSnapshotAsBillboards =
                        [&](const char* label, const ParticleSystem::RenderSnapshot& snapshot) -> bool {
                            if (!label) return false;
                            if (snapshot.particles.empty()) return false;

                            const std::uint8_t blendMode = toBackendBlendMode(snapshot.renderSettings.blend);
                            const std::string frag = toLowerCopy(snapshot.shaderFragPath);
                            const bool tailFireShader = (frag.find("fire_tail") != std::string::npos);
                            const bool tailFireExactCpuEnabled = backendUseExactTailFireCpuPathEnabled();

                            if (tailFireShader && appendTailFireExactGpuBatch(label, snapshot)) {
                                return true;
                            }

                            if (tailFireShader && snapshot.useFlipbook && !snapshot.flipbookPath.empty()) {
                                BackendTextureCacheEntry* primaryTex = resolveTailFirePremulAtlas(snapshot.flipbookPath);
                                BackendTextureCacheEntry* secondaryTex =
                                    (snapshot.useSecondaryFlipbook && !snapshot.flipbookPath2.empty())
                                        ? resolveTailFirePremulAtlas(snapshot.flipbookPath2)
                                        : nullptr;
                                if (!primaryTex || !primaryTex->valid || primaryTex->rgba.empty()) {
                                    return false;
                                }
                                BackendTextureCacheEntry* primaryRawTex =
                                    ensureBackendTextureLoaded(snapshot.flipbookPath, true);
                                BackendTextureCacheEntry* secondaryRawTex =
                                    (snapshot.useSecondaryFlipbook && !snapshot.flipbookPath2.empty())
                                        ? ensureBackendTextureLoaded(snapshot.flipbookPath2, true)
                                        : nullptr;

                                auto rawAtlasValid = [](const BackendTextureCacheEntry* t) {
                                    return t && t->valid && !t->rgba.empty() && t->width > 0 && t->height > 0;
                                };

                                if (tailFireExactCpuEnabled && rawAtlasValid(primaryRawTex)) {
                                    WorldIndexedBatch exactBatch;
                                    exactBatch.textureKey =
                                        std::string("particle:") + label + ":tail_fire_exact_cpu";
                                    exactBatch.textureWrapS = 33071;
                                    exactBatch.textureWrapT = 33071;
                                    exactBatch.alphaMode = 2u;
                                    exactBatch.blendMode = toBackendBlendMode(snapshot.renderSettings.blend);
                                    exactBatch.alphaCutoff = 0.0f;
                                    exactBatch.sortDepth = 0.0f;
                                    exactBatch.vertices.reserve(snapshot.particles.size() * 4u);
                                    exactBatch.indices.reserve(snapshot.particles.size() * 6u);

                                    // CPU port of fire_tail.frag is expensive; use cached quantized tiles so
                                    // shared paths stay inspectable while preserving the legacy shader look.
                                    const int tileSize = 24;
                                    const int tilePad = 1;
                                    const int tilePitch = tileSize + tilePad * 2;
                                    const int particleCount = static_cast<int>(snapshot.particles.size());
                                    const int atlasCols = std::max(1, static_cast<int>(std::ceil(std::sqrt(
                                        static_cast<float>(std::max(1, particleCount))))));
                                    const int atlasRows = std::max(1, (particleCount + atlasCols - 1) / atlasCols);
                                    const int atlasW = atlasCols * tilePitch;
                                    const int atlasH = atlasRows * tilePitch;
                                    exactBatch.textureWidth = atlasW;
                                    exactBatch.textureHeight = atlasH;
                                    exactBatch.ownedTextureRgba.assign(
                                        static_cast<std::size_t>(atlasW) * static_cast<std::size_t>(atlasH) * 4u, 0u);
                                    exactBatch.textureRgba = exactBatch.ownedTextureRgba.data();

                                    const auto clamp01 = [](float x) { return std::clamp(x, 0.0f, 1.0f); };
                                    const auto fractf = [](float x) { return x - std::floor(x); };
                                    const auto hash11 = [&](float x) {
                                        return fractf(std::sin(x * 12.9898f) * 43758.5453f);
                                    };
                                    const auto hash21 = [&](const glm::vec2& p) {
                                        const float n = glm::dot(p, glm::vec2(127.1f, 311.7f));
                                        return fractf(std::sin(n) * 43758.5453f);
                                    };
                                    const auto smoothstepf = [](float e0, float e1, float x) {
                                        const float d = e1 - e0;
                                        if (std::fabs(d) <= 1e-6f) {
                                            return (x < e0) ? 0.0f : 1.0f;
                                        }
                                        const float t = std::clamp((x - e0) / d, 0.0f, 1.0f);
                                        return t * t * (3.0f - 2.0f * t);
                                    };
                                    const auto valueNoise2D = [&](const glm::vec2& p) {
                                        const glm::vec2 i = glm::floor(p);
                                        const glm::vec2 f = glm::fract(p);
                                        const glm::vec2 u = f * f * (glm::vec2(3.0f) - 2.0f * f);
                                        const float a = hash21(i);
                                        const float b = hash21(i + glm::vec2(1.0f, 0.0f));
                                        const float c = hash21(i + glm::vec2(0.0f, 1.0f));
                                        const float d = hash21(i + glm::vec2(1.0f, 1.0f));
                                        return glm::mix(glm::mix(a, b, u.x), glm::mix(c, d, u.x), u.y);
                                    };
                                    const auto fbm2D = [&](glm::vec2 p) {
                                        float v = 0.0f;
                                        float a = 0.5f;
                                        for (int k = 0; k < 5; ++k) {
                                            v += a * valueNoise2D(p);
                                            p *= 2.02f;
                                            a *= 0.5f;
                                        }
                                        return v;
                                    };
                                    const auto fbmGrad = [&](const glm::vec2& p) {
                                        const float e = 0.03f;
                                        const float nx = fbm2D(p + glm::vec2(e, 0.0f)) - fbm2D(p - glm::vec2(e, 0.0f));
                                        const float ny = fbm2D(p + glm::vec2(0.0f, e)) - fbm2D(p - glm::vec2(0.0f, e));
                                        return glm::vec2(nx, ny) / (2.0f * e);
                                    };
                                    const auto curl2D = [&](const glm::vec2& p) {
                                        const glm::vec2 g = fbmGrad(p);
                                        return glm::vec2(g.y, -g.x);
                                    };
                                    const auto advect2D = [&](glm::vec2 p, float flowY, float amount) {
                                        const glm::vec2 c1 = curl2D(p * 1.30f + glm::vec2(0.0f, -flowY * 0.10f));
                                        const glm::vec2 c2 = curl2D(p * 2.70f + glm::vec2(3.1f, -flowY * 0.18f));
                                        return p + (c1 * 0.65f + c2 * 0.35f) * amount;
                                    };
                                    const auto smoothFlicker = [&](float t, float seed) {
                                        const float x = t * 9.0f + seed * 97.0f;
                                        const float i = std::floor(x);
                                        float f = fractf(x);
                                        f = f * f * (3.0f - 2.0f * f);
                                        return glm::mix(hash11(i), hash11(i + 1.0f), f);
                                    };
                                    const auto tonemapSoftLocal = [](const glm::vec3& c) {
                                        return c / (glm::vec3(1.0f) + c);
                                    };
                                    const auto sampleTextureLinear =
                                        [&](const BackendTextureCacheEntry& tex, glm::vec2 uv) -> glm::vec4 {
                                        uv = glm::clamp(uv, glm::vec2(0.0f), glm::vec2(1.0f));
                                        const float x = uv.x * static_cast<float>(std::max(1, tex.width - 1));
                                        const float y = uv.y * static_cast<float>(std::max(1, tex.height - 1));
                                        const int x0 = std::clamp(static_cast<int>(std::floor(x)), 0, tex.width - 1);
                                        const int y0 = std::clamp(static_cast<int>(std::floor(y)), 0, tex.height - 1);
                                        const int x1 = std::clamp(x0 + 1, 0, tex.width - 1);
                                        const int y1 = std::clamp(y0 + 1, 0, tex.height - 1);
                                        const float tx = x - static_cast<float>(x0);
                                        const float ty = y - static_cast<float>(y0);
                                        const auto sampleAt = [&](int sx, int sy) -> glm::vec4 {
                                            const std::size_t idx =
                                                (static_cast<std::size_t>(sy) * static_cast<std::size_t>(tex.width) +
                                                 static_cast<std::size_t>(sx)) * 4u;
                                            return glm::vec4(
                                                static_cast<float>(tex.rgba[idx + 0u]) / 255.0f,
                                                static_cast<float>(tex.rgba[idx + 1u]) / 255.0f,
                                                static_cast<float>(tex.rgba[idx + 2u]) / 255.0f,
                                                static_cast<float>(tex.rgba[idx + 3u]) / 255.0f);
                                        };
                                        const glm::vec4 c00 = sampleAt(x0, y0);
                                        const glm::vec4 c10 = sampleAt(x1, y0);
                                        const glm::vec4 c01 = sampleAt(x0, y1);
                                        const glm::vec4 c11 = sampleAt(x1, y1);
                                        return glm::mix(glm::mix(c00, c10, tx), glm::mix(c01, c11, tx), ty);
                                    };
                                    const auto sampleAtlasLegacy =
                                        [&](const BackendTextureCacheEntry& tex,
                                            int cols,
                                            int rows,
                                            int frameCount,
                                            float fps,
                                            const glm::vec2& localUV01,
                                            float seed,
                                            float t) -> glm::vec4 {
                                        const int safeCols = std::max(1, cols);
                                        const int safeRows = std::max(1, rows);
                                        const int maxFrames = std::max(1, safeCols * safeRows);
                                        const int frames = std::clamp(frameCount, 1, maxFrames);
                                        const float safeFps = std::max(0.0f, fps);
                                        if (frames <= 1 || safeFps <= 0.0f) {
                                            return sampleTextureLinear(tex, glm::clamp(localUV01, glm::vec2(0.0f), glm::vec2(1.0f)));
                                        }
                                        const float speed = glm::mix(0.85f, 1.10f, hash11(seed * 31.7f + 2.3f));
                                        const float f = std::floor(t * safeFps * speed + seed * static_cast<float>(frames));
                                        float frameF = std::fmod(f, static_cast<float>(frames));
                                        if (frameF < 0.0f) frameF += static_cast<float>(frames);
                                        const int frame = std::clamp(static_cast<int>(frameF), 0, frames - 1);
                                        const int col = frame % safeCols;
                                        const int rowFromTop = frame / safeCols;
                                        const int row = (safeRows - 1) - rowFromTop;
                                        const glm::vec2 cellUV =
                                            (glm::vec2(static_cast<float>(col), static_cast<float>(row)) + localUV01) /
                                            glm::vec2(static_cast<float>(safeCols), static_cast<float>(safeRows));
                                        return sampleTextureLinear(tex, cellUV);
                                    };
                                    const auto lickBlobs =
                                        [&](float x, float y, const glm::vec2& advP, float flowY, float seed) {
                                        const float k = y * 6.6f + flowY * 0.55f;
                                        const float seg = std::floor(k);
                                        const float ff = fractf(k);
                                        const float cx1 = (hash11(seg + seed * 31.0f) - 0.5f) * 0.95f * (1.0f - y);
                                        const float cx2 = (hash11(seg + seed * 73.0f) - 0.5f) * 0.95f * (1.0f - y);
                                        const float w = glm::mix(0.34f, 0.085f, y);
                                        const glm::vec2 q1((x - cx1) / std::max(1e-6f, w),
                                                           (ff - 0.30f) / 0.70f);
                                        const glm::vec2 q2((x - cx2) / std::max(1e-6f, w * 0.85f),
                                                           (ff - 0.45f) / 0.65f);
                                        const float m1 = 1.0f - smoothstepf(0.60f, 1.00f, glm::length(q1 * glm::vec2(1.0f, 1.45f)));
                                        const float m2 = 1.0f - smoothstepf(0.60f, 1.00f, glm::length(q2 * glm::vec2(1.0f, 1.60f)));
                                        const float br = fbm2D(advP * glm::vec2(7.0f, 12.0f) + seed * 17.0f);
                                        const float broken = smoothstepf(0.25f, 0.88f, br);
                                        const float gate =
                                            smoothstepf(0.05f, 0.22f, y) *
                                            (1.0f - smoothstepf(0.86f, 1.0f, y));
                                        return clamp01((m1 + 0.85f * m2) * broken * gate);
                                    };
                                    float tailFireCpuEvalTimeSec = snapshot.timeSec;
                                    const auto evalTailFirePixel =
                                        [&](float age01, float seed, glm::vec2 glPointCoord) -> glm::vec4 {
                                        const float t = tailFireCpuEvalTimeSec;

                                        glm::vec2 uv = glPointCoord;
                                        uv.y = 1.0f - uv.y;
                                        const glm::vec2 cc = (uv - 0.5f) * 2.0f;
                                        const float x = cc.x;
                                        const float y = clamp01(uv.y);

                                        const float bottomFade = smoothstepf(0.00f, 0.11f, y);

                                        const float baseT = smoothstepf(0.00f, 0.22f, y);
                                        const float xScaleBase = glm::mix(2.55f, 1.90f, baseT);
                                        const float yScaleBase = glm::mix(1.05f, 0.75f, baseT);
                                        const float reBase = glm::length(glm::vec2(cc.x * xScaleBase, cc.y * yScaleBase));
                                        const float radialMaskBase = 1.0f - smoothstepf(0.98f, 1.10f, reBase);
                                        const float tightMask = 1.0f - smoothstepf(0.62f, 0.88f, reBase);

                                        const float reLoose = glm::length(cc * glm::vec2(0.55f, 0.85f));
                                        const float radialMaskLoose = 1.0f - smoothstepf(0.98f, 1.20f, reLoose);

                                        float fade = (1.0f - age01);
                                        fade = std::pow(glm::mix(fade, 1.0f, 0.25f), 0.75f);

                                        glm::vec2 wobble(
                                            smoothFlicker(t * 0.9f, seed + 0.17f),
                                            smoothFlicker(t * 1.1f, seed + 0.73f));
                                        wobble -= 0.5f;

                                        const glm::vec2 local1 = uv + wobble * 0.010f;
                                        const glm::vec2 local2 = uv + wobble * 0.002f;

                                        glm::vec4 fb1(1.0f), fb2(1.0f);
                                        const bool has1 = rawAtlasValid(primaryRawTex);
                                        const bool has2 = has1 && rawAtlasValid(secondaryRawTex);
                                        if (has1) {
                                            fb1 = sampleAtlasLegacy(*primaryRawTex,
                                                                    snapshot.flipbookCols,
                                                                    snapshot.flipbookRows,
                                                                    snapshot.flipbookFrames,
                                                                    snapshot.flipbookFps,
                                                                    local1,
                                                                    seed,
                                                                    t);
                                            if (has2) {
                                                fb2 = sampleAtlasLegacy(*secondaryRawTex,
                                                                        snapshot.flipbookCols2,
                                                                        snapshot.flipbookRows2,
                                                                        snapshot.flipbookFrames2,
                                                                        snapshot.flipbookFps2,
                                                                        local2,
                                                                        seed,
                                                                        t);
                                            } else {
                                                fb2 = fb1;
                                            }
                                        }

                                        const float fb1A = clamp01(fb1.a);
                                        const float fb1Lum = clamp01(glm::dot(glm::vec3(fb1), glm::vec3(0.3333f)));

                                        const float speed = glm::mix(0.95f, 1.10f, hash11(seed * 19.31f));
                                        const float flow = t * 1.55f * speed;
                                        const float flowY = flow * glm::mix(0.75f, 1.55f, y * y);

                                        const float width = glm::mix(0.30f, 0.055f, std::pow(y, 2.35f));
                                        const float widthHybrid = width * 2.80f;

                                        float yy = (y * 2.0f - 1.0f);
                                        yy = yy * 1.45f + 0.38f;
                                        yy /= 1.12f;

                                        glm::vec2 p(x / std::max(1e-6f, widthHybrid), yy);
                                        p *= 1.22f;
                                        const float sway = fbm2D(glm::vec2(x * 1.7f, y * 3.8f) +
                                                                 glm::vec2(0.0f, -flowY * 0.65f) +
                                                                 seed * 7.0f);
                                        p.x += (sway - 0.5f) * 0.015f * (1.0f - y);

                                        const float d0 = glm::length(p);
                                        const glm::vec2 advP = advect2D(p * glm::vec2(1.20f, 1.0f) + seed * 6.0f,
                                                                        flowY,
                                                                        0.25f);
                                        const float n = fbm2D(advP * glm::vec2(2.7f, 4.5f) + seed * 11.0f);
                                        const float d = d0 + (n - 0.5f) * 0.18f * (1.0f - y);

                                        const float core = clamp01(1.0f - smoothstepf(0.00f, 0.88f, d));
                                        const float outer = clamp01(1.0f - smoothstepf(0.30f, 1.05f, d));
                                        const float blobs = lickBlobs(x, y, advP, flowY, seed);
                                        const float body = clamp01(smoothstepf(0.92f, 0.12f, d));

                                        float procAlpha = body * (0.60f + 0.55f * blobs);
                                        procAlpha *= (0.92f + 0.15f * smoothFlicker(t * 1.2f, seed));
                                        procAlpha *= bottomFade;
                                        procAlpha *= fade;
                                        procAlpha = 1.0f - std::exp(-procAlpha * 1.85f);
                                        procAlpha = glm::clamp(procAlpha, 0.0f, 0.96f);

                                        const glm::vec3 yellow(1.70f, 1.20f, 0.28f);
                                        const glm::vec3 red(1.45f, 0.18f, 0.06f);
                                        const glm::vec3 orange(1.60f, 0.55f, 0.12f);

                                        const float wave = 0.5f + 0.5f *
                                            std::sin((x * 1.8f + y * 8.5f - flowY * 4.9f) + seed * 7.0f);
                                        const float baseBoundary = 0.34f;
                                        const float segCount = 6.0f;
                                        const float kk = y * segCount - flowY * 0.55f;
                                        const float seg = std::floor(kk);
                                        const float segRand = hash11(seg + seed * 71.3f);
                                        const float segRand2 = hash11(seg + seed * 19.7f + 5.0f);
                                        const float tri1 = std::abs(fractf((x * 0.85f + y * 1.05f - flowY * 0.18f) * 2.8f + seed * 7.0f) - 0.5f) * 2.0f;
                                        const float tri2 = std::abs(fractf((x * 1.10f - y * 0.60f - flowY * 0.14f) * 3.8f + seed * 3.0f) - 0.5f) * 2.0f;
                                        float zig = glm::mix(tri1, tri2, 0.50f + 0.50f * (segRand - 0.5f));
                                        zig = smoothstepf(0.15f, 0.85f, zig);
                                        const float warp = fbm2D(advect2D(glm::vec2(x * 0.85f, y * 1.2f) + seed * 6.0f,
                                                                          flowY,
                                                                          0.22f) *
                                                                 glm::vec2(4.5f, 7.5f)) - 0.5f;

                                        float jag = 0.0f;
                                        jag += (segRand - 0.5f) * 0.10f;
                                        jag += (segRand2 - 0.5f) * 0.05f;
                                        jag += (zig - 0.5f) * 0.14f;
                                        jag += warp * 0.06f;
                                        jag *= (1.0f - 0.55f * smoothstepf(0.65f, 1.0f, y));

                                        const float boundary = glm::clamp(baseBoundary + jag, 0.14f, 0.62f);
                                        const float splitWidth = 0.11f;
                                        const float redMask = smoothstepf(boundary, boundary + splitWidth, y);

                                        glm::vec3 procRgb = glm::mix(yellow, red, redMask);
                                        const float band =
                                            smoothstepf(boundary - 0.02f, boundary + 0.02f, y) *
                                            (1.0f - smoothstepf(boundary + 0.02f, boundary + 0.10f, y));
                                        procRgb = glm::mix(procRgb, orange, 0.55f * band);
                                        const float climb =
                                            core * (1.0f - smoothstepf(0.55f, 0.95f, y)) * (0.35f + 0.65f * wave);
                                        procRgb = glm::mix(procRgb, yellow, 0.18f * climb);
                                        procRgb *= (1.18f + 0.35f * outer);

                                        glm::vec3 hybridRgb = procRgb;
                                        float hybridAlpha = procAlpha;
                                        if (has1) {
                                            const float aMod = glm::mix(0.55f, 1.65f, fb1A);
                                            const float lMod = glm::mix(0.85f, 1.25f, fb1Lum);
                                            hybridAlpha = glm::clamp(hybridAlpha * aMod, 0.0f, 0.96f);
                                            hybridRgb *= lMod;
                                            hybridRgb *= glm::mix(glm::vec3(1.0f), glm::vec3(fb1) * 1.35f, 0.30f);
                                        }

                                        glm::vec3 fb2Rgb = glm::vec3(fb2);
                                        float fb2Alpha = std::pow(clamp01(fb2.a), 0.66f);
                                        const float hot = smoothstepf(0.10f, 0.55f, 1.0f - y);
                                        const glm::vec3 tint = glm::mix(red, yellow, hot);
                                        fb2Rgb *= tint * 1.30f;
                                        fb2Alpha *= tightMask;
                                        fb2Alpha *= bottomFade;

                                        const float hybridMaskedA = hybridAlpha * radialMaskLoose * bottomFade;
                                        const float fb2MaskedA = fb2Alpha * radialMaskBase;

                                        const float mixW = 0.50f;
                                        glm::vec3 rgb = glm::mix(hybridRgb, fb2Rgb, mixW);
                                        float alpha = glm::mix(hybridMaskedA, fb2MaskedA, mixW);
                                        alpha *= fade;
                                        alpha = glm::clamp(alpha + 0.10f * outer * fade, 0.0f, 0.985f);

                                        const float exposure = 2.60f;
                                        rgb *= exposure;
                                        const float emissive = (0.85f * outer + 0.45f * core) * fade;
                                        rgb *= (1.0f + 2.10f * emissive);
                                        rgb = tonemapSoftLocal(rgb);

                                        if (alpha < 0.003f) return glm::vec4(0.0f);
                                        rgb *= alpha; // premultiplied output
                                        return glm::vec4(glm::clamp(rgb, glm::vec3(0.0f), glm::vec3(1.0f)),
                                                         glm::clamp(alpha, 0.0f, 1.0f));
                                    };

                                    auto storePixel = [&](int x, int y, const glm::vec4& c) {
                                        if (x < 0 || y < 0 || x >= atlasW || y >= atlasH) return;
                                        const std::size_t idx =
                                            (static_cast<std::size_t>(y) * static_cast<std::size_t>(atlasW) +
                                             static_cast<std::size_t>(x)) * 4u;
                                        exactBatch.ownedTextureRgba[idx + 0u] = static_cast<unsigned char>(
                                            std::clamp<int>(static_cast<int>(std::lround(clamp01(c.r) * 255.0f)), 0, 255));
                                        exactBatch.ownedTextureRgba[idx + 1u] = static_cast<unsigned char>(
                                            std::clamp<int>(static_cast<int>(std::lround(clamp01(c.g) * 255.0f)), 0, 255));
                                        exactBatch.ownedTextureRgba[idx + 2u] = static_cast<unsigned char>(
                                            std::clamp<int>(static_cast<int>(std::lround(clamp01(c.b) * 255.0f)), 0, 255));
                                        exactBatch.ownedTextureRgba[idx + 3u] = static_cast<unsigned char>(
                                            std::clamp<int>(static_cast<int>(std::lround(clamp01(c.a) * 255.0f)), 0, 255));
                                    };
                                    auto readPixel = [&](int x, int y) -> glm::vec4 {
                                        x = std::clamp(x, 0, atlasW - 1);
                                        y = std::clamp(y, 0, atlasH - 1);
                                        const std::size_t idx =
                                            (static_cast<std::size_t>(y) * static_cast<std::size_t>(atlasW) +
                                             static_cast<std::size_t>(x)) * 4u;
                                        return glm::vec4(
                                            static_cast<float>(exactBatch.ownedTextureRgba[idx + 0u]) / 255.0f,
                                            static_cast<float>(exactBatch.ownedTextureRgba[idx + 1u]) / 255.0f,
                                            static_cast<float>(exactBatch.ownedTextureRgba[idx + 2u]) / 255.0f,
                                            static_cast<float>(exactBatch.ownedTextureRgba[idx + 3u]) / 255.0f);
                                    };

                                    struct TailFireCpuTileCacheEntry {
                                        std::vector<unsigned char> rgba;
                                        std::uint64_t stamp = 0u;
                                    };
                                    static thread_local std::unordered_map<std::uint64_t, TailFireCpuTileCacheEntry>
                                        tailFireCpuTileCache;
                                    static thread_local std::uint64_t tailFireCpuTileCacheStamp = 0u;
                                    ++tailFireCpuTileCacheStamp;
                                    if (tailFireCpuTileCache.size() > 16384u) {
                                        tailFireCpuTileCache.clear();
                                    }

                                    constexpr int kTailFireCpuAgeBins = 16;
                                    constexpr int kTailFireCpuSeedBins = 16;
                                    constexpr int kTailFireCpuTimeFps = 8;
                                    const auto quantizeTailFire01 = [](float v, int bins) {
                                        const int maxBin = std::max(1, bins - 1);
                                        const int q = std::clamp(
                                            static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * maxBin)),
                                            0,
                                            maxBin);
                                        return std::pair<int, float>(q, static_cast<float>(q) / static_cast<float>(maxBin));
                                    };
                                    auto blitCachedTailTile =
                                        [&](const std::vector<unsigned char>& tileRgba, int tileX, int tileY) {
                                        if (tileRgba.size() !=
                                            static_cast<std::size_t>(tilePitch) *
                                                static_cast<std::size_t>(tilePitch) * 4u) {
                                            return false;
                                        }
                                        const int dstX0 = tileX - tilePad;
                                        const int dstY0 = tileY - tilePad;
                                        for (int ly = 0; ly < tilePitch; ++ly) {
                                            const std::size_t srcIdx =
                                                (static_cast<std::size_t>(ly) * static_cast<std::size_t>(tilePitch)) * 4u;
                                            const std::size_t dstIdx =
                                                (static_cast<std::size_t>(dstY0 + ly) * static_cast<std::size_t>(atlasW) +
                                                 static_cast<std::size_t>(dstX0)) * 4u;
                                            std::copy_n(tileRgba.data() + srcIdx,
                                                        static_cast<std::size_t>(tilePitch) * 4u,
                                                        exactBatch.ownedTextureRgba.data() + dstIdx);
                                        }
                                        return true;
                                    };
                                    auto captureTailTileToCache =
                                        [&](TailFireCpuTileCacheEntry& entry, int tileX, int tileY) {
                                        entry.rgba.assign(
                                            static_cast<std::size_t>(tilePitch) * static_cast<std::size_t>(tilePitch) * 4u,
                                            0u);
                                        const int srcX0 = tileX - tilePad;
                                        const int srcY0 = tileY - tilePad;
                                        for (int ly = 0; ly < tilePitch; ++ly) {
                                            const std::size_t srcIdx =
                                                (static_cast<std::size_t>(srcY0 + ly) * static_cast<std::size_t>(atlasW) +
                                                 static_cast<std::size_t>(srcX0)) * 4u;
                                            const std::size_t dstIdx =
                                                (static_cast<std::size_t>(ly) * static_cast<std::size_t>(tilePitch)) * 4u;
                                            std::copy_n(exactBatch.ownedTextureRgba.data() + srcIdx,
                                                        static_cast<std::size_t>(tilePitch) * 4u,
                                                        entry.rgba.data() + dstIdx);
                                        }
                                    };

                                    bool appendedAnyExact = false;
                                    for (std::size_t pi = 0; pi < snapshot.particles.size(); ++pi) {
                                        const auto& particle = snapshot.particles[pi];
                                        const float maxLife = std::max(0.0001f, particle.maxLifeSec);
                                        float age01 = 1.0f - (particle.lifeSec / maxLife);
                                        age01 = std::clamp(age01, 0.0f, 1.0f);

                                        const glm::vec4 clip = viewProj * glm::vec4(particle.pos, 1.0f);
                                        if (!std::isfinite(clip.x) || !std::isfinite(clip.y) ||
                                            !std::isfinite(clip.z) || !std::isfinite(clip.w)) {
                                            continue;
                                        }
                                        if (clip.w <= 0.0001f) continue;
                                        const float ndcZ = clip.z / clip.w;
                                        if (!std::isfinite(ndcZ) || ndcZ < -1.2f || ndcZ > 1.2f) continue;

                                        const float pxSize = std::clamp(
                                            particle.sizePx * snapshot.pointScale / std::max(0.0001f, clip.w),
                                            3.0f,
                                            160.0f);
                                        const float halfNdcX = pxSize / std::max(1, drawableW);
                                        const float halfNdcY = pxSize / std::max(1, drawableH);
                                        if (halfNdcX <= 0.000001f || halfNdcY <= 0.000001f) continue;

                                        const float ndcX = clip.x / clip.w;
                                        const float ndcY = clip.y / clip.w;
                                        glm::vec3 corners[4];
                                        if (!safeUnprojectClip(glm::vec4((ndcX - halfNdcX) * clip.w,
                                                                         (ndcY - halfNdcY) * clip.w,
                                                                         clip.z,
                                                                         clip.w),
                                                               corners[0]) ||
                                            !safeUnprojectClip(glm::vec4((ndcX + halfNdcX) * clip.w,
                                                                         (ndcY - halfNdcY) * clip.w,
                                                                         clip.z,
                                                                         clip.w),
                                                               corners[1]) ||
                                            !safeUnprojectClip(glm::vec4((ndcX + halfNdcX) * clip.w,
                                                                         (ndcY + halfNdcY) * clip.w,
                                                                         clip.z,
                                                                         clip.w),
                                                               corners[2]) ||
                                            !safeUnprojectClip(glm::vec4((ndcX - halfNdcX) * clip.w,
                                                                         (ndcY + halfNdcY) * clip.w,
                                                                         clip.z,
                                                                         clip.w),
                                                               corners[3])) {
                                            continue;
                                        }

                                        const int tileIndex = static_cast<int>(pi);
                                        const int tileCol = tileIndex % atlasCols;
                                        const int tileRow = tileIndex / atlasCols;
                                        const int tileX = tileCol * tilePitch + tilePad;
                                        const int tileY = tileRow * tilePitch + tilePad;
                                        const float seed = std::clamp(particle.seed, 0.0f, 1.0f);
                                        const auto [ageQ, ageEval] = quantizeTailFire01(age01, kTailFireCpuAgeBins);
                                        const auto [seedQ, seedEval] = quantizeTailFire01(seed, kTailFireCpuSeedBins);
                                        const int timeQ = (static_cast<int>(std::floor(snapshot.timeSec *
                                                                                        static_cast<float>(kTailFireCpuTimeFps))) &
                                                          0x7ff);
                                        tailFireCpuEvalTimeSec =
                                            static_cast<float>(timeQ) / static_cast<float>(kTailFireCpuTimeFps);
                                        const std::uint64_t cacheKey =
                                            (static_cast<std::uint64_t>(tileSize & 0xff) << 0) |
                                            (static_cast<std::uint64_t>(ageQ & 0xff) << 8) |
                                            (static_cast<std::uint64_t>(seedQ & 0xff) << 16) |
                                            (static_cast<std::uint64_t>(timeQ & 0x7ff) << 24) |
                                            (static_cast<std::uint64_t>(rawAtlasValid(secondaryRawTex) ? 1u : 0u) << 35);
                                        bool usedCachedTile = false;
                                        auto cacheIt = tailFireCpuTileCache.find(cacheKey);
                                        if (cacheIt != tailFireCpuTileCache.end()) {
                                            cacheIt->second.stamp = tailFireCpuTileCacheStamp;
                                            usedCachedTile = blitCachedTailTile(cacheIt->second.rgba, tileX, tileY);
                                        }

                                        if (!usedCachedTile) {
                                            for (int ty = 0; ty < tileSize; ++ty) {
                                                for (int tx = 0; tx < tileSize; ++tx) {
                                                    const glm::vec2 glPointCoord(
                                                        (static_cast<float>(tx) + 0.5f) / static_cast<float>(tileSize),
                                                        (static_cast<float>(ty) + 0.5f) / static_cast<float>(tileSize));
                                                    storePixel(
                                                        tileX + tx,
                                                        tileY + ty,
                                                        evalTailFirePixel(ageEval, seedEval, glPointCoord));
                                                }
                                            }
                                            // Duplicate edge texels into the tile padding to avoid linear-filter bleed.
                                            for (int tx = 0; tx < tileSize; ++tx) {
                                                const glm::vec4 topPx = readPixel(tileX + tx, tileY);
                                                const glm::vec4 botPx = readPixel(tileX + tx, tileY + tileSize - 1);
                                                storePixel(tileX + tx, tileY - 1, topPx);
                                                storePixel(tileX + tx, tileY + tileSize, botPx);
                                            }
                                            for (int ty = 0; ty < tileSize; ++ty) {
                                                const glm::vec4 leftPx = readPixel(tileX, tileY + ty);
                                                const glm::vec4 rightPx = readPixel(tileX + tileSize - 1, tileY + ty);
                                                storePixel(tileX - 1, tileY + ty, leftPx);
                                                storePixel(tileX + tileSize, tileY + ty, rightPx);
                                            }
                                            storePixel(tileX - 1, tileY - 1, readPixel(tileX, tileY));
                                            storePixel(tileX + tileSize, tileY - 1, readPixel(tileX + tileSize - 1, tileY));
                                            storePixel(tileX - 1, tileY + tileSize, readPixel(tileX, tileY + tileSize - 1));
                                            storePixel(tileX + tileSize,
                                                      tileY + tileSize,
                                                      readPixel(tileX + tileSize - 1, tileY + tileSize - 1));

                                            auto& cacheEntry = tailFireCpuTileCache[cacheKey];
                                            cacheEntry.stamp = tailFireCpuTileCacheStamp;
                                            captureTailTileToCache(cacheEntry, tileX, tileY);
                                        }

                                        const float u0 = (static_cast<float>(tileX) + 0.5f) / static_cast<float>(atlasW);
                                        const float v0 = (static_cast<float>(tileY) + 0.5f) / static_cast<float>(atlasH);
                                        const float u1 = (static_cast<float>(tileX + tileSize) - 0.5f) / static_cast<float>(atlasW);
                                        const float v1 = (static_cast<float>(tileY + tileSize) - 0.5f) / static_cast<float>(atlasH);

                                        const std::uint32_t baseVertex =
                                            static_cast<std::uint32_t>(exactBatch.vertices.size());
                                        const auto pushVertex = [&](const glm::vec3& p, float u, float v) {
                                            IRenderBackend::WorldMeshVertex vtx;
                                            vtx.x = p.x;
                                            vtx.y = p.y;
                                            vtx.z = p.z;
                                            vtx.u = u;
                                            vtx.v = v;
                                            vtx.r = 1.0f;
                                            vtx.g = 1.0f;
                                            vtx.b = 1.0f;
                                            vtx.a = 1.0f;
                                            exactBatch.vertices.push_back(vtx);
                                        };
                                        // Exact tail-fire tiles are CPU-baked into a top-down row-major atlas.
                                        // Flip V here so the shared billboard reads the same orientation as the
                                        // legacy point-sprite shader output.
                                        pushVertex(corners[0], u0, v1);
                                        pushVertex(corners[1], u1, v1);
                                        pushVertex(corners[2], u1, v0);
                                        pushVertex(corners[3], u0, v0);
                                        exactBatch.indices.push_back(baseVertex + 0u);
                                        exactBatch.indices.push_back(baseVertex + 1u);
                                        exactBatch.indices.push_back(baseVertex + 2u);
                                        exactBatch.indices.push_back(baseVertex + 0u);
                                        exactBatch.indices.push_back(baseVertex + 2u);
                                        exactBatch.indices.push_back(baseVertex + 3u);
                                        const float distSq =
                                            glm::dot(cameraWorldPos - particle.pos, cameraWorldPos - particle.pos);
                                        exactBatch.sortDepth = std::max(exactBatch.sortDepth, distSq);
                                        appendedAnyExact = true;
                                    }

                                    if (appendedAnyExact &&
                                        !exactBatch.vertices.empty() &&
                                        !exactBatch.indices.empty() &&
                                        !exactBatch.ownedTextureRgba.empty()) {
                                        worldIndexedBatches.push_back(std::move(exactBatch));
                                        return true;
                                    }
                                }

                                auto initParticleBatch =
                                    [&](WorldIndexedBatch& batch,
                                        const char* passName,
                                        const std::string& texPath,
                                        const BackendTextureCacheEntry& texRef) {
                                        batch = {};
                                        batch.textureKey =
                                            std::string("particle:") + label + ":" + passName + ":" + texPath;
                                        batch.textureRgba = texRef.rgba.data();
                                        batch.textureWidth = texRef.width;
                                        batch.textureHeight = texRef.height;
                                        batch.textureWrapS = 33071; // clamp
                                        batch.textureWrapT = 33071; // clamp
                                        batch.alphaMode = 2u;
                                        batch.blendMode = blendMode;
                                        batch.alphaCutoff = 0.0f;
                                        batch.sortDepth = 0.0f;
                                        batch.vertices.reserve(snapshot.particles.size() * 4u);
                                        batch.indices.reserve(snapshot.particles.size() * 6u);
                                    };

                                WorldIndexedBatch hybridBatch;
                                initParticleBatch(hybridBatch, "tail_fire_hybrid", snapshot.flipbookPath, *primaryTex);

                                WorldIndexedBatch coreBatch;
                                const bool hasSecondary =
                                    (secondaryTex && secondaryTex->valid && !secondaryTex->rgba.empty());
                                if (hasSecondary) {
                                    initParticleBatch(coreBatch, "tail_fire_core", snapshot.flipbookPath2, *secondaryTex);
                                }

                                auto computeTailFireFrameUv =
                                    [&](const ParticleSystem::Particle& particle,
                                        bool secondary,
                                        float& u0,
                                        float& v0,
                                        float& u1,
                                        float& v1) {
                                        const int cols = std::max(1, secondary ? snapshot.flipbookCols2 : snapshot.flipbookCols);
                                        const int rows = std::max(1, secondary ? snapshot.flipbookRows2 : snapshot.flipbookRows);
                                        const int maxFrames = std::max(1, cols * rows);
                                        const int frameCountRaw = secondary ? snapshot.flipbookFrames2 : snapshot.flipbookFrames;
                                        const int frames = std::clamp(frameCountRaw, 1, maxFrames);
                                        const float fps = std::max(0.0f, secondary ? snapshot.flipbookFps2 : snapshot.flipbookFps);
                                        if (frames <= 1 || cols <= 0 || rows <= 0 || fps <= 0.0f) {
                                            u0 = 0.0f; v0 = 0.0f; u1 = 1.0f; v1 = 1.0f;
                                            return;
                                        }

                                        const float seed = std::clamp(particle.seed, 0.0f, 1.0f);
                                        const float speedNoise = hashFrac01(seed * 31.7f + 2.3f);
                                        const float speed = glm::mix(0.85f, 1.10f, speedNoise);
                                        const float f = std::floor(snapshot.timeSec * fps * speed + seed * static_cast<float>(frames));
                                        int frame = static_cast<int>(std::fmod(f, static_cast<float>(frames)));
                                        if (frame < 0) frame += frames;

                                        const int col = frame % cols;
                                        const int rowFromTop = frame / cols;
                                        const int row = (rows - 1) - rowFromTop;
                                        u0 = static_cast<float>(col) / static_cast<float>(cols);
                                        v0 = static_cast<float>(row) / static_cast<float>(rows);
                                        u1 = static_cast<float>(col + 1) / static_cast<float>(cols);
                                        v1 = static_cast<float>(row + 1) / static_cast<float>(rows);
                                    };

                                auto appendBillboardToBatch =
                                    [&](WorldIndexedBatch& batch,
                                        const glm::vec4& clip,
                                        const glm::vec3& particlePos,
                                        float pxSize,
                                        float sizeMul,
                                        const glm::vec3& color,
                                        float alpha,
                                        bool secondaryAtlas,
                                        const ParticleSystem::Particle& particle) -> bool {
                                        if (alpha <= 0.001f || sizeMul <= 0.0001f) return false;
                                        const float px = std::clamp(pxSize * sizeMul, 3.0f, 160.0f);
                                        const float halfNdcX = px / std::max(1, drawableW);
                                        const float halfNdcY = px / std::max(1, drawableH);
                                        if (halfNdcX <= 0.000001f || halfNdcY <= 0.000001f) return false;

                                        const float ndcX = clip.x / clip.w;
                                        const float ndcY = clip.y / clip.w;
                                        glm::vec3 corners[4];
                                        if (!safeUnprojectClip(glm::vec4((ndcX - halfNdcX) * clip.w,
                                                                         (ndcY - halfNdcY) * clip.w,
                                                                         clip.z,
                                                                         clip.w),
                                                               corners[0]) ||
                                            !safeUnprojectClip(glm::vec4((ndcX + halfNdcX) * clip.w,
                                                                         (ndcY - halfNdcY) * clip.w,
                                                                         clip.z,
                                                                         clip.w),
                                                               corners[1]) ||
                                            !safeUnprojectClip(glm::vec4((ndcX + halfNdcX) * clip.w,
                                                                         (ndcY + halfNdcY) * clip.w,
                                                                         clip.z,
                                                                         clip.w),
                                                               corners[2]) ||
                                            !safeUnprojectClip(glm::vec4((ndcX - halfNdcX) * clip.w,
                                                                         (ndcY + halfNdcY) * clip.w,
                                                                         clip.z,
                                                                         clip.w),
                                                               corners[3])) {
                                            return false;
                                        }

                                        float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
                                        computeTailFireFrameUv(particle, secondaryAtlas, u0, v0, u1, v1);

                                        const std::uint32_t baseVertex =
                                            static_cast<std::uint32_t>(batch.vertices.size());
                                        const auto pushVertex = [&](const glm::vec3& p, float u, float v) {
                                            IRenderBackend::WorldMeshVertex vtx;
                                            vtx.x = p.x;
                                            vtx.y = p.y;
                                            vtx.z = p.z;
                                            vtx.u = u;
                                            vtx.v = v;
                                            vtx.r = color.r;
                                            vtx.g = color.g;
                                            vtx.b = color.b;
                                            vtx.a = alpha;
                                            batch.vertices.push_back(vtx);
                                        };
                                        // Tail-fire atlases are loaded using the legacy ParticleSystem flip policy,
                                        // and frame-row selection already mirrors legacy sampleAtlas() indexing.
                                        // Use the normal quad-local UV winding here to avoid a double Y flip.
                                        pushVertex(corners[0], u0, v0);
                                        pushVertex(corners[1], u1, v0);
                                        pushVertex(corners[2], u1, v1);
                                        pushVertex(corners[3], u0, v1);
                                        batch.indices.push_back(baseVertex + 0u);
                                        batch.indices.push_back(baseVertex + 1u);
                                        batch.indices.push_back(baseVertex + 2u);
                                        batch.indices.push_back(baseVertex + 0u);
                                        batch.indices.push_back(baseVertex + 2u);
                                        batch.indices.push_back(baseVertex + 3u);
                                        const float distSq =
                                            glm::dot(cameraWorldPos - particlePos, cameraWorldPos - particlePos);
                                        batch.sortDepth = std::max(batch.sortDepth, distSq);
                                        return true;
                                    };

                                bool appendedAny = false;
                                for (const auto& particle : snapshot.particles) {
                                    const float maxLife = std::max(0.0001f, particle.maxLifeSec);
                                    float age01 = 1.0f - (particle.lifeSec / maxLife);
                                    age01 = std::clamp(age01, 0.0f, 1.0f);

                                    const glm::vec4 clip = viewProj * glm::vec4(particle.pos, 1.0f);
                                    if (!std::isfinite(clip.x) || !std::isfinite(clip.y) ||
                                        !std::isfinite(clip.z) || !std::isfinite(clip.w)) {
                                        continue;
                                    }
                                    if (clip.w <= 0.0001f) continue;
                                    const float ndcZ = clip.z / clip.w;
                                    if (!std::isfinite(ndcZ) || ndcZ < -1.2f || ndcZ > 1.2f) continue;

                                    const float pxSize = std::clamp(
                                        particle.sizePx * snapshot.pointScale / std::max(0.0001f, clip.w),
                                        3.0f,
                                        160.0f);

                                    const float seed = std::clamp(particle.seed, 0.0f, 1.0f);
                                    const float fade = std::pow(glm::mix(1.0f - age01, 1.0f, 0.25f), 0.75f);
                                    const float flicker =
                                        glm::mix(0.92f,
                                                 1.08f,
                                                 hashFrac01(std::floor(snapshot.timeSec * 11.0f) + seed * 91.0f));
                                    glm::vec3 hybridColor = tailFireRampOrangeRed(age01);
                                    hybridColor *= (0.95f + 0.15f * flicker);
                                    hybridColor = glm::clamp(hybridColor, glm::vec3(0.0f), glm::vec3(1.0f));
                                    const float hybridAlpha = std::clamp(
                                        (0.74f + 0.30f * (1.0f - age01)) * fade * (0.98f + 0.12f * flicker),
                                        0.0f,
                                        0.95f);
                                    const glm::vec3 hybridColorPremul = hybridColor * hybridAlpha;

                                    if (appendBillboardToBatch(
                                            hybridBatch,
                                            clip,
                                            particle.pos,
                                            pxSize,
                                            1.12f,
                                            hybridColorPremul,
                                            hybridAlpha,
                                            false,
                                            particle)) {
                                        appendedAny = true;
                                    }

                                    if (hasSecondary) {
                                        const float hot = glm::smoothstep(0.10f, 0.55f, 1.0f - age01);
                                        glm::vec3 coreTint =
                                            glm::mix(glm::vec3(1.45f, 0.18f, 0.06f),
                                                     glm::vec3(1.70f, 1.20f, 0.28f),
                                                     hot);
                                        coreTint *= 0.90f;
                                        coreTint *= (0.98f + 0.10f * flicker);
                                        coreTint = glm::clamp(coreTint, glm::vec3(0.0f), glm::vec3(1.0f));
                                        const float coreAlpha = std::clamp(
                                            (0.62f + 0.24f * (1.0f - age01)) * fade * (0.98f + 0.12f * flicker),
                                            0.0f,
                                            0.90f);
                                        const glm::vec3 coreTintPremul = coreTint * coreAlpha;
                                        if (appendBillboardToBatch(
                                                coreBatch,
                                                clip,
                                                particle.pos,
                                                pxSize,
                                                0.86f,
                                                coreTintPremul,
                                                coreAlpha,
                                                true,
                                                particle)) {
                                            appendedAny = true;
                                        }
                                    }
                                }

                                if (!hybridBatch.vertices.empty() && !hybridBatch.indices.empty()) {
                                    worldIndexedBatches.push_back(std::move(hybridBatch));
                                }
                                if (hasSecondary && !coreBatch.vertices.empty() && !coreBatch.indices.empty()) {
                                    worldIndexedBatches.push_back(std::move(coreBatch));
                                }
                                return appendedAny;
                            }

                            std::string texturePath = "__proc:soft_circle";
                            if (snapshot.useFlipbook && !snapshot.flipbookPath.empty()) {
                                texturePath = snapshot.flipbookPath;
                            } else {
                                if (frag.find("leaf_impact") != std::string::npos) texturePath = "__proc:leaf";
                                else if (frag.find("splat_impact") != std::string::npos) texturePath = "__proc:starburst";
                                else if (frag.find("impact_spark") != std::string::npos) texturePath = "__proc:dot";
                                else if (frag.find("claw_swipe") != std::string::npos) texturePath = "__proc:claw";
                                else if (frag.find("aqua_swoosh") != std::string::npos) texturePath = "__proc:swoosh";
                                else if (frag.find("seed_projectile") != std::string::npos) texturePath = "__proc:seed";
                                else if (frag.find("leech_drain_dot") != std::string::npos) texturePath = "__proc:dot";
                                else if (frag.find("heal_plus") != std::string::npos) texturePath = "__proc:plus";
                            }

                            BackendTextureCacheEntry* tex = ensureBackendTextureLoaded(texturePath);
                            if (!tex || !tex->valid || tex->rgba.empty()) {
                                tex = ensureBackendTextureLoaded("");
                            }
                            if (!tex || !tex->valid || tex->rgba.empty()) return false;

                            WorldIndexedBatch batch;
                            batch.textureKey = std::string("particle:") + label + ":" + texturePath;
                            batch.textureRgba = tex->rgba.data();
                            batch.textureWidth = tex->width;
                            batch.textureHeight = tex->height;
                            batch.textureWrapS = 33071; // clamp
                            batch.textureWrapT = 33071; // clamp
                            batch.alphaMode = 2u;
                            batch.blendMode = blendMode;
                            batch.alphaCutoff = 0.0f;
                            batch.sortDepth = 0.0f;
                            batch.vertices.reserve(snapshot.particles.size() * 4u);
                            batch.indices.reserve(snapshot.particles.size() * 6u);

                            const int cols = std::max(1, snapshot.flipbookCols);
                            const int rows = std::max(1, snapshot.flipbookRows);
                            const int maxFrames = std::max(1, cols * rows);
                            const int frames = std::clamp(snapshot.flipbookFrames, 1, maxFrames);
                            bool appendedAny = false;

                            for (const auto& particle : snapshot.particles) {
                                const float maxLife = std::max(0.0001f, particle.maxLifeSec);
                                float age01 = 1.0f - (particle.lifeSec / maxLife);
                                age01 = std::clamp(age01, 0.0f, 1.0f);

                                const ParticleVisualStyle style =
                                    resolveParticleStyle(snapshot, particle, age01);
                                if (style.alpha <= 0.001f) continue;

                                const glm::vec4 clip = viewProj * glm::vec4(particle.pos, 1.0f);
                                if (!std::isfinite(clip.x) || !std::isfinite(clip.y) ||
                                    !std::isfinite(clip.z) || !std::isfinite(clip.w)) {
                                    continue;
                                }
                                if (clip.w <= 0.0001f) continue;
                                const float ndcZ = clip.z / clip.w;
                                if (!std::isfinite(ndcZ) || ndcZ < -1.2f || ndcZ > 1.2f) continue;

                                const float pxSize = std::clamp(
                                    particle.sizePx * snapshot.pointScale / std::max(0.0001f, clip.w),
                                    3.0f,
                                    160.0f);
                                const float halfNdcX = pxSize / std::max(1, drawableW);
                                const float halfNdcY = pxSize / std::max(1, drawableH);
                                if (halfNdcX <= 0.000001f || halfNdcY <= 0.000001f) continue;

                                const float ndcX = clip.x / clip.w;
                                const float ndcY = clip.y / clip.w;
                                glm::vec3 corners[4];
                                if (!safeUnprojectClip(
                                        glm::vec4((ndcX - halfNdcX) * clip.w, (ndcY - halfNdcY) * clip.w, clip.z, clip.w),
                                        corners[0]) ||
                                    !safeUnprojectClip(
                                        glm::vec4((ndcX + halfNdcX) * clip.w, (ndcY - halfNdcY) * clip.w, clip.z, clip.w),
                                        corners[1]) ||
                                    !safeUnprojectClip(
                                        glm::vec4((ndcX + halfNdcX) * clip.w, (ndcY + halfNdcY) * clip.w, clip.z, clip.w),
                                        corners[2]) ||
                                    !safeUnprojectClip(
                                        glm::vec4((ndcX - halfNdcX) * clip.w, (ndcY + halfNdcY) * clip.w, clip.z, clip.w),
                                        corners[3])) {
                                    continue;
                                }

                                float u0 = 0.0f;
                                float v0 = 0.0f;
                                float u1 = 1.0f;
                                float v1 = 1.0f;
                                if (snapshot.useFlipbook && frames > 1 && cols > 0 && rows > 0) {
                                    int frame = static_cast<int>(std::round(age01 * static_cast<float>(frames - 1)));
                                    frame = std::clamp(frame, 0, frames - 1);
                                    const int col = frame % cols;
                                    const int row = frame / cols;
                                    u0 = static_cast<float>(col) / static_cast<float>(cols);
                                    v0 = static_cast<float>(row) / static_cast<float>(rows);
                                    u1 = static_cast<float>(col + 1) / static_cast<float>(cols);
                                    v1 = static_cast<float>(row + 1) / static_cast<float>(rows);
                                }

                                const std::uint32_t baseVertex =
                                    static_cast<std::uint32_t>(batch.vertices.size());
                                const auto pushVertex =
                                    [&](const glm::vec3& p, float u, float v) {
                                        IRenderBackend::WorldMeshVertex vtx;
                                        vtx.x = p.x;
                                        vtx.y = p.y;
                                        vtx.z = p.z;
                                        vtx.u = u;
                                        vtx.v = v;
                                        vtx.r = style.color.r;
                                        vtx.g = style.color.g;
                                        vtx.b = style.color.b;
                                        vtx.a = style.alpha;
                                        batch.vertices.push_back(vtx);
                                    };
                                pushVertex(corners[0], u0, v0);
                                pushVertex(corners[1], u1, v0);
                                pushVertex(corners[2], u1, v1);
                                pushVertex(corners[3], u0, v1);
                                batch.indices.push_back(baseVertex + 0u);
                                batch.indices.push_back(baseVertex + 1u);
                                batch.indices.push_back(baseVertex + 2u);
                                batch.indices.push_back(baseVertex + 0u);
                                batch.indices.push_back(baseVertex + 2u);
                                batch.indices.push_back(baseVertex + 3u);

                                const float distSq =
                                    glm::dot(cameraWorldPos - particle.pos, cameraWorldPos - particle.pos);
                                batch.sortDepth = std::max(batch.sortDepth, distSq);
                            }

                            if (!batch.vertices.empty() && !batch.indices.empty()) {
                                worldIndexedBatches.push_back(std::move(batch));
                                appendedAny = true;
                            }
                            return appendedAny;
                        };

                    bool appendedTailFireBillboards =
                        appendSnapshotAsBillboards("tail_fire", vfxSnapshots.tailFire);
                    appendSnapshotAsBillboards("grass_impact", vfxSnapshots.grassImpact);
                    appendSnapshotAsBillboards("tackle_burst", vfxSnapshots.tackleBurst);
                    appendSnapshotAsBillboards("tackle_spark", vfxSnapshots.tackleSpark);
                    appendSnapshotAsBillboards("leech_seed_projectile", vfxSnapshots.leechSeedProjectile);
                    const bool appendedLeechDrainBillboards =
                        appendSnapshotAsBillboards("leech_seed_drain", vfxSnapshots.leechSeedDrain);
                    appendSnapshotAsBillboards("heal_plus", vfxSnapshots.healPlus);
                    appendSnapshotAsBillboards("claw_swipe", vfxSnapshots.clawSwipe);
                    appendSnapshotAsBillboards("aqua_swoosh", vfxSnapshots.aquaSwoosh);

                    if (!appendedTailFireBillboards && gameWorld) {
                        const TailFireVFX::Config& sTailFireFallbackCfg = getSharedTailFireFallbackCfg();
                        struct SharedTailFireFallbackEmitterState {
                            ParticleSystem particles;
                            bool configured = false;
                            double lastSimTimeSec = -1.0;
                            std::unordered_map<int, float> emitAccumulator;
                            std::unordered_map<int, std::uint32_t> spawnSerial;
                            std::unordered_map<int, glm::vec3> prevTailWorld;
                            std::unordered_map<int, glm::vec3> smoothedTailWorld;
                            std::unordered_map<int, int> prevAnimIndex;
                            std::unordered_map<int, float> prevAnimTimeSec;
                            std::unordered_map<int, glm::vec3> filteredTailVel;
                        };
                        static thread_local SharedTailFireFallbackEmitterState sTailFireFallbackState;

                        const auto resetSharedTailFireFallbackState = [&]() {
                            sTailFireFallbackState.particles.shutdown();
                            sTailFireFallbackState.configured = false;
                            sTailFireFallbackState.lastSimTimeSec = -1.0;
                            sTailFireFallbackState.emitAccumulator.clear();
                            sTailFireFallbackState.spawnSerial.clear();
                            sTailFireFallbackState.prevTailWorld.clear();
                            sTailFireFallbackState.smoothedTailWorld.clear();
                            sTailFireFallbackState.prevAnimIndex.clear();
                            sTailFireFallbackState.prevAnimTimeSec.clear();
                            sTailFireFallbackState.filteredTailVel.clear();
                        };
                        const auto ensureSharedTailFireFallbackConfigured = [&]() {
                            if (sTailFireFallbackState.configured) return;
                            sTailFireFallbackState.particles.setShaderPaths(
                                sTailFireFallbackCfg.vertShaderPath,
                                sTailFireFallbackCfg.fragShaderPath);
                            sTailFireFallbackState.particles.setUseFlipbook(sTailFireFallbackCfg.useFlipbook);
                            if (sTailFireFallbackCfg.useFlipbook) {
                                sTailFireFallbackState.particles.setFlipbook(
                                    sTailFireFallbackCfg.flipbookPath,
                                    sTailFireFallbackCfg.flipbookCols,
                                    sTailFireFallbackCfg.flipbookRows,
                                    sTailFireFallbackCfg.flipbookFrames,
                                    sTailFireFallbackCfg.flipbookFps);
                                if (sTailFireFallbackCfg.useFlipbook2) {
                                    sTailFireFallbackState.particles.setSecondaryFlipbook(
                                        sTailFireFallbackCfg.flipbook2Path,
                                        sTailFireFallbackCfg.flipbook2Cols,
                                        sTailFireFallbackCfg.flipbook2Rows,
                                        sTailFireFallbackCfg.flipbook2Frames,
                                        sTailFireFallbackCfg.flipbook2Fps);
                                } else {
                                    sTailFireFallbackState.particles.setSecondaryFlipbook("", 1, 1, 1, 0.0f);
                                }
                            } else {
                                sTailFireFallbackState.particles.setSecondaryFlipbook("", 1, 1, 1, 0.0f);
                            }
                            ParticleSystem::RenderSettings rs;
                            rs.blend = sTailFireFallbackCfg.blend;
                            rs.depthTest = sTailFireFallbackCfg.depthTest;
                            rs.depthWrite = sTailFireFallbackCfg.depthWrite;
                            rs.programPointSize = true;
                            sTailFireFallbackState.particles.setRenderSettings(rs);
                            ParticleSystem::UpdateSettings us;
                            us.acceleration = sTailFireFallbackCfg.acceleration;
                            us.dampingBase = sTailFireFallbackCfg.dampingBase;
                            sTailFireFallbackState.particles.setUpdateSettings(us);
                            sTailFireFallbackState.particles.setPointScale(sTailFireFallbackCfg.pointScale);
                            sTailFireFallbackState.configured = true;
                        };
                        ensureSharedTailFireFallbackConfigured();

                        auto tailHash01 = [](float x) {
                            const float s = std::sin(x * 12.9898f) * 43758.5453f;
                            return s - std::floor(s);
                        };
                        auto tailHashSigned = [&](float x) {
                            return tailHash01(x) * 2.0f - 1.0f;
                        };
                        auto safeNormOr = [](glm::vec3 v, const glm::vec3& fallback) {
                            const float len2 = glm::dot(v, v);
                            if (len2 <= 1e-10f) return fallback;
                            return v * (1.0f / std::sqrt(len2));
                        };

                        const auto emitSharedTailFireForList =
                            [&](float dt, const std::vector<PokemonInstance>& list) {
                                dt = std::clamp(dt, 0.0f, 0.05f);
                                if (dt <= 0.0f) return;

                                for (const auto& unit : list) {
                                    const std::string species = toLowerCopy(unit.name);
                                    if (species != "charmander") continue;
                                    if (!unit.alive) continue;

                                    float& acc = sTailFireFallbackState.emitAccumulator[unit.id];
                                    acc += dt * sTailFireFallbackCfg.emitRatePerSec;
                                    int emitCount = static_cast<int>(std::floor(acc));
                                    if (emitCount <= 0) continue;
                                    acc -= static_cast<float>(emitCount);

                                    int animIdx = unit.activeAnimIndex;
                                    if (animIdx < 0) animIdx = unit.animIdleIndex;

                                    {
                                        int& prevIdx = sTailFireFallbackState.prevAnimIndex[unit.id];
                                        if (prevIdx != animIdx) {
                                            prevIdx = animIdx;
                                            sTailFireFallbackState.prevTailWorld.erase(unit.id);
                                            sTailFireFallbackState.smoothedTailWorld.erase(unit.id);
                                            sTailFireFallbackState.prevAnimTimeSec.erase(unit.id);
                                            sTailFireFallbackState.filteredTailVel.erase(unit.id);
                                        }
                                    }

                                    bool timeWrapped = false;
                                    {
                                        auto itT = sTailFireFallbackState.prevAnimTimeSec.find(unit.id);
                                        if (itT == sTailFireFallbackState.prevAnimTimeSec.end()) {
                                            sTailFireFallbackState.prevAnimTimeSec[unit.id] = unit.animTimeSec;
                                        } else {
                                            const float prevT = itT->second;
                                            if (unit.animTimeSec + 1e-4f < prevT) timeWrapped = true;
                                            itT->second = unit.animTimeSec;
                                        }
                                    }
                                    if (timeWrapped) {
                                        sTailFireFallbackState.prevTailWorld.erase(unit.id);
                                        sTailFireFallbackState.smoothedTailWorld.erase(unit.id);
                                        sTailFireFallbackState.filteredTailVel.erase(unit.id);
                                    }

                                    const auto extents =
                                        game::runtime::backend_proxy::computeUnitProxyExtents(unit, worldCellSize);
                                    if (extents.height <= 0.0001f) continue;

                                    const auto anchorIt = sharedTailFireAnchors.find(unit.id);
                                    const bool hasTailAnchor =
                                        (anchorIt != sharedTailFireAnchors.end()) && anchorIt->second.valid;
                                    const SharedTailFireAnchor tailAnchorData =
                                        hasTailAnchor ? anchorIt->second : SharedTailFireAnchor{};

                                    const glm::vec3 center =
                                        unit.position + glm::vec3(0.0f, unit.visualYOffset, 0.0f);
                                    const glm::vec3 up(0.0f, 1.0f, 0.0f);
                                    const glm::vec3 fwd = game::runtime::backend_proxy::yawForward(unit.rotation.y);
                                    const glm::vec3 right = game::runtime::backend_proxy::yawRight(unit.rotation.y);
                                    const float scaleMul =
                                        std::clamp(extents.height / std::max(0.05f, worldCellSize * 0.72f),
                                                   0.80f,
                                                   2.40f);
                                    const float spawnRadius =
                                        std::max(0.004f,
                                                 sTailFireFallbackCfg.spawnRadius *
                                                     (hasTailAnchor ? tailAnchorData.particleSizeScale : scaleMul));
                                    const float tailBackOffset =
                                        std::max(0.03f,
                                                 extents.halfDepth * 0.82f +
                                                     sTailFireFallbackCfg.spawnRadius * 2.5f);
                                    const glm::vec3 proxyTailDir = safeNormOr(
                                        (-fwd * 0.85f) + (up * 0.52f),
                                        glm::vec3(0.0f, 1.0f, 0.0f));
                                    const glm::vec3 tailPosWorld =
                                        hasTailAnchor
                                            ? tailAnchorData.pos
                                            : (center - fwd * tailBackOffset +
                                               up * std::max(0.02f, sTailFireFallbackCfg.tailWorldYOffset) +
                                               proxyTailDir * std::max(0.003f, spawnRadius * 0.8f));
                                    const glm::mat3 tailBasis =
                                        hasTailAnchor ? tailAnchorData.basis : glm::mat3(right, up, fwd);
                                    glm::vec3 backDirWorld =
                                        hasTailAnchor ? tailAnchorData.backDir : proxyTailDir;
                                    backDirWorld = safeNormOr(backDirWorld, glm::vec3(0.0f, 1.0f, 0.0f));

                                    glm::vec3 anchor = tailPosWorld;
                                    if (sTailFireFallbackCfg.followSmoothing > 0.0f) {
                                        auto itS = sTailFireFallbackState.smoothedTailWorld.find(unit.id);
                                        if (itS == sTailFireFallbackState.smoothedTailWorld.end()) {
                                            sTailFireFallbackState.smoothedTailWorld[unit.id] = tailPosWorld;
                                        }
                                        glm::vec3& s = sTailFireFallbackState.smoothedTailWorld[unit.id];
                                        const float a = 1.0f - std::exp(-sTailFireFallbackCfg.followSmoothing * dt);
                                        s = (1.0f - a) * s + a * tailPosWorld;
                                        anchor = s;
                                    }

                                    glm::vec3 tailVel(0.0f);
                                    {
                                        auto itPrev = sTailFireFallbackState.prevTailWorld.find(unit.id);
                                        if (itPrev != sTailFireFallbackState.prevTailWorld.end()) {
                                            const glm::vec3 prev = itPrev->second;
                                            const glm::vec3 delta = (tailPosWorld - prev);
                                            const float maxDeltaPerFrame = 0.20f;
                                            const bool discontinuity =
                                                (glm::dot(delta, delta) >
                                                 maxDeltaPerFrame * maxDeltaPerFrame);
                                            if (!discontinuity) {
                                                const float invDt = (dt > 1e-6f) ? (1.0f / dt) : 0.0f;
                                                glm::vec3 rawVel = delta * invDt;
                                                const float maxTailVel = 4.0f;
                                                const float sp2 = glm::dot(rawVel, rawVel);
                                                if (sp2 > maxTailVel * maxTailVel) {
                                                    rawVel *= (maxTailVel / std::sqrt(sp2));
                                                }

                                                auto itFilt =
                                                    sTailFireFallbackState.filteredTailVel.find(unit.id);
                                                if (itFilt ==
                                                    sTailFireFallbackState.filteredTailVel.end()) {
                                                    sTailFireFallbackState.filteredTailVel[unit.id] = rawVel;
                                                }
                                                glm::vec3& vFilt =
                                                    sTailFireFallbackState.filteredTailVel[unit.id];
                                                const float k = 25.0f;
                                                const float a = 1.0f - std::exp(-k * dt);
                                                vFilt = (1.0f - a) * vFilt + a * rawVel;
                                                tailVel = vFilt;
                                            } else {
                                                sTailFireFallbackState.filteredTailVel.erase(unit.id);
                                                tailVel = glm::vec3(0.0f);
                                            }
                                        }
                                        sTailFireFallbackState.prevTailWorld[unit.id] = tailPosWorld;
                                    }

                                    std::uint32_t& serial = sTailFireFallbackState.spawnSerial[unit.id];
                                    for (int k = 0; k < emitCount; ++k) {
                                        const float base = static_cast<float>(serial++);
                                        const float rx = tailHashSigned(base + 1.0f) * spawnRadius;
                                        const float ry = tailHashSigned(base + 2.0f) * spawnRadius;
                                        const float rz = tailHashSigned(base + 3.0f) * spawnRadius;
                                        const glm::vec3 localJitter(rx, ry, rz);
                                        const glm::vec3 worldJitter = tailBasis * localJitter;

                                        ParticleSystem::Particle p;
                                        p.pos = anchor + worldJitter;

                                        const float upVel = 0.055f + tailHash01(base + 5.0f) * 0.095f;
                                        const float backVel = 0.050f + tailHash01(base + 6.0f) * 0.050f;
                                        p.vel = glm::vec3(0.0f, upVel, 0.0f) + backDirWorld * backVel;

                                        if (sTailFireFallbackCfg.inheritVelocity != 0.0f) {
                                            glm::vec3 inh = tailVel * sTailFireFallbackCfg.inheritVelocity;
                                            const float maxInherit = 2.5f;
                                            const float inh2 = glm::dot(inh, inh);
                                            if (inh2 > maxInherit * maxInherit) {
                                                inh *= (maxInherit / std::sqrt(inh2));
                                            }
                                            p.vel += inh;
                                        }

                                        p.maxLifeSec = 0.14f + tailHash01(base + 7.0f) * 0.10f;
                                        p.lifeSec = p.maxLifeSec;
                                        const float sizeScale =
                                            hasTailAnchor ? tailAnchorData.particleSizeScale : scaleMul;
                                        p.sizePx = (0.22f + tailHash01(base + 8.0f) * 0.10f) * sizeScale;
                                        p.seed = tailHash01(base + 9.0f);
                                        sTailFireFallbackState.particles.emit(p);
                                    }
                                }
                            };

                        double simNowSec = timeSource.nowSeconds();
                        if (!std::isfinite(simNowSec)) simNowSec = 0.0;
                        if (sTailFireFallbackState.lastSimTimeSec < 0.0 ||
                            simNowSec + 1e-6 < sTailFireFallbackState.lastSimTimeSec ||
                            (simNowSec - sTailFireFallbackState.lastSimTimeSec) > 2.0) {
                            resetSharedTailFireFallbackState();
                            ensureSharedTailFireFallbackConfigured();
                            sTailFireFallbackState.lastSimTimeSec = simNowSec;
                        }
                        double simDeltaSec = simNowSec - sTailFireFallbackState.lastSimTimeSec;
                        if (!std::isfinite(simDeltaSec) || simDeltaSec < 0.0) simDeltaSec = 0.0;
                        simDeltaSec = std::min(simDeltaSec, 0.50);
                        sTailFireFallbackState.lastSimTimeSec = simNowSec;

                        while (simDeltaSec > 1e-6) {
                            const float step =
                                static_cast<float>(std::min(simDeltaSec, 0.05));
                            sTailFireFallbackState.particles.update(step);
                            emitSharedTailFireForList(step, gameWorld->getPokemons());
                            emitSharedTailFireForList(step, gameWorld->getBenchPokemons());
                            simDeltaSec -= static_cast<double>(step);
                        }

                        ParticleSystem::RenderSnapshot syntheticTailFire;
                        if (sTailFireFallbackState.particles.buildRenderSnapshot(syntheticTailFire)) {
                            syntheticTailFire.timeSec = static_cast<float>(simNowSec);
                            appendedTailFireBillboards =
                                appendSnapshotAsBillboards("tail_fire_synth", syntheticTailFire) ||
                                appendedTailFireBillboards;
                        }
                    }

                    if (!appendedTailFireBillboards ||
                        (!appendedLeechDrainBillboards && !useLegacyParticleVfxSnapshotBridge)) {
                        for (const auto& unit : gameWorld->getPokemons()) {
                            const auto extents =
                                game::runtime::backend_proxy::computeUnitProxyExtents(unit, worldCellSize);
                            const glm::vec3 proxyCenter =
                                unit.position + glm::vec3(0.0f, unit.visualYOffset, 0.0f);

                            if (!appendedTailFireBillboards) {
                                appendProjectedTailFire(
                                    unit,
                                    proxyCenter,
                                    extents,
                                    unit.rotation.y,
                                    std::max(1.0f, line * 0.92f));
                            }
                            if (!appendedLeechDrainBillboards && !useLegacyParticleVfxSnapshotBridge) {
                                appendProjectedLeechDrain(
                                    unit,
                                    std::max(0.12f, worldCellSize * 0.24f),
                                    std::max(1.0f, line));
                            }
                        }
                    }
                };
                const auto resolveModelMesh = [&](const PokemonInstance& unit)
                    -> const runtime::backend_model::MeshData* {
                    const PokemonStats* stats = dataDb.pokemon.getStats(unit.name);
                    if (!stats || stats->model.empty()) return nullptr;

                    const std::string modelPath = "assets/models/" + stats->model;
                    runtime::backend_model::MeshData* mesh = ensureBackendMeshLoaded(modelPath);
                    if (!mesh || mesh->indices.size() < 3u) {
                        return nullptr;
                    }
                    return mesh;
                };
                const std::size_t boardTrianglesStart2D = worldTriangles.size();
                const std::size_t boardTrianglesStart3D = world3DTriangles.size();
                struct DepthTri {
                    IRenderBackend::DebugTriangle tri;
                    float depth = 0.0f;
                };
                struct DepthWorldTri {
                    IRenderBackend::WorldTriangle tri;
                    float depth = 0.0f;
                };
                static thread_local std::vector<DepthTri> modelDepthTris;
                modelDepthTris.clear();
                if (modelDepthTris.capacity() < 12000u) modelDepthTris.reserve(12000u);
                static thread_local std::vector<DepthWorldTri> modelDepthWorldTris;
                modelDepthWorldTris.clear();
                if (modelDepthWorldTris.capacity() < 12000u) modelDepthWorldTris.reserve(12000u);
                std::size_t remainingModelTrianglesBudget = backendModelTriangleFrameBudget();

                const float boardSurfaceY = 0.006f;
                for (int r = 0; r < rows; ++r) {
                    for (int c = 0; c < cols; ++c) {
                        const float x0 = boardMinX + static_cast<float>(c) * worldCellSize;
                        const float z0 = boardMinZ + static_cast<float>(r) * worldCellSize;
                        const float x1 = x0 + worldCellSize;
                        const float z1 = z0 + worldCellSize;
                        const bool darkCell = ((r + c) % 2) == 0;
                        const float cr = darkCell ? 0.07f : 0.10f;
                        const float cg = darkCell ? 0.08f : 0.11f;
                        const float cb = darkCell ? 0.09f : 0.12f;
                        const float ca = darkCell ? 0.32f : 0.26f;
                        const glm::vec3 qa(x0, boardSurfaceY, z0);
                        const glm::vec3 qb(x1, boardSurfaceY, z0);
                        const glm::vec3 qc(x1, boardSurfaceY, z1);
                        const glm::vec3 qd(x0, boardSurfaceY, z1);
                        if (supportsWorldTriangles3D) {
                            appendWorldQuad(qa, qb, qc, qd, cr, cg, cb, ca);
                        } else {
                            appendProjectedQuad(qa, qb, qc, qd, cr, cg, cb, ca);
                        }
                    }
                }

                // Slightly thicker/brighter depth-tested grid strips improve readability at grazing angles
                // while preserving model occlusion (legacy-shared parity issue).
                const float gridY = 0.0090f;
                const float gridHalfWidthWorld = std::max(0.0035f, worldCellSize * 0.0180f);
                for (int c = 0; c <= cols; ++c) {
                    const float x = boardMinX + static_cast<float>(c) * worldCellSize;
                    if (supportsWorldTriangles3D) {
                        appendWorldQuad(
                            glm::vec3(x - gridHalfWidthWorld, gridY, boardMinZ),
                            glm::vec3(x + gridHalfWidthWorld, gridY, boardMinZ),
                            glm::vec3(x + gridHalfWidthWorld, gridY, boardMaxZ),
                            glm::vec3(x - gridHalfWidthWorld, gridY, boardMaxZ),
                            0.82f, 0.83f, 0.85f, 0.94f);
                    } else {
                        appendProjectedLine(
                            glm::vec3(x, 0.01f, boardMinZ),
                            glm::vec3(x, 0.01f, boardMaxZ),
                            0.82f, 0.83f, 0.85f, 0.94f, line);
                    }
                }
                for (int r = 0; r <= rows; ++r) {
                    const float z = boardMinZ + static_cast<float>(r) * worldCellSize;
                    if (supportsWorldTriangles3D) {
                        appendWorldQuad(
                            glm::vec3(boardMinX, gridY, z - gridHalfWidthWorld),
                            glm::vec3(boardMaxX, gridY, z - gridHalfWidthWorld),
                            glm::vec3(boardMaxX, gridY, z + gridHalfWidthWorld),
                            glm::vec3(boardMinX, gridY, z + gridHalfWidthWorld),
                            0.82f, 0.83f, 0.85f, 0.94f);
                    } else {
                        appendProjectedLine(
                            glm::vec3(boardMinX, 0.01f, z),
                            glm::vec3(boardMaxX, 0.01f, z),
                            0.82f, 0.83f, 0.85f, 0.94f, line);
                    }
                }

                // Legacy OpenGL draws a separate bench grid just beyond the board front edge.
                // Mirror that in shared routes so bench slots are visible in-world (not only as 2D UI).
                {
                    const int benchSlots = std::max(1, config.benchSlots);
                    const float benchGapWorld = std::max(0.5f, worldCellSize * 0.5f);
                    const float benchMinX = -0.5f * static_cast<float>(benchSlots) * worldCellSize;
                    const float benchMaxX = benchMinX + static_cast<float>(benchSlots) * worldCellSize;
                    const float benchMinZ = boardMaxZ + benchGapWorld;
                    const float benchMaxZ = benchMinZ + worldCellSize;
                    const float benchSurfaceY = boardSurfaceY;

                    for (int slot = 0; slot < benchSlots; ++slot) {
                        const float x0 = benchMinX + static_cast<float>(slot) * worldCellSize;
                        const float x1 = x0 + worldCellSize;
                        const bool darkCell = (slot % 2) == 0;
                        const float cr = darkCell ? 0.075f : 0.105f;
                        const float cg = darkCell ? 0.085f : 0.115f;
                        const float cb = darkCell ? 0.095f : 0.125f;
                        const float ca = darkCell ? 0.28f : 0.24f;
                        const glm::vec3 qa(x0, benchSurfaceY, benchMinZ);
                        const glm::vec3 qb(x1, benchSurfaceY, benchMinZ);
                        const glm::vec3 qc(x1, benchSurfaceY, benchMaxZ);
                        const glm::vec3 qd(x0, benchSurfaceY, benchMaxZ);
                        if (supportsWorldTriangles3D) {
                            appendWorldQuad(qa, qb, qc, qd, cr, cg, cb, ca);
                        } else {
                            appendProjectedQuad(qa, qb, qc, qd, cr, cg, cb, ca);
                        }
                    }

                    for (int c = 0; c <= benchSlots; ++c) {
                        const float x = benchMinX + static_cast<float>(c) * worldCellSize;
                        if (supportsWorldTriangles3D) {
                            appendWorldQuad(
                                glm::vec3(x - gridHalfWidthWorld, gridY, benchMinZ),
                                glm::vec3(x + gridHalfWidthWorld, gridY, benchMinZ),
                                glm::vec3(x + gridHalfWidthWorld, gridY, benchMaxZ),
                                glm::vec3(x - gridHalfWidthWorld, gridY, benchMaxZ),
                                0.82f, 0.83f, 0.85f, 0.94f);
                        } else {
                            appendProjectedLine(
                                glm::vec3(x, 0.01f, benchMinZ),
                                glm::vec3(x, 0.01f, benchMaxZ),
                                0.82f, 0.83f, 0.85f, 0.94f, line);
                        }
                    }

                    for (int r = 0; r <= 1; ++r) {
                        const float z = benchMinZ + static_cast<float>(r) * worldCellSize;
                        if (supportsWorldTriangles3D) {
                            appendWorldQuad(
                                glm::vec3(benchMinX, gridY, z - gridHalfWidthWorld),
                                glm::vec3(benchMaxX, gridY, z - gridHalfWidthWorld),
                                glm::vec3(benchMaxX, gridY, z + gridHalfWidthWorld),
                                glm::vec3(benchMinX, gridY, z + gridHalfWidthWorld),
                                0.82f, 0.83f, 0.85f, 0.94f);
                        } else {
                            appendProjectedLine(
                                glm::vec3(benchMinX, 0.01f, z),
                                glm::vec3(benchMaxX, 0.01f, z),
                                0.82f, 0.83f, 0.85f, 0.94f, line);
                        }
                    }
                }
                if (worldTriangles.size() == boardTrianglesStart2D &&
                    world3DTriangles.size() == boardTrianglesStart3D) {
                    IRenderBackend::DebugQuad boardFallback;
                    boardFallback.x = boardX;
                    boardFallback.y = boardY;
                    boardFallback.w = boardW;
                    boardFallback.h = boardH;
                    boardFallback.r = 0.06f;
                    boardFallback.g = 0.07f;
                    boardFallback.b = 0.08f;
                    boardFallback.a = 0.92f;
                    worldBackgroundQuads.push_back(boardFallback);

                    for (int r = 0; r < rows; ++r) {
                        for (int c = 0; c < cols; ++c) {
                            IRenderBackend::DebugQuad cell;
                            cell.x = boardX + cellW * static_cast<float>(c);
                            cell.y = boardY + cellH * static_cast<float>(r);
                            cell.w = cellW;
                            cell.h = cellH;
                            const bool darkCell = ((r + c) % 2) == 0;
                            cell.r = darkCell ? 0.09f : 0.14f;
                            cell.g = darkCell ? 0.14f : 0.19f;
                            cell.b = darkCell ? 0.19f : 0.25f;
                            cell.a = darkCell ? 0.34f : 0.26f;
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
                        vLine.r = 0.26f;
                        vLine.g = 0.38f;
                        vLine.b = 0.47f;
                        vLine.a = 0.96f;
                        lines.push_back(vLine);
                    }
                    for (int r = 0; r <= rows; ++r) {
                        IRenderBackend::DebugLine hLine;
                        hLine.x1 = boardX;
                        hLine.y1 = boardY + cellH * static_cast<float>(r);
                        hLine.x2 = boardX + boardW;
                        hLine.y2 = hLine.y1;
                        hLine.thickness = line;
                        hLine.r = 0.26f;
                        hLine.g = 0.38f;
                        hLine.b = 0.47f;
                        hLine.a = 0.96f;
                        lines.push_back(hLine);
                    }
                }

                struct BackendPoseEval {
                    bool hasScenePose = false;
                    bool hasClipPose = false;
                    std::vector<pac_model_types::NodeTRS> nodeLocals;
                    std::vector<glm::mat4> nodeGlobals;
                };

                const auto trsToMat4 = [](const pac_model_types::NodeTRS& n) {
                    if (n.hasMatrix) return n.matrix;
                    const glm::mat4 t = glm::translate(glm::mat4(1.0f), n.t);
                    const glm::mat4 r = glm::mat4_cast(glm::normalize(n.r));
                    const glm::mat4 s = glm::scale(glm::mat4(1.0f), n.s);
                    return t * r * s;
                };
                const auto wrapTime = [](float t, float duration) {
                    if (duration <= 0.0f) return 0.0f;
                    float wrapped = std::fmod(t, duration);
                    if (wrapped < 0.0f) wrapped += duration;
                    return wrapped;
                };
                const auto findKeyframe = [](const std::vector<float>& times, float t) -> std::size_t {
                    if (times.empty()) return 0u;
                    if (t <= times.front()) return 0u;
                    if (t >= times.back()) return times.size() - 1u;
                    const auto it = std::upper_bound(times.begin(), times.end(), t);
                    return (it == times.begin()) ? 0u : static_cast<std::size_t>((it - times.begin()) - 1u);
                };
                const auto sampleVec4 = [&](const pac_model_types::AnimationSampler& sampler, float t) {
                    if (sampler.inputs.empty() || sampler.outputs.empty()) return glm::vec4(0.0f);
                    const std::size_t i = findKeyframe(sampler.inputs, t);
                    if (i >= sampler.inputs.size() - 1u) {
                        return sampler.outputs[std::min(i, sampler.outputs.size() - 1u)];
                    }
                    const float t0 = sampler.inputs[i];
                    const float t1 = sampler.inputs[i + 1u];
                    const float a = (t1 > t0) ? ((t - t0) / (t1 - t0)) : 0.0f;
                    const glm::vec4 v0 = sampler.outputs[std::min(i, sampler.outputs.size() - 1u)];
                    const glm::vec4 v1 = sampler.outputs[std::min(i + 1u, sampler.outputs.size() - 1u)];
                    if (sampler.interpolation == "STEP") return v0;
                    return glm::mix(v0, v1, std::clamp(a, 0.0f, 1.0f));
                };
                const auto sampleQuat = [&](const pac_model_types::AnimationSampler& sampler, float t) {
                    const glm::vec4 v = sampleVec4(sampler, t);
                    return glm::normalize(glm::quat(v.w, v.x, v.y, v.z));
                };
                const auto rootMotionCarrierMaskForMesh =
                    [](const runtime::backend_model::MeshData& mesh) -> const std::vector<std::uint8_t>& {
                    static std::unordered_map<
                        const runtime::backend_model::MeshData*,
                        std::vector<std::uint8_t>>
                        cache;
                    const auto found = cache.find(&mesh);
                    if (found != cache.end()) {
                        return found->second;
                    }

                    std::vector<std::uint8_t> mask(mesh.nodesDefault.size(), 0u);
                    for (const auto& skin : mesh.skins) {
                        if (skin.joints.empty()) continue;
                        std::unordered_set<int> jointSet;
                        jointSet.reserve(skin.joints.size());
                        for (const int jointNode : skin.joints) {
                            jointSet.insert(jointNode);
                        }
                        for (const int jointNode : skin.joints) {
                            if (jointNode < 0 ||
                                static_cast<std::size_t>(jointNode) >= mesh.nodeParent.size() ||
                                static_cast<std::size_t>(jointNode) >= mask.size()) {
                                continue;
                            }
                            const int parent = mesh.nodeParent[static_cast<std::size_t>(jointNode)];
                            if (parent < 0 || jointSet.find(parent) == jointSet.end()) {
                                mask[static_cast<std::size_t>(jointNode)] = 1u;
                            }
                        }
                    }

                    const auto inserted = cache.emplace(&mesh, std::move(mask));
                    return inserted.first->second;
                };
                const auto evaluateScenePose = [&](const runtime::backend_model::MeshData& mesh,
                                                   const PokemonInstance& unit) {
                    BackendPoseEval eval;
                    if (mesh.nodesDefault.empty()) return eval;
                    eval.hasScenePose = true;
                    eval.nodeLocals = mesh.nodesDefault;
                    eval.nodeGlobals.assign(mesh.nodesDefault.size(), glm::mat4(1.0f));
                    const auto& rootMotionCarrierMask = rootMotionCarrierMaskForMesh(mesh);

                    int animIndex = unit.activeAnimIndex;
                    if (animIndex < 0 || static_cast<std::size_t>(animIndex) >= mesh.animations.size()) {
                        animIndex = unit.currentAttackAnimIndex;
                    }
                    if (animIndex < 0 || static_cast<std::size_t>(animIndex) >= mesh.animations.size()) {
                        animIndex = unit.animMoveIndex;
                    }
                    if (animIndex < 0 || static_cast<std::size_t>(animIndex) >= mesh.animations.size()) {
                        animIndex = unit.animIdleIndex;
                    }
                    if (animIndex < 0 && !mesh.animations.empty()) {
                        animIndex = 0;
                    }
                    if (animIndex >= 0 && static_cast<std::size_t>(animIndex) < mesh.animations.size()) {
                        const auto& clip = mesh.animations[static_cast<std::size_t>(animIndex)];
                        const float clipTime = wrapTime(unit.animTimeSec, clip.durationSec);
                        for (const auto& channel : clip.channels) {
                            if (channel.targetNode < 0 ||
                                static_cast<std::size_t>(channel.targetNode) >= eval.nodeLocals.size()) {
                                continue;
                            }
                            if (channel.samplerIndex < 0 ||
                                static_cast<std::size_t>(channel.samplerIndex) >= clip.samplers.size()) {
                                continue;
                            }
                            auto& local = eval.nodeLocals[static_cast<std::size_t>(channel.targetNode)];
                            const auto& sampler = clip.samplers[static_cast<std::size_t>(channel.samplerIndex)];
                            if (channel.path == pac_model_types::ChannelPath::Translation) {
                                const glm::vec4 tr = sampleVec4(sampler, clipTime);
                                const bool rootMotionCarrier =
                                    (channel.targetNode >= 0) &&
                                    (static_cast<std::size_t>(channel.targetNode) <
                                     rootMotionCarrierMask.size()) &&
                                    (rootMotionCarrierMask[static_cast<std::size_t>(channel.targetNode)] != 0u);
                                if (rootMotionCarrier) {
                                    const auto& bind =
                                        mesh.nodesDefault[static_cast<std::size_t>(channel.targetNode)];
                                    if (bind.hasMatrix) {
                                        local = bind;
                                        local.matrix[3].x = bind.matrix[3].x;
                                        local.matrix[3].y = tr.y;
                                        local.matrix[3].z = bind.matrix[3].z;
                                        local.matrix[3].w = 1.0f;
                                        local.hasMatrix = true;
                                    } else {
                                        local.t = glm::vec3(bind.t.x, tr.y, bind.t.z);
                                        local.hasMatrix = false;
                                    }
                                } else {
                                    local.t = glm::vec3(tr.x, tr.y, tr.z);
                                    local.hasMatrix = false;
                                }
                            } else if (channel.path == pac_model_types::ChannelPath::Scale) {
                                const glm::vec4 sc = sampleVec4(sampler, clipTime);
                                local.s = glm::vec3(sc.x, sc.y, sc.z);
                                local.hasMatrix = false;
                            } else if (channel.path == pac_model_types::ChannelPath::Rotation) {
                                local.r = sampleQuat(sampler, clipTime);
                                local.hasMatrix = false;
                            }
                        }
                        eval.hasClipPose = true;
                    }

                    const auto dfs = [&](const auto& self, int node, const glm::mat4& parentM) -> void {
                        if (node < 0 || static_cast<std::size_t>(node) >= eval.nodeLocals.size()) return;
                        const glm::mat4 global = parentM * trsToMat4(eval.nodeLocals[static_cast<std::size_t>(node)]);
                        eval.nodeGlobals[static_cast<std::size_t>(node)] = global;
                        if (static_cast<std::size_t>(node) >= mesh.nodeChildren.size()) return;
                        for (int child : mesh.nodeChildren[static_cast<std::size_t>(node)]) {
                            self(self, child, global);
                        }
                    };

                    if (!mesh.sceneRoots.empty()) {
                        for (int root : mesh.sceneRoots) {
                            dfs(dfs, root, glm::mat4(1.0f));
                        }
                    } else if (!eval.nodeLocals.empty()) {
                        bool drewAny = false;
                        for (std::size_t i = 0; i < mesh.nodeParent.size(); ++i) {
                            if (mesh.nodeParent[i] >= 0) continue;
                            dfs(dfs, static_cast<int>(i), glm::mat4(1.0f));
                            drewAny = true;
                        }
                        if (!drewAny) dfs(dfs, 0, glm::mat4(1.0f));
                    }
                    return eval;
                };

                const auto evaluateScenePoseForClipTime =
                    [&](const runtime::backend_model::MeshData& mesh,
                        int animIndex,
                        float animTimeSec) {
                    BackendPoseEval eval;
                    if (mesh.nodesDefault.empty()) return eval;
                    eval.hasScenePose = true;
                    eval.nodeLocals = mesh.nodesDefault;
                    eval.nodeGlobals.assign(mesh.nodesDefault.size(), glm::mat4(1.0f));

                    if (animIndex >= 0 && static_cast<std::size_t>(animIndex) < mesh.animations.size()) {
                        const auto& clip = mesh.animations[static_cast<std::size_t>(animIndex)];
                        const float clipTime = wrapTime(animTimeSec, clip.durationSec);
                        for (const auto& channel : clip.channels) {
                            if (channel.targetNode < 0 ||
                                static_cast<std::size_t>(channel.targetNode) >= eval.nodeLocals.size()) {
                                continue;
                            }
                            if (channel.samplerIndex < 0 ||
                                static_cast<std::size_t>(channel.samplerIndex) >= clip.samplers.size()) {
                                continue;
                            }
                            auto& local = eval.nodeLocals[static_cast<std::size_t>(channel.targetNode)];
                            const auto& sampler = clip.samplers[static_cast<std::size_t>(channel.samplerIndex)];
                            if (channel.path == pac_model_types::ChannelPath::Translation) {
                                const glm::vec4 tr = sampleVec4(sampler, clipTime);
                                local.t = glm::vec3(tr.x, tr.y, tr.z);
                                local.hasMatrix = false;
                            } else if (channel.path == pac_model_types::ChannelPath::Scale) {
                                const glm::vec4 sc = sampleVec4(sampler, clipTime);
                                local.s = glm::vec3(sc.x, sc.y, sc.z);
                                local.hasMatrix = false;
                            } else if (channel.path == pac_model_types::ChannelPath::Rotation) {
                                local.r = sampleQuat(sampler, clipTime);
                                local.hasMatrix = false;
                            }
                        }
                        eval.hasClipPose = true;
                    }

                    const auto dfs = [&](const auto& self, int node, const glm::mat4& parentM) -> void {
                        if (node < 0 || static_cast<std::size_t>(node) >= eval.nodeLocals.size()) return;
                        const glm::mat4 global = parentM * trsToMat4(eval.nodeLocals[static_cast<std::size_t>(node)]);
                        eval.nodeGlobals[static_cast<std::size_t>(node)] = global;
                        if (static_cast<std::size_t>(node) >= mesh.nodeChildren.size()) return;
                        for (int child : mesh.nodeChildren[static_cast<std::size_t>(node)]) {
                            self(self, child, global);
                        }
                    };

                    if (!mesh.sceneRoots.empty()) {
                        for (int root : mesh.sceneRoots) {
                            dfs(dfs, root, glm::mat4(1.0f));
                        }
                    } else if (!eval.nodeLocals.empty()) {
                        bool didAny = false;
                        for (std::size_t i = 0; i < mesh.nodeParent.size(); ++i) {
                            if (mesh.nodeParent[i] >= 0) continue;
                            dfs(dfs, static_cast<int>(i), glm::mat4(1.0f));
                            didAny = true;
                        }
                        if (!didAny) dfs(dfs, 0, glm::mat4(1.0f));
                    }
                    return eval;
                };

                SharedCaptureSnapshotCache sharedCaptureAttemptCache;
                sharedCaptureAttemptCache.snaps.reserve(8);
                sharedCaptureAttemptCache.byTargetId.reserve(8);

                const auto appendSharedCaptureAttemptModels = [&]() -> bool {
                    if (!gameWorld) return false;
                    if (!supportsWorldIndexedMeshes || !hasWorldViewProj) return false;

                    if (sharedCaptureAttemptCache.snaps.empty()) {
                        if (!sharedCaptureAttemptCache.refresh(gameWorld.get())) return false;
                    }
                    const auto& captureSnaps = sharedCaptureAttemptCache.snaps;

                    runtime::backend_model::MeshData* mesh =
                        ensureBackendMeshLoaded("assets/models/pokeball.glb");
                    if (!mesh || mesh->vertices.empty() || mesh->indices.empty()) {
                        static bool sLoggedSharedPokeballModelFailure = false;
                        if (!sLoggedSharedPokeballModelFailure) {
                            std::cout
                                << "[Render][CaptureShared] pokeball.glb unavailable for shared capture model path; "
                                   "2D fallback is suppressed by policy.\n";
                            sLoggedSharedPokeballModelFailure = true;
                        }
                        return false;
                    }
                    if (mesh->animations.empty()) {
                        static bool sLoggedSharedPokeballAnimMissing = false;
                        if (!sLoggedSharedPokeballAnimMissing) {
                            std::cout << "[Render][CaptureShared] pokeball cache has no animations; "
                                         "shared clip playback is disabled (clear/rebuild cache if pokeball.glb was updated).\n";
                            sLoggedSharedPokeballAnimMissing = true;
                        }
                    }

                    bool appendedAny = false;
                    const std::size_t triCount = mesh->indices.size() / 3u;
                    if (triCount == 0u) return false;
                    struct PreparedCaptureVertex {
                        glm::vec3 bindPos{0.0f};
                        int nodeIndex = -1;
                        float u = 0.0f;
                        float v = 0.0f;
                        float r = 1.0f;
                        float g = 1.0f;
                        float b = 1.0f;
                        float a = 1.0f;
                    };
                    struct PreparedCaptureSubmesh {
                        std::vector<PreparedCaptureVertex> vertices;
                        std::vector<std::uint32_t> indices;
                        std::uint8_t alphaMode = 0u;
                        float alphaCutoff = 0.5f;
                    };
                    struct PreparedCaptureMeshCache {
                        const runtime::backend_model::MeshData* sourceMesh = nullptr;
                        std::size_t sourceVertexCount = 0u;
                        std::size_t sourceIndexCount = 0u;
                        std::vector<glm::mat4> bindNodeGlobalInv;
                        std::vector<PreparedCaptureSubmesh> submeshes;
                    };
                    static thread_local PreparedCaptureMeshCache sCaptureMeshCache;
                    const bool captureMeshCacheValid =
                        (sCaptureMeshCache.sourceMesh == mesh) &&
                        (sCaptureMeshCache.sourceVertexCount == mesh->vertices.size()) &&
                        (sCaptureMeshCache.sourceIndexCount == mesh->indices.size()) &&
                        !sCaptureMeshCache.submeshes.empty();
                    if (!captureMeshCacheValid) {
                        sCaptureMeshCache = {};
                        sCaptureMeshCache.sourceMesh = mesh;
                        sCaptureMeshCache.sourceVertexCount = mesh->vertices.size();
                        sCaptureMeshCache.sourceIndexCount = mesh->indices.size();
                        sCaptureMeshCache.bindNodeGlobalInv.assign(
                            mesh->bindNodeGlobals.size(),
                            glm::mat4(1.0f));
                        for (std::size_t ni = 0; ni < mesh->bindNodeGlobals.size(); ++ni) {
                            sCaptureMeshCache.bindNodeGlobalInv[ni] = glm::inverse(mesh->bindNodeGlobals[ni]);
                        }

                        const auto& nodeGlobals = mesh->bindNodeGlobals;
                        const std::size_t batchCount =
                            std::max<std::size_t>(1u, mesh->submeshBaseTextures.size());
                        sCaptureMeshCache.submeshes.resize(batchCount);

                        std::vector<std::unordered_map<std::uint64_t, std::uint32_t>> remap(batchCount);
                        for (std::size_t si = 0; si < batchCount; ++si) {
                            auto& sub = sCaptureMeshCache.submeshes[si];
                            if (si < mesh->submeshAlphaMode.size()) sub.alphaMode = mesh->submeshAlphaMode[si];
                            if (si < mesh->submeshAlphaCutoff.size()) sub.alphaCutoff = mesh->submeshAlphaCutoff[si];
                            sub.vertices.reserve(std::max<std::size_t>(32u, mesh->vertices.size() / batchCount));
                            sub.indices.reserve(std::max<std::size_t>(96u, mesh->indices.size() / batchCount));
                            remap[si].reserve(std::max<std::size_t>(64u, mesh->vertices.size() / batchCount));
                        }

                        const auto nodeGlobalForTri = [&](int triNodeIndex) -> const glm::mat4& {
                            static const glm::mat4 kIdentity(1.0f);
                            if (triNodeIndex >= 0 && static_cast<std::size_t>(triNodeIndex) < nodeGlobals.size()) {
                                return nodeGlobals[static_cast<std::size_t>(triNodeIndex)];
                            }
                            return kIdentity;
                        };

                        const auto appendPreparedVertex =
                            [&](std::size_t submesh,
                                std::uint32_t srcIndex,
                                int triNodeIndex,
                                const glm::vec3& triTint,
                                float triAlpha) -> std::uint32_t {
                                auto& sub = sCaptureMeshCache.submeshes[submesh];
                                auto& subRemap = remap[submesh];
                                const std::uint64_t key =
                                    (static_cast<std::uint64_t>(static_cast<std::uint32_t>(triNodeIndex + 1)) << 32u) |
                                    static_cast<std::uint64_t>(srcIndex);
                                const auto it = subRemap.find(key);
                                if (it != subRemap.end()) {
                                    return it->second;
                                }
                                if (sub.vertices.size() >=
                                    static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
                                    return std::numeric_limits<std::uint32_t>::max();
                                }

                                const auto& src = mesh->vertices[srcIndex];
                                const glm::mat4& nodeGlobal = nodeGlobalForTri(triNodeIndex);
                                const glm::vec3 bindPos = glm::vec3(nodeGlobal * glm::vec4(src.position, 1.0f));

                                PreparedCaptureVertex v{};
                                v.bindPos = bindPos;
                                v.nodeIndex = triNodeIndex;
                                v.u = src.uv.x;
                                v.v = src.uv.y;
                                if (mesh->hasVertexBaseColor && srcIndex < mesh->vertexBaseColors.size()) {
                                    const glm::vec3 vc = glm::clamp(mesh->vertexBaseColors[srcIndex], 0.0f, 1.0f);
                                    v.r = vc.r;
                                    v.g = vc.g;
                                    v.b = vc.b;
                                } else if (mesh->hasVertexColor) {
                                    v.r = std::clamp(src.color.r, 0.0f, 1.0f);
                                    v.g = std::clamp(src.color.g, 0.0f, 1.0f);
                                    v.b = std::clamp(src.color.b, 0.0f, 1.0f);
                                } else {
                                    v.r = triTint.r;
                                    v.g = triTint.g;
                                    v.b = triTint.b;
                                }
                                v.a = triAlpha;
                                const std::uint32_t outIndex = static_cast<std::uint32_t>(sub.vertices.size());
                                sub.vertices.push_back(v);
                                subRemap.emplace(key, outIndex);
                                return outIndex;
                            };

                        for (std::size_t triIdx = 0; triIdx < triCount; ++triIdx) {
                            const std::size_t idxBase = triIdx * 3u;
                            const std::uint32_t i0 = mesh->indices[idxBase + 0u];
                            const std::uint32_t i1 = mesh->indices[idxBase + 1u];
                            const std::uint32_t i2 = mesh->indices[idxBase + 2u];
                            if (i0 >= mesh->vertices.size() || i1 >= mesh->vertices.size() || i2 >= mesh->vertices.size()) {
                                continue;
                            }
                            std::size_t submesh = 0u;
                            if (triIdx < mesh->triangleSubmesh.size()) {
                                submesh = static_cast<std::size_t>(mesh->triangleSubmesh[triIdx]);
                                if (submesh >= sCaptureMeshCache.submeshes.size()) submesh = 0u;
                            }
                            const int triNodeIndex =
                                (triIdx < mesh->triangleNodeIndex.size()) ? mesh->triangleNodeIndex[triIdx] : -1;
                            const glm::mat4& nodeGlobal = nodeGlobalForTri(triNodeIndex);
                            const bool flipWinding = (glm::determinant(glm::mat3(nodeGlobal)) < 0.0f);
                            const bool doubleSided =
                                (triIdx < mesh->triangleDoubleSided.size()) &&
                                (mesh->triangleDoubleSided[triIdx] != 0u);

                            glm::vec3 triTint(1.0f, 1.0f, 1.0f);
                            if (triIdx < mesh->triangleBaseColors.size()) {
                                triTint = glm::clamp(mesh->triangleBaseColors[triIdx], 0.0f, 1.0f);
                            } else if (submesh < mesh->submeshBaseColors.size()) {
                                const glm::vec4 sc = mesh->submeshBaseColors[submesh];
                                triTint = glm::clamp(glm::vec3(sc.r, sc.g, sc.b), 0.0f, 1.0f);
                            }
                            float triAlpha = 1.0f;
                            if (submesh < mesh->submeshBaseColors.size()) {
                                triAlpha = std::clamp(mesh->submeshBaseColors[submesh].a, 0.0f, 1.0f);
                            }
                            if (triIdx < mesh->triangleOpacity.size()) {
                                triAlpha = std::max(
                                    triAlpha,
                                    std::clamp(mesh->triangleOpacity[triIdx], 0.0f, 1.0f));
                            }
                            triAlpha = std::clamp(triAlpha, 0.35f, 1.0f);

                            const std::uint32_t o0 = appendPreparedVertex(submesh, i0, triNodeIndex, triTint, triAlpha);
                            const std::uint32_t o1 = appendPreparedVertex(submesh, i1, triNodeIndex, triTint, triAlpha);
                            const std::uint32_t o2 = appendPreparedVertex(submesh, i2, triNodeIndex, triTint, triAlpha);
                            if (o0 == std::numeric_limits<std::uint32_t>::max() ||
                                o1 == std::numeric_limits<std::uint32_t>::max() ||
                                o2 == std::numeric_limits<std::uint32_t>::max()) {
                                continue;
                            }
                            auto& sub = sCaptureMeshCache.submeshes[submesh];
                            sub.indices.push_back(o0);
                            sub.indices.push_back(flipWinding ? o2 : o1);
                            sub.indices.push_back(flipWinding ? o1 : o2);
                            if (doubleSided) {
                                sub.indices.push_back(o0);
                                sub.indices.push_back(flipWinding ? o1 : o2);
                                sub.indices.push_back(flipWinding ? o2 : o1);
                            }
                        }
                    }

                    int captureAnimIndex = -1;
                    float captureAnimDurationSec = 0.0f;
                    if (!mesh->animations.empty()) {
                        captureAnimIndex = findPokeballAnimIndex(*mesh);
                        if (captureAnimIndex >= 0 &&
                            static_cast<std::size_t>(captureAnimIndex) < mesh->animations.size()) {
                            captureAnimDurationSec = std::max(
                                0.0f,
                                mesh->animations[static_cast<std::size_t>(captureAnimIndex)].durationSec);
                        }
                    }

                    for (const auto& snap : captureSnaps) {
                        if (snap.timeLeftSec <= 0.0f) continue;

                        const float baseScale =
                            std::max(0.01f, mesh->modelScaleFactor) * std::max(0.02f, snap.ballScale);
                        glm::vec3 renderPos = snap.ballPos;
                        const float minAllowedY = boardSurfaceY + 0.0025f;
                        const float approxMinY = renderPos.y + mesh->boundsMin.y * baseScale;
                        if (std::isfinite(approxMinY) && approxMinY < minAllowedY) {
                            renderPos.y += (minAllowedY - approxMinY);
                        }
                        const glm::mat4 modelM =
                            buildCaptureBallModelMatrix(renderPos, snap.ballYawDeg, baseScale);

                        BackendPoseEval capturePoseEval;
                        bool hasCaptureClipPose = false;
                        std::vector<glm::mat4> captureNodeDelta;
                        if (captureAnimIndex >= 0 && captureAnimDurationSec > 0.0f && snap.phase == 1) {
                            const float clipAnimTimeSec =
                                sharedCaptureBallClipTimeSec(snap, captureAnimDurationSec);
                            capturePoseEval = evaluateScenePoseForClipTime(*mesh, captureAnimIndex, clipAnimTimeSec);
                            hasCaptureClipPose =
                                capturePoseEval.hasScenePose &&
                                !capturePoseEval.nodeGlobals.empty() &&
                                !mesh->bindNodeGlobals.empty();
                            if (hasCaptureClipPose) {
                                const std::size_t nodeCount = std::min(
                                    capturePoseEval.nodeGlobals.size(),
                                    sCaptureMeshCache.bindNodeGlobalInv.size());
                                captureNodeDelta.assign(nodeCount, glm::mat4(1.0f));
                                for (std::size_t ni = 0; ni < nodeCount; ++ni) {
                                    captureNodeDelta[ni] =
                                        capturePoseEval.nodeGlobals[ni] * sCaptureMeshCache.bindNodeGlobalInv[ni];
                                }
                            }
                        }

                        const std::size_t batchCount = sCaptureMeshCache.submeshes.size();
                        std::vector<WorldIndexedBatch> captureBatches(batchCount);
                        for (std::size_t si = 0; si < batchCount; ++si) {
                            auto& batch = captureBatches[si];
                            const auto& prepared = sCaptureMeshCache.submeshes[si];
                            batch.vertices.reserve(prepared.vertices.size());
                            batch.indices.reserve(prepared.indices.size());
                            batch.sortDepth = glm::dot(cameraWorldPos - renderPos, cameraWorldPos - renderPos);
                            if (si < mesh->submeshBaseTextures.size()) {
                                const auto& tex = mesh->submeshBaseTextures[si];
                                if (tex.hasPixels()) {
                                    batch.textureKey = "assets/models/pokeball.glb#submesh:" + std::to_string(si);
                                    batch.textureRgba = tex.rgba.data();
                                    batch.textureWidth = tex.width;
                                    batch.textureHeight = tex.height;
                                    batch.textureWrapS = tex.wrapS;
                                    batch.textureWrapT = tex.wrapT;
                                }
                            }
                            if ((!batch.textureRgba || batch.textureWidth <= 0 || batch.textureHeight <= 0)) {
                                if (BackendTextureCacheEntry* white = ensureBackendTextureLoaded("")) {
                                    batch.textureKey =
                                        "assets/models/pokeball.glb#submesh:" + std::to_string(si) + ":white";
                                    batch.textureRgba = white->rgba.data();
                                    batch.textureWidth = white->width;
                                    batch.textureHeight = white->height;
                                    batch.textureWrapS = 33071;
                                    batch.textureWrapT = 33071;
                                }
                            }
                            batch.alphaMode = prepared.alphaMode;
                            batch.alphaCutoff = prepared.alphaCutoff;
                        }
                        for (std::size_t si = 0; si < batchCount; ++si) {
                            auto& batch = captureBatches[si];
                            const auto& prepared = sCaptureMeshCache.submeshes[si];
                            if (prepared.vertices.empty() || prepared.indices.empty()) continue;
                            if (!batch.textureRgba || batch.textureWidth <= 0 || batch.textureHeight <= 0) {
                                continue;
                            }
                            batch.indices = prepared.indices;
                            batch.vertices.resize(prepared.vertices.size());
                            for (std::size_t vi = 0; vi < prepared.vertices.size(); ++vi) {
                                const auto& src = prepared.vertices[vi];
                                glm::vec3 posedBindPos = src.bindPos;
                                if (hasCaptureClipPose &&
                                    src.nodeIndex >= 0 &&
                                    static_cast<std::size_t>(src.nodeIndex) < captureNodeDelta.size()) {
                                    posedBindPos = glm::vec3(
                                        captureNodeDelta[static_cast<std::size_t>(src.nodeIndex)] *
                                        glm::vec4(src.bindPos, 1.0f));
                                }
                                const glm::vec3 pos = glm::vec3(modelM * glm::vec4(posedBindPos, 1.0f));
                                auto& dst = batch.vertices[vi];
                                dst.x = pos.x;
                                dst.y = pos.y;
                                dst.z = pos.z;
                                dst.u = src.u;
                                dst.v = src.v;
                                dst.r = src.r;
                                dst.g = src.g;
                                dst.b = src.b;
                                dst.a = src.a;
                            }
                        }

                        for (auto& batch : captureBatches) {
                            if (batch.vertices.empty() || batch.indices.empty()) continue;
                            worldIndexedBatches.push_back(std::move(batch));
                            appendedAny = true;
                        }
                    }
                    return appendedAny;
                };
                const auto drawProjectedUnits = [&](const std::vector<PokemonInstance>& units) {
                    for (const auto& unit : units) {
                        if (!unit.alive && !unit.captureInProgress && !unit.fainting) continue;
                        if (!unit.alive && unit.visualScale <= 0.0001f && !unit.captureInProgress) continue;

                        const runtime::backend_anim::ProceduralPose pose =
                            runtime::backend_anim::computeProceduralPose(unit, worldCellSize);
                        const runtime::backend_model::MeshData* meshForUnit = resolveModelMesh(unit);
                        BackendPoseEval scenePose;
                        bool scenePoseReady = false;
                        if (meshForUnit) {
                            scenePose = evaluateScenePose(*meshForUnit, unit);
                            scenePoseReady = true;
                        }
                        const bool hasClipPoseDrivenModel = scenePoseReady && scenePose.hasClipPose;
                        const bool applyProceduralModelMotion = !hasClipPoseDrivenModel;
                        const glm::vec3 attackOffset = applyProceduralModelMotion
                            ? (game::runtime::backend_proxy::yawForward(unit.rotation.y) * pose.attackLunge)
                            : glm::vec3(0.0f);
                        const float animYaw = applyProceduralModelMotion ? pose.yawDeg : unit.rotation.y;
                        const float animPitch = applyProceduralModelMotion ? pose.pitchDeg : unit.rotation.x;
                        const float animRoll = applyProceduralModelMotion
                            ? (pose.rollDeg + (unit.side == PokemonSide::Player ? -pose.faintRoll : pose.faintRoll))
                            : unit.rotation.z;
                        const float attackPulse = applyProceduralModelMotion ? pose.attackPulse : 1.0f;
                        const float proceduralBobY = applyProceduralModelMotion ? pose.bobY : 0.0f;
                        const float proceduralFaintDrop = applyProceduralModelMotion ? pose.faintDrop : 0.0f;
                        const glm::vec3 animatedCenter =
                            unit.position + attackOffset +
                            glm::vec3(0.0f, unit.visualYOffset + proceduralBobY - proceduralFaintDrop, 0.0f);
                        const glm::vec3 worldPos =
                            animatedCenter +
                            glm::vec3(0.0f, std::max(0.2f, worldCellSize * 0.22f), 0.0f);
                        float cx = 0.0f;
                        float cy = 0.0f;
                        float cz = 0.0f;
                        if (!projectWorld(worldPos, cx, cy, cz)) continue;
                        if (cz < 0.0f || cz > 1.0f) continue;

                        float sx = 0.0f;
                        float sy = 0.0f;
                        float sz = 0.0f;
                        const bool hasCellX = projectWorld(
                            worldPos + glm::vec3(worldCellSize, 0.0f, 0.0f),
                            sx,
                            sy,
                            sz);
                        float cellPx = hasCellX ? glm::length(glm::vec2(sx - cx, sy - cy)) : 0.0f;
                        if (!std::isfinite(cellPx) || cellPx < 8.0f) {
                            cellPx = std::max(14.0f, minDim * 0.035f);
                        }
                        const float unitSize = std::clamp(cellPx * 0.75f, 10.0f, 84.0f);
                        const game::runtime::backend_proxy::UnitProxyExtents extents =
                            game::runtime::backend_proxy::computeUnitProxyExtents(unit, worldCellSize);
                        const glm::vec3 proxyCenter = animatedCenter;
                        const GameWorld::CaptureAttemptRenderSnapshot* captureSnapForUnit =
                            unit.captureInProgress ? sharedCaptureAttemptCache.findByTarget(unit.id) : nullptr;
                        float captureVisualTintStrength =
                            unit.captureInProgress ? std::clamp(unit.captureTintStrength, 0.0f, 1.0f) : 0.0f;
                        float captureVisualAlphaScale = 1.0f;
                        const glm::vec3 captureTintColor(1.0f, 0.1f, 0.1f);

                        const float renderVisualScale = (unit.fainting || !unit.alive)
                            ? std::max(0.0f, unit.visualScale)
                            : std::max(0.05f, unit.visualScale);
                        float renderCaptureScale = (unit.fainting || !unit.alive || unit.captureInProgress)
                            ? std::max(0.0f, unit.captureScale)
                            : std::max(0.05f, unit.captureScale);
                        if (captureSnapForUnit && captureSnapForUnit->phase == 1) {
                            const float lateSuckP =
                                std::clamp(captureSnapForUnit->absorbLateVisual01, 0.0f, 1.0f);
                            renderCaptureScale = std::min(renderCaptureScale, std::max(0.0f, 1.0f - lateSuckP));
                            captureVisualTintStrength = std::max(captureVisualTintStrength, lateSuckP);
                            captureVisualAlphaScale = std::clamp(1.0f - 0.5f * lateSuckP, 0.0f, 1.0f);
                        } else if (captureVisualTintStrength > 0.0f) {
                            captureVisualAlphaScale =
                                std::clamp(1.0f - 0.5f * captureVisualTintStrength, 0.0f, 1.0f);
                        }
                        const float faintFadeAlpha =
                            (unit.fainting || !unit.alive)
                                ? std::clamp(renderVisualScale * renderCaptureScale, 0.0f, 1.0f)
                                : 1.0f;
                        const float modelFadeAlpha =
                            std::clamp(faintFadeAlpha * captureVisualAlphaScale, 0.0f, 1.0f);
                        if ((unit.fainting || !unit.alive) &&
                            (renderVisualScale <= 0.0001f || renderCaptureScale <= 0.0001f)) {
                            continue;
                        }

                        IRenderBackend::DebugQuad tint;
                        runtime::backend_units::applyWorldUnitTint(tint, unit);
                        float topR = std::clamp(tint.r * 0.86f + 0.12f, 0.0f, 1.0f);
                        float topG = std::clamp(tint.g * 0.86f + 0.12f, 0.0f, 1.0f);
                        float topB = std::clamp(tint.b * 0.86f + 0.12f, 0.0f, 1.0f);
                        float sideR = std::clamp(tint.r * 0.72f, 0.0f, 1.0f);
                        float sideG = std::clamp(tint.g * 0.72f, 0.0f, 1.0f);
                        float sideB = std::clamp(tint.b * 0.72f, 0.0f, 1.0f);
                        float topAlpha = unit.alive ? 0.96f : 0.78f;
                        float sideAlpha = unit.alive ? 0.88f : 0.70f;
                        if (captureVisualTintStrength > 0.001f) {
                            const glm::vec3 topTinted = glm::mix(
                                glm::vec3(topR, topG, topB),
                                captureTintColor,
                                captureVisualTintStrength);
                            const glm::vec3 sideTinted = glm::mix(
                                glm::vec3(sideR, sideG, sideB),
                                captureTintColor,
                                captureVisualTintStrength);
                            topR = topTinted.r;
                            topG = topTinted.g;
                            topB = topTinted.b;
                            sideR = sideTinted.r;
                            sideG = sideTinted.g;
                            sideB = sideTinted.b;
                            topAlpha *= captureVisualAlphaScale;
                            sideAlpha *= captureVisualAlphaScale;
                        }
                        if (!meshForUnit) {
                            const auto shadow = game::runtime::backend_proxy::computeShadowQuad(
                                proxyCenter,
                                extents.halfWidth * 1.15f,
                                extents.halfDepth * 1.15f,
                                animYaw,
                                0.010f);
                            if (supportsWorldTriangles3D) {
                                appendWorldQuad(
                                    shadow[0],
                                    shadow[1],
                                    shadow[2],
                                    shadow[3],
                                    0.02f,
                                    0.03f,
                                    0.04f,
                                    unit.alive ? 0.42f : 0.24f);
                            } else {
                                appendProjectedQuad(
                                    shadow[0],
                                    shadow[1],
                                    shadow[2],
                                    shadow[3],
                                    0.02f,
                                    0.03f,
                                    0.04f,
                                    unit.alive ? 0.42f : 0.24f);
                            }
                        }

                        bool drewModelMesh = false;
                        if (const runtime::backend_model::MeshData* mesh = meshForUnit) {
                            const std::size_t triangleCount = mesh->indices.size() / 3u;
                            if (triangleCount == 0u) continue;
                            const std::size_t maxTrianglesPerUnit = backendModelTriangleLimit();
                            const float detailScale = std::clamp(unitSize / 70.0f, 0.45f, 1.0f);
                            const std::size_t minTrianglesPerUnit =
                                std::min<std::size_t>(1800u, maxTrianglesPerUnit);
                            const std::size_t scaledBudget = static_cast<std::size_t>(
                                std::clamp(
                                    static_cast<double>(maxTrianglesPerUnit) *
                                        static_cast<double>(detailScale),
                                    static_cast<double>(minTrianglesPerUnit),
                                    static_cast<double>(maxTrianglesPerUnit)));
                            const std::size_t unitTriangleBudget =
                                std::min(triangleCount, std::max(minTrianglesPerUnit, scaledBudget));
                            const bool useIndexedWorldModelPath =
                                supportsWorldTriangles3D && supportsWorldIndexedMeshes;
                            const bool fullIndexedMeshPath =
                                useIndexedWorldModelPath && backendModelFullMeshEnabled();
                            std::size_t effectiveUnitTriangleBudget = unitTriangleBudget;
                            if (fullIndexedMeshPath) {
                                effectiveUnitTriangleBudget = triangleCount;
                            } else {
                                if (remainingModelTrianglesBudget > 0u) {
                                    effectiveUnitTriangleBudget =
                                        std::min(effectiveUnitTriangleBudget, remainingModelTrianglesBudget);
                                } else {
                                    effectiveUnitTriangleBudget =
                                        std::min<std::size_t>(triangleCount, 384u);
                                }
                                if (effectiveUnitTriangleBudget == 0u) {
                                    effectiveUnitTriangleBudget = std::min<std::size_t>(triangleCount, 384u);
                                }
                                if (remainingModelTrianglesBudget >= effectiveUnitTriangleBudget) {
                                    remainingModelTrianglesBudget -= effectiveUnitTriangleBudget;
                                } else {
                                    remainingModelTrianglesBudget = 0u;
                                }
                            }

                            float resolvedScaleCorrection = std::max(0.05f, unit.modelScaleCorrection);
                            if (!unit.model) {
                                if (const PokemonStats* stats = dataDb.pokemon.getStats(unit.name)) {
                                    const std::string mode = toLowerCopy(stats->modelScaleMode);
                                    if (mode != "normalized") {
                                        const float importerScale = std::max(0.0f, mesh->modelScaleFactor);
                                        if (importerScale > 1e-6f) {
                                            resolvedScaleCorrection =
                                                std::max(0.05f, 1.0f / importerScale);
                                        }
                                    }
                                }
                            }
                            const float modelScale =
                                std::max(0.01f, mesh->modelScaleFactor) *
                                resolvedScaleCorrection *
                                std::max(0.05f, unit.speciesScale) *
                                renderVisualScale *
                                renderCaptureScale *
                                attackPulse;
                            glm::vec3 renderPos = proxyCenter;
                            const float minAllowedModelY = boardSurfaceY + 0.0025f;
                            const float approxModelMinY = renderPos.y + mesh->boundsMin.y * modelScale;
                            if (std::isfinite(approxModelMinY) && approxModelMinY < minAllowedModelY) {
                                renderPos.y += (minAllowedModelY - approxModelMinY);
                            }
                            const glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(modelScale));
                            const glm::mat4 rotationX =
                                glm::rotate(glm::mat4(1.0f), glm::radians(animPitch), glm::vec3(1, 0, 0));
                            const glm::mat4 rotationY =
                                glm::rotate(glm::mat4(1.0f), glm::radians(animYaw), glm::vec3(0, 1, 0));
                            const glm::mat4 rotationZ =
                                glm::rotate(glm::mat4(1.0f), glm::radians(animRoll), glm::vec3(0, 0, 1));
                            const glm::mat4 translation = glm::translate(glm::mat4(1.0f), renderPos);
                            const glm::mat4 modelM = translation * rotationY * rotationX * rotationZ * scale;
                            const std::size_t modelDepthCountBefore = modelDepthTris.size();
                            const std::size_t modelDepthWorldCountBefore = modelDepthWorldTris.size();
                            const std::size_t world3DTriangleCountBefore = world3DTriangles.size();
                            static thread_local std::vector<int> submeshNodeFallback;
                            submeshNodeFallback.clear();
                            if (!mesh->submeshMeshIndex.empty()) {
                                submeshNodeFallback.assign(mesh->submeshMeshIndex.size(), -1);
                                for (std::size_t si = 0; si < mesh->submeshMeshIndex.size(); ++si) {
                                    const int meshIndex = mesh->submeshMeshIndex[si];
                                    if (meshIndex >= 0 &&
                                        static_cast<std::size_t>(meshIndex) < mesh->meshIndexToNode.size()) {
                                        submeshNodeFallback[si] =
                                            mesh->meshIndexToNode[static_cast<std::size_t>(meshIndex)];
                                    }
                                }
                            }
                            std::vector<WorldIndexedBatch> modelIndexedBatchesPerSubmesh;
                            std::vector<std::vector<int>> modelIndexedVertexRemap;
                            if (useIndexedWorldModelPath) {
                                const std::size_t batchCount =
                                    std::max<std::size_t>(1u, mesh->submeshBaseTextures.size());
                                modelIndexedBatchesPerSubmesh.resize(batchCount);
                                if (fullIndexedMeshPath && !mesh->vertices.empty()) {
                                    modelIndexedVertexRemap.resize(batchCount);
                                    for (auto& remap : modelIndexedVertexRemap) {
                                        remap.assign(mesh->vertices.size(), -1);
                                    }
                                }

                                std::string unitModelPath;
                                if (const PokemonStats* stats = dataDb.pokemon.getStats(unit.name)) {
                                    if (!stats->model.empty()) {
                                        unitModelPath = "assets/models/" + stats->model;
                                    }
                                }
                                for (std::size_t si = 0; si < modelIndexedBatchesPerSubmesh.size(); ++si) {
                                    auto& batch = modelIndexedBatchesPerSubmesh[si];
                                    batch.vertices.reserve((effectiveUnitTriangleBudget * 3u) / batchCount + 64u);
                                    batch.indices.reserve((effectiveUnitTriangleBudget * 3u) / batchCount + 64u);
                                    batch.sortDepth = glm::dot(cameraWorldPos - proxyCenter, cameraWorldPos - proxyCenter);
                                    if (si < mesh->submeshBaseTextures.size()) {
                                        const auto& tex = mesh->submeshBaseTextures[si];
                                        if (tex.hasPixels() && !unitModelPath.empty()) {
                                            batch.textureKey = unitModelPath + "#submesh:" + std::to_string(si);
                                            batch.textureRgba = tex.rgba.data();
                                            batch.textureWidth = tex.width;
                                            batch.textureHeight = tex.height;
                                            batch.textureWrapS = tex.wrapS;
                                            batch.textureWrapT = tex.wrapT;
                                        }
                                    }
                                    if (si < mesh->submeshAlphaMode.size()) {
                                        batch.alphaMode = mesh->submeshAlphaMode[si];
                                    }
                                    if (si < mesh->submeshAlphaCutoff.size()) {
                                        batch.alphaCutoff = mesh->submeshAlphaCutoff[si];
                                    }
                                    // During faint fade-out, force alpha blending for textured submeshes so MASK/OPAQUE
                                    // materials don't pop/cut out while the model fades away.
                                    if (modelFadeAlpha < 0.999f) {
                                        batch.alphaMode = 2u;
                                        batch.blendMode = 0u;
                                        batch.alphaCutoff = 0.0f;
                                    }
                                }
                            }
                            if (!scenePoseReady) {
                                scenePose = evaluateScenePose(*mesh, unit);
                                scenePoseReady = true;
                            }
                            const auto& nodeGlobals = scenePose.hasScenePose ? scenePose.nodeGlobals : mesh->bindNodeGlobals;
                            const bool hasClipPose = scenePose.hasClipPose;
                            const bool useFastTexturedFullMeshPath =
                                supportsWorldTriangles3D &&
                                useIndexedWorldModelPath &&
                                backendModelFastTexturedPathEnabled() &&
                                fullIndexedMeshPath;
                            bool allSubmeshesTextured = !mesh->submeshBaseTextures.empty();
                            if (allSubmeshesTextured) {
                                for (const auto& tex : mesh->submeshBaseTextures) {
                                    if (!tex.hasPixels()) {
                                        allSubmeshesTextured = false;
                                        break;
                                    }
                                }
                            }
                            const bool usePositionOnlyVertexPath =
                                useFastTexturedFullMeshPath &&
                                allSubmeshesTextured;

                            const glm::vec3 lightDir = glm::normalize(glm::vec3(0.45f, 0.90f, 0.35f));
                            const glm::vec3 fallbackBase(
                                std::clamp(tint.r * 0.85f + 0.10f, 0.0f, 1.0f),
                                std::clamp(tint.g * 0.85f + 0.10f, 0.0f, 1.0f),
                                std::clamp(tint.b * 0.85f + 0.10f, 0.0f, 1.0f));
                            const auto safeNormalize = [](const glm::vec3& v) {
                                const float lenSq = glm::dot(v, v);
                                if (lenSq > 1e-12f) return glm::normalize(v);
                                return glm::vec3(0.0f, 1.0f, 0.0f);
                            };

                            const std::size_t nodeCount = nodeGlobals.size();
                            static thread_local std::vector<std::vector<glm::mat4>> skinMatricesByNode;
                            static thread_local std::vector<std::uint8_t> skinMatricesReady;
                            static thread_local std::vector<glm::mat4> nodeGlobalInverseCache;
                            static thread_local std::vector<std::uint8_t> nodeGlobalInverseReady;
                            if (skinMatricesByNode.size() < nodeCount) skinMatricesByNode.resize(nodeCount);
                            for (std::size_t ni = 0; ni < nodeCount; ++ni) skinMatricesByNode[ni].clear();
                            if (skinMatricesReady.size() < nodeCount) skinMatricesReady.resize(nodeCount, 0u);
                            std::fill(skinMatricesReady.begin(), skinMatricesReady.begin() + nodeCount, 0u);
                            if (nodeGlobalInverseCache.size() < nodeCount) {
                                nodeGlobalInverseCache.resize(nodeCount, glm::mat4(1.0f));
                            }
                            if (nodeGlobalInverseReady.size() < nodeCount) nodeGlobalInverseReady.resize(nodeCount, 0u);
                            std::fill(nodeGlobalInverseReady.begin(), nodeGlobalInverseReady.begin() + nodeCount, 0u);

                            const auto ensureSkinMatricesForNode =
                                [&](int nodeIndex) -> const std::vector<glm::mat4>* {
                                if (nodeIndex < 0 ||
                                    static_cast<std::size_t>(nodeIndex) >= mesh->nodeSkin.size() ||
                                    static_cast<std::size_t>(nodeIndex) >= nodeGlobals.size()) {
                                    return nullptr;
                                }
                                const std::size_t nodeIdx = static_cast<std::size_t>(nodeIndex);
                                const int skinIndex = mesh->nodeSkin[nodeIdx];
                                if (skinIndex < 0 || static_cast<std::size_t>(skinIndex) >= mesh->skins.size()) {
                                    return nullptr;
                                }
                                if (skinMatricesReady[nodeIdx] == 0u) {
                                    const auto& skin = mesh->skins[static_cast<std::size_t>(skinIndex)];
                                    if (nodeGlobalInverseReady[nodeIdx] == 0u) {
                                        nodeGlobalInverseCache[nodeIdx] = glm::inverse(nodeGlobals[nodeIdx]);
                                        nodeGlobalInverseReady[nodeIdx] = 1u;
                                    }
                                    const glm::mat4& invMeshGlobal = nodeGlobalInverseCache[nodeIdx];
                                    auto& mats = skinMatricesByNode[nodeIdx];
                                    mats.assign(skin.joints.size(), glm::mat4(1.0f));
                                    for (std::size_t j = 0; j < skin.joints.size(); ++j) {
                                        const int jointNode = skin.joints[j];
                                        if (jointNode < 0 ||
                                            static_cast<std::size_t>(jointNode) >= nodeGlobals.size()) {
                                            continue;
                                        }
                                        const glm::mat4 invBind =
                                            (j < skin.inverseBind.size())
                                                ? skin.inverseBind[j]
                                                : glm::mat4(1.0f);
                                        mats[j] =
                                            invMeshGlobal *
                                            nodeGlobals[static_cast<std::size_t>(jointNode)] *
                                            invBind;
                                    }
                                    skinMatricesReady[nodeIdx] = 1u;
                                }
                                return &skinMatricesByNode[nodeIdx];
                            };
                            const auto skinVertexAtNode = [&](int nodeIndex,
                                                             const runtime::backend_model::MeshVertex& vtx,
                                                             const glm::vec3& localPos,
                                                             const glm::vec3& localNormal) {
                                struct SkinResult {
                                    glm::vec3 pos;
                                    glm::vec3 normal;
                                    bool applied = false;
                                } outSkin{localPos, localNormal, false};
                                const auto* matsPtr = ensureSkinMatricesForNode(nodeIndex);
                                if (!matsPtr) return outSkin;
                                const auto& mats = *matsPtr;
                                const std::uint16_t joints[4] = {vtx.j0, vtx.j1, vtx.j2, vtx.j3};
                                const float weights[4] = {vtx.w0, vtx.w1, vtx.w2, vtx.w3};
                                const bool rigidSingleJoint =
                                    (weights[0] >= 0.999f) &&
                                    (weights[1] <= 0.00001f) &&
                                    (weights[2] <= 0.00001f) &&
                                    (weights[3] <= 0.00001f) &&
                                    (static_cast<std::size_t>(joints[0]) < mats.size());
                                if (rigidSingleJoint) {
                                    const glm::mat4& m = mats[static_cast<std::size_t>(joints[0])];
                                    outSkin.pos = glm::vec3(m * glm::vec4(localPos, 1.0f));
                                    outSkin.normal = safeNormalize(glm::mat3(m) * localNormal);
                                    outSkin.applied = true;
                                    return outSkin;
                                }
                                glm::vec4 blendedPos(0.0f);
                                glm::vec3 blendedNormal(0.0f);
                                float totalWeight = 0.0f;
                                for (int i = 0; i < 4; ++i) {
                                    const float w = weights[i];
                                    if (w <= 0.00001f) continue;
                                    const std::size_t joint = static_cast<std::size_t>(joints[i]);
                                    if (joint >= mats.size()) continue;
                                    blendedPos += (mats[joint] * glm::vec4(localPos, 1.0f)) * w;
                                    blendedNormal += (glm::mat3(mats[joint]) * localNormal) * w;
                                    totalWeight += w;
                                }
                                if (totalWeight <= 0.00001f) return outSkin;
                                if (totalWeight < 0.999f) {
                                    const float remain = 1.0f - totalWeight;
                                    blendedPos += glm::vec4(localPos, 1.0f) * remain;
                                    blendedNormal += localNormal * remain;
                                }
                                outSkin.pos = glm::vec3(blendedPos);
                                outSkin.normal = safeNormalize(blendedNormal);
                                outSkin.applied = true;
                                return outSkin;
                            };
                            const auto skinPositionAtNode = [&](int nodeIndex,
                                                                const runtime::backend_model::MeshVertex& vtx,
                                                                const glm::vec3& localPos) {
                                glm::vec3 outPos = localPos;
                                const auto* matsPtr = ensureSkinMatricesForNode(nodeIndex);
                                if (!matsPtr) return outPos;
                                const auto& mats = *matsPtr;
                                const std::uint16_t joints[4] = {vtx.j0, vtx.j1, vtx.j2, vtx.j3};
                                const float weights[4] = {vtx.w0, vtx.w1, vtx.w2, vtx.w3};
                                const bool rigidSingleJoint =
                                    (weights[0] >= 0.999f) &&
                                    (weights[1] <= 0.00001f) &&
                                    (weights[2] <= 0.00001f) &&
                                    (weights[3] <= 0.00001f) &&
                                    (static_cast<std::size_t>(joints[0]) < mats.size());
                                if (rigidSingleJoint) {
                                    outPos = glm::vec3(
                                        mats[static_cast<std::size_t>(joints[0])] *
                                        glm::vec4(localPos, 1.0f));
                                    return outPos;
                                }
                                glm::vec4 blendedPos(0.0f);
                                float totalWeight = 0.0f;
                                for (int i = 0; i < 4; ++i) {
                                    const float w = weights[i];
                                    if (w <= 0.00001f) continue;
                                    const std::size_t joint = static_cast<std::size_t>(joints[i]);
                                    if (joint >= mats.size()) continue;
                                    blendedPos += (mats[joint] * glm::vec4(localPos, 1.0f)) * w;
                                    totalWeight += w;
                                }
                                if (totalWeight <= 0.00001f) return outPos;
                                if (totalWeight < 0.999f) {
                                    const float remain = 1.0f - totalWeight;
                                    blendedPos += glm::vec4(localPos, 1.0f) * remain;
                                }
                                outPos = glm::vec3(blendedPos);
                                return outPos;
                            };

                            struct NodeTransformCacheEntry {
                                glm::mat4 worldM{1.0f};
                                glm::mat3 worldNormalM{1.0f};
                            };
                            static thread_local std::vector<NodeTransformCacheEntry> nodeTransformCache;
                            static thread_local std::vector<std::uint8_t> nodeTransformWorldReady;
                            static thread_local std::vector<std::uint8_t> nodeTransformNormalReady;
                            const std::size_t nodeCacheCount = nodeCount + 1u;
                            if (nodeTransformCache.size() < nodeCacheCount) nodeTransformCache.resize(nodeCacheCount);
                            if (nodeTransformWorldReady.size() < nodeCacheCount) {
                                nodeTransformWorldReady.resize(nodeCacheCount, 0u);
                            }
                            if (nodeTransformNormalReady.size() < nodeCacheCount) {
                                nodeTransformNormalReady.resize(nodeCacheCount, 0u);
                            }
                            std::fill(
                                nodeTransformWorldReady.begin(),
                                nodeTransformWorldReady.begin() + nodeCacheCount,
                                0u);
                            std::fill(
                                nodeTransformNormalReady.begin(),
                                nodeTransformNormalReady.begin() + nodeCacheCount,
                                0u);

                            const auto nodeTransformIndexFor = [&](int triNodeIndex) -> std::size_t {
                                std::size_t cacheIndex = 0u;
                                if (triNodeIndex >= 0 && static_cast<std::size_t>(triNodeIndex) < nodeCount) {
                                    cacheIndex = static_cast<std::size_t>(triNodeIndex) + 1u;
                                }
                                return cacheIndex;
                            };
                            const auto worldMatrixForNode = [&](int triNodeIndex) -> const glm::mat4& {
                                const std::size_t cacheIndex = nodeTransformIndexFor(triNodeIndex);
                                if (nodeTransformWorldReady[cacheIndex] != 0u) {
                                    return nodeTransformCache[cacheIndex].worldM;
                                }
                                const glm::mat4 nodeGlobal =
                                    (triNodeIndex >= 0 &&
                                     static_cast<std::size_t>(triNodeIndex) < nodeGlobals.size())
                                        ? nodeGlobals[static_cast<std::size_t>(triNodeIndex)]
                                        : glm::mat4(1.0f);
                                auto& entry = nodeTransformCache[cacheIndex];
                                entry.worldM = modelM * nodeGlobal;
                                nodeTransformWorldReady[cacheIndex] = 1u;
                                return entry.worldM;
                            };
                            const auto worldNormalMatrixForNode = [&](int triNodeIndex) -> const glm::mat3& {
                                const std::size_t cacheIndex = nodeTransformIndexFor(triNodeIndex);
                                if (nodeTransformNormalReady[cacheIndex] != 0u) {
                                    return nodeTransformCache[cacheIndex].worldNormalM;
                                }
                                if (nodeTransformWorldReady[cacheIndex] == 0u) {
                                    (void)worldMatrixForNode(triNodeIndex);
                                }
                                auto& entry = nodeTransformCache[cacheIndex];
                                entry.worldNormalM = glm::transpose(glm::inverse(glm::mat3(entry.worldM)));
                                nodeTransformNormalReady[cacheIndex] = 1u;
                                return entry.worldNormalM;
                            };

                            if (unit.alive && !unit.fainting && toLowerCopy(unit.name) == "charmander") {
                                const TailFireVFX::Config& tailCfg = getSharedTailFireFallbackCfg();
                                const int tailNodeIndex = tailCfg.tailTipNodeIndex;
                                if (tailNodeIndex >= 0 &&
                                    static_cast<std::size_t>(tailNodeIndex) < nodeGlobals.size()) {
                                    const glm::mat4& tailWorldM = worldMatrixForNode(tailNodeIndex);

                                    auto safeNorm = [](glm::vec3 v, const glm::vec3& fallback) {
                                        const float len2 = glm::dot(v, v);
                                        if (len2 <= 1e-10f) return fallback;
                                        return v * (1.0f / std::sqrt(len2));
                                    };
                                    glm::vec3 bx = safeNorm(glm::vec3(tailWorldM[0]), glm::vec3(1.0f, 0.0f, 0.0f));
                                    glm::vec3 by = glm::vec3(tailWorldM[1]);
                                    by = by - bx * glm::dot(by, bx);
                                    by = safeNorm(by, glm::vec3(0.0f, 1.0f, 0.0f));
                                    glm::vec3 bz = safeNorm(glm::cross(bx, by), glm::vec3(0.0f, 0.0f, 1.0f));
                                    if (glm::dot(glm::cross(bx, by), bz) < 0.0f) {
                                        bz = -bz;
                                    }
                                    const glm::mat3 tailBasis(bx, by, bz);
                                    glm::vec3 backDirWorld = tailBasis * tailCfg.backDir;
                                    backDirWorld = safeNorm(backDirWorld, glm::vec3(0.0f, 1.0f, 0.0f));

                                    SharedTailFireAnchor& anchor = sharedTailFireAnchors[unit.id];
                                    anchor.valid = true;
                                    anchor.pos = glm::vec3(tailWorldM[3]) + glm::vec3(0.0f, tailCfg.tailWorldYOffset, 0.0f);
                                    anchor.basis = tailBasis;
                                    anchor.backDir = backDirWorld;
                                    anchor.particleSizeScale =
                                        std::max(0.01f, std::max(0.01f, mesh->modelScaleFactor) * resolvedScaleCorrection);
                                }
                            }

                            struct WorldVertexSample {
                                glm::vec3 pos{0.0f};
                                glm::vec3 normal{0.0f, 1.0f, 0.0f};
                            };
                            const std::size_t meshVertexCount = mesh->vertices.size();
                            static thread_local std::vector<WorldVertexSample> worldVertexCache;
                            static thread_local std::vector<int> worldVertexCacheNode;
                            static thread_local std::vector<std::uint8_t> worldVertexCacheValid;
                            static thread_local std::vector<glm::vec3> worldVertexPosCache;
                            static thread_local std::vector<int> worldVertexPosCacheNode;
                            static thread_local std::vector<std::uint8_t> worldVertexPosCacheValid;
                            if (!usePositionOnlyVertexPath) {
                                worldVertexCache.resize(meshVertexCount);
                                worldVertexCacheNode.assign(meshVertexCount, std::numeric_limits<int>::min());
                                worldVertexCacheValid.assign(meshVertexCount, 0u);
                            } else {
                                worldVertexCache.clear();
                                worldVertexCacheNode.clear();
                                worldVertexCacheValid.clear();
                            }
                            worldVertexPosCache.resize(meshVertexCount);
                            worldVertexPosCacheNode.assign(meshVertexCount, std::numeric_limits<int>::min());
                            worldVertexPosCacheValid.assign(meshVertexCount, 0u);
                            const auto resolveWorldVertex = [&](int triNodeIndex,
                                                                std::uint32_t vertexIndex,
                                                                const runtime::backend_model::MeshVertex& vtx) {
                                if (vertexIndex < worldVertexCache.size() &&
                                    worldVertexCacheValid[vertexIndex] != 0u &&
                                    worldVertexCacheNode[vertexIndex] == triNodeIndex) {
                                    return worldVertexCache[vertexIndex];
                                }

                                glm::vec3 local = vtx.position;
                                if (!hasClipPose) {
                                    local = runtime::backend_anim::deformLocalVertex(
                                        unit,
                                        pose,
                                        local,
                                        mesh->boundsMin,
                                        mesh->boundsMax,
                                        worldCellSize);
                                }
                                const auto sk = skinVertexAtNode(triNodeIndex, vtx, local, vtx.normal);
                                const glm::mat4& worldM = worldMatrixForNode(triNodeIndex);
                                const glm::mat3& worldNormalM = worldNormalMatrixForNode(triNodeIndex);
                                WorldVertexSample out;
                                out.pos = glm::vec3(worldM * glm::vec4(sk.pos, 1.0f));
                                out.normal = safeNormalize(worldNormalM * sk.normal);
                                if (vertexIndex < worldVertexCache.size()) {
                                    worldVertexCache[vertexIndex] = out;
                                    worldVertexCacheNode[vertexIndex] = triNodeIndex;
                                    worldVertexCacheValid[vertexIndex] = 1u;
                                }
                                return out;
                            };
                            const auto resolveWorldVertexPos = [&](int triNodeIndex,
                                                                   std::uint32_t vertexIndex,
                                                                   const runtime::backend_model::MeshVertex& vtx) {
                                if (vertexIndex < worldVertexPosCache.size() &&
                                    worldVertexPosCacheValid[vertexIndex] != 0u &&
                                    worldVertexPosCacheNode[vertexIndex] == triNodeIndex) {
                                    return worldVertexPosCache[vertexIndex];
                                }

                                glm::vec3 local = vtx.position;
                                if (!hasClipPose) {
                                    local = runtime::backend_anim::deformLocalVertex(
                                        unit,
                                        pose,
                                        local,
                                        mesh->boundsMin,
                                        mesh->boundsMax,
                                        worldCellSize);
                                }
                                const glm::vec3 skinnedPos = skinPositionAtNode(triNodeIndex, vtx, local);
                                const glm::mat4& worldM = worldMatrixForNode(triNodeIndex);
                                const glm::vec3 outPos = glm::vec3(worldM * glm::vec4(skinnedPos, 1.0f));
                                if (vertexIndex < worldVertexPosCache.size()) {
                                    worldVertexPosCache[vertexIndex] = outPos;
                                    worldVertexPosCacheNode[vertexIndex] = triNodeIndex;
                                    worldVertexPosCacheValid[vertexIndex] = 1u;
                                }
                                return outPos;
                            };

                            const auto pushModelTriangle = [&](const glm::vec3& a,
                                                                const glm::vec3& b,
                                                                const glm::vec3& c,
                                                                std::uint32_t src0,
                                                                std::uint32_t src1,
                                                                std::uint32_t src2,
                                                                const glm::vec2& uv0,
                                                                const glm::vec2& uv1,
                                                                const glm::vec2& uv2,
                                                                const glm::vec3& n0,
                                                                const glm::vec3& n1,
                                                                const glm::vec3& n2,
                                                                const glm::vec3& baseColor0,
                                                                const glm::vec3& baseColor1,
                                                                const glm::vec3& baseColor2,
                                                                std::uint16_t submeshIndex,
                                                                float alpha,
                                                                bool doubleSided) {
                                float x1 = 0.0f;
                                float y1 = 0.0f;
                                float z1 = 0.0f;
                                float x2 = 0.0f;
                                float y2 = 0.0f;
                                float z2 = 0.0f;
                                float x3 = 0.0f;
                                float y3 = 0.0f;
                                float z3 = 0.0f;
                                if (!supportsWorldTriangles3D) {
                                    if (!projectWorld(a, x1, y1, z1) ||
                                        !projectWorld(b, x2, y2, z2) ||
                                        !projectWorld(c, x3, y3, z3)) {
                                        return;
                                    }
                                    if ((z1 < 0.0f || z1 > 1.0f) &&
                                        (z2 < 0.0f || z2 > 1.0f) &&
                                        (z3 < 0.0f || z3 > 1.0f)) {
                                        return;
                                    }
                                }

                                const float outAlpha = alpha;
                                if (supportsWorldTriangles3D &&
                                    useIndexedWorldModelPath &&
                                    backendModelFastTexturedPathEnabled()) {
                                    std::size_t fastBatchIndex = static_cast<std::size_t>(submeshIndex);
                                    if (fastBatchIndex >= modelIndexedBatchesPerSubmesh.size()) fastBatchIndex = 0u;
                                    auto& fastBatch = modelIndexedBatchesPerSubmesh[fastBatchIndex];
                                    const bool fastTexturedBatch =
                                        fastBatch.textureRgba != nullptr &&
                                        fastBatch.textureWidth > 0 &&
                                        fastBatch.textureHeight > 0;
                                    if (fastTexturedBatch) {
                                        const glm::vec3 flatTint(1.0f, 1.0f, 1.0f);
                                        const bool canReuseIndexedVertices =
                                            fullIndexedMeshPath &&
                                            fastBatchIndex < modelIndexedVertexRemap.size();
                                        const auto appendFastVertex =
                                            [&](std::uint32_t src,
                                                const glm::vec3& pos,
                                                const glm::vec2& uv) -> std::uint32_t {
                                            if (canReuseIndexedVertices &&
                                                src < modelIndexedVertexRemap[fastBatchIndex].size()) {
                                                int& mapped = modelIndexedVertexRemap[fastBatchIndex][src];
                                                if (mapped >= 0) {
                                                    return static_cast<std::uint32_t>(mapped);
                                                }
                                                if (fastBatch.vertices.size() >=
                                                    static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
                                                    return std::numeric_limits<std::uint32_t>::max();
                                                }
                                                const std::uint32_t next =
                                                    static_cast<std::uint32_t>(fastBatch.vertices.size());
                                                fastBatch.vertices.push_back(IRenderBackend::WorldMeshVertex{
                                                    pos.x,
                                                    pos.y,
                                                    pos.z,
                                                    uv.x,
                                                    uv.y,
                                                    flatTint.r,
                                                    flatTint.g,
                                                    flatTint.b,
                                                    outAlpha});
                                                mapped = static_cast<int>(next);
                                                return next;
                                            }
                                            if (fastBatch.vertices.size() >=
                                                static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
                                                return std::numeric_limits<std::uint32_t>::max();
                                            }
                                            const std::uint32_t next =
                                                static_cast<std::uint32_t>(fastBatch.vertices.size());
                                            fastBatch.vertices.push_back(IRenderBackend::WorldMeshVertex{
                                                pos.x,
                                                pos.y,
                                                pos.z,
                                                uv.x,
                                                uv.y,
                                                flatTint.r,
                                                flatTint.g,
                                                flatTint.b,
                                                outAlpha});
                                            return next;
                                        };

                                        const std::uint32_t outI0 = appendFastVertex(src0, a, uv0);
                                        const std::uint32_t outI1 = appendFastVertex(src1, b, uv1);
                                        const std::uint32_t outI2 = appendFastVertex(src2, c, uv2);
                                        if (outI0 == std::numeric_limits<std::uint32_t>::max() ||
                                            outI1 == std::numeric_limits<std::uint32_t>::max() ||
                                            outI2 == std::numeric_limits<std::uint32_t>::max()) {
                                            return;
                                        }
                                        fastBatch.indices.push_back(outI0);
                                        fastBatch.indices.push_back(outI1);
                                        fastBatch.indices.push_back(outI2);
                                        return;
                                    }
                                }

                                const glm::vec3 triCenter = (a + b + c) * (1.0f / 3.0f);
                                const glm::vec3 rawFaceNormal = glm::cross(b - a, c - a);
                                const float rawFaceLenSq = glm::dot(rawFaceNormal, rawFaceNormal);
                                const glm::vec3 faceNormal = (rawFaceLenSq > 0.000001f)
                                    ? glm::normalize(rawFaceNormal)
                                    : safeNormalize(n0 + n1 + n2);
                                glm::vec3 toCameraCenter = cameraWorldPos - triCenter;
                                const float toCameraCenterLenSq = glm::dot(toCameraCenter, toCameraCenter);
                                if (toCameraCenterLenSq > 0.000001f) {
                                    toCameraCenter = glm::normalize(toCameraCenter);
                                } else {
                                    toCameraCenter = glm::vec3(0.0f, 0.0f, -1.0f);
                                }
                                const float faceFacing = std::clamp(glm::dot(faceNormal, toCameraCenter), -1.0f, 1.0f);
                                if (backendModelBackfaceCullingEnabled() && !doubleSided && faceFacing <= 0.01f) {
                                    return;
                                }
                                const bool flipForBackface = doubleSided && (faceFacing < 0.0f);

                                if (supportsWorldTriangles3D && useIndexedWorldModelPath) {
                                    std::size_t batchIndex = static_cast<std::size_t>(submeshIndex);
                                    if (batchIndex >= modelIndexedBatchesPerSubmesh.size()) batchIndex = 0u;
                                    auto& batch = modelIndexedBatchesPerSubmesh[batchIndex];
                                    const bool texturedBatch =
                                        batch.textureRgba != nullptr &&
                                        batch.textureWidth > 0 &&
                                        batch.textureHeight > 0;

                                    if (texturedBatch) {
                                        const auto shadeTint = [&](const glm::vec3& normal,
                                                                   const glm::vec3& worldPos) {
                                            return runtime::backend_material::shadeVertexLitColor(
                                                glm::vec3(1.0f),
                                                normal,
                                                lightDir,
                                                cameraWorldPos - worldPos,
                                                flipForBackface);
                                        };
                                        const glm::vec3 outC0 = shadeTint(n0, a);
                                        const glm::vec3 outC1 = shadeTint(n1, b);
                                        const glm::vec3 outC2 = shadeTint(n2, c);

                                        const bool canReuseIndexedVertices =
                                            fullIndexedMeshPath &&
                                            batchIndex < modelIndexedVertexRemap.size();
                                        const auto appendIndexedVertex =
                                            [&](std::uint32_t src,
                                                const glm::vec3& pos,
                                                const glm::vec2& uv,
                                                const glm::vec3& outColor) -> std::uint32_t {
                                            if (canReuseIndexedVertices &&
                                                src < modelIndexedVertexRemap[batchIndex].size()) {
                                                int& mapped = modelIndexedVertexRemap[batchIndex][src];
                                                if (mapped >= 0) {
                                                    return static_cast<std::uint32_t>(mapped);
                                                }
                                                if (batch.vertices.size() >=
                                                    static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
                                                    return std::numeric_limits<std::uint32_t>::max();
                                                }
                                                const std::uint32_t next =
                                                    static_cast<std::uint32_t>(batch.vertices.size());
                                                batch.vertices.push_back(IRenderBackend::WorldMeshVertex{
                                                    pos.x,
                                                    pos.y,
                                                    pos.z,
                                                    uv.x,
                                                    uv.y,
                                                    outColor.r,
                                                    outColor.g,
                                                    outColor.b,
                                                    outAlpha});
                                                mapped = static_cast<int>(next);
                                                return next;
                                            }
                                            if (batch.vertices.size() >=
                                                static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
                                                return std::numeric_limits<std::uint32_t>::max();
                                            }
                                            const std::uint32_t next = static_cast<std::uint32_t>(batch.vertices.size());
                                            batch.vertices.push_back(IRenderBackend::WorldMeshVertex{
                                                pos.x,
                                                pos.y,
                                                pos.z,
                                                uv.x,
                                                uv.y,
                                                outColor.r,
                                                outColor.g,
                                                outColor.b,
                                                outAlpha});
                                            return next;
                                        };

                                        const std::uint32_t outI0 = appendIndexedVertex(src0, a, uv0, outC0);
                                        const std::uint32_t outI1 = appendIndexedVertex(src1, b, uv1, outC1);
                                        const std::uint32_t outI2 = appendIndexedVertex(src2, c, uv2, outC2);
                                        if (outI0 == std::numeric_limits<std::uint32_t>::max() ||
                                            outI1 == std::numeric_limits<std::uint32_t>::max() ||
                                            outI2 == std::numeric_limits<std::uint32_t>::max()) {
                                            return;
                                        }
                                        batch.indices.push_back(outI0);
                                        batch.indices.push_back(outI1);
                                        batch.indices.push_back(outI2);
                                        return;
                                    }

                                    const auto shadeColor = [&](const glm::vec3& baseColor,
                                                                const glm::vec3& normal,
                                                                const glm::vec3& worldPos) {
                                        return runtime::backend_material::shadeVertexLitColor(
                                            baseColor,
                                            normal,
                                            lightDir,
                                            cameraWorldPos - worldPos,
                                            flipForBackface);
                                    };
                                    const glm::vec3 shaded0 = shadeColor(baseColor0, n0, a);
                                    const glm::vec3 shaded1 = shadeColor(baseColor1, n1, b);
                                    const glm::vec3 shaded2 = shadeColor(baseColor2, n2, c);
                                    const std::size_t nextVertexCount = batch.vertices.size() + 3u;
                                    if (nextVertexCount >=
                                        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
                                        return;
                                    }

                                    const std::uint32_t base = static_cast<std::uint32_t>(batch.vertices.size());
                                    batch.vertices.push_back(IRenderBackend::WorldMeshVertex{
                                        a.x, a.y, a.z, uv0.x, uv0.y, shaded0.r, shaded0.g, shaded0.b, outAlpha});
                                    batch.vertices.push_back(IRenderBackend::WorldMeshVertex{
                                        b.x, b.y, b.z, uv1.x, uv1.y, shaded1.r, shaded1.g, shaded1.b, outAlpha});
                                    batch.vertices.push_back(IRenderBackend::WorldMeshVertex{
                                        c.x, c.y, c.z, uv2.x, uv2.y, shaded2.r, shaded2.g, shaded2.b, outAlpha});
                                    batch.indices.push_back(base + 0u);
                                    batch.indices.push_back(base + 1u);
                                    batch.indices.push_back(base + 2u);
                                    return;
                                }

                                const auto shadeColor = [&](const glm::vec3& baseColor,
                                                            const glm::vec3& normal,
                                                            const glm::vec3& worldPos) {
                                    return runtime::backend_material::shadeVertexLitColor(
                                        baseColor,
                                        normal,
                                        lightDir,
                                        cameraWorldPos - worldPos,
                                        flipForBackface);
                                };
                                const glm::vec3 shaded0 = shadeColor(baseColor0, n0, a);
                                const glm::vec3 shaded1 = shadeColor(baseColor1, n1, b);
                                const glm::vec3 shaded2 = shadeColor(baseColor2, n2, c);
                                const glm::vec3 shadedAvg = (shaded0 + shaded1 + shaded2) * (1.0f / 3.0f);

                                if (supportsWorldTriangles3D) {
                                    IRenderBackend::WorldTriangle tri3d;
                                    tri3d.x1 = a.x;
                                    tri3d.y1 = a.y;
                                    tri3d.z1 = a.z;
                                    tri3d.x2 = b.x;
                                    tri3d.y2 = b.y;
                                    tri3d.z2 = b.z;
                                    tri3d.x3 = c.x;
                                    tri3d.y3 = c.y;
                                    tri3d.z3 = c.z;
                                    tri3d.r = shadedAvg.r;
                                    tri3d.g = shadedAvg.g;
                                    tri3d.b = shadedAvg.b;
                                    tri3d.a = outAlpha;
                                    tri3d.r1 = shaded0.r;
                                    tri3d.g1 = shaded0.g;
                                    tri3d.b1 = shaded0.b;
                                    tri3d.a1 = outAlpha;
                                    tri3d.r2 = shaded1.r;
                                    tri3d.g2 = shaded1.g;
                                    tri3d.b2 = shaded1.b;
                                    tri3d.a2 = outAlpha;
                                    tri3d.r3 = shaded2.r;
                                    tri3d.g3 = shaded2.g;
                                    tri3d.b3 = shaded2.b;
                                    tri3d.a3 = outAlpha;
                                    world3DTriangles.push_back(tri3d);
                                    return;
                                }

                                DepthTri dt;
                                dt.tri.x1 = x1;
                                dt.tri.y1 = y1;
                                dt.tri.x2 = x2;
                                dt.tri.y2 = y2;
                                dt.tri.x3 = x3;
                                dt.tri.y3 = y3;
                                dt.tri.r = shadedAvg.r;
                                dt.tri.g = shadedAvg.g;
                                dt.tri.b = shadedAvg.b;
                                dt.tri.a = outAlpha;
                                dt.depth = (z1 + z2 + z3) * (1.0f / 3.0f);
                                modelDepthTris.push_back(dt);
                            };
                            const bool downsampleModelTriangles = effectiveUnitTriangleBudget < triangleCount;
                            const float fastTexturedAlpha = std::clamp(modelFadeAlpha, 0.0f, 1.0f);
                            const glm::vec3 fastTexturedTint = glm::mix(
                                glm::vec3(1.0f),
                                captureTintColor,
                                std::clamp(captureVisualTintStrength, 0.0f, 1.0f));
                            std::size_t previousTriSample = triangleCount;
                            for (std::size_t sampleIdx = 0; sampleIdx < effectiveUnitTriangleBudget; ++sampleIdx) {
                                std::size_t triIdx = sampleIdx;
                                if (downsampleModelTriangles) {
                                    triIdx =
                                        selectUniformTriangleIndex(sampleIdx, effectiveUnitTriangleBudget, triangleCount);
                                    if (triIdx == previousTriSample && triIdx + 1u < triangleCount) ++triIdx;
                                }
                                previousTriSample = triIdx;

                                const std::size_t i = triIdx * 3u;
                                const std::uint32_t i0 = mesh->indices[i + 0];
                                const std::uint32_t i1 = mesh->indices[i + 1];
                                const std::uint32_t i2 = mesh->indices[i + 2];
                                if (i0 >= mesh->vertices.size() ||
                                    i1 >= mesh->vertices.size() ||
                                    i2 >= mesh->vertices.size()) {
                                    continue;
                                }

                                const auto& v0 = mesh->vertices[i0];
                                const auto& v1 = mesh->vertices[i1];
                                const auto& v2 = mesh->vertices[i2];

                                int triNodeIndex =
                                    (triIdx < mesh->triangleNodeIndex.size())
                                        ? mesh->triangleNodeIndex[triIdx]
                                        : -1;
                                if (triNodeIndex < 0 &&
                                    triIdx < mesh->triangleSubmesh.size() &&
                                    !submeshNodeFallback.empty()) {
                                    const std::uint16_t submeshIndex = mesh->triangleSubmesh[triIdx];
                                    if (submeshIndex < submeshNodeFallback.size()) {
                                        triNodeIndex = submeshNodeFallback[submeshIndex];
                                    }
                                }

                                const std::uint16_t triSubmeshIndex =
                                    (triIdx < mesh->triangleSubmesh.size())
                                        ? mesh->triangleSubmesh[triIdx]
                                        : static_cast<std::uint16_t>(0u);
                                const bool texturedSubmesh =
                                    useIndexedWorldModelPath &&
                                    static_cast<std::size_t>(triSubmeshIndex) <
                                        modelIndexedBatchesPerSubmesh.size() &&
                                    modelIndexedBatchesPerSubmesh[static_cast<std::size_t>(triSubmeshIndex)]
                                            .textureRgba != nullptr &&
                                    modelIndexedBatchesPerSubmesh[static_cast<std::size_t>(triSubmeshIndex)]
                                            .textureWidth > 0 &&
                                    modelIndexedBatchesPerSubmesh[static_cast<std::size_t>(triSubmeshIndex)]
                                            .textureHeight > 0;
                                if (useFastTexturedFullMeshPath && texturedSubmesh) {
                                    std::size_t fastBatchIndex = static_cast<std::size_t>(triSubmeshIndex);
                                    if (fastBatchIndex >= modelIndexedBatchesPerSubmesh.size()) fastBatchIndex = 0u;
                                    auto& fastBatch = modelIndexedBatchesPerSubmesh[fastBatchIndex];
                                    const bool canReuseIndexedVertices =
                                        fastBatchIndex < modelIndexedVertexRemap.size();
                                    const auto appendFastVertex = [&](std::uint32_t src,
                                                                      const runtime::backend_model::MeshVertex& srcVertex)
                                        -> std::uint32_t {
                                        if (canReuseIndexedVertices &&
                                            src < modelIndexedVertexRemap[fastBatchIndex].size()) {
                                            int& mapped = modelIndexedVertexRemap[fastBatchIndex][src];
                                            if (mapped >= 0) {
                                                return static_cast<std::uint32_t>(mapped);
                                            }
                                            if (fastBatch.vertices.size() >=
                                                static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
                                                return std::numeric_limits<std::uint32_t>::max();
                                            }
                                            const glm::vec3 pos = resolveWorldVertexPos(triNodeIndex, src, srcVertex);
                                            const std::uint32_t next =
                                                static_cast<std::uint32_t>(fastBatch.vertices.size());
                                            fastBatch.vertices.push_back(IRenderBackend::WorldMeshVertex{
                                                pos.x,
                                                pos.y,
                                                pos.z,
                                                srcVertex.uv.x,
                                                srcVertex.uv.y,
                                                fastTexturedTint.r,
                                                fastTexturedTint.g,
                                                fastTexturedTint.b,
                                                fastTexturedAlpha});
                                            mapped = static_cast<int>(next);
                                            return next;
                                        }
                                        if (fastBatch.vertices.size() >=
                                            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
                                            return std::numeric_limits<std::uint32_t>::max();
                                        }
                                        const glm::vec3 pos = resolveWorldVertexPos(triNodeIndex, src, srcVertex);
                                        const std::uint32_t next =
                                            static_cast<std::uint32_t>(fastBatch.vertices.size());
                                        fastBatch.vertices.push_back(IRenderBackend::WorldMeshVertex{
                                            pos.x,
                                            pos.y,
                                            pos.z,
                                            srcVertex.uv.x,
                                            srcVertex.uv.y,
                                            fastTexturedTint.r,
                                            fastTexturedTint.g,
                                            fastTexturedTint.b,
                                            fastTexturedAlpha});
                                        return next;
                                    };

                                    const std::uint32_t outI0 = appendFastVertex(i0, v0);
                                    const std::uint32_t outI1 = appendFastVertex(i1, v1);
                                    const std::uint32_t outI2 = appendFastVertex(i2, v2);
                                    if (outI0 == std::numeric_limits<std::uint32_t>::max() ||
                                        outI1 == std::numeric_limits<std::uint32_t>::max() ||
                                        outI2 == std::numeric_limits<std::uint32_t>::max()) {
                                        continue;
                                    }
                                    fastBatch.indices.push_back(outI0);
                                    fastBatch.indices.push_back(outI1);
                                    fastBatch.indices.push_back(outI2);
                                    continue;
                                }

                                const float triOpacity = (triIdx < mesh->triangleOpacity.size())
                                    ? mesh->triangleOpacity[triIdx]
                                    : 1.0f;
                                // Textured indexed batches apply alpha in the pixel shader.
                                // Avoid pre-multiplying with sampled triangle opacity (which would double-attenuate).
                                const float alphaBase = std::clamp(modelFadeAlpha, 0.0f, 1.0f);
                                const float alpha = texturedSubmesh
                                    ? alphaBase
                                    : alphaBase * std::clamp(triOpacity, 0.0f, 1.0f);
                                if (alpha < 0.03f && !texturedSubmesh) continue;
                                const bool triDoubleSided =
                                    (triIdx < mesh->triangleDoubleSided.size()) &&
                                    (mesh->triangleDoubleSided[triIdx] != 0u);

                                const auto sk0 = resolveWorldVertex(triNodeIndex, i0, v0);
                                const auto sk1 = resolveWorldVertex(triNodeIndex, i1, v1);
                                const auto sk2 = resolveWorldVertex(triNodeIndex, i2, v2);

                                const glm::vec3& a = sk0.pos;
                                const glm::vec3& b = sk1.pos;
                                const glm::vec3& c = sk2.pos;
                                const glm::vec3& n0 = sk0.normal;
                                const glm::vec3& n1 = sk1.normal;
                                const glm::vec3& n2 = sk2.normal;

                                glm::vec3 baseColor0 = fallbackBase;
                                glm::vec3 baseColor1 = fallbackBase;
                                glm::vec3 baseColor2 = fallbackBase;
                                auto resolveVertexBase = [&](std::uint32_t vi,
                                                             const runtime::backend_model::MeshVertex& v) {
                                    if (mesh->hasVertexBaseColor && vi < mesh->vertexBaseColors.size()) {
                                        return glm::clamp(mesh->vertexBaseColors[vi], 0.0f, 1.0f);
                                    }
                                    if (mesh->hasVertexColor) {
                                        return glm::clamp(
                                            glm::vec3(v.color.r, v.color.g, v.color.b), 0.0f, 1.0f);
                                    }
                                    if (triIdx < mesh->triangleBaseColors.size()) {
                                        return glm::clamp(mesh->triangleBaseColors[triIdx], 0.0f, 1.0f);
                                    }
                                    if (triIdx < mesh->triangleSubmesh.size() &&
                                        !mesh->submeshBaseColors.empty()) {
                                        const std::uint16_t submeshIndex = mesh->triangleSubmesh[triIdx];
                                        if (submeshIndex < mesh->submeshBaseColors.size()) {
                                            const glm::vec4 subColor = mesh->submeshBaseColors[submeshIndex];
                                            return glm::clamp(
                                                glm::vec3(subColor.r, subColor.g, subColor.b), 0.0f, 1.0f);
                                        }
                                    }
                                    (void)vi;
                                    return fallbackBase;
                                };
                                baseColor0 = resolveVertexBase(i0, v0);
                                baseColor1 = resolveVertexBase(i1, v1);
                                baseColor2 = resolveVertexBase(i2, v2);
                                if (captureVisualTintStrength > 0.001f) {
                                    const float tintAmt = std::clamp(captureVisualTintStrength, 0.0f, 1.0f);
                                    baseColor0 = glm::mix(baseColor0, captureTintColor, tintAmt);
                                    baseColor1 = glm::mix(baseColor1, captureTintColor, tintAmt);
                                    baseColor2 = glm::mix(baseColor2, captureTintColor, tintAmt);
                                }
                                pushModelTriangle(
                                    a,
                                    b,
                                    c,
                                    i0,
                                    i1,
                                    i2,
                                    v0.uv,
                                    v1.uv,
                                    v2.uv,
                                    n0,
                                    n1,
                                    n2,
                                    baseColor0,
                                    baseColor1,
                                    baseColor2,
                                    triSubmeshIndex,
                                    alpha,
                                    triDoubleSided);
                            }
                            bool queuedIndexedBatch = false;
                            if (useIndexedWorldModelPath && !modelIndexedBatchesPerSubmesh.empty()) {
                                for (auto& batch : modelIndexedBatchesPerSubmesh) {
                                    if (batch.vertices.empty() || batch.indices.empty()) continue;
                                    worldIndexedBatches.push_back(std::move(batch));
                                    queuedIndexedBatch = true;
                                }
                            }

                            drewModelMesh = runtime::backend_units::didAccumulateModelGeometry(
                                modelDepthCountBefore,
                                modelDepthTris.size(),
                                modelDepthWorldCountBefore,
                                modelDepthWorldTris.size()) ||
                                (world3DTriangles.size() > world3DTriangleCountBefore) ||
                                queuedIndexedBatch;
                        }

                        const game::runtime::backend_proxy::UnitProxyCorners corners =
                            game::runtime::backend_proxy::computeUnitProxyCorners(
                                proxyCenter,
                                extents,
                                animYaw);
                        if (!drewModelMesh) {
                            if (supportsWorldTriangles3D) {
                                appendWorldQuad(
                                    corners.top[0],
                                    corners.top[1],
                                    corners.top[2],
                                    corners.top[3],
                                    topR, topG, topB, topAlpha);
                                appendWorldQuad(
                                    corners.bottom[0], corners.bottom[1], corners.top[1], corners.top[0],
                                    sideR, sideG, sideB, sideAlpha);
                                appendWorldQuad(
                                    corners.bottom[1], corners.bottom[2], corners.top[2], corners.top[1],
                                    sideR, sideG, sideB, sideAlpha);
                                appendWorldQuad(
                                    corners.bottom[2], corners.bottom[3], corners.top[3], corners.top[2],
                                    sideR, sideG, sideB, sideAlpha);
                                appendWorldQuad(
                                    corners.bottom[3], corners.bottom[0], corners.top[0], corners.top[3],
                                    sideR, sideG, sideB, sideAlpha);
                            } else {
                                appendProjectedQuad(
                                    corners.top[0],
                                    corners.top[1],
                                    corners.top[2],
                                    corners.top[3],
                                    topR, topG, topB, topAlpha);
                                appendProjectedQuad(
                                    corners.bottom[0], corners.bottom[1], corners.top[1], corners.top[0],
                                    sideR, sideG, sideB, sideAlpha);
                                appendProjectedQuad(
                                    corners.bottom[1], corners.bottom[2], corners.top[2], corners.top[1],
                                    sideR, sideG, sideB, sideAlpha);
                                appendProjectedQuad(
                                    corners.bottom[2], corners.bottom[3], corners.top[3], corners.top[2],
                                    sideR, sideG, sideB, sideAlpha);
                                appendProjectedQuad(
                                    corners.bottom[3], corners.bottom[0], corners.top[0], corners.top[3],
                                    sideR, sideG, sideB, sideAlpha);
                            }
                        }

                        const bool shouldShowPortrait = runtime::backend_units::shouldRenderWorldUnitPortrait(
                            drewModelMesh,
                            forcePortraitOverlay,
                            allowPortraitFallback);
                        if (shouldShowPortrait) {
                            const std::string unitImagePath =
                                runtime::backend_units::resolveWorldUnitImagePath(unit.name);
                            IRenderBackend::DebugSprite unitSprite =
                                runtime::backend_units::makeWorldUnitSprite(
                                    cx,
                                    cy,
                                    unitSize * 1.20f,
                                    unitSize * 1.20f,
                                    unitImagePath,
                                    unit.alive ? 0.96f : 0.76f);
                            if (!unitSprite.texturePath.empty()) {
                                sprites.push_back(std::move(unitSprite));
                            }
                        }

                        std::string routeMoveLower = toLowerCopy(unit.activeAttackMoveName);
                        if (routeMoveLower.empty()) {
                            routeMoveLower = toLowerCopy(unit.pendingDamageMoveName);
                        }
                        const MoveImpactRoute impactRoute = classifyMoveImpactRoute(routeMoveLower);
                        const bool pendingGrowl = (impactRoute == MoveImpactRoute::GrowlSoundRings);
                        const bool pendingClaw = (impactRoute == MoveImpactRoute::ClawSwipe);
                        const bool pendingAqua = (impactRoute == MoveImpactRoute::AquaSwoosh);
                        const bool pendingLeechSeed =
                            routeMoveLower.find("leech") != std::string::npos ||
                            routeMoveLower.find("seed") != std::string::npos;
                        const bool pendingGrass = (impactRoute == MoveImpactRoute::GrassImpact) || pendingLeechSeed;
                        const glm::vec3 forward = game::runtime::backend_proxy::yawForward(animYaw);
                        const glm::vec3 right = game::runtime::backend_proxy::yawRight(animYaw);
                        const glm::vec3 up(0.0f, 1.0f, 0.0f);

                        if (!useLegacyParticleVfxSnapshotBridge) {
                            appendProjectedTailFire(unit, proxyCenter, extents, animYaw, std::max(1.0f, line * 0.92f));
                            appendProjectedLeechDrain(unit, std::max(0.12f, worldCellSize * 0.24f), std::max(1.0f, line));
                        }

                        if (pendingGrowl && unit.attackTimerSec > 0.0f && !useLegacyGrowlWaveVfx) {
                            const float safeAttackDur = std::max(0.001f, unit.attackDurationSec);
                            const float attackProgress =
                                std::clamp(unit.animTimeSec / safeAttackDur, 0.0f, 1.0f);
                            const glm::vec3 growlSource =
                                proxyCenter +
                                up * std::max(0.10f, extents.height * 0.32f) +
                                forward * std::max(0.03f, extents.halfDepth * 0.56f);
                            const float baseRadius = std::max(0.04f, extents.halfWidth * 0.62f);
                            const float ringAlpha = 0.42f + 0.32f * (1.0f - attackProgress);
                            const float ringLine = std::max(1.0f, line * 0.90f);

                            // Legacy growl draw-pass approximation: layered source rings.
                            appendProjectedRing(
                                growlSource + forward * std::max(0.01f, worldCellSize * 0.05f),
                                baseRadius * (0.80f + attackProgress * 0.65f),
                                1.00f, 0.60f, 1.00f, ringAlpha * 0.95f, ringLine, 14);
                            appendProjectedRing(
                                growlSource + forward * std::max(0.03f, worldCellSize * 0.14f),
                                baseRadius * (1.00f + attackProgress * 0.78f),
                                1.00f, 0.70f, 0.82f, ringAlpha * 0.90f, ringLine, 16);
                            appendProjectedRing(
                                growlSource + forward * std::max(0.05f, worldCellSize * 0.24f),
                                baseRadius * (1.24f + attackProgress * 0.92f),
                                0.96f, 0.56f, 0.96f, ringAlpha * 0.82f, ringLine, 16);

                            // Legacy line-pass approximation: radial fan from source.
                            constexpr int kGrowlSpokes = 16;
                            for (int s = 0; s < kGrowlSpokes; ++s) {
                                const float t =
                                    (static_cast<float>(s) / static_cast<float>(kGrowlSpokes)) * 6.2831853f;
                                const float ripple = std::sin(attackProgress * 14.0f + static_cast<float>(s) * 0.65f);
                                const glm::vec3 dir = glm::normalize(
                                    right * (std::cos(t) * 1.10f) +
                                    up * (std::sin(t) * 1.10f) +
                                    forward * (1.00f + ripple * 0.12f));
                                appendProjectedLine(
                                    growlSource,
                                    growlSource + dir * (baseRadius * (1.65f + attackProgress * 0.90f)),
                                    1.00f,
                                    0.64f,
                                    0.88f,
                                    ringAlpha * (0.55f + 0.30f * (0.5f + 0.5f * ripple)),
                                    std::max(1.0f, line * 0.76f));
                            }
                        }

                        if (!useLegacyParticleVfxSnapshotBridge &&
                            unit.pendingProjectileActive && unit.pendingProjectileTargetId >= 0) {
                            if (const PokemonInstance* target = gameWorld->findUnitById(unit.pendingProjectileTargetId)) {
                                const glm::vec3 from =
                                    proxyCenter + glm::vec3(0.0f, std::max(0.10f, extents.height * 0.42f), 0.0f);
                                const glm::vec3 to =
                                    target->position +
                                    glm::vec3(0.0f, std::max(0.12f, worldCellSize * 0.24f) + target->visualYOffset, 0.0f);
                                const float spawnT = std::max(0.0f, unit.pendingProjectileSpawnTimeSec);
                                const float travelSec = std::max(0.001f, unit.pendingProjectileTravelSec);
                                float travelT = 0.0f;
                                if (unit.animTimeSec >= spawnT) {
                                    travelT = std::clamp((unit.animTimeSec - spawnT) / travelSec, 0.0f, 1.0f);
                                }
                                if (unit.pendingProjectileSpawned && travelT <= 0.0f) {
                                    travelT = 1.0f;
                                }
                                if (travelT > 0.0f) {
                                    const float prevT = std::clamp(travelT - 0.07f, 0.0f, 1.0f);
                                    const glm::vec3 curPos =
                                        glm::mix(from, to, travelT) + glm::vec3(0.0f, std::sin(travelT * 3.1415926f) * 0.08f, 0.0f);
                                    const glm::vec3 prevPos =
                                        glm::mix(from, to, prevT) + glm::vec3(0.0f, std::sin(prevT * 3.1415926f) * 0.08f, 0.0f);
                                    appendProjectedLine(
                                        prevPos,
                                        curPos,
                                        0.38f,
                                        0.92f,
                                        0.34f,
                                        0.95f,
                                        std::max(1.0f, line * 1.20f));
                                    appendProjectedBurst(
                                        curPos,
                                        forward,
                                        std::max(0.02f, worldCellSize * 0.05f),
                                        0.52f,
                                        0.98f,
                                        0.40f,
                                        0.85f,
                                        std::max(1.0f, line * 0.9f),
                                        5);
                                }
                            }
                        }

                        const bool pendingImpactBurst =
                            (unit.pendingImpactActive && !unit.pendingImpactApplied) ||
                            (unit.pendingDamageActive && !unit.pendingDamageApplied);
                        const bool allowProjectedImpactFallback =
                            !useLegacyParticleVfxSnapshotBridge || (pendingGrowl && !useLegacyGrowlWaveVfx);
                        if (allowProjectedImpactFallback && pendingImpactBurst) {
                            const int impactTargetId =
                                unit.pendingImpactActive ? unit.pendingImpactTargetId : unit.pendingDamageTargetId;
                            const float impactTimeSec =
                                unit.pendingImpactActive ? unit.pendingImpactTimeSec : unit.pendingDamageHitTimeSec;
                            if (impactTargetId >= 0 && impactTimeSec >= 0.0f) {
                                if (const PokemonInstance* target = gameWorld->findUnitById(impactTargetId)) {
                                    const float distToHit = std::abs(unit.animTimeSec - impactTimeSec);
                                    if (distToHit <= 0.16f || (unit.pendingDamageApplied || unit.pendingImpactApplied)) {
                                        const float burst = std::clamp(1.0f - (distToHit / 0.16f), 0.0f, 1.0f);
                                        const float radius =
                                            std::max(0.03f, worldCellSize * (0.11f + (1.0f - burst) * 0.11f));
                                        const glm::vec3 center =
                                            target->position +
                                            glm::vec3(0.0f, std::max(0.12f, worldCellSize * 0.24f) + target->visualYOffset, 0.0f);
                                        const float ia = 0.44f + burst * 0.42f;
                                        if (pendingGrowl) {
                                            if (!useLegacyGrowlWaveVfx) {
                                                const glm::vec3 growlSource =
                                                    proxyCenter +
                                                    up * std::max(0.10f, extents.height * 0.32f) +
                                                    forward * std::max(0.03f, extents.halfDepth * 0.56f);
                                                appendProjectedRing(
                                                    growlSource,
                                                    radius * 0.98f,
                                                    1.00f,
                                                    0.60f,
                                                    1.00f,
                                                    ia * 0.95f,
                                                    std::max(1.0f, line * 0.88f),
                                                    12);
                                                appendProjectedRing(
                                                    growlSource + forward * std::max(0.03f, worldCellSize * 0.14f),
                                                    radius * 1.26f,
                                                    1.00f,
                                                    0.70f,
                                                    0.82f,
                                                    ia * 0.86f,
                                                    std::max(1.0f, line * 0.92f),
                                                    14);
                                                appendProjectedRing(
                                                    growlSource + forward * std::max(0.05f, worldCellSize * 0.24f),
                                                    radius * 1.52f,
                                                    0.96f,
                                                    0.56f,
                                                    0.96f,
                                                    ia * 0.74f,
                                                    std::max(1.0f, line * 0.86f),
                                                    14);
                                            }
                                        } else if (pendingClaw) {
                                            appendProjectedBurst(
                                                center,
                                                forward,
                                                radius * 1.10f,
                                                0.95f,
                                                (routeMoveLower == "metal_claw") ? 0.95f : 0.82f,
                                                (routeMoveLower == "metal_claw") ? 0.99f : 0.82f,
                                                ia,
                                                std::max(1.0f, line),
                                                6);
                                        } else if (pendingAqua) {
                                            appendProjectedRing(
                                                center,
                                                radius * 1.30f,
                                                0.36f,
                                                0.78f,
                                                1.00f,
                                                ia,
                                                std::max(1.0f, line * 0.95f),
                                                14);
                                            appendProjectedBurst(
                                                center,
                                                up,
                                                radius * 0.75f,
                                                0.52f,
                                                0.90f,
                                                1.00f,
                                                ia * 0.86f,
                                                std::max(1.0f, line * 0.9f),
                                                7);
                                        } else if (pendingGrass || unit.pendingImpactIsGrass || unit.pendingImpactIsLeechSeed) {
                                            appendProjectedRing(
                                                center,
                                                radius * 1.18f,
                                                0.42f,
                                                0.92f,
                                                0.34f,
                                                ia,
                                                std::max(1.0f, line * 0.95f),
                                                13);
                                            appendProjectedBurst(
                                                center,
                                                forward,
                                                radius * 0.85f,
                                                0.42f,
                                                0.96f,
                                                0.36f,
                                                ia * 0.92f,
                                                std::max(1.0f, line * 0.95f),
                                                8);
                                        } else {
                                            appendProjectedRing(
                                                center,
                                                radius * 1.15f,
                                                1.00f,
                                                0.76f,
                                                0.28f,
                                                ia,
                                                std::max(1.0f, line * 0.95f),
                                                12);
                                            appendProjectedBurst(
                                                center,
                                                forward,
                                                radius * 0.95f,
                                                0.98f,
                                                0.72f,
                                                0.26f,
                                                ia * 0.9f,
                                                std::max(1.0f, line),
                                                8);
                                        }
                                    }
                                }
                            }
                        }

                        const float hudCellPxBase = std::clamp(minDim * 0.070f, 38.0f, 58.0f);
                        const float hudCellPx =
                            std::clamp(cellPx, hudCellPxBase * 0.90f, hudCellPxBase * 1.10f);
                        if (unit.alive) {
                            appendLegacyUnitHud(unit, cx, cy, hudCellPx);
                        }
                    }
                };

                (void)sharedCaptureAttemptCache.refresh(gameWorld.get());
                drawProjectedUnits(gameWorld->getPokemons());
                drawProjectedUnits(gameWorld->getBenchPokemons());
                const bool useOpenGlDirectCaptureModel =
                    (renderer && renderer->backendId() && toLowerCopy(renderer->backendId()) == "opengl");
                if (!useOpenGlDirectCaptureModel) {
                    (void)appendSharedCaptureAttemptModels();
                }
                appendSharedParticleVfx();
                appendSharedGrowlWaveVfx();
                if (!modelDepthWorldTris.empty()) {
                    std::sort(
                        modelDepthWorldTris.begin(),
                        modelDepthWorldTris.end(),
                        [](const DepthWorldTri& lhs, const DepthWorldTri& rhs) {
                            return lhs.depth > rhs.depth;
                        });
                    world3DTriangles.reserve(world3DTriangles.size() + modelDepthWorldTris.size());
                    for (const DepthWorldTri& tri : modelDepthWorldTris) {
                        world3DTriangles.push_back(tri.tri);
                    }
                }
                if (!modelDepthTris.empty()) {
                    std::sort(
                        modelDepthTris.begin(),
                        modelDepthTris.end(),
                        [](const DepthTri& lhs, const DepthTri& rhs) {
                            return lhs.depth > rhs.depth;
                        });
                    worldTriangles.reserve(worldTriangles.size() + modelDepthTris.size());
                    for (const DepthTri& tri : modelDepthTris) {
                        worldTriangles.push_back(tri.tri);
                    }
                }
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
                        appendLegacyUnitHud(unit, centerX, centerY, hudCellPx);
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

        if (showPerfOverlay && engineServices) {
            const EngineFramePerfStats& perf = engineServices->framePerf;
            if (perf.fps > 0.0f) {
                const float fpsNorm = std::clamp(perf.fps / 120.0f, 0.0f, 1.0f);
                IRenderBackend::DebugQuad fpsBarBg;
                fpsBarBg.x = edgePad;
                fpsBarBg.y = std::max(8.0f, edgePad - lineStep * 0.2f);
                fpsBarBg.w = std::clamp(220.0f * uiScale, 140.0f, 320.0f);
                fpsBarBg.h = std::clamp(10.0f * uiScale, 8.0f, 16.0f);
                fpsBarBg.r = 0.15f;
                fpsBarBg.g = 0.15f;
                fpsBarBg.b = 0.18f;
                fpsBarBg.a = 1.0f;
                overlayQuads.push_back(fpsBarBg);

                IRenderBackend::DebugQuad fpsBar = fpsBarBg;
                fpsBar.w *= fpsNorm;
                fpsBar.r = (fpsNorm < 0.5f) ? 0.85f : 0.30f;
                fpsBar.g = (fpsNorm < 0.5f) ? 0.28f : 0.88f;
                fpsBar.b = 0.30f;
                overlayQuads.push_back(fpsBar);
            }
        }

        const auto appendText = [&](float x,
                                    float y,
                                    const std::string& text,
                                    float scale,
                                    const glm::vec3& color) {
            runtime::backend_text::appendTextLines(
                textLines, x, y, text, scale, color.r, color.g, color.b, 1.0f, 0.88f);
        };
        const auto appendRightText = [&](float y,
                                         const std::string& text,
                                         float scale,
                                         const glm::vec3& color) {
            const float textW = std::max(1.0f, runtime::backend_text::measureTextWidth(text, scale));
            const float x = std::max(edgePad, static_cast<float>(drawableW) - textW - edgePad);
            appendText(x, y, text, scale, color);
        };

        const std::string mode = (services ? services->gameMode : std::string("classic"));
        appendRightText(edgePad + lineStep * 1.1f,
                        runtime::backend_status_text::modeLine(mode),
                        std::clamp(1.2f * uiScale, 0.95f, 1.7f),
                        glm::vec3(0.93f, 0.95f, 0.99f));
        if (services) {
            appendRightText(edgePad + lineStep * 2.2f,
                            runtime::backend_status_text::backendLine(
                                services->activeRendererBackend,
                                services->gpuRenderer),
                            std::clamp(1.0f * uiScale, 0.80f, 1.35f),
                            glm::vec3(0.68f, 0.80f, 0.94f));
        }

        RoundPhase roundPhase = RoundPhase::Planning;
        bool combatActive = false;
        if (ecsWorld.alive(roundPhaseEntity)) {
            if (const auto* roundState = ecsWorld.get<game::RoundState>(roundPhaseEntity)) {
                roundPhase = roundState->phase;
            }
            if (const auto* combatState = ecsWorld.get<game::CombatActive>(roundPhaseEntity)) {
                combatActive = combatState->active;
            }
        }
        appendRightText(edgePad + lineStep * 3.3f,
                        runtime::backend_status_text::roundLine(roundPhase, combatActive),
                        std::clamp(1.0f * uiScale, 0.80f, 1.35f),
                        glm::vec3(0.83f, 0.91f, 0.98f));

        int playerAlive = 0;
        int enemyAlive = 0;
        if (gameWorld) {
            for (const auto& unit : gameWorld->getPokemons()) {
                if (!unit.alive && !unit.captureInProgress) continue;
                if (unit.side == PokemonSide::Player) ++playerAlive;
                else ++enemyAlive;
            }
            appendRightText(edgePad + lineStep * 4.4f,
                            runtime::backend_status_text::unitsLine(playerAlive, enemyAlive),
                            std::clamp(1.0f * uiScale, 0.80f, 1.35f),
                            glm::vec3(0.72f, 0.90f, 0.84f));
            appendRightText(edgePad + lineStep * 5.5f,
                            runtime::backend_status_text::goldLine(gameWorld->getMoney()),
                            std::clamp(1.0f * uiScale, 0.80f, 1.35f),
                            glm::vec3(0.96f, 0.88f, 0.56f));
            const std::string selectedItem = gameWorld->getSelectedItem();
            if (!selectedItem.empty()) {
                appendRightText(edgePad + lineStep * 6.6f,
                                runtime::backend_status_text::selectedItemLine(selectedItem),
                                std::clamp(1.0f * uiScale, 0.80f, 1.35f),
                                glm::vec3(0.84f, 0.90f, 0.98f));
            }

            refreshBackendInventoryFromWorld();
            const auto& inventoryModel = backendInventoryPanel.model;
            const float leftX = edgePad;
            const float invStartY = edgePad + lineStep * 7.7f;
            const bool adventureModeInventoryIcons = services && services->gameMode == "adventure";

            if (inventoryModel.totalCount > 0 || !selectedItem.empty()) {
                if (adventureModeInventoryIcons) {
                    const float panelScale = std::clamp(uiScale, 0.85f, 1.30f);
                    const float cardW = std::round(std::clamp(72.0f * panelScale, 60.0f, 90.0f));
                    const float cardH = cardW;
                    const float countScale = std::clamp(0.86f * panelScale, 0.72f, 1.05f);
                    const float titleScale = std::clamp(1.00f * panelScale, 0.82f, 1.20f);
                    const float labelScale = std::clamp(0.78f * panelScale, 0.68f, 0.95f);
                    const float navScale = std::clamp(0.80f * panelScale, 0.68f, 0.95f);
                    const float titleH =
                        std::max(12.0f, runtime::backend_text::measureTextHeight("Items", titleScale));
                    const float countH =
                        std::max(10.0f, runtime::backend_text::measureTextHeight("x99", countScale));
                    const float nameH =
                        std::max(10.0f, runtime::backend_text::measureTextHeight("Pokeball", labelScale));
                    const float rowPitch = cardH + std::max(6.0f, countH + 4.0f) + std::max(8.0f, nameH + 8.0f);
                    const float rightInset = std::round(std::max(edgePad, 24.0f * panelScale));
                    const float panelX = std::round(static_cast<float>(drawableW) - rightInset - cardW);
                    const float panelTop = std::round(std::max(
                        invStartY,
                        std::max(110.0f, static_cast<float>(drawableH) * 0.16f)));

                    const std::string title = runtime::backend_inventory::makeTitleLabel(inventoryModel);
                    const float titleW = std::max(1.0f, runtime::backend_text::measureTextWidth(title, titleScale));
                    appendText(std::max(edgePad, panelX + cardW - titleW),
                               panelTop,
                               title,
                               titleScale,
                               glm::vec3(0.92f, 0.95f, 0.99f));

                    float navY = panelTop + titleH + std::max(4.0f, lineStep * 0.20f);
                    const bool hasPrev = runtime::backend_inventory::canScrollPrev(inventoryModel);
                    const bool hasNext = runtime::backend_inventory::canScrollNext(inventoryModel);
                    if (hasPrev || hasNext) {
                        const std::string prevLabel = "[Up] Prev";
                        const std::string nextLabel = "[Down] Next";
                        appendText(panelX,
                                   navY,
                                   prevLabel,
                                   navScale,
                                   hasPrev ? glm::vec3(0.75f, 0.87f, 0.96f)
                                           : glm::vec3(0.42f, 0.48f, 0.55f));
                        if (hasPrev) {
                            runtime::backend_inventory_panel::HitRegion prevHit;
                            prevHit.action = runtime::backend_inventory_panel::HitAction::ScrollOffset;
                            prevHit.offsetDelta = -1;
                            prevHit.x = panelX;
                            prevHit.y = navY;
                            prevHit.w = std::max(1.0f, runtime::backend_text::measureTextWidth(prevLabel, navScale));
                            prevHit.h = std::max(1.0f, runtime::backend_text::measureTextHeight(prevLabel, navScale));
                            backendInventoryPanel.hitRegions.push_back(std::move(prevHit));
                        }

                        const float nextW =
                            std::max(1.0f, runtime::backend_text::measureTextWidth(nextLabel, navScale));
                        const float nextX = panelX + cardW - nextW;
                        appendText(nextX,
                                   navY,
                                   nextLabel,
                                   navScale,
                                   hasNext ? glm::vec3(0.75f, 0.87f, 0.96f)
                                           : glm::vec3(0.42f, 0.48f, 0.55f));
                        if (hasNext) {
                            runtime::backend_inventory_panel::HitRegion nextHit;
                            nextHit.action = runtime::backend_inventory_panel::HitAction::ScrollOffset;
                            nextHit.offsetDelta = 1;
                            nextHit.x = nextX;
                            nextHit.y = navY;
                            nextHit.w = nextW;
                            nextHit.h = std::max(1.0f, runtime::backend_text::measureTextHeight(nextLabel, navScale));
                            backendInventoryPanel.hitRegions.push_back(std::move(nextHit));
                        }
                        navY += std::max(12.0f, runtime::backend_text::measureTextHeight(prevLabel, navScale)) +
                                std::max(6.0f, lineStep * 0.12f);
                    }

                    for (std::size_t i = 0; i < inventoryModel.visibleEntries.size(); ++i) {
                        const auto& entry = inventoryModel.visibleEntries[i];
                        const bool selected = (entry.id == selectedItem);
                        const float y = navY + static_cast<float>(i) * rowPitch;
                        const float cardX = panelX;
                        const float cardY = y;

                        IRenderBackend::DebugQuad shadow;
                        shadow.x = cardX + 2.0f;
                        shadow.y = cardY + 2.0f;
                        shadow.w = cardW;
                        shadow.h = cardH;
                        shadow.r = 0.02f;
                        shadow.g = 0.03f;
                        shadow.b = 0.05f;
                        shadow.a = 0.50f;
                        worldQuads.push_back(shadow);

                        IRenderBackend::DebugQuad cardBg;
                        cardBg.x = cardX;
                        cardBg.y = cardY;
                        cardBg.w = cardW;
                        cardBg.h = cardH;
                        cardBg.r = selected ? 0.18f : 0.10f;
                        cardBg.g = selected ? 0.18f : 0.11f;
                        cardBg.b = selected ? 0.16f : 0.13f;
                        cardBg.a = 0.95f;
                        worldQuads.push_back(cardBg);

                        const float border = std::clamp(cardW * 0.045f, 2.0f, 4.0f);
                        const auto addBorderQuad = [&](float x, float y, float w, float h, const glm::vec4& c) {
                            IRenderBackend::DebugQuad q;
                            q.x = x;
                            q.y = y;
                            q.w = w;
                            q.h = h;
                            q.r = c.r;
                            q.g = c.g;
                            q.b = c.b;
                            q.a = c.a;
                            worldQuads.push_back(q);
                        };
                        const glm::vec4 borderColor = selected
                            ? glm::vec4(0.95f, 0.78f, 0.33f, 0.98f)
                            : glm::vec4(0.58f, 0.66f, 0.78f, 0.92f);
                        addBorderQuad(cardX, cardY, cardW, border, borderColor);
                        addBorderQuad(cardX, cardY + cardH - border, cardW, border, borderColor);
                        addBorderQuad(cardX, cardY + border, border, std::max(0.0f, cardH - border * 2.0f), borderColor);
                        addBorderQuad(cardX + cardW - border, cardY + border, border, std::max(0.0f, cardH - border * 2.0f), borderColor);

                        const BackendItemAtlasIcon* itemIcon = findBackendItemAtlasIcon(entry.id);
                        if (itemIcon) {
                            const glm::vec2 uvMin = backendItemAtlasUvMin(itemIcon->row, itemIcon->col);
                            const glm::vec2 uvMax = backendItemAtlasUvMax(itemIcon->row, itemIcon->col);
                            IRenderBackend::DebugSprite sprite;
                            const float pad = std::clamp(cardW * 0.10f, 6.0f, 10.0f);
                            sprite.x = cardX + pad;
                            sprite.y = cardY + pad;
                            sprite.w = cardW - pad * 2.0f;
                            sprite.h = cardH - pad * 2.0f;
                            sprite.u0 = uvMin.x;
                            sprite.v0 = uvMin.y;
                            sprite.u1 = uvMax.x;
                            sprite.v1 = uvMax.y;
                            sprite.r = 1.0f;
                            sprite.g = 1.0f;
                            sprite.b = 1.0f;
                            sprite.a = selected ? 1.0f : 0.96f;
                            sprite.texturePath = "assets/images/items_atlas.png";
                            sprites.push_back(std::move(sprite));
                        } else {
                            appendText(cardX + 6.0f,
                                       cardY + cardH * 0.36f,
                                       runtime::hud::humanizeToken(entry.id),
                                       labelScale,
                                       glm::vec3(0.86f, 0.90f, 0.96f));
                        }

                        const std::string countText = "x" + std::to_string(std::max(0, entry.count));
                        const float countW =
                            std::max(1.0f, runtime::backend_text::measureTextWidth(countText, countScale));
                        appendText(cardX + std::max(0.0f, (cardW - countW) * 0.5f),
                                   cardY + cardH + 2.0f,
                                   countText,
                                   countScale,
                                   selected ? glm::vec3(0.99f, 0.90f, 0.56f)
                                            : glm::vec3(0.90f, 0.94f, 0.99f));

                        const std::string nameText = runtime::hud::humanizeToken(entry.id);
                        const float nameW =
                            std::max(1.0f, runtime::backend_text::measureTextWidth(nameText, labelScale));
                        appendText(cardX + std::max(0.0f, (cardW - nameW) * 0.5f),
                                   cardY + cardH + 2.0f + countH + 2.0f,
                                   nameText,
                                   labelScale,
                                   glm::vec3(0.76f, 0.84f, 0.92f));

                        runtime::backend_inventory_panel::HitRegion hit;
                        hit.action = runtime::backend_inventory_panel::HitAction::SelectItem;
                        hit.itemId = entry.id;
                        hit.x = cardX;
                        hit.y = cardY;
                        hit.w = cardW;
                        hit.h = rowPitch - std::max(2.0f, lineStep * 0.08f);
                        backendInventoryPanel.hitRegions.push_back(std::move(hit));
                    }

                    const float footerY = navY + static_cast<float>(inventoryModel.visibleEntries.size()) * rowPitch;
                    const std::string clearLine = runtime::backend_inventory::clearSelectionLabel();
                    appendText(panelX,
                               footerY + 1.0f,
                               clearLine,
                               0.90f,
                               selectedItem.empty()
                                   ? glm::vec3(0.62f, 0.68f, 0.76f)
                                   : glm::vec3(0.95f, 0.78f, 0.66f));
                    runtime::backend_inventory_panel::HitRegion clearHit;
                    clearHit.action = runtime::backend_inventory_panel::HitAction::ClearSelection;
                    clearHit.itemId.clear();
                    clearHit.x = panelX;
                    clearHit.y = footerY + 1.0f;
                    clearHit.w = std::max(1.0f, runtime::backend_text::measureTextWidth(clearLine, 0.90f));
                    clearHit.h = std::max(1.0f, runtime::backend_text::measureTextHeight(clearLine, 0.90f));
                    backendInventoryPanel.hitRegions.push_back(std::move(clearHit));

                    appendText(panelX,
                               footerY + lineStep,
                               "[1-9] select   Wheel/Arrows page",
                               0.80f,
                               glm::vec3(0.66f, 0.76f, 0.90f));
                } else {
                float invY = invStartY;
                appendText(leftX,
                           invY,
                           runtime::backend_inventory::makeTitleLabel(inventoryModel),
                           std::clamp(1.0f * uiScale, 0.80f, 1.30f),
                           glm::vec3(0.92f, 0.95f, 0.99f));
                invY += lineStep;

                const bool hasPrev = runtime::backend_inventory::canScrollPrev(inventoryModel);
                const bool hasNext = runtime::backend_inventory::canScrollNext(inventoryModel);
                if (hasPrev || hasNext) {
                    constexpr float kNavScale = 0.84f;
                    const std::string prevLabel = runtime::backend_inventory::prevPageLabel();
                    const std::string nextLabel = runtime::backend_inventory::nextPageLabel();
                    appendText(leftX,
                               invY,
                               prevLabel,
                               kNavScale,
                               hasPrev ? glm::vec3(0.75f, 0.87f, 0.96f)
                                       : glm::vec3(0.42f, 0.48f, 0.55f));
                    if (hasPrev) {
                        runtime::backend_inventory_panel::HitRegion prevHit;
                        prevHit.action = runtime::backend_inventory_panel::HitAction::ScrollOffset;
                        prevHit.offsetDelta = -1;
                        prevHit.x = leftX;
                        prevHit.y = invY;
                        prevHit.w = std::max(1.0f, runtime::backend_text::measureTextWidth(prevLabel, kNavScale));
                        prevHit.h = std::max(1.0f, runtime::backend_text::measureTextHeight(prevLabel, kNavScale));
                        backendInventoryPanel.hitRegions.push_back(std::move(prevHit));
                    }

                    const float nextX = leftX + std::max(1.0f, runtime::backend_text::measureTextWidth(prevLabel, kNavScale)) + std::max(10.0f, lineStep * 0.75f);
                    appendText(nextX,
                               invY,
                               nextLabel,
                               kNavScale,
                               hasNext ? glm::vec3(0.75f, 0.87f, 0.96f)
                                       : glm::vec3(0.42f, 0.48f, 0.55f));
                    if (hasNext) {
                        runtime::backend_inventory_panel::HitRegion nextHit;
                        nextHit.action = runtime::backend_inventory_panel::HitAction::ScrollOffset;
                        nextHit.offsetDelta = 1;
                        nextHit.x = nextX;
                        nextHit.y = invY;
                        nextHit.w = std::max(1.0f, runtime::backend_text::measureTextWidth(nextLabel, kNavScale));
                        nextHit.h = std::max(1.0f, runtime::backend_text::measureTextHeight(nextLabel, kNavScale));
                        backendInventoryPanel.hitRegions.push_back(std::move(nextHit));
                    }
                    invY += lineStep * 0.88f;
                }

                for (const auto& row : inventoryModel.rows) {
                    constexpr float kItemScale = 0.95f;
                    appendText(leftX,
                               invY,
                               row.line,
                               kItemScale,
                               row.selected
                                   ? glm::vec3(0.98f, 0.90f, 0.58f)
                                   : glm::vec3(0.84f, 0.90f, 0.97f));
                    runtime::backend_inventory_panel::HitRegion hit;
                    hit.action = runtime::backend_inventory_panel::HitAction::SelectItem;
                    hit.itemId = row.itemId;
                    hit.x = leftX;
                    hit.y = invY;
                    hit.w = std::max(1.0f, runtime::backend_text::measureTextWidth(row.line, kItemScale));
                    hit.h = std::max(1.0f, runtime::backend_text::measureTextHeight(row.line, kItemScale));
                    backendInventoryPanel.hitRegions.push_back(std::move(hit));
                    invY += lineStep * 0.93f;
                }

                const std::string clearLine = runtime::backend_inventory::clearSelectionLabel();
                appendText(leftX,
                           invY + 1.0f,
                           clearLine,
                           0.90f,
                           selectedItem.empty()
                               ? glm::vec3(0.62f, 0.68f, 0.76f)
                               : glm::vec3(0.95f, 0.78f, 0.66f));
                runtime::backend_inventory_panel::HitRegion clearHit;
                clearHit.action = runtime::backend_inventory_panel::HitAction::ClearSelection;
                clearHit.itemId.clear();
                clearHit.x = leftX;
                clearHit.y = invY + 1.0f;
                clearHit.w = std::max(1.0f, runtime::backend_text::measureTextWidth(clearLine, 0.90f));
                clearHit.h = std::max(1.0f, runtime::backend_text::measureTextHeight(clearLine, 0.90f));
                backendInventoryPanel.hitRegions.push_back(std::move(clearHit));
                invY += lineStep;
                appendText(leftX,
                           invY + 2.0f,
                           runtime::backend_inventory::hintLabel(),
                           0.82f,
                           glm::vec3(0.66f, 0.76f, 0.90f));
                }
            }

            auto typeCounts = gameWorld->getPlayerTypeLineCounts();
            if (!typeCounts.empty()) {
                std::sort(typeCounts.begin(), typeCounts.end(),
                          [](const GameWorld::TypeLineCount& a, const GameWorld::TypeLineCount& b) {
                              if (a.uniqueLineCount != b.uniqueLineCount) {
                                  return a.uniqueLineCount > b.uniqueLineCount;
                              }
                              return a.type < b.type;
                          });

                float typeY = edgePad + lineStep * 6.6f;
                appendText(edgePad, typeY, "Type Lines", std::clamp(1.0f * uiScale, 0.80f, 1.30f), glm::vec3(0.98f, 0.90f, 0.60f));
                typeY += lineStep;
                const std::size_t maxRows = std::min<std::size_t>(6, typeCounts.size());
                for (std::size_t i = 0; i < maxRows; ++i) {
                    appendText(edgePad,
                               typeY,
                               runtime::hud::formatTypeLineEntry(typeCounts[i].type, typeCounts[i].uniqueLineCount),
                               0.95f,
                               glm::vec3(0.92f, 0.94f, 0.98f));
                    typeY += lineStep * 0.93f;
                }
            }

            const auto& benchUnits = gameWorld->getBenchPokemons();
            if (!benchUnits.empty()) {
                float benchY = edgePad + lineStep * 13.6f;
                appendText(edgePad, benchY, "Bench", std::clamp(1.0f * uiScale, 0.80f, 1.30f), glm::vec3(0.86f, 0.94f, 0.98f));
                benchY += lineStep;
                const std::size_t maxRows = std::min<std::size_t>(5, benchUnits.size());
                for (std::size_t i = 0; i < maxRows; ++i) {
                    appendText(edgePad,
                               benchY,
                               runtime::hud::formatUnitEntry(benchUnits[i].name, benchUnits[i].level),
                               0.95f,
                               glm::vec3(0.80f, 0.88f, 0.96f));
                    benchY += lineStep * 0.93f;
                }
            }

            const auto& shopCards = gameWorld->getClassicShopCards();
            if (!shopCards.empty()) {
                float shopY = edgePad + lineStep * 13.6f;
                appendRightText(shopY, "Shop Offers", std::clamp(1.0f * uiScale, 0.80f, 1.30f), glm::vec3(0.98f, 0.90f, 0.60f));
                shopY += lineStep;
                const std::size_t maxRows = std::min<std::size_t>(5, shopCards.size());
                for (std::size_t i = 0; i < maxRows; ++i) {
                    appendRightText(shopY,
                                    runtime::hud::formatShopCardEntry(shopCards[i].name,
                                                                      shopCards[i].level,
                                                                      shopCards[i].cost),
                                    0.95f,
                                    glm::vec3(0.92f, 0.94f, 0.98f));
                    shopY += lineStep * 0.93f;
                }
            }
        }

        const auto recentMain = log.recentMainLines(7);
        if (!recentMain.empty()) {
            float y = std::max(edgePad + lineStep * 7.0f, static_cast<float>(drawableH) - lineStep * 11.0f);
            for (const auto& line : recentMain) {
                const std::string text = trimDebugLine(line.text, 84);
                const float scale = 1.0f;
                const float textW = std::max(1.0f, runtime::backend_text::measureTextWidth(text, scale));
                const float x = std::max(edgePad, static_cast<float>(drawableW) - textW - edgePad);
                appendText(x,
                           y,
                           text,
                           scale,
                           glm::vec3(
                               std::clamp(line.color.r, 0.0f, 1.0f),
                               std::clamp(line.color.g, 0.0f, 1.0f),
                               std::clamp(line.color.b, 0.0f, 1.0f)));
                y += lineStep;
            }
        }

        const bool classicMode = (mode == "classic");
        const auto sideLines = classicMode ? log.recentEconomyLines(5) : log.recentCatchLines(5);
        if (!sideLines.empty()) {
            float y = std::max(edgePad + lineStep * 7.0f, static_cast<float>(drawableH) - lineStep * 11.0f);
            for (const auto& line : sideLines) {
                const std::string text = trimDebugLine(line.text, 54);
                const float scale = 1.0f;
                appendText(edgePad,
                           y,
                           text,
                           scale,
                           glm::vec3(
                               std::clamp(line.color.r, 0.0f, 1.0f),
                               std::clamp(line.color.g, 0.0f, 1.0f),
                               std::clamp(line.color.b, 0.0f, 1.0f)));
                y += lineStep;
            }
        }

        if (!worldBackgroundQuads.empty()) {
            renderer->drawDebugQuads(worldBackgroundQuads.data(), worldBackgroundQuads.size(), drawableW, drawableH);
        }
        if (!world3DTriangles.empty() && hasWorldViewProj && supportsWorldTriangles3D) {
            renderer->drawWorldTriangles(
                world3DTriangles.data(),
                world3DTriangles.size(),
                worldViewProj,
                drawableW,
                drawableH);
        }
        if (!worldIndexedBatches.empty() && hasWorldViewProj && supportsWorldIndexedMeshes) {
            const auto drawIndexedBatch = [&](const WorldIndexedBatch& batch) {
                const unsigned char* rgbaData = batch.textureRgba;
                if (!rgbaData && !batch.ownedTextureRgba.empty()) {
                    rgbaData = batch.ownedTextureRgba.data();
                }
                IRenderBackend::WorldTextureData tex;
                tex.key = batch.textureKey.c_str();
                tex.rgba = rgbaData;
                tex.width = batch.textureWidth;
                tex.height = batch.textureHeight;
                tex.wrapS = batch.textureWrapS;
                tex.wrapT = batch.textureWrapT;
                tex.alphaMode = batch.alphaMode;
                tex.blendMode = batch.blendMode;
                tex.materialMode = batch.materialMode;
                tex.alphaCutoff = batch.alphaCutoff;
                tex.materialTimeSec = batch.materialTimeSec;
                tex.materialFlags = batch.materialFlags;
                tex.materialAtlasWidth = batch.materialAtlasWidth;
                tex.materialAtlasHeight = batch.materialAtlasHeight;
                tex.materialRect0U = batch.materialRect0U;
                tex.materialRect0V = batch.materialRect0V;
                tex.materialRect0W = batch.materialRect0W;
                tex.materialRect0H = batch.materialRect0H;
                tex.materialRect1U = batch.materialRect1U;
                tex.materialRect1V = batch.materialRect1V;
                tex.materialRect1W = batch.materialRect1W;
                tex.materialRect1H = batch.materialRect1H;
                tex.materialFlipbook0Cols = batch.materialFlipbook0Cols;
                tex.materialFlipbook0Rows = batch.materialFlipbook0Rows;
                tex.materialFlipbook0Frames = batch.materialFlipbook0Frames;
                tex.materialFlipbook0Fps = batch.materialFlipbook0Fps;
                tex.materialFlipbook1Cols = batch.materialFlipbook1Cols;
                tex.materialFlipbook1Rows = batch.materialFlipbook1Rows;
                tex.materialFlipbook1Frames = batch.materialFlipbook1Frames;
                tex.materialFlipbook1Fps = batch.materialFlipbook1Fps;
                renderer->drawWorldIndexedMeshTextured(
                    batch.vertices.data(),
                    batch.vertices.size(),
                    batch.indices.data(),
                    batch.indices.size(),
                    &tex,
                    worldViewProj,
                    drawableW,
                    drawableH);
            };

            for (const WorldIndexedBatch& batch : worldIndexedBatches) {
                if (batch.vertices.empty() || batch.indices.empty()) continue;
                if (batch.alphaMode == 2u) continue;
                drawIndexedBatch(batch);
            }

            static thread_local std::vector<const WorldIndexedBatch*> blendBatches;
            blendBatches.clear();
            if (blendBatches.capacity() < worldIndexedBatches.size()) {
                blendBatches.reserve(worldIndexedBatches.size());
            }
            for (const WorldIndexedBatch& batch : worldIndexedBatches) {
                if (batch.vertices.empty() || batch.indices.empty()) continue;
                if (batch.alphaMode != 2u) continue;
                blendBatches.push_back(&batch);
            }
            std::stable_sort(
                blendBatches.begin(),
                blendBatches.end(),
                [](const WorldIndexedBatch* lhs, const WorldIndexedBatch* rhs) {
                    return lhs->sortDepth > rhs->sortDepth;
                });
            for (const WorldIndexedBatch* batch : blendBatches) {
                if (!batch) continue;
                drawIndexedBatch(*batch);
            }
        }
        if (renderWorld && hasWorldViewProj && supportsWorldIndexedMeshes &&
            renderer && renderer->backendId() &&
            toLowerCopy(renderer->backendId()) == "opengl" &&
            gameWorld && camera && engineServices && engineServices->resources) {
            std::vector<GameWorld::CaptureAttemptRenderSnapshot> captureSnaps;
            if (gameWorld->buildCaptureAttemptRenderSnapshots(captureSnaps)) {
                std::shared_ptr<Model> pokeballModel =
                    engineServices->resources->getModel("assets/models/pokeball.glb");
                if (pokeballModel) {
                    const int captureAnimIndex = findPokeballAnimIndex(pokeballModel);
                    const float captureAnimDurSec =
                        (captureAnimIndex >= 0) ? pokeballModel->getAnimationDurationSec(captureAnimIndex) : 0.0f;
                    for (const auto& snap : captureSnaps) {
                        if (snap.timeLeftSec <= 0.0f) continue;
                        const float scaleFactor =
                            pokeballModel->getScaleFactor() * std::max(0.0f, snap.ballScale);
                        const glm::mat4 instanceTransform =
                            buildCaptureBallModelMatrix(snap.ballPos, snap.ballYawDeg, scaleFactor);
                        const float animTimeSec =
                            (captureAnimIndex >= 0 && captureAnimDurSec > 0.0f)
                                ? sharedCaptureBallClipTimeSec(snap, captureAnimDurSec)
                                : 0.0f;
                        const int animIndexForDraw = (captureAnimIndex >= 0) ? captureAnimIndex : 0;
                        pokeballModel->drawAnimated(*camera, instanceTransform, animTimeSec, animIndexForDraw);
                    }
                }
            }
        }
        if (!worldTriangles.empty()) {
            renderer->drawDebugTriangles(worldTriangles.data(), worldTriangles.size(), drawableW, drawableH);
        }
        if (!worldQuads.empty()) {
            renderer->drawDebugQuads(worldQuads.data(), worldQuads.size(), drawableW, drawableH);
        }
        if (!sprites.empty()) {
            renderer->drawDebugSprites(sprites.data(), sprites.size(), drawableW, drawableH);
        }
        if (!lines.empty()) {
            renderer->drawDebugLines(lines.data(), lines.size(), drawableW, drawableH);
        }
        if (!overlayQuads.empty()) {
            renderer->drawDebugQuads(overlayQuads.data(), overlayQuads.size(), drawableW, drawableH);
        }
        if (!textLines.empty()) {
            renderer->drawDebugLines(textLines.data(), textLines.size(), drawableW, drawableH);
        }
    }

    void renderLegacyWorldLayer(int drawableW, int drawableH, bool renderWorld) {
        if (!renderWorld) return;
        if (board && gameWorld) {
            board->setCellSize(gameWorld->getBoardCellSize());
        }
        if (gameWorld && camera && board) gameWorld->drawAll(*camera, *board);
        if (gameWorld && camera) {
            auto healthBarData = gameWorld->getHealthBarData(*camera, drawableW, drawableH);
            healthBarRenderer.render(healthBarData);
        }
    }

    void renderLegacyHudLayer(int drawableW, int drawableH, bool renderWorld) {
        if (!renderWorld) return;

        if (gameWorld) {
            const bool adventureMode = services && services->gameMode == "adventure";
            const int invTopInset = adventureMode
                ? std::max(110, static_cast<int>(std::round(static_cast<float>(drawableH) * 0.16f)))
                : 16;
            const int invRightInset = adventureMode ? 24 : 16;
            itemInventoryUI.setLayoutInsets(invTopInset, invRightInset, 16, 16);
            itemInventoryUI.updateFromWorld(*gameWorld, drawableW, drawableH);
        }
        itemInventoryUI.render(drawableW, drawableH);

        const float cornerX = std::round(std::max(10.0f, static_cast<float>(drawableW) * 0.012f));
        const float cornerY = std::round(std::max(10.0f, static_cast<float>(drawableH) * 0.020f));
        const float minDim = static_cast<float>(std::min(drawableW, drawableH));
        const bool classicMode = services && services->gameMode == "classic";
        const auto computeClassicShopTopY = [&]() -> float {
            const game::ui::ShopRowLayout layout = game::ui::computeShopRowLayout(drawableW, drawableH, false);
            const game::ui::ShopRowPlacement place =
                game::ui::computeShopRowPlacement(drawableW, drawableH, 0, layout);
            return static_cast<float>(place.y);
        };
        const float classicShopTopY = computeClassicShopTopY();

        if (battleFeed) {
            const float wrap = std::max(240.0f, std::min(640.0f, static_cast<float>(drawableW) * 0.42f));
            battleFeed->setWrapWidth(wrap);
            battleFeed->setBaseScale(0.40f);
            const float battleLift = std::max(18.0f, minDim * 0.03f);
            battleFeed->setPadding(cornerX, cornerY + battleLift);
            battleFeed->clearBaselineYOverride();
        }
        if (catchFeed) {
            const float wrap = std::max(200.0f, std::min(420.0f, static_cast<float>(drawableW) * 0.30f));
            catchFeed->setWrapWidth(wrap);
            catchFeed->setBaseScale(0.38f);
            catchFeed->setPadding(cornerX, cornerY);
            catchFeed->clearBaselineYOverride();
        }
        if (economyFeed) {
            const float wrap = std::max(220.0f, std::min(380.0f, static_cast<float>(drawableW) * 0.28f));
            economyFeed->setWrapWidth(wrap);
            economyFeed->setBaseScale(0.36f);
            economyFeed->setPadding(cornerX, cornerY);
            economyFeed->clearBaselineYOverride();
            if (classicMode) {
                const float clearance = std::max(22.0f, minDim * 0.03f);
                economyFeed->setBaselineYOverride(std::max(8.0f, classicShopTopY - clearance));
            }
        }
        if (battleFeed) battleFeed->render(drawableW, drawableH);
        if (classicMode) {
            if (economyFeed) economyFeed->render(drawableW, drawableH);
        } else {
            if (catchFeed) catchFeed->render(drawableW, drawableH);
        }

        if (gameWorld && typeBonusText) {
            const auto counts = gameWorld->getPlayerTypeLineCounts();
            if (!counts.empty()) {
                auto formatType = [](std::string t) {
                    if (t.empty()) return t;
                    t[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(t[0])));
                    return t;
                };

                const float panelX = std::round(std::max(10.0f, static_cast<float>(drawableW) * 0.012f));
                const float panelY = std::round(std::max(110.0f, static_cast<float>(drawableH) * 0.20f));
                const float titleScale = 0.44f;
                const float rowScale = 0.40f;

                typeBonusText->renderText("Type Lines", panelX, panelY,
                                          glm::vec3(0.98f, 0.90f, 0.60f), titleScale);

                float y = panelY + typeBonusText->measureTextHeight(titleScale) + 6.0f;
                for (const auto& entry : counts) {
                    const std::string line = formatType(entry.type) + " x" + std::to_string(entry.uniqueLineCount);
                    typeBonusText->renderText(line, panelX, y, glm::vec3(0.94f, 0.94f, 0.94f), rowScale);
                    y += typeBonusText->measureTextHeight(rowScale) + 3.0f;
                }
            }
        }

        if (showPerfOverlay && typeBonusText && engineServices) {
            const EngineFramePerfStats& perf = engineServices->framePerf;
            if (perf.fps > 0.0f) {
                std::ostringstream line1;
                line1 << std::fixed << std::setprecision(1)
                      << "FPS " << perf.fps
                      << "  frame " << perf.frameMs << "ms"
                      << "  fixed " << perf.fixedMs << "ms"
                      << "  render " << perf.renderMs << "ms"
                      << "  swap " << perf.swapMs << "ms";

                const std::string stats = line1.str();
                const std::string ticks = "ticks " + std::to_string(perf.fixedTicks);

                const float scale = 0.34f;
                const float xPad = std::round(std::max(10.0f, static_cast<float>(drawableW) * 0.012f));
                const float yPad = std::round(std::max(10.0f, static_cast<float>(drawableH) * 0.020f));

                const float statsW = typeBonusText->measureTextWidth(stats, scale);
                const float statsX = std::max(8.0f, static_cast<float>(drawableW) - statsW - xPad);
                typeBonusText->renderText(stats, statsX, yPad, glm::vec3(0.96f, 0.96f, 0.65f), scale);

                const float ticksW = typeBonusText->measureTextWidth(ticks, scale);
                const float ticksX = std::max(8.0f, static_cast<float>(drawableW) - ticksW - xPad);
                const float ticksY = yPad + typeBonusText->measureTextHeight(scale) + 2.0f;
                typeBonusText->renderText(ticks, ticksX, ticksY, glm::vec3(0.86f, 0.93f, 0.98f), scale);
            }
        }
    }

    void renderWorldLayer(int drawableW, int drawableH, bool renderWorld) {
        const runtime::render::RenderRoutes routes = activeRenderRoutes();
        if (routes.usesLegacyRenderPath()) {
            renderLegacyWorldLayer(drawableW, drawableH, renderWorld);
            return;
        }
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
        if (flow.renderLegacyHudLayer) {
            renderLegacyHudLayer(drawableW, drawableH, renderWorld);
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

        if (board) {
            board->shutdown();
            board.reset();
        }

        if (usesLegacyGameRenderPath()) {
            UIManager::shutdown();
        }

        battleFeed.reset();
        catchFeed.reset();
        economyFeed.reset();
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

