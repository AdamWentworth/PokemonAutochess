#pragma once

// Minimal "composition root" container for game-level services.
// Goal: pass dependencies explicitly to states/systems instead of reaching for singletons.
//
// This is intentionally small and non-owning (stores references).

#include "game/GameConfig.h"

// Forward decls (keep headers light)
struct GameDataDb;

namespace LogBus { struct Logger; }
class ScriptEventBus;
namespace engine { class IAssetStore; class IRandom; class ITimeSource; }

struct GameServices {
    const GameConfigData& config;
    GameDataDb& dataDb;
    LogBus::Logger& log;
    ScriptEventBus& events;
    engine::IAssetStore& assets;
    engine::IRandom& rng;
    engine::ITimeSource& time;

    GameServices(const GameConfigData& cfg,
                 GameDataDb& db,
                 LogBus::Logger& logger,
                 ScriptEventBus& eventBus,
                 engine::IAssetStore& assetStore,
                 engine::IRandom& random,
                 engine::ITimeSource& timeSource)
        : config(cfg)
        , dataDb(db)
        , log(logger)
        , events(eventBus)
        , assets(assetStore)
        , rng(random)
        , time(timeSource) {}
};
