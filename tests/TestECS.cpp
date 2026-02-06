// tests/TestECS.cpp
#include <string>
#include <memory>

#include "engine/core/Services.h"
#include "engine/core/StdoutLogger.h"
#include "engine/core/EventBus.h"

#include "engine/core/ecs/World.h"
#include "engine/core/ecs/Scheduler.h"

using namespace engine;

namespace {

struct Counter {
    int value = 0;
};

class CounterSystem final : public ecs::ISystem {
public:
    void update(ecs::World& world, float /*dt*/) override {
        auto& store = world.components<Counter>();
        for (auto& kv : store.raw()) {
            kv.second.value += 1;
        }
    }
};

} // namespace

bool test_ecs_smoke(std::string& outFail) {
    StdoutLogger log;
    EventBus bus;
    CoreServices svc;
    svc.log = &log;
    svc.events = &bus;

    ecs::World world(&svc);
    ecs::Scheduler sched;
    sched.add(std::make_unique<CounterSystem>());

    auto e = world.create();
    world.components<Counter>().emplace(e, Counter{0});

    for (int i = 0; i < 10; ++i) {
        sched.tick(world, 1.0f / 60.0f);
    }

    auto* c = world.components<Counter>().get(e);
    if (!c) {
        outFail = "Counter component missing after emplace()";
        return false;
    }
    if (c->value != 10) {
        outFail = "Counter value expected 10, got " + std::to_string(c->value);
        return false;
    }
    return true;
}
