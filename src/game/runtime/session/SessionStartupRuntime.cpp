#include "game/runtime/session/SessionStartupRuntime.h"

#include "engine/core/EngineServices.h"
#include "engine/core/Environment.h"
#include "engine/core/GameContext.h"
#include "engine/core/Paths.h"
#include "engine/render/IRenderBackend.h"
#include "engine/utils/LogSink.h"
#include "engine/utils/ResourceManager.h"
#include "game/GameConfig.h"
#include "game/GameServices.h"
#include "game/GameStateManager.h"
#include "game/config/GameDataDb.h"
#include "game/logging/LogBus.h"
#include "game/runtime/session/SessionRenderConfig.h"
#include "game/runtime/startup/RuntimeUiCardPrewarm.h"
#include "game/runtime/startup/RuntimeWorldLayerPrewarm.h"
#include "game/state/scripted/ScriptedState.h"
#include "game/world/GameWorld.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

namespace game::runtime::session_startup_runtime {

void run(const Args& args) {
    if (!args.ctx || !args.dataDb || !args.config || !args.services ||
        !args.gameWorld || !args.stateManager || !args.log ||
        !args.worldLayerPrewarmFramesRemaining || !args.usesBackendGameRenderPath) {
        return;
    }

    engine::log::Sink log("SessionStartup", &std::cout, &std::cerr);

    log.info("[Init] Shared gameplay render path: using render model cache loader.");
    if (game::runtime::session_render_config::backendPreloadModelCacheEnabled()) {
        log.info("[Init] Shared gameplay render path: preloading render model cache...");
        const bool prewarmModelTextures =
            args.usesBackendGameRenderPath() &&
            args.renderer &&
            args.renderer->supportsWorldIndexedMeshes() &&
            game::runtime::session_render_config::backendPrewarmModelTexturesEnabled();
        const bool prewarmModelGeometry =
            args.usesBackendGameRenderPath() &&
            args.renderer &&
            args.renderer->supportsWorldIndexedMeshes() &&
            game::runtime::session_render_config::backendModelFullMeshEnabled() &&
            game::runtime::session_render_config::backendModelFastTexturedPathEnabled() &&
            game::runtime::session_render_config::backendPrewarmModelGeometryEnabled();
        std::vector<std::string> modelPathsToPreload;
        modelPathsToPreload.reserve(args.dataDb->pokemon.all().size());
        std::unordered_set<std::string> seenModelPaths;
        seenModelPaths.reserve(args.dataDb->pokemon.all().size());
        for (const auto& [name, stats] : args.dataDb->pokemon.all()) {
            (void)name;
            if (stats.model.empty()) continue;
            const std::string modelPath = "assets/models/" + stats.model;
            if (seenModelPaths.insert(modelPath).second) {
                modelPathsToPreload.push_back(modelPath);
            }
        }
        if (!engine::env::equals("PAC_BACKEND_PRELOAD_CAPTURE_POKEBALL", "0")) {
            const std::string sharedCapturePokeballPath = "assets/models/pokeball.glb";
            if (seenModelPaths.insert(sharedCapturePokeballPath).second) {
                modelPathsToPreload.push_back(sharedCapturePokeballPath);
            }
        }
        const render_model_prewarm::Options prewarmOptions{
            .verboseModelCacheLog = game::runtime::session_render_config::backendModelVerboseLoggingEnabled(),
            .prewarmAnimRoles = game::runtime::session_render_config::backendPrewarmAnimRolesEnabled(),
            .prewarmModelTextures = prewarmModelTextures,
            .prewarmModelGeometry = prewarmModelGeometry,
            .maxFailureSamples = 8u,
        };
        (void)game::runtime::render_model_prewarm::run(
            modelPathsToPreload,
            prewarmOptions,
            game::runtime::render_model_prewarm::Callbacks{
                .setTitle = args.ctx->setTitle,
                .renderBootLoading = args.ctx->renderBootLoading,
                .pumpPreloadEvents = args.ctx->pumpPreloadEvents,
                .requestQuit = args.ctx->requestQuit,
                .loadModel = args.loadModel,
                .prewarmAnimRoles = args.prewarmAnimRoles,
                .prewarmTextures = args.prewarmTextures,
                .prewarmGeometry = args.prewarmGeometry,
            },
            log);
    } else {
        log.info("[Init] Shared gameplay render path: render model cache preload disabled.");
    }

    if (args.usesBackendGameRenderPath() &&
        args.renderer &&
        args.renderer->backendId() &&
        std::string(args.renderer->backendId()) == "opengl" &&
        args.engineServices &&
        args.engineServices->resources) {
        (void)args.engineServices->resources->getModel("assets/models/pokeball.glb");
    }

    const bool usesBackendPathForStartupPrewarm = args.usesBackendGameRenderPath() && args.renderer;
    std::vector<std::string> uiSpritePrewarmPaths;
    if (usesBackendPathForStartupPrewarm &&
        game::runtime::session_render_config::backendUiSpritePrewarmEnabled()) {
        uiSpritePrewarmPaths =
            game::runtime::startup_asset_prewarm::collectUiSpritePrewarmPaths(*args.dataDb);
    }

    std::vector<game::runtime::startup_asset_prewarm::AuthoredVfxPrewarmEntry> enabledAuthoredVfx;
    enabledAuthoredVfx.reserve(args.prewarmAuthoredVfx.size());
    for (const auto& entry : args.prewarmAuthoredVfx) {
        if (!entry.prewarm) continue;
        bool enabled = false;
        switch (entry.kind) {
        case game::runtime::startup_asset_prewarm::AuthoredVfxKind::Growl:
            enabled = game::runtime::session_render_config::backendPrewarmGrowlVfxEnabled();
            break;
        case game::runtime::startup_asset_prewarm::AuthoredVfxKind::Tackle:
            enabled = game::runtime::session_render_config::backendPrewarmTackleVfxEnabled();
            break;
        }
        if (enabled) {
            enabledAuthoredVfx.push_back(entry);
        }
    }

    (void)game::runtime::startup_asset_prewarm::run(
        game::runtime::startup_asset_prewarm::Options{
            .usesBackendRenderPath = usesBackendPathForStartupPrewarm,
            .uiSpritePrewarmEnabled = game::runtime::session_render_config::backendUiSpritePrewarmEnabled(),
            .drawableW = args.ctx->drawableW,
            .drawableH = args.ctx->drawableH,
        },
        uiSpritePrewarmPaths,
        game::runtime::startup_asset_prewarm::Callbacks{
            .setTitle = args.ctx->setTitle,
            .renderBootLoading = args.ctx->renderBootLoading,
            .pumpPreloadEvents = args.ctx->pumpPreloadEvents,
            .requestQuit = args.ctx->requestQuit,
            .prewarmWorldShading =
                [&]() {
                    if (args.renderer) {
                        args.renderer->prewarmWorldRenderAssets();
                    }
                },
            .prewarmTailFire = args.prewarmTailFire,
            .prewarmAuthoredVfx = enabledAuthoredVfx,
            .prewarmParticleVfx =
                game::runtime::session_render_config::backendPrewarmParticleVfxEnabled()
                ? args.prewarmParticleVfx
                : std::function<startup_asset_prewarm::ParticleVfxStats()>{},
            .prewarmSpriteTextures =
                [&](const std::vector<std::string>& texturePaths) {
                    if (!args.renderer || texturePaths.empty()) return;
                    std::vector<const char*> rawPaths;
                    rawPaths.reserve(texturePaths.size());
                    for (const std::string& path : texturePaths) {
                        rawPaths.push_back(path.c_str());
                    }
                    args.renderer->prewarmDebugSpriteTextures(rawPaths.data(), rawPaths.size());
                },
            .prewarmBackendCardUi =
                [&](int drawableW,
                    int drawableH,
                    const std::vector<std::string>& texturePaths) {
                    (void)game::runtime::ui_card_prewarm::run(
                        args.renderer,
                        drawableW,
                        drawableH,
                        texturePaths);
                },
        },
        log);

    if (args.usesBackendGameRenderPath() &&
        args.renderer &&
        game::runtime::session_render_config::backendWorldLayerPrewarmEnabled()) {
        game::runtime::world_layer_prewarm::schedule(
            *args.worldLayerPrewarmFramesRemaining,
            args.worldLayerPrewarmFrameCount,
            game::runtime::world_layer_prewarm::Callbacks{
                .setTitle = args.ctx->setTitle,
                .renderBootLoading = args.ctx->renderBootLoading,
                .pumpPreloadEvents = args.ctx->pumpPreloadEvents,
                .requestQuit = args.ctx->requestQuit,
                .renderWorldLayer =
                    [&](int drawableW, int drawableH) {
                        args.renderWorldLayer(drawableW, drawableH);
                    },
            },
            log);
    }

    args.stateManager->pushState(std::make_unique<ScriptedState>(
        args.stateManager,
        args.gameWorld,
        *args.services,
        engine::paths::data("scripts/states/main_menu.lua")
    ));

    if (*args.worldLayerPrewarmFramesRemaining > 0 &&
        args.ctx->drawableW > 0 &&
        args.ctx->drawableH > 0 &&
        args.usesBackendGameRenderPath() &&
        args.renderer) {
        game::runtime::world_layer_prewarm::drainStartupFrames(
            *args.worldLayerPrewarmFramesRemaining,
            args.worldLayerPrewarmFrameCount,
            args.ctx->drawableW,
            args.ctx->drawableH,
            game::runtime::world_layer_prewarm::Callbacks{
                .setTitle = args.ctx->setTitle,
                .renderBootLoading = args.ctx->renderBootLoading,
                .pumpPreloadEvents = args.ctx->pumpPreloadEvents,
                .requestQuit = args.ctx->requestQuit,
                .renderWorldLayer =
                    [&](int drawableW, int drawableH) {
                        args.renderWorldLayer(drawableW, drawableH);
                    },
            },
            log);
    }

    game::runtime::world_layer_prewarm::restoreTitleAfterInit(
        *args.worldLayerPrewarmFramesRemaining,
        game::runtime::world_layer_prewarm::Callbacks{
            .setTitle = args.ctx->setTitle,
        });
    if (!args.snapshotPath.empty() && std::filesystem::exists(args.snapshotPath)) {
        if (args.autoLoadSnapshotOnStartup) {
            log.info("[StateSnapshot] Snapshot present and will auto-load on startup: " +
                     args.snapshotPath);
        } else {
            log.info("[StateSnapshot] Snapshot present but not auto-loaded: " +
                     args.snapshotPath + " (press F9 to restore)");
        }
    }
    log.info("[Init] Game initialized.");

    if (engine::env::get("PAC_LOG_ECHO_STDOUT").has_value()) {
        args.log->setEchoToStdout(engine::env::flagEnabled("PAC_LOG_ECHO_STDOUT"));
    } else {
        args.log->setEchoToStdout(false);
    }
}

} // namespace game::runtime::session_startup_runtime
