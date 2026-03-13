// tests/TestRoundFlow.cpp
#include <string>

#include "engine/core/Random.h"
#include "engine/core/TimeSources.h"
#include "engine/core/Services.h"
#include "engine/core/Paths.h"
#include "engine/core/ecs/World.h"
#include "engine/core/ecs/Scheduler.h"

#include "game/GameConfig.h"
#include "game/GameServices.h"
#include "game/runtime/session/GameUpdateGraph.h"
#include "game/ecs/RoundState.h"
#include "game/scripting/ScriptEventBus.h"
#include "game/assets/DevAssetStore.h"
#include "game/config/GameDataDb.h"
#include "game/logging/LogBus.h"
#include "game/systems/RoundSystem.h"
#include "game/systems/ShopSystem.h"

bool test_round_flow_headless(std::string& outFail) {
    GameConfigData cfg;
    GameDataDb db;
    LogBus::Logger log;
    log.setEchoToStdout(false);
    log.setFeedEnabled(false);

    ScriptEventBus events;
    game::assets::DevAssetStore assets(engine::paths::dataRoot());
    engine::XorShift32 rng(123u);
    engine::ManualTimeSource time;

    GameServices services(cfg, db, log, events, assets, rng, time);

    engine::CoreServices core;
    core.rng = &services.rng;
    core.time = &services.time;

    engine::ecs::World world(&core);
    engine::ecs::Entity phaseEntity = world.create();

    engine::ecs::Scheduler scheduler;

    auto shop = std::make_unique<ShopSystem>(services.rng);
    ShopSystem* shopPtr = shop.get();
    scheduler.add(std::move(shop), engine::ecs::Scheduler::Phase::Update);

    auto roundSys = std::make_unique<RoundSystem>(services, phaseEntity);
    RoundSystem* roundPtr = roundSys.get();
    world.add<game::RoundState>(phaseEntity, game::RoundState{ roundPtr->getCurrentPhase() });
    scheduler.add(std::move(roundSys), engine::ecs::Scheduler::Phase::Update);

    game::GameUpdateGraph graph;
    graph.configure({
        &scheduler,
        &world,
        phaseEntity,
        shopPtr,
        &log,
        &events
    });

    RoundPhase startPhase = world.get<game::RoundState>(phaseEntity)->phase;
    bool sawBattle = false;
    bool sawResolution = false;

    constexpr float dt = 0.25f;
    constexpr int maxSteps = 200; // 50 seconds total

    RoundPhase last = startPhase;
    for (int i = 0; i < maxSteps; ++i) {
        graph.tick(dt);
        auto* state = world.get<game::RoundState>(phaseEntity);
        if (!state) {
            outFail = "RoundState missing during tick.";
            return false;
        }
        if (state->phase != last) {
            last = state->phase;
        }
        if (state->phase == RoundPhase::Battle) sawBattle = true;
        if (state->phase == RoundPhase::Resolution) sawResolution = true;
        if (sawBattle && sawResolution) break;
    }

    if (!sawBattle) {
        outFail = "Did not transition to Battle phase.";
        return false;
    }
    if (!sawResolution) {
        outFail = "Did not transition to Resolution phase.";
        return false;
    }

    auto drained = events.drain();
    int phaseEvents = 0;
    for (const auto& e : drained) {
        if (e.type == "round_phase_changed") ++phaseEvents;
    }
    if (phaseEvents < 2) {
        outFail = "Expected round_phase_changed events to be emitted.";
        return false;
    }

    return true;
}

