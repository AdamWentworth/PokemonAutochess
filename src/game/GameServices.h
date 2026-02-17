#pragma once

// Minimal "composition root" container for game-level services.
// Goal: pass dependencies explicitly to states/systems instead of reaching for singletons.
//
// This is intentionally small and non-owning (stores references).

#include "game/GameConfig.h"
#include "engine/core/ecs/Entity.h"
#include <functional>
#include <string>
#include <vector>

// Forward decls (keep headers light)
struct GameDataDb;

namespace LogBus { struct Logger; }
class ScriptEventBus;
namespace engine { class IAssetStore; class IRandom; class ITimeSource; }
namespace engine::ecs { class World; }
namespace game::ui { struct UIViewport; }

struct GameServices {
    struct VideoMode {
        int width = 1280;
        int height = 720;
        bool fullscreen = false;
    };

    const GameConfigData& config;
    GameDataDb& dataDb;
    LogBus::Logger& log;
    ScriptEventBus& events;
    engine::IAssetStore& assets;
    engine::IRandom& rng;
    engine::ITimeSource& time;
    engine::ecs::World* ecsWorld = nullptr;
    engine::ecs::Entity combatStateEntity{};
    game::ui::UIViewport* viewport = nullptr;
    bool renderEnabled = false;
    std::string gameMode = "classic";
    bool hasStartedGame = false;
    std::function<bool(int, int, bool)> applyVideoMode;
    std::function<VideoMode()> queryVideoMode;
    std::function<void()> requestQuit;

    std::string requestedRendererBackend = "auto";
    std::string activeRendererBackend = "opengl";
    bool rendererBackendFallback = false;
    std::string gpuVendor;
    std::string gpuRenderer;
    std::vector<std::string> availableGpuAdapters;
    std::string preferredGpuAdapter;
    bool gpuDiscrete = false;
    bool requireDiscreteGpu = false;

    GameServices(const GameConfigData& cfg,
                 GameDataDb& db,
                 LogBus::Logger& logger,
                 ScriptEventBus& eventBus,
                 engine::IAssetStore& assetStore,
                 engine::IRandom& random,
                 engine::ITimeSource& timeSource,
                 engine::ecs::World* ecsWorldPtr = nullptr,
                 engine::ecs::Entity combatEntity = {},
                 game::ui::UIViewport* viewportPtr = nullptr,
                 bool renderEnabled_ = false)
        : config(cfg)
        , dataDb(db)
        , log(logger)
        , events(eventBus)
        , assets(assetStore)
        , rng(random)
        , time(timeSource)
        , ecsWorld(ecsWorldPtr)
        , combatStateEntity(combatEntity)
        , viewport(viewportPtr)
        , renderEnabled(renderEnabled_) {}
};
