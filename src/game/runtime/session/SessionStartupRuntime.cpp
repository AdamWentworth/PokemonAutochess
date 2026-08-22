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
#include "game/scripting/LuaScript.h"
#include "game/runtime/startup/RuntimeUiCardPrewarm.h"
#include "game/runtime/startup/RuntimeWorldLayerPrewarm.h"
#include "game/state/scripted/ScriptedState.h"
#include "game/world/MoveImpactMath.h"
#include "game/world/GameWorld.h"

#include <filesystem>
#include <array>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

namespace game::runtime::session_startup_runtime {

namespace {

constexpr char kSharedCapturePokeballPath[] = "assets/models/pokeball.glb";
constexpr std::array<const char*, 9> kStartupScriptPrewarmPaths = {
    "scripts/states/main_menu.lua",
    "scripts/states/starter.lua",
    "scripts/ui/starter_menu.lua",
    "scripts/states/flow.lua",
    "scripts/states/route1.lua",
    "scripts/states/shared/combat_route_shared.lua",
    "scripts/states/shared/route_catalog.lua",
    "scripts/states/shared/mode_utils.lua",
    "scripts/states/shared/round_economy.lua",
};

} // namespace

bool shouldPreloadRenderModelCache(
    const GameContext& context) {
    return !context.deferBulkModelPrewarm &&
        game::runtime::session_render_config::
            backendPreloadModelCacheEnabled();
}

void run(const Args& args) {
    if (!args.ctx || !args.dataDb || !args.config || !args.services ||
        !args.gameWorld || !args.stateManager || !args.log ||
        !args.worldLayerPrewarmFramesRemaining || !args.usesBackendGameRenderPath) {
        return;
    }

    engine::log::Sink log("SessionStartup", &std::cout, &std::cerr);

    log.info("[Init] Shared gameplay render path: using render model cache loader.");
    if (shouldPreloadRenderModelCache(*args.ctx)) {
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
        modelPathsToPreload.reserve(args.dataDb->pokemon.all().size() + 1u);
        std::vector<std::string> moveImpactModelPathsToPreload;
        moveImpactModelPathsToPreload.reserve(args.dataDb->pokemon.all().size());
        std::unordered_set<std::string> seenModelPaths;
        seenModelPaths.reserve(args.dataDb->pokemon.all().size() + 1u);
        for (const auto& [name, stats] : args.dataDb->pokemon.all()) {
            (void)name;
            for (const auto& [variant, model] : stats.modelVariants) {
                (void)variant;
                if (model.empty()) continue;
                const std::string modelPath = "assets/models/" + model;
                if (seenModelPaths.insert(modelPath).second) {
                    modelPathsToPreload.push_back(modelPath);
                    moveImpactModelPathsToPreload.push_back(modelPath);
                }
            }
        }
        if (!engine::env::equals("PAC_BACKEND_PRELOAD_CAPTURE_POKEBALL", "0")) {
            if (seenModelPaths.insert(kSharedCapturePokeballPath).second) {
                modelPathsToPreload.push_back(kSharedCapturePokeballPath);
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
        std::vector<std::pair<std::string, const render_model::MeshData*>> moveImpactPrewarmModels;
        moveImpactPrewarmModels.reserve(moveImpactModelPathsToPreload.size());
        for (const std::string& modelPath : moveImpactModelPathsToPreload) {
            const auto load = args.loadModel(modelPath);
            if (!load.error.empty() || !load.mesh) {
                continue;
            }
            moveImpactPrewarmModels.emplace_back(modelPath, load.mesh);
        }
        const auto moveImpactStats =
            prewarmMoveImpactModelPaths(moveImpactPrewarmModels);
        log.info("[Init] Backend move impact prewarm complete: meshes=" +
                 std::to_string(moveImpactStats.meshesWarmed) +
                 " growl_anchor_models=" +
                 std::to_string(moveImpactStats.growlAnchorModelsWarmed));
    } else if (args.ctx->deferBulkModelPrewarm) {
        log.info(
            "[Init] Shared gameplay render path: bulk model cache prewarm "
            "deferred for embedded preview; visible models load on demand.");
    } else {
        log.info("[Init] Shared gameplay render path: render model cache preload disabled.");
    }

    if (args.usesBackendGameRenderPath() &&
        args.renderer &&
        args.renderer->backendId() &&
        std::string(args.renderer->backendId()) == "opengl" &&
        args.engineServices &&
        args.engineServices->resources) {
        (void)args.engineServices->resources->getModel(kSharedCapturePokeballPath);
    }

    {
        std::vector<std::string> scriptPaths;
        scriptPaths.reserve(kStartupScriptPrewarmPaths.size());
        for (const char* path : kStartupScriptPrewarmPaths) {
            scriptPaths.emplace_back(path);
        }
        const auto scriptPrewarmStats =
            LuaScript::prewarmScriptSources(*args.services, scriptPaths);
        log.info("[Init] Script source prewarm complete: warmed=" +
                 std::to_string(scriptPrewarmStats.warmed) +
                 " failed=" +
                 std::to_string(scriptPrewarmStats.failed));
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
        if (game::runtime::session_render_config::backendPrewarmAuthoredVfxEnabled(entry.kind)) {
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

    if (const auto editorMode =
            engine::env::get("PAC_EDITOR_GAME_MODE")) {
        if (*editorMode == "classic" ||
            *editorMode == "adventure") {
            args.services->gameMode = *editorMode;
            log.info(
                "[EditorPreview] Game mode: " +
                args.services->gameMode);
        } else {
            log.warn(
                "[EditorPreview] Ignoring unsupported game mode: " +
                *editorMode);
        }
    }

    std::string startupStatePath =
        "scripts/states/main_menu.lua";
    if (const auto editorStartState =
            engine::env::get("PAC_EDITOR_START_STATE")) {
        if (*editorStartState == "starter") {
            startupStatePath = "scripts/states/starter.lua";
        } else if (*editorStartState != "main_menu") {
            log.warn(
                "[EditorPreview] Ignoring unsupported start state: " +
                *editorStartState);
        }
        log.info(
            "[EditorPreview] Start state: " +
            *editorStartState);
    }

    args.stateManager->pushState(std::make_unique<ScriptedState>(
        args.stateManager,
        args.gameWorld,
        *args.services,
        engine::paths::data(startupStatePath)
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
