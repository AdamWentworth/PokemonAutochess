#include <string>

#include "engine/core/Paths.h"
#include "engine/core/Random.h"
#include "engine/core/TimeSources.h"

#include "game/GameConfig.h"
#include "game/GameServices.h"
#include "game/assets/DevAssetStore.h"
#include "game/config/GameDataDb.h"
#include "game/logging/LogBus.h"
#include "game/scripting/ScriptEventBus.h"

bool test_game_services_route_helpers(std::string& outFail) {
    GameConfigData cfg;
    GameDataDb db;
    LogBus::Logger log;
    log.setEchoToStdout(false);
    log.setFeedEnabled(false);

    ScriptEventBus events;
    game::assets::DevAssetStore assets(engine::paths::dataRoot());
    engine::XorShift32 rng(11u);
    engine::ManualTimeSource time;

    GameServices services(cfg, db, log, events, assets, rng, time);

    services.renderEnabled = false;
    if (services.usesBackendGameRenderPath()) {
        outFail = "backend render path should be false when renderEnabled is false";
        return false;
    }
    if (services.usesBackendGameUiPath()) {
        outFail = "backend UI path should be false when renderEnabled is false";
        return false;
    }

    services.renderEnabled = true;
    if (!services.usesBackendGameRenderPath()) {
        outFail = "backend render path should be true when renderEnabled is true";
        return false;
    }
    if (!services.usesBackendGameUiPath()) {
        outFail = "backend UI path should be true when renderEnabled is true";
        return false;
    }

    return true;
}
