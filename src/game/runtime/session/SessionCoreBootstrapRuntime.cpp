#include "game/runtime/session/SessionCoreBootstrapRuntime.h"

#include "engine/core/EngineServices.h"
#include "engine/core/Environment.h"
#include "engine/core/GameContext.h"
#include "engine/core/Paths.h"
#include "engine/core/Services.h"
#include "engine/core/Random.h"
#include "engine/core/TimeSources.h"
#include "engine/core/ecs/Scheduler.h"
#include "engine/core/ecs/World.h"
#include "game/GameConfig.h"
#include "game/GameServices.h"
#include "game/GameStateManager.h"
#include "game/GameWorld.h"
#include "game/PhaseState.h"
#include "game/assets/DevAssetStore.h"
#include "game/assets/PackedAssetStore.h"
#include "game/config/GameDataDb.h"
#include "game/logging/LogBus.h"
#include "game/logging/LoggerUtil.h"
#include "game/runtime/session/GameUpdateGraph.h"
#include "game/scripting/ScriptEventBus.h"
#include "game/systems/CameraSystem.h"
#include "game/systems/CombatSystem.h"
#include "game/systems/LegacySystemAdapters.h"
#include "game/systems/MovementSystem.h"
#include "game/systems/RoundSystem.h"
#include "game/systems/ShopSystem.h"
#include "game/systems/UnitInteractionSystem.h"
#include "game/ui/UIViewport.h"

#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <utility>

