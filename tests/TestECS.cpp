// tests/TestECS.cpp
#include <string>
#include <memory>
#include <vector>
#include <algorithm>

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
    if (!c) { outFail = "Counter component missing after emplace()"; return false; }
    if (c->value != 10) { outFail = "Counter value expected 10, got " + std::to_string(c->value); return false; }
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

    if (world.components<Counter>().get(e) != nullptr) { outFail = "Counter still present after destroy"; return false; }
    if (world.components<Tag>().get(e) != nullptr) { outFail = "Tag still present after destroy"; return false; }
    return true;
}

bool test_ecs_for_each_join(std::string& outFail) {
    StdoutLogger log;
    EventBus bus;
    CoreServices svc;
    svc.log = &log;
    svc.events = &bus;

    ecs::World world(&svc);

    // e1 has both Counter+Tag
    auto e1 = world.create();
    world.components<Counter>().emplace(e1, Counter{1});
    world.components<Tag>().emplace(e1, Tag{10});

    // e2 has only Counter
    auto e2 = world.create();
    world.components<Counter>().emplace(e2, Counter{2});

    // e3 has only Tag
    auto e3 = world.create();
    world.components<Tag>().emplace(e3, Tag{30});

    std::vector<std::uint32_t> seen;
    int sum = 0;

    world.for_each<Counter, Tag>([&](ecs::Entity e, Counter& c, Tag& t) {
        seen.push_back(e.id);
        sum += c.value + t.x;
    });

    std::sort(seen.begin(), seen.end());

    if (seen.size() != 1 || seen[0] != e1.id) {
        outFail = "for_each<Counter,Tag> should visit exactly e1";
        return false;
    }
    if (sum != (1 + 10)) {
        outFail = "for_each join sum mismatch";
        return false;
    }

    return true;
}
