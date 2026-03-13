#include "game/runtime/session/GameSession.h"

// Heavy includes live here (not in headers).
#include <iostream>
#include <string>
#include <utility>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <filesystem>
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
#include "game/runtime/startup/RuntimeBackendModelPrewarm.h"
#include "game/runtime/startup/RuntimeBackendCardUiPrewarm.h"
#include "game/runtime/startup/RuntimeStartupAssetPrewarm.h"
#include "game/runtime/startup/RuntimeWorldLayerPrewarm.h"
#include "game/runtime/session/SessionBackendUnitHydration.h"
#include "game/runtime/shared/backend/SharedBackendTextureCache.h"
#include "game/runtime/shared/backend/SharedBackendPoseEval.h"
#include "game/runtime/shared/capture/SharedCapturePresentation.h"
#include "game/runtime/shared/capture/SharedCaptureModelBridge.h"
#include "game/runtime/shared/world/SharedBoardGridBatches.h"
#include "game/runtime/shared/projected/SharedProjectedDebugVfx.h"
#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshRenderer.h"
#include "game/runtime/shared/ui/SharedBackendDebugViewOverlay.h"
#include "game/runtime/shared/projected/SharedProjectedWorldSceneHelpers.h"
#include "game/runtime/shared/projected/SharedProjectedUnitRenderer.h"
#include "game/runtime/shared/vfx/particles/SharedParticleBillboardBatches.h"
#include "game/runtime/shared/vfx/particles/SharedParticleSnapshotBillboards.h"
#include "game/runtime/shared/vfx/particles/SharedParticleVfxBridgeDispatch.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireMeshPlayback.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireFallbackEmitter.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireExactGpuBatches.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireAtlasHelpers.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireSnapshotAtlasCache.h"
#include "game/runtime/shared/vfx/growl/SharedGrowlVfxHelpers.h"
#include "game/runtime/shared/vfx/growl/SharedGrowlWaveBridge.h"
#include "game/runtime/shared/vfx/growl/SharedGrowlWaveBatches.h"
#include "game/runtime/shared/ui/SharedUnitHudBatches.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"
#include "game/GameServices.h"
#include "game/GameConfig.h"
#include "game/runtime/session/GameUpdateGraph.h"
#include "game/runtime/session/SessionBackendInventoryUi.h"
#include "game/runtime/session/SessionBackendRenderHelpers.h"
#include "game/runtime/session/SessionDebugSnapshot.h"
#include "game/runtime/session/SessionRenderConfig.h"
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
#include "game/state/CombatState.h"
#include "game/logging/LogBus.h"
#include "game/logging/LoggerUtil.h"
#include "game/scripting/ScriptEventBus.h"
#include "game/world/MoveImpactRouting.h"

namespace {
constexpr int kWorldLayerPrewarmFrames = 2;

std::string debugStateSnapshotPath() {
    return game::runtime::session_debug_snapshot::snapshotPath();
}

using DebugSessionSnapshot = game::runtime::session_debug_snapshot::SessionSnapshotMetadata;

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
    int worldLayerPrewarmFramesRemaining = 0;
    std::function<void(const std::string&)> setTitleCallback;
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
    using BackendAnimRoleEntry = game::runtime::session_backend_unit_hydration::BackendAnimRoleEntry;
    using BackendAnimRoleCache = game::runtime::session_backend_unit_hydration::BackendAnimRoleCache;
    BackendAnimRoleCache backendAnimByModelPath;
    using BackendTextureCacheEntry = game::runtime::SharedBackendTextureCacheEntry;
    std::unordered_map<std::string, BackendTextureCacheEntry> backendTextureByPath;

    std::shared_ptr<CameraSystem>           cameraSystem;
    std::shared_ptr<UnitInteractionSystem>  unitSystem;
    ShopSystem*                             shopSystem = nullptr;
    RoundSystem*                            roundSystem = nullptr;


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

    game::runtime::startup_asset_prewarm::TailFireStats prewarmSharedTailFireAssets() {
        if (!renderer) return {};

        const TailFireVFX::Config& cfg =
            game::runtime::shared_projected_scene::getTailFireFallbackCfg();
        if (!cfg.useFlipbook || cfg.flipbookPath.empty()) return {};

        ParticleSystem::RenderSnapshot snapshot{};
        snapshot.useFlipbook = cfg.useFlipbook;
        snapshot.flipbookPath = cfg.flipbookPath;
        snapshot.flipbookCols = cfg.flipbookCols;
        snapshot.flipbookRows = cfg.flipbookRows;
        snapshot.flipbookFrames = cfg.flipbookFrames;
        snapshot.flipbookFps = cfg.flipbookFps;
        snapshot.useSecondaryFlipbook = cfg.useFlipbook2 && !cfg.flipbook2Path.empty();
        snapshot.flipbookPath2 = cfg.flipbook2Path;
        snapshot.flipbookCols2 = cfg.flipbook2Cols;
        snapshot.flipbookRows2 = cfg.flipbook2Rows;
        snapshot.flipbookFrames2 = cfg.flipbook2Frames;
        snapshot.flipbookFps2 = cfg.flipbook2Fps;

        auto ensureTextureFn =
            [&](const std::string& path, bool flip) -> BackendTextureCacheEntry* {
                return ensureBackendTextureLoaded(path, flip);
            };

        game::runtime::startup_asset_prewarm::TailFireStats warmed{};
        const auto prewarmAtlas = [&](const std::string& key,
                                      const BackendTextureCacheEntry* atlas) {
            if (!atlas || !atlas->valid || atlas->rgba.empty() ||
                atlas->width <= 0 || atlas->height <= 0) {
                return;
            }

            IRenderBackend::WorldTextureData tex{};
            tex.key = key.c_str();
            tex.rgba = atlas->rgba.data();
            tex.width = atlas->width;
            tex.height = atlas->height;
            tex.wrapS = 33071; // GL_CLAMP_TO_EDGE
            tex.wrapT = 33071; // GL_CLAMP_TO_EDGE
            tex.alphaMode = 2u;
            tex.blendMode = 2u;
            renderer->prewarmWorldTextureData(&tex);
            ++warmed.legacyAtlases;
        };

        const auto combined =
            game::runtime::shared_tail_fire_snapshot_billboards::resolveTailFireCombinedAtlas(
                snapshot,
                backendTextureByPath,
                ensureTextureFn);
        if (!combined.cacheKey.empty()) {
            prewarmAtlas(combined.cacheKey, combined.atlas);
        }

        const bool prewarmLegacyPremul =
            game::runtime::session_render_config::backendPrewarmLegacyTailFirePremulEnabled() ||
            !snapshot.useSecondaryFlipbook ||
            !(combined.atlas && combined.atlas->valid);
        if (prewarmLegacyPremul) {
            const std::string primaryPremulKey =
                std::string("__tailfire_premul:") + snapshot.flipbookPath;
            BackendTextureCacheEntry* primaryPremul =
                game::runtime::shared_tail_fire_snapshot_billboards::resolveTailFirePremulAtlas(
                    snapshot.flipbookPath,
                    backendTextureByPath,
                    ensureTextureFn);
            prewarmAtlas(primaryPremulKey, primaryPremul);

            if (snapshot.useSecondaryFlipbook && !snapshot.flipbookPath2.empty()) {
                const std::string secondaryPremulKey =
                    std::string("__tailfire_premul:") + snapshot.flipbookPath2;
                BackendTextureCacheEntry* secondaryPremul =
                    game::runtime::shared_tail_fire_snapshot_billboards::resolveTailFirePremulAtlas(
                        snapshot.flipbookPath2,
                        backendTextureByPath,
                        ensureTextureFn);
                prewarmAtlas(secondaryPremulKey, secondaryPremul);
            }
        }

        const auto& authoredSpecs =
            game::runtime::shared_tail_fire_mesh_playback::authoredFlipbookSpecs();
        if (!authoredSpecs.empty()) {
            const auto& charmanderSpec = authoredSpecs.front();
            if (charmanderSpec.path && charmanderSpec.path[0] != '\0') {
                const auto cpuLoadStart = std::chrono::steady_clock::now();
                BackendTextureCacheEntry* authoredCpuTexture =
                    ensureBackendTextureLoaded(charmanderSpec.path, false);
                const auto cpuLoadEnd = std::chrono::steady_clock::now();
                std::cout << "[TailFire][CPU] authored_mesh_flipbook path="
                          << charmanderSpec.path
                          << " load_ms="
                          << std::chrono::duration<double, std::milli>(cpuLoadEnd - cpuLoadStart).count()
                          << " size="
                          << ((authoredCpuTexture && authoredCpuTexture->valid) ? authoredCpuTexture->width : 0)
                          << "x"
                          << ((authoredCpuTexture && authoredCpuTexture->valid) ? authoredCpuTexture->height : 0)
                          << " result="
                          << ((authoredCpuTexture && authoredCpuTexture->valid) ? "ok" : "failed")
                          << "\n";
                if (authoredCpuTexture && authoredCpuTexture->valid) {
                    ++warmed.meshFlipbookCpu;
                    IRenderBackend::WorldTextureData tex{};
                    tex.key = charmanderSpec.path;
                    tex.cacheKey = charmanderSpec.path;
                    tex.rgba = authoredCpuTexture->rgba.data();
                    tex.width = authoredCpuTexture->width;
                    tex.height = authoredCpuTexture->height;
                    tex.wrapS = 33071; // GL_CLAMP_TO_EDGE
                    tex.wrapT = 33071; // GL_CLAMP_TO_EDGE
                    tex.alphaMode = 1u;
                    tex.blendMode = 0u;
                    renderer->prewarmWorldTextureData(&tex);
                    ++warmed.meshFlipbookGpu;
                }
            }
        }

        return warmed;
    }

