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

struct Counter { int value = 0; };
struct Tag { int x = 0; };

class CounterSystem final : public ecs::ISystem {
public:
    void update(ecs::World& world, float /*dt*/) override {
        world.each<Counter>([](ecs::Entity, Counter& c){ c.value += 1; });
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

bool test_ecs_destroy_cleans_components(std::string& outFail) {
    StdoutLogger log;
    EventBus bus;
    CoreServices svc;
    svc.log = &log;
    svc.events = &bus;

    ecs::World world(&svc);

    auto e = world.create();
    world.components<Counter>().emplace(e, Counter{123});
    world.components<Tag>().emplace(e, Tag{7});

    world.destroy(e);

    if (world.components<Counter>().get(e) != nullptr) {
        outFail = "Counter component still present after World::destroy()";
        return false;
    }
    if (world.components<Tag>().get(e) != nullptr) {
        outFail = "Tag component still present after World::destroy()";
        return false;
    }
    return true;
}