namespace game::runtime::session_core_bootstrap_runtime {

void run(const Args& args) {
    if (!args.ctx || !args.startupRoutes || !args.dataDb || !args.log || !args.scriptEvents ||
        !args.assetStore || !args.rng || !args.timeSource || !args.config ||
        !args.services || !args.viewport || !args.coreServices || !args.ecsWorld ||
        !args.roundPhaseEntity || !args.stateManager || !args.gameWorld ||
        !args.scheduler || !args.updateGraph || !args.cameraSystem || !args.unitSystem ||
        !args.shopSystem || !args.roundSystem) {
        return;
    }

    const std::string packPath = engine::paths::dataPack();
    if (!packPath.empty()) {
        auto pack = std::make_unique<assets::PackedAssetStore>();
        std::string err;
        if (pack->open(packPath, &err)) {
            *args.assetStore = std::move(pack);
            game::log::info(args.log, std::string("[Init] Using packed data bundle: ") + packPath);
        } else {
            game::log::warn(args.log, std::string("[Init] Failed to open pack: ") + packPath +
                (err.empty() ? "" : (" (" + err + ")")));
        }
    }
    if (!*args.assetStore) {
        auto dev = std::make_unique<assets::DevAssetStore>(engine::paths::dataRoot());
        *args.assetStore = std::move(dev);
    }

    {
        std::uint32_t seed = 0;
        bool hasSeed = false;
        if (const auto v = engine::env::get("PAC_RANDOM_SEED")) {
            try {
                seed = static_cast<std::uint32_t>(std::stoul(*v));
                hasSeed = true;
            } catch (...) {
                game::log::warn(args.log, std::string("[Init] Invalid PAC_RANDOM_SEED value: ") + *v);
            }
        }
        if (!hasSeed) {
            std::random_device rd;
            seed = (static_cast<std::uint32_t>(rd()) << 16) ^ static_cast<std::uint32_t>(rd());
        }
        args.rng->reseed(seed);
        game::log::info(args.log, std::string("[Init] RNG seed: ") + std::to_string(seed));
    }

    *args.roundPhaseEntity = args.ecsWorld->create();
    args.ecsWorld->add<game::CombatActive>(*args.roundPhaseEntity, game::CombatActive{false});

    *args.config = GameConfig::load(args.log, args.assetStore->get());
    *args.services = std::make_unique<GameServices>(
        *args.config,
        *args.dataDb,
        *args.log,
        *args.scriptEvents,
        **args.assetStore,
        *args.rng,
        *args.timeSource,
        args.ecsWorld,
        *args.roundPhaseEntity,
        args.viewport,
        args.startupRoutes->hasRenderer);
    (*args.services)->renderer = args.renderer;
    (*args.services)->engineServices = args.ctx->services;
    (*args.services)->applyVideoMode = args.ctx->applyVideoMode;
    (*args.services)->requestQuit = args.ctx->requestQuit;
    if (args.ctx->services) {
        (*args.services)->requestedRendererBackend = args.ctx->services->requestedRendererBackend;
        (*args.services)->activeRendererBackend = args.ctx->services->activeRendererBackend;
        (*args.services)->rendererBackendFallback = args.ctx->services->rendererBackendFallback;
        (*args.services)->gpuVendor = args.ctx->services->gpuVendor;
        (*args.services)->gpuRenderer = args.ctx->services->gpuRenderer;
        (*args.services)->availableGpuAdapters = args.ctx->services->availableGpuAdapters;
        (*args.services)->preferredGpuAdapter = args.ctx->services->preferredGpuAdapter;
        (*args.services)->gpuDiscrete = args.ctx->services->gpuDiscrete;
        (*args.services)->vsyncEnabled = args.ctx->services->vsyncEnabled;
        (*args.services)->fpsCap = args.ctx->services->fpsCap;
        (*args.services)->graphicsQuality = args.ctx->services->graphicsQuality;
        (*args.services)->graphicsQualityGeneration =
            args.ctx->services->graphicsQualityGeneration == 0u
                ? 1u
                : args.ctx->services->graphicsQualityGeneration;
        (*args.services)->requireDiscreteGpu = args.ctx->services->requireDiscreteGpu;
        (*args.services)->characterInkingEnabled = args.ctx->services->characterInkingEnabled;
        (*args.services)->audioMasterVolume = args.ctx->services->audioMasterVolume;
        (*args.services)->audioMusicVolume = args.ctx->services->audioMusicVolume;
        (*args.services)->audioSfxVolume = args.ctx->services->audioSfxVolume;
        (*args.services)->audioVoiceVolume = args.ctx->services->audioVoiceVolume;
        (*args.services)->audioMute = args.ctx->services->audioMute;
        (*args.services)->bootMenuScreen = args.ctx->services->bootMenuScreen;
    }
    if (args.ctx->queryVideoMode) {
        (*args.services)->queryVideoMode = [q = args.ctx->queryVideoMode]() {
            auto vm = q();
            GameServices::VideoMode out;
            out.width = vm.width;
            out.height = vm.height;
            out.fullscreen = vm.fullscreen;
            return out;
        };
    }
    args.coreServices->rng = &(*args.services)->rng;
    args.coreServices->time = &(*args.services)->time;

    *args.gameWorld = std::make_unique<GameWorld>(*args.config);
    (*args.gameWorld)->setRenderEnabled(args.startupRoutes->hasRenderer);
    (*args.gameWorld)->setLogger(args.log);
    (*args.gameWorld)->setRng(&(*args.services)->rng);
    if (args.ctx->services) (*args.gameWorld)->setResources(args.ctx->services->resources);
    (*args.gameWorld)->setData(args.dataDb);

    *args.stateManager = std::make_unique<GameStateManager>();

    if (args.camera) {
        *args.cameraSystem = std::make_shared<CameraSystem>(args.camera, **args.services);
        *args.unitSystem = std::make_shared<UnitInteractionSystem>(
            args.camera,
            args.gameWorld->get(),
            args.ctx->drawableW,
            args.ctx->drawableH);
    }
    using Phase = engine::ecs::Scheduler::Phase;

    if (*args.cameraSystem) {
        args.scheduler->add(
            std::make_unique<game::UpdatableSystemAdapter>(args.cameraSystem->get(), "camera"),
            Phase::Update);
    }
    if (*args.unitSystem) {
        args.scheduler->add(
            std::make_unique<game::UpdatableSystemAdapter>(args.unitSystem->get(), "unit_interaction"),
            Phase::Update);
    }
    auto shopSystemImpl = std::make_unique<ShopSystem>((*args.services)->rng);
    *args.shopSystem = shopSystemImpl.get();
    args.scheduler->add(std::move(shopSystemImpl), Phase::Update);

    auto roundSystemImpl = std::make_unique<RoundSystem>(**args.services, *args.roundPhaseEntity);
    *args.roundSystem = roundSystemImpl.get();
    args.ecsWorld->add<game::RoundState>(
        *args.roundPhaseEntity,
        game::RoundState{roundSystemImpl->getCurrentPhase()});
    args.scheduler->add(std::move(roundSystemImpl), Phase::Update);

    if (auto* stateMgr = args.stateManager->get()) {
        args.scheduler->add(std::make_unique<game::CallbackSystemAdapter>(
            [stateMgr, engineServices = args.engineServices](float dt) {
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
    if (auto* worldPtr = args.gameWorld->get()) {
        auto movementSystem = std::make_unique<MovementSystem>(worldPtr, **args.services, *args.roundPhaseEntity);
        args.scheduler->add(std::move(movementSystem), Phase::PostUpdate);

        auto combatSystem = std::make_unique<CombatSystem>(worldPtr, **args.services, *args.roundPhaseEntity);
        args.scheduler->add(std::move(combatSystem), Phase::PostUpdate);
    }
    if (auto* worldPtr = args.gameWorld->get()) {
        args.scheduler->add(std::make_unique<game::CallbackSystemAdapter>(
            [worldPtr](float dt) { worldPtr->update(dt); },
            "world"
        ), Phase::PostUpdate);
    }

    args.updateGraph->configure({
        args.scheduler,
        args.ecsWorld,
        *args.roundPhaseEntity,
        *args.shopSystem,
        args.log,
        args.scriptEvents,
        args.engineServices
    });
}

} // namespace game::runtime::session_core_bootstrap_runtime