    void hydrateBackendUnitAnimationAndScale() {
        if (!usesBackendGameRenderPath() || !gameWorld) return;
        game::runtime::session_backend_unit_hydration::hydrateBackendUnits(
            gameWorld->getPokemons(),
            gameWorld->getBenchPokemons(),
            dataDb,
            backendAnimByModelPath,
            [&](const std::string& modelPath) {
                return ensureBackendMeshLoaded(modelPath);
            });
    }

    void init(GameContext& ctx) {
        camera = ctx.camera;
        renderer = ctx.renderer;
        engineServices = ctx.services;
        setTitleCallback = ctx.setTitle;
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
        services->engineServices = ctx.services;
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
            services->vsyncEnabled = ctx.services->vsyncEnabled;
            services->requireDiscreteGpu = ctx.services->requireDiscreteGpu;
            services->characterInkingEnabled = ctx.services->characterInkingEnabled;
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

        if (cameraSystem) {
            scheduler.add(
                std::make_unique<game::UpdatableSystemAdapter>(cameraSystem.get(), "camera"),
                Phase::Update);
        }
        if (unitSystem) {
            scheduler.add(
                std::make_unique<game::UpdatableSystemAdapter>(unitSystem.get(), "unit_interaction"),
                Phase::Update);
        }
        auto shopSystemImpl = std::make_unique<ShopSystem>(services->rng);
        shopSystem = shopSystemImpl.get();
        scheduler.add(std::move(shopSystemImpl), Phase::Update);

        auto roundSystemImpl = std::make_unique<RoundSystem>(*services, roundPhaseEntity);
        roundSystem = roundSystemImpl.get();
        ecsWorld.add<game::RoundState>(roundPhaseEntity, game::RoundState{ roundSystemImpl->getCurrentPhase() });
        scheduler.add(std::move(roundSystemImpl), Phase::Update);

        if (auto* stateMgr = stateManager.get()) {
            scheduler.add(std::make_unique<game::CallbackSystemAdapter>(
                [stateMgr, engineServices = engineServices](float dt) {
                    stateMgr->update(dt);
                    if (engineServices) {
                        const auto& timing = stateMgr->lastUpdateTiming();
                        engineServices->frameFixedBreakdown.stateUpdateMs += timing.stateUpdateMs;
                        engineServices->frameFixedBreakdown.stateFlushMs += timing.flushPendingMs;
                    }
                },
                "state_manager"
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
                [worldPtr](float dt) { worldPtr->update(dt); },
                "world"
            ), Phase::PostUpdate);
        }

        updateGraph.configure({
            &scheduler,
            &ecsWorld,
            roundPhaseEntity,
            shopSystem,
            &log,
            &scriptEvents,
            engineServices
        });

        std::cout << "[Init] Shared gameplay render path: using backend model cache loader.\n";
        if (game::runtime::session_render_config::backendPreloadModelCacheEnabled()) {
            std::cout << "[Init] Shared gameplay render path: preloading backend model cache...\n";
            const bool prewarmModelTextures =
                usesBackendGameRenderPath() &&
                renderer &&
                renderer->supportsWorldIndexedMeshes() &&
                game::runtime::session_render_config::backendPrewarmModelTexturesEnabled();
            const bool prewarmModelGeometry =
                usesBackendGameRenderPath() &&
                renderer &&
                renderer->supportsWorldIndexedMeshes() &&
                game::runtime::session_render_config::backendModelFullMeshEnabled() &&
                game::runtime::session_render_config::backendModelFastTexturedPathEnabled() &&
                game::runtime::session_render_config::backendPrewarmModelGeometryEnabled();
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
            const game::runtime::backend_model_prewarm::Options prewarmOptions{
                .verboseModelCacheLog = game::runtime::session_render_config::backendModelVerboseLoggingEnabled(),
                .prewarmAnimRoles = game::runtime::session_render_config::backendPrewarmAnimRolesEnabled(),
                .prewarmModelTextures = prewarmModelTextures,
                .prewarmModelGeometry = prewarmModelGeometry,
                .maxFailureSamples = 8u,
            };
            (void)game::runtime::backend_model_prewarm::run(
                modelPathsToPreload,
                prewarmOptions,
                game::runtime::backend_model_prewarm::Callbacks{
                    .setTitle = ctx.setTitle,
                    .renderBootLoading = ctx.renderBootLoading,
                    .pumpPreloadEvents = ctx.pumpPreloadEvents,
                    .requestQuit = ctx.requestQuit,
                    .loadModel =
                        [&](const std::string& modelPath) {
                            auto& cacheEntry = backendMeshByModelPath[modelPath];
                            if (cacheEntry.attemptedLoad) {
                                return game::runtime::backend_model_prewarm::ModelLoadResult{
                                    false,
                                    cacheEntry.error.empty() ? &cacheEntry.mesh : nullptr,
                                    cacheEntry.error,
                                };
                            }

                            cacheEntry.attemptedLoad = true;
                            std::string err;
                            if (!runtime::backend_model::loadMeshFromCache(
                                    modelPath, cacheEntry.mesh, &err)) {
                                cacheEntry.error = std::move(err);
                                cacheEntry.mesh = {};
                                return game::runtime::backend_model_prewarm::ModelLoadResult{
                                    true,
                                    nullptr,
                                    cacheEntry.error,
                                };
                            }

                            return game::runtime::backend_model_prewarm::ModelLoadResult{
                                true,
                                &cacheEntry.mesh,
                                {},
                            };
                        },
                    .prewarmAnimRoles =
                        [&](const std::string& modelPath,
                            const runtime::backend_model::MeshData& mesh) {
                            auto it = backendAnimByModelPath.find(modelPath);
                            const bool alreadyResolved =
                                (it != backendAnimByModelPath.end()) && it->second.attemptedResolve;
                            BackendAnimRoleEntry& roles =
                                game::runtime::session_backend_unit_hydration::ensureBackendAnimRoles(
                                    modelPath,
                                    &mesh,
                                    backendAnimByModelPath);
                            return !alreadyResolved && roles.attemptedResolve;
                        },
                    .prewarmTextures =
                        [&](const std::string&, const runtime::backend_model::MeshData& mesh) {
                            return game::runtime::session_backend_render_helpers::prewarmBackendWorldTexturesForMesh(renderer, &mesh);
                        },
                    .prewarmGeometry =
                        [&](const runtime::backend_model::MeshData& mesh) {
                            return game::runtime::shared_projected_unit_backend_mesh::
                                prewarmProjectedUnitBackendMeshGeometryCache(*renderer, mesh);
                        },
                },
                std::cout);
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

        const bool usesBackendPathForStartupPrewarm = usesBackendGameRenderPath() && renderer;
        std::vector<std::string> uiSpritePrewarmPaths;
        if (usesBackendPathForStartupPrewarm && game::runtime::session_render_config::backendUiSpritePrewarmEnabled()) {
            uiSpritePrewarmPaths =
                game::runtime::startup_asset_prewarm::collectUiSpritePrewarmPaths(dataDb);
        }

        (void)game::runtime::startup_asset_prewarm::run(
            game::runtime::startup_asset_prewarm::Options{
                .usesBackendRenderPath = usesBackendPathForStartupPrewarm,
                .uiSpritePrewarmEnabled = game::runtime::session_render_config::backendUiSpritePrewarmEnabled(),
                .drawableW = ctx.drawableW,
                .drawableH = ctx.drawableH,
            },
            uiSpritePrewarmPaths,
            game::runtime::startup_asset_prewarm::Callbacks{
                .setTitle = ctx.setTitle,
                .renderBootLoading = ctx.renderBootLoading,
                .pumpPreloadEvents = ctx.pumpPreloadEvents,
                .requestQuit = ctx.requestQuit,
                .prewarmWorldShading =
                    [&]() {
                        if (renderer) {
                            renderer->prewarmWorldRenderAssets();
                        }
                    },
                .prewarmTailFire =
                    [&]() {
                        return prewarmSharedTailFireAssets();
                    },
                .prewarmSpriteTextures =
                    [&](const std::vector<std::string>& texturePaths) {
                        if (!renderer || texturePaths.empty()) return;
                        std::vector<const char*> rawPaths;
                        rawPaths.reserve(texturePaths.size());
                        for (const std::string& path : texturePaths) {
                            rawPaths.push_back(path.c_str());
                        }
                        renderer->prewarmDebugSpriteTextures(rawPaths.data(), rawPaths.size());
                    },
                .prewarmBackendCardUi =
                    [&](int drawableW,
                        int drawableH,
                        const std::vector<std::string>& texturePaths) {
                        (void)game::runtime::backend_card_ui_prewarm::run(
                            renderer,
                            drawableW,
                            drawableH,
                            texturePaths);
                    },
            },
            std::cout);

        if (usesBackendGameRenderPath() &&
            renderer &&
            game::runtime::session_render_config::backendWorldLayerPrewarmEnabled()) {
            game::runtime::world_layer_prewarm::schedule(
                worldLayerPrewarmFramesRemaining,
                kWorldLayerPrewarmFrames,
                game::runtime::world_layer_prewarm::Callbacks{
                    .setTitle = ctx.setTitle,
                    .renderBootLoading = ctx.renderBootLoading,
                    .pumpPreloadEvents = ctx.pumpPreloadEvents,
                    .requestQuit = ctx.requestQuit,
                    .renderWorldLayer =
                        [&](int drawableW, int drawableH) {
                            renderWorldLayer(drawableW, drawableH, /*renderWorld=*/true);
                        },
                },
                std::cout);
        }

        stateManager->pushState(std::make_unique<ScriptedState>(
            stateManager.get(),
            gameWorld.get(),
            *services,
            engine::paths::data("scripts/states/main_menu.lua")
        ));

        if (worldLayerPrewarmFramesRemaining > 0 &&
            ctx.drawableW > 0 &&
            ctx.drawableH > 0 &&
            usesBackendGameRenderPath() &&
            renderer) {
            game::runtime::world_layer_prewarm::drainStartupFrames(
                worldLayerPrewarmFramesRemaining,
                kWorldLayerPrewarmFrames,
                ctx.drawableW,
                ctx.drawableH,
                game::runtime::world_layer_prewarm::Callbacks{
                    .setTitle = ctx.setTitle,
                    .renderBootLoading = ctx.renderBootLoading,
                    .pumpPreloadEvents = ctx.pumpPreloadEvents,
                    .requestQuit = ctx.requestQuit,
                    .renderWorldLayer =
                        [&](int drawableW, int drawableH) {
                            renderWorldLayer(drawableW, drawableH, /*renderWorld=*/true);
                        },
                },
                std::cout);
        }

        game::runtime::world_layer_prewarm::restoreTitleAfterInit(
            worldLayerPrewarmFramesRemaining,
            game::runtime::world_layer_prewarm::Callbacks{
                .setTitle = ctx.setTitle,
            });
        const std::string snapshotPath = debugStateSnapshotPath();
        if (std::filesystem::exists(snapshotPath)) {
            std::cout << "[StateSnapshot] Snapshot present but not auto-loaded: "
                      << snapshotPath << " (press F9 to restore)\n";
        }
        std::cout << "[Init] Game initialized.\n";

        // Keep startup/perf stdout intact, but stop mirroring gameplay feed spam
        // to the terminal unless explicitly requested.
        if (engine::env::get("PAC_LOG_ECHO_STDOUT").has_value()) {
            log.setEchoToStdout(engine::env::flagEnabled("PAC_LOG_ECHO_STDOUT"));
        } else {
            log.setEchoToStdout(false);
        }
    }

    game::runtime::session_backend_inventory_ui::Dependencies backendInventoryUiDependencies() {
        return game::runtime::session_backend_inventory_ui::Dependencies{
            .getSelectedItem =
                [&]() -> std::string {
                    return gameWorld ? gameWorld->getSelectedItem() : std::string{};
                },
            .setSelectedItem =
                [&](const std::string& itemId) {
                    if (gameWorld) {
                        gameWorld->setSelectedItem(itemId);
                    }
                },
            .listItems =
                [&]() -> std::vector<std::pair<std::string, int>> {
                    return gameWorld ? gameWorld->listItems()
                                     : std::vector<std::pair<std::string, int>>{};
                },
            .logInfo = [&](const std::string& message) { log.catchInfo(message); },
        };
    }

    DebugSessionSnapshot captureDebugSessionSnapshot() const {
        DebugSessionSnapshot out;

        if (stateManager) {
            if (GameState* current = stateManager->getCurrentState()) {
                if (const auto* combat = dynamic_cast<const CombatState*>(current)) {
                    out.stateKind = "combat";
                    out.stateScriptPath = combat->debugScriptPath();
                } else if (const auto* scripted = dynamic_cast<const ScriptedState*>(current)) {
                    out.stateKind = "scripted";
                    out.stateScriptPath = scripted->debugScriptPath();
                }
            }
        }

        if (services && services->ecsWorld && services->ecsWorld->alive(services->combatStateEntity)) {
            if (const auto* combatState = services->ecsWorld->get<game::CombatActive>(services->combatStateEntity)) {
                out.hasCombatActive = true;
                out.combatActive = combatState->active;
            }
            if (const auto* roundState = services->ecsWorld->get<game::RoundState>(services->combatStateEntity)) {
                out.hasRoundPhase = true;
                out.roundPhase = roundState->phase;
            }
        }

        return out;
    }

    void restoreStateStackForSnapshot(const DebugSessionSnapshot& session, bool preferCombatState) {
        if (!stateManager || !gameWorld || !services) return;

        if (preferCombatState || session.stateKind == "combat") {
            if (dynamic_cast<CombatState*>(stateManager->getCurrentState())) {
                return;
            }

            std::string combatScript = session.stateScriptPath;
            if (combatScript.empty()) {
                game::log::warn(
                    &log,
                    "[StateSnapshot] Missing combat script path; keeping current state stack.");
                return;
            }

            stateManager->clearAndPushState(std::make_unique<CombatState>(
                stateManager.get(),
                gameWorld.get(),
                *services,
                combatScript,
                true));
            return;
        }

        if (session.stateKind == "scripted" && !session.stateScriptPath.empty()) {
            const auto* scripted = dynamic_cast<ScriptedState*>(stateManager->getCurrentState());
            if (scripted && scripted->debugScriptPath() == session.stateScriptPath) {
                return;
            }
            stateManager->clearAndPushState(std::make_unique<ScriptedState>(
                stateManager.get(),
                gameWorld.get(),
                *services,
                session.stateScriptPath));
        }
    }

    void applyRuntimeFlagsForSnapshot(const DebugSessionSnapshot& session, bool preferCombatState) {
        if (!services || !services->ecsWorld || !services->ecsWorld->alive(services->combatStateEntity)) return;

        bool combatActive = preferCombatState;
        if (!preferCombatState && session.hasCombatActive) {
            combatActive = session.combatActive;
        }

        RoundPhase phase = combatActive ? RoundPhase::Battle : RoundPhase::Planning;
        if (!preferCombatState && session.hasRoundPhase) {
            phase = session.roundPhase;
        }

        if (auto* combatState = services->ecsWorld->get<game::CombatActive>(services->combatStateEntity)) {
            combatState->active = combatActive;
        } else {
            services->ecsWorld->add<game::CombatActive>(services->combatStateEntity, game::CombatActive{combatActive});
        }
        if (auto* roundState = services->ecsWorld->get<game::RoundState>(services->combatStateEntity)) {
            roundState->phase = phase;
        } else {
            services->ecsWorld->add<game::RoundState>(services->combatStateEntity, game::RoundState{phase});
        }

        if (roundSystem) {
            float timer = 30.0f;
            if (phase == RoundPhase::Battle) timer = 10.0f;
            else if (phase == RoundPhase::Resolution) timer = 5.0f;
            roundSystem->debugSetPhase(phase, timer);
        }

        if (gameWorld) {
            gameWorld->setBoardInteractionLocked(combatActive);
            if (combatActive) {
                if (!gameWorld->hasBattleStartPositions()) {
                    gameWorld->capturePlayerPositionsForBattle();
                }
                gameWorld->clearClassicShopCards();
                gameWorld->setUnitDropZoneLayoutHint(0, false);
            }
        }
    }

    void saveDebugStateSnapshot() {
        if (!gameWorld) {
            game::log::warn(&log, "[StateSnapshot] Save skipped: world is not ready.");
            log.infoTerminalOnly("[StateSnapshot] Save skipped: world is not ready.");
            return;
        }

        GameWorld::DebugStateSnapshot snapshot;
        if (!gameWorld->buildDebugStateSnapshot(snapshot)) {
            game::log::warn(&log, "[StateSnapshot] Save failed: could not build world snapshot.");
            log.infoTerminalOnly(
                "[StateSnapshot] Save failed: could not build world snapshot.");
            return;
        }

        const DebugSessionSnapshot session = captureDebugSessionSnapshot();
        const std::string path = debugStateSnapshotPath();
        std::string err;
        if (!game::runtime::session_debug_snapshot::writeFile(snapshot, path, &session, &err)) {
            game::log::warn(
                &log,
                std::string("[StateSnapshot] Save failed: ") + err + " (" + path + ")");
            log.infoTerminalOnly(
                std::string("[StateSnapshot] Save failed: ") + err + " (" + path + ")");
            return;
        }

        game::log::info(&log, std::string("[StateSnapshot] Saved: ") + path);
        log.infoTerminalOnly(
            std::string("[StateSnapshot] Saved: ") + path + " "
            + game::runtime::session_debug_snapshot::summarizeSessionSnapshot(session) + " "
            + game::runtime::session_debug_snapshot::summarizeWorldSnapshot(snapshot));
    }

    void loadDebugStateSnapshot() {
        if (!gameWorld) {
            game::log::warn(&log, "[StateSnapshot] Load skipped: world is not ready.");
            log.infoTerminalOnly("[StateSnapshot] Load skipped: world is not ready.");
            return;
        }

        using SnapshotClock = std::chrono::high_resolution_clock;
        const auto loadStart = SnapshotClock::now();
        const std::string path = debugStateSnapshotPath();
        log.infoTerminalOnly(std::string("[StateSnapshot] Load requested: ") + path);
        GameWorld::DebugStateSnapshot snapshot;
        DebugSessionSnapshot session;
        std::string err;
        const auto readStart = SnapshotClock::now();
        if (!game::runtime::session_debug_snapshot::readFile(path, snapshot, &session, &err)) {
            const auto readEnd = SnapshotClock::now();
            game::log::warn(
                &log,
                std::string("[StateSnapshot] Load failed: ") + err + " (" + path + ")");
            log.infoTerminalOnly(
                std::string("[StateSnapshot] Load failed: ") + err + " (" + path + ")"
                + " read=" + game::runtime::session_debug_snapshot::formatMillis(
                    std::chrono::duration<double, std::milli>(readEnd - readStart).count()));
            return;
        }
        const auto readEnd = SnapshotClock::now();

        const bool preferCombatState =
            game::runtime::session_debug_snapshot::hasActiveEnemyUnits(snapshot);
        const auto stateRestoreStart = SnapshotClock::now();
        restoreStateStackForSnapshot(session, preferCombatState);
        const auto stateRestoreEnd = SnapshotClock::now();

        std::string worldErr;
        const auto worldApplyStart = SnapshotClock::now();
        const bool exact = gameWorld->applyDebugStateSnapshot(snapshot, &worldErr);
        const auto worldApplyEnd = SnapshotClock::now();
        const auto flagsStart = SnapshotClock::now();
        applyRuntimeFlagsForSnapshot(session, preferCombatState);
        const auto flagsEnd = SnapshotClock::now();
        const auto inventoryStart = SnapshotClock::now();
        game::runtime::session_backend_inventory_ui::refreshPanel(
            backendInventoryPanel,
            kBackendInventoryVisibleCount,
            backendInventoryUiDependencies());
        const auto inventoryEnd = SnapshotClock::now();
        double prewarmIndexedMs = 0.0;
        std::size_t prewarmIndexedBatches = 0u;
        if (game::runtime::session_render_config::snapshotPrewarmRestoreRenderEnabled() &&
            renderer &&
            usesBackendGameRenderPath() &&
            renderer->supportsWorldIndexedMeshes() &&
            viewport.width > 0 &&
            viewport.height > 0) {
            bool renderWorld = true;
            if (stateManager) {
                if (auto* state = stateManager->getCurrentState()) {
                    renderWorld = state->shouldRenderWorld();
                }
            }
            const auto indexedPrewarmStart = SnapshotClock::now();
            prewarmIndexedBatches =
                prewarmWorldIndexedLayer(viewport.width, viewport.height, renderWorld);
            const auto indexedPrewarmEnd = SnapshotClock::now();
            prewarmIndexedMs =
                std::chrono::duration<double, std::milli>(
                    indexedPrewarmEnd - indexedPrewarmStart).count();
        }
        const auto loadEnd = SnapshotClock::now();

        const double readMs =
            std::chrono::duration<double, std::milli>(readEnd - readStart).count();
        const double stateRestoreMs =
            std::chrono::duration<double, std::milli>(stateRestoreEnd - stateRestoreStart).count();
        const double worldApplyMs =
            std::chrono::duration<double, std::milli>(worldApplyEnd - worldApplyStart).count();
        const double flagsMs =
            std::chrono::duration<double, std::milli>(flagsEnd - flagsStart).count();
        const double inventoryMs =
            std::chrono::duration<double, std::milli>(inventoryEnd - inventoryStart).count();
        const double totalMs =
            std::chrono::duration<double, std::milli>(loadEnd - loadStart).count();

        if (!exact) {
            if (worldErr.empty()) {
                worldErr = "snapshot applied with missing units";
            }
            game::log::warn(
                &log,
                std::string("[StateSnapshot] Loaded with warnings: ") + worldErr);
            log.infoTerminalOnly(
                std::string("[StateSnapshot] Loaded with warnings: ") + worldErr);
        }

        game::log::info(&log, std::string("[StateSnapshot] Loaded: ") + path);
        log.infoTerminalOnly(
            std::string("[StateSnapshot] Loaded: ") + path
            + " exact=" + (exact ? "1" : "0")
            + " preferCombat=" + (preferCombatState ? std::string("1") : std::string("0"))
            + " " + game::runtime::session_debug_snapshot::summarizeSessionSnapshot(session)
            + " " + game::runtime::session_debug_snapshot::summarizeWorldSnapshot(snapshot));
        log.infoTerminalOnly(
            std::string("[StateSnapshot] Load phases: ")
            + "read=" + game::runtime::session_debug_snapshot::formatMillis(readMs)
            + " state=" + game::runtime::session_debug_snapshot::formatMillis(stateRestoreMs)
            + " apply=" + game::runtime::session_debug_snapshot::formatMillis(worldApplyMs)
            + " flags=" + game::runtime::session_debug_snapshot::formatMillis(flagsMs)
            + " inventory=" + game::runtime::session_debug_snapshot::formatMillis(inventoryMs)
            + " prewarm_indexed=" + game::runtime::session_debug_snapshot::formatMillis(prewarmIndexedMs)
            + " prewarm_batches=" + std::to_string(prewarmIndexedBatches)
            + " total=" + game::runtime::session_debug_snapshot::formatMillis(totalMs));
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
            if (event.keyId == InputEvent::Key::F5) {
                saveDebugStateSnapshot();
                return;
            }
            if (event.keyId == InputEvent::Key::F9) {
                loadDebugStateSnapshot();
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
            if (game::runtime::session_backend_inventory_ui::clearSelection(
                    backendInventoryUiDependencies())) {
                return; // consume key so gameplay actions do not fire simultaneously.
            }
        }

        if (renderWorldForInput && usesBackendGameUiPath()) {
            if (game::runtime::session_backend_inventory_ui::handleInput(
                    backendInventoryPanel,
                    event,
                    kBackendInventoryVisibleCount,
                    backendInventoryUiDependencies())) {
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
            const auto hydrateStart = std::chrono::high_resolution_clock::now();
            hydrateBackendUnitAnimationAndScale();
            if (engineServices) {
                const auto hydrateEnd = std::chrono::high_resolution_clock::now();
                engineServices->frameFixedBreakdown.backendHydrateMs += static_cast<float>(
                    std::chrono::duration<double, std::milli>(hydrateEnd - hydrateStart).count());
            }
        }
        updateGraph.tick(dt);
        if (devPauseWorld && devPauseStepTicks > 0) {
            --devPauseStepTicks;
        }
    }

    std::size_t renderBackendDebugView(int drawableW,
                                       int drawableH,
                                       bool renderWorld,
                                       bool prewarmWorldIndexedOnly = false) {
        if (!renderer || drawableW <= 0 || drawableH <= 0) return 0u;
        using RenderBuildClock = std::chrono::steady_clock;
        const auto worldComposeStart = RenderBuildClock::now();
        const bool useLegacyGrowlWaveVfx = game::runtime::session_render_config::backendUseLegacyGrowlWaveVfxEnabled();
        const bool useLegacyParticleVfxSnapshotBridge = game::runtime::session_render_config::backendUseLegacyParticleVfxSnapshotBridgeEnabled();

        runtime::backend_inventory_panel::clearHitRegions(backendInventoryPanel);

        using WorldIndexedBatch = runtime::shared_world_batches::WorldIndexedBatch;
        struct BackendUnitLabel {
            float x = 0.0f;
            float y = 0.0f;
            std::string text;
            glm::vec3 color{1.0f, 1.0f, 1.0f};
        };
        struct ProjectedBackdropCacheKey {
            bool supportsWorldTriangles3D = false;
            int rows = 0;
            int cols = 0;
            int benchSlots = 0;
            float worldCellSize = 0.0f;
            float boardMinX = 0.0f;
            float boardMinZ = 0.0f;
            float boardMaxX = 0.0f;
            float boardMaxZ = 0.0f;
            float boardX = 0.0f;
            float boardY = 0.0f;
            float boardW = 0.0f;
            float boardH = 0.0f;
            float cellW = 0.0f;
            float cellH = 0.0f;
            float line = 0.0f;

            bool operator==(const ProjectedBackdropCacheKey& other) const {
                return supportsWorldTriangles3D == other.supportsWorldTriangles3D &&
                       rows == other.rows &&
                       cols == other.cols &&
                       benchSlots == other.benchSlots &&
                       worldCellSize == other.worldCellSize &&
                       boardMinX == other.boardMinX &&
                       boardMinZ == other.boardMinZ &&
                       boardMaxX == other.boardMaxX &&
                       boardMaxZ == other.boardMaxZ &&
                       boardX == other.boardX &&
                       boardY == other.boardY &&
                       boardW == other.boardW &&
                       boardH == other.boardH &&
                       cellW == other.cellW &&
                       cellH == other.cellH &&
                       line == other.line;
            }
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
            std::unordered_map<int, runtime::shared_tail_fire_fallback::Anchor> sharedTailFireAnchors;
            runtime::shared_capture::SnapshotCache sharedCaptureAttemptCache;
            bool projectedBackdropValid = false;
            ProjectedBackdropCacheKey projectedBackdropKey{};
            std::size_t projectedBackdropWorldBackgroundQuadsCount = 0u;
            std::size_t projectedBackdropWorldTrianglesCount = 0u;
            std::size_t projectedBackdropWorld3DTrianglesCount = 0u;
            std::size_t projectedBackdropLinesCount = 0u;
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
        auto& sharedTailFireAnchors = scratch.sharedTailFireAnchors;
        auto& sharedCaptureAttemptCache = scratch.sharedCaptureAttemptCache;

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
        if (sharedTailFireAnchors.bucket_count() < 16u) sharedTailFireAnchors.reserve(16u);
        if (sharedCaptureAttemptCache.snaps.capacity() < 8u) sharedCaptureAttemptCache.snaps.reserve(8u);
        if (sharedCaptureAttemptCache.byTargetId.bucket_count() < 8u) sharedCaptureAttemptCache.byTargetId.reserve(8u);
        std::uint32_t visibleAnimatedUnitsThisFrame = 0u;
        std::uint32_t particleCountThisFrame = 0u;
        float projectedUnitsMsThisFrame = 0.0f;
        float projectedPoseEvalMsThisFrame = 0.0f;
        float projectedModelMsThisFrame = 0.0f;
        float projectedModelPrepMsThisFrame = 0.0f;
        float projectedModelGeometryMsThisFrame = 0.0f;
        float projectedOverlayMsThisFrame = 0.0f;
        std::uint32_t projectedUnitsProcessedThisFrame = 0u;
        std::uint32_t projectedModelUnitsThisFrame = 0u;
        std::uint32_t projectedClipSkinnedUnitsThisFrame = 0u;
        float worldBackdropComposeMsThisFrame = 0.0f;
        float worldVfxBridgeMsThisFrame = 0.0f;
        float worldDepthFlushMsThisFrame = 0.0f;

        const bool supportsWorldTriangles3D = renderer->supportsWorldTriangles3D();
        const bool supportsWorldIndexedMeshes = renderer->supportsWorldIndexedMeshes();
        const bool allowPortraitFallback = game::runtime::session_render_config::backendWorldPortraitFallbackEnabled();
        const bool forcePortraitOverlay = game::runtime::session_render_config::backendWorldPortraitOverlayForced();
        float worldViewProj[16] = {};
        bool hasWorldViewProj = false;
        float cameraWorldPos3[3] = {0.0f, 7.0f, 9.0f};
        float cameraForward3[3] = {0.0f, -0.6139406f, -0.7893522f};
        float cameraTarget3[3] = {0.0f, -1.0f, 0.0f};
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
        const bool useProjectedWorldLayout =
            showWorldBackdrop && renderWorld && gameWorld && (camera != nullptr);

        worldQuads.clear();
        worldIndexedBatches.clear();
        overlayQuads.clear();
        textLines.clear();
        sprites.clear();
        unitLabels.clear();
        sharedTailFireAnchors.clear();
        sharedCaptureAttemptCache.snaps.clear();
        sharedCaptureAttemptCache.byTargetId.clear();
        if (useProjectedWorldLayout && scratch.projectedBackdropValid) {
            worldBackgroundQuads.resize(scratch.projectedBackdropWorldBackgroundQuadsCount);
            worldTriangles.resize(scratch.projectedBackdropWorldTrianglesCount);
            world3DTriangles.resize(scratch.projectedBackdropWorld3DTrianglesCount);
            lines.resize(scratch.projectedBackdropLinesCount);
        } else {
            worldBackgroundQuads.clear();
            worldTriangles.clear();
            world3DTriangles.clear();
            lines.clear();
            if (!useProjectedWorldLayout) {
                scratch.projectedBackdropValid = false;
                scratch.projectedBackdropWorldBackgroundQuadsCount = 0u;
                scratch.projectedBackdropWorldTrianglesCount = 0u;
                scratch.projectedBackdropWorld3DTrianglesCount = 0u;
                scratch.projectedBackdropLinesCount = 0u;
            }
        }
        if (showWorldBackdrop) {
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
                cameraWorldPos3[0] = cameraWorldPos.x;
                cameraWorldPos3[1] = cameraWorldPos.y;
                cameraWorldPos3[2] = cameraWorldPos.z;
                const glm::vec3 cameraForward = camera->getDirection();
                cameraForward3[0] = cameraForward.x;
                cameraForward3[1] = cameraForward.y;
                cameraForward3[2] = cameraForward.z;
                const glm::vec3 cameraTarget = camera->getTarget();
                cameraTarget3[0] = cameraTarget.x;
                cameraTarget3[1] = cameraTarget.y;
                cameraTarget3[2] = cameraTarget.z;
                const glm::vec4 screenViewport(
                    0.0f,
                    0.0f,
                    static_cast<float>(drawableW),
                    static_cast<float>(drawableH));
                const float line = std::max(1.0f, minDim * 0.0019f);

                ProjectedBackdropCacheKey projectedBackdropKey{};
                projectedBackdropKey.supportsWorldTriangles3D = supportsWorldTriangles3D;
                projectedBackdropKey.rows = rows;
                projectedBackdropKey.cols = cols;
                projectedBackdropKey.benchSlots = config.benchSlots;
                projectedBackdropKey.worldCellSize = worldCellSize;
                projectedBackdropKey.boardMinX = boardMinX;
                projectedBackdropKey.boardMinZ = boardMinZ;
                projectedBackdropKey.boardMaxX = boardMaxX;
                projectedBackdropKey.boardMaxZ = boardMaxZ;
                projectedBackdropKey.boardX = boardX;
                projectedBackdropKey.boardY = boardY;
                projectedBackdropKey.boardW = boardW;
                projectedBackdropKey.boardH = boardH;
                projectedBackdropKey.cellW = cellW;
                projectedBackdropKey.cellH = cellH;
                projectedBackdropKey.line = line;

                game::runtime::shared_projected_debug::ProjectedDebugVfxBuilder projectedDebug(
                    supportsWorldTriangles3D,
                    view,
                    proj,
                    drawableH,
                    screenViewport,
                    worldTriangles,
                    world3DTriangles,
                    lines);
                const bool canCacheProjectedBackdrop = supportsWorldTriangles3D;
                const auto backdropComposeStart = RenderBuildClock::now();
                if (canCacheProjectedBackdrop) {
                    if (!scratch.projectedBackdropValid ||
                        !(scratch.projectedBackdropKey == projectedBackdropKey)) {
                        worldBackgroundQuads.clear();
                        worldTriangles.clear();
                        world3DTriangles.clear();
                        lines.clear();

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
                        scratch.projectedBackdropValid = true;
                        scratch.projectedBackdropKey = projectedBackdropKey;
                        scratch.projectedBackdropWorldBackgroundQuadsCount =
                            worldBackgroundQuads.size();
                        scratch.projectedBackdropWorldTrianglesCount =
                            worldTriangles.size();
                        scratch.projectedBackdropWorld3DTrianglesCount =
                            world3DTriangles.size();
                        scratch.projectedBackdropLinesCount =
                            lines.size();
                    } else {
                        worldBackgroundQuads.resize(
                            scratch.projectedBackdropWorldBackgroundQuadsCount);
                        worldTriangles.resize(scratch.projectedBackdropWorldTrianglesCount);
                        world3DTriangles.resize(
                            scratch.projectedBackdropWorld3DTrianglesCount);
                        lines.resize(scratch.projectedBackdropLinesCount);
                    }
                }
                using DepthTri = game::runtime::shared_projected_scene::DepthTri;
                using DepthWorldTri = game::runtime::shared_projected_scene::DepthWorldTri;
                auto modelDepthBuffers =
                    game::runtime::shared_projected_scene::acquireModelDepthBuffers(12000u);
                auto& modelDepthTris = modelDepthBuffers.modelDepthTris;
                auto& modelDepthWorldTris = modelDepthBuffers.modelDepthWorldTris;
                std::size_t remainingModelTrianglesBudget = game::runtime::session_render_config::backendModelTriangleFrameBudget();
                runtime::shared_projected_units::PerfStats projectedUnitPerf{};

                if (!canCacheProjectedBackdrop) {
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
                const auto backdropComposeEnd = RenderBuildClock::now();
                worldBackdropComposeMsThisFrame = static_cast<float>(
                    std::chrono::duration<double, std::milli>(
                        backdropComposeEnd - backdropComposeStart).count());
                const float boardSurfaceY = 0.006f;

                using BackendPoseEval = game::runtime::shared_backend_pose::PoseEval;

                runtime::shared_projected_units::Args projectedUnitArgs;
                projectedUnitArgs.dataDb = &dataDb;
                projectedUnitArgs.gameWorld = gameWorld.get();
                projectedUnitArgs.worldCellSize = worldCellSize;
                projectedUnitArgs.minDim = minDim;
                projectedUnitArgs.boardSurfaceY = boardSurfaceY;
                projectedUnitArgs.lineThickness = line;
                projectedUnitArgs.supportsWorldTriangles3D = supportsWorldTriangles3D;
                projectedUnitArgs.supportsWorldIndexedMeshes = supportsWorldIndexedMeshes;
                projectedUnitArgs.characterInkingEnabled =
                    (services ? services->characterInkingEnabled : false);
                projectedUnitArgs.enableGpuClipSkinning = game::runtime::session_render_config::backendGpuClipSkinningEnabled(renderer);
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
                    return game::runtime::session_render_config::backendModelTriangleLimit();
                };
                projectedUnitArgs.backendModelFullMeshEnabled = [&]() {
                    return game::runtime::session_render_config::backendModelFullMeshEnabled();
                };
                projectedUnitArgs.backendModelFastTexturedPathEnabled = [&]() {
                    return game::runtime::session_render_config::backendModelFastTexturedPathEnabled();
                };
                projectedUnitArgs.backendModelBackfaceCullingEnabled = [&]() {
                    return game::runtime::session_render_config::backendModelBackfaceCullingEnabled();
                };
                projectedUnitArgs.getTailFireFallbackCfg = [&]() -> const TailFireVFX::Config& {
                    return game::runtime::shared_projected_scene::getTailFireFallbackCfg();
                };
                projectedUnitArgs.perfStats = &projectedUnitPerf;

                const bool hasActiveCaptureAttempts =
                    gameWorld->countActiveCaptureAttempts() > 0u;
                if (hasActiveCaptureAttempts) {
                    (void)sharedCaptureAttemptCache.refresh(gameWorld.get());
                }
                const auto& boardUnits = gameWorld->getPokemons();
                if (!boardUnits.empty()) {
                    game::runtime::shared_projected_units::drawProjectedUnits(
                        projectedUnitArgs, boardUnits);
                }
                const auto& benchUnits = gameWorld->getBenchPokemons();
                if (!benchUnits.empty()) {
                    game::runtime::shared_projected_units::drawProjectedUnits(
                        projectedUnitArgs, benchUnits);
                }
                const bool capturePrewarmRequested =
                    gameWorld->getSelectedItem() == "pokeball" ||
                    gameWorld->getItemCount("pokeball") > 0;
                if (hasActiveCaptureAttempts ||
                    capturePrewarmRequested ||
                    !sharedCaptureAttemptCache.snaps.empty()) {
                    (void)game::runtime::shared_projected_scene::appendSharedCaptureAttemptModelsIfNeededForProjectedWorld(
                        renderer, gameWorld.get(), supportsWorldIndexedMeshes, hasWorldViewProj, drawableW,
                        drawableH, worldCellSize, worldViewProj, cameraWorldPos, sharedCaptureAttemptCache,
                        worldIndexedBatches, backendTextureByPath,
                        [&](const std::string& path) { return ensureBackendMeshLoaded(path); },
                        [&](const std::string& path) { return ensureBackendTextureLoaded(path); });
                }
                const auto worldVfxStart = RenderBuildClock::now();
                game::runtime::shared_projected_scene::appendSharedProjectedVfxBridgesSession(
                    useLegacyParticleVfxSnapshotBridge, useLegacyGrowlWaveVfx, supportsWorldIndexedMeshes,
                    hasWorldViewProj, game::runtime::session_render_config::backendUseExactTailFireCpuPathEnabled(), gameWorld.get(), viewProj,
                    invViewProj, cameraWorldPos, drawableW, drawableH, worldCellSize, timeSource.nowSeconds(),
                    line, sharedTailFireAnchors, backendTextureByPath, worldIndexedBatches, projectedDebug,
                    [&](const std::string& meshPath) { return ensureBackendMeshLoaded(meshPath); },
                    [&](const std::string& texturePath, bool flipVertical) {
                        return ensureBackendTextureLoaded(texturePath, flipVertical);
                    });
                const auto worldVfxEnd = RenderBuildClock::now();
                worldVfxBridgeMsThisFrame = static_cast<float>(
                    std::chrono::duration<double, std::milli>(
                        worldVfxEnd - worldVfxStart).count());
                const auto depthFlushStart = RenderBuildClock::now();
                game::runtime::shared_projected_scene::flushModelDepthBuffers(
                    modelDepthTris,
                    modelDepthWorldTris,
                    worldTriangles,
                    world3DTriangles);
                const auto depthFlushEnd = RenderBuildClock::now();
                worldDepthFlushMsThisFrame = static_cast<float>(
                    std::chrono::duration<double, std::milli>(
                        depthFlushEnd - depthFlushStart).count());
                projectedUnitsMsThisFrame = static_cast<float>(projectedUnitPerf.totalMs);
                projectedPoseEvalMsThisFrame = static_cast<float>(projectedUnitPerf.poseEvalMs);
                projectedModelMsThisFrame = static_cast<float>(projectedUnitPerf.modelRenderMs);
                projectedModelPrepMsThisFrame = static_cast<float>(projectedUnitPerf.modelPrepMs);
                projectedModelGeometryMsThisFrame = static_cast<float>(projectedUnitPerf.modelGeometryMs);
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
        if (prewarmWorldIndexedOnly) {
            if (!worldIndexedBatches.empty() && hasWorldViewProj && supportsWorldIndexedMeshes) {
                return runtime::shared_world_batches::prewarmWorldIndexedBatches(
                    *renderer,
                    worldIndexedBatches,
                    cameraWorldPos3,
                    cameraForward3,
                    cameraTarget3);
            }
            return 0u;
        }
        const auto worldComposeEnd = RenderBuildClock::now();
        if (engineServices) {
            engineServices->frameVisibleAnimatedUnits = visibleAnimatedUnitsThisFrame;
            engineServices->frameParticleCount = particleCountThisFrame;
            engineServices->frameProjectedUnitsMs = projectedUnitsMsThisFrame;
            engineServices->frameProjectedPoseEvalMs = projectedPoseEvalMsThisFrame;
            engineServices->frameProjectedModelMs = projectedModelMsThisFrame;
            engineServices->frameProjectedModelPrepMs = projectedModelPrepMsThisFrame;
            engineServices->frameProjectedModelGeometryMs = projectedModelGeometryMsThisFrame;
            engineServices->frameProjectedOverlayMs = projectedOverlayMsThisFrame;
            engineServices->frameProjectedUnitsProcessed = projectedUnitsProcessedThisFrame;
            engineServices->frameProjectedModelUnits = projectedModelUnitsThisFrame;
            engineServices->frameProjectedClipSkinnedUnits = projectedClipSkinnedUnitsThisFrame;
            engineServices->frameRenderBuildBreakdown = {};
            const float totalWorldComposeMs = static_cast<float>(
                std::chrono::duration<double, std::milli>(
                    worldComposeEnd - worldComposeStart).count());
            engineServices->frameRenderBuildBreakdown.worldComposeMs =
                std::max(0.0f, totalWorldComposeMs - projectedUnitsMsThisFrame);
            engineServices->frameRenderBuildBreakdown.worldBackdropMs =
                worldBackdropComposeMsThisFrame;
            engineServices->frameRenderBuildBreakdown.worldVfxMs =
                worldVfxBridgeMsThisFrame;
            engineServices->frameRenderBuildBreakdown.worldDepthFlushMs =
                worldDepthFlushMsThisFrame;
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
        overlayArgs.refreshBackendInventoryFromWorld = [&]() {
            game::runtime::session_backend_inventory_ui::refreshPanel(
                backendInventoryPanel,
                kBackendInventoryVisibleCount,
                backendInventoryUiDependencies());
        };
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
        overlayArgs.renderBuildBreakdown =
            engineServices ? &engineServices->frameRenderBuildBreakdown : nullptr;
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
        return 0u;
    }

    void renderWorldLayer(int drawableW, int drawableH, bool renderWorld) {
        const runtime::render::RenderRoutes routes = activeRenderRoutes();
        if (routes.usesBackendRenderPath()) {
            (void)renderBackendDebugView(drawableW, drawableH, renderWorld);
        }
    }

    std::size_t prewarmWorldIndexedLayer(int drawableW, int drawableH, bool renderWorld) {
        const runtime::render::RenderRoutes routes = activeRenderRoutes();
        if (!routes.usesBackendRenderPath()) return 0u;
        return renderBackendDebugView(
            drawableW,
            drawableH,
            renderWorld,
            /*prewarmWorldIndexedOnly=*/true);
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
        game::runtime::world_layer_prewarm::maybeRunDeferredFrame(
            worldLayerPrewarmFramesRemaining,
            kWorldLayerPrewarmFrames,
            flow.renderWorldLayer,
            drawableW,
            drawableH,
            game::runtime::world_layer_prewarm::Callbacks{
                .setTitle = setTitleCallback,
                .renderWorldLayer =
                    [&](int prewarmW, int prewarmH) {
                        renderWorldLayer(prewarmW, prewarmH, /*renderWorld=*/true);
                    },
            },
            std::cout);
        renderFrameFromFlow(flow, drawableW, drawableH, renderWorld);
    }

    void shutdown() {
        std::cout << "[Shutdown] Game.\n";

        log.attach(nullptr);
        log.attachCatchFeed(nullptr);
        log.attachEconomyFeed(nullptr);
        shopSystem = nullptr;
        roundSystem = nullptr;
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


