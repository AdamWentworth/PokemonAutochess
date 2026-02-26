#include <string>

#include "engine/core/Paths.h"
#include "engine/core/Random.h"
#include "engine/core/TimeSources.h"

#include "game/GameConfig.h"
#include "game/GameServices.h"
#include "game/assets/DevAssetStore.h"
#include "game/config/GameDataDb.h"
#include "game/logging/LogBus.h"
#include "game/runtime/routes/GameServiceRenderRoutes.h"
#include "game/scripting/ScriptEventBus.h"

bool test_game_service_render_routes_contract(std::string& outFail) {
    GameConfigData cfg;
    GameDataDb db;
    LogBus::Logger log;
    log.setEchoToStdout(false);
    log.setFeedEnabled(false);

    ScriptEventBus events;
    game::assets::DevAssetStore assets(engine::paths::dataRoot());
    engine::XorShift32 rng(17u);
    engine::ManualTimeSource time;

    GameServices services(cfg, db, log, events, assets, rng, time);

    {
        services.renderEnabled = false;
        const auto routes = game::runtime::render::routesFromServices(services);
        if (routes.hasRenderer || routes.usesBackendRenderPath() || routes.usesBackendUiPath()) {
            outFail = "routesFromServices should report no active routes when renderEnabled is false";
            return false;
        }
    }

    {
        services.renderEnabled = true;
        const auto routes = game::runtime::render::routesFromServices(services);
        if (!routes.hasRenderer || !routes.usesBackendRenderPath() || !routes.usesBackendUiPath()) {
            outFail = "routesFromServices shared route mapping mismatch";
            return false;
        }
    }

    return true;
}
