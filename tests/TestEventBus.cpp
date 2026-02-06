// tests/TestEventBus.cpp
#include <string>
#include <atomic>

#include "engine/events/EventBus.h"
#include "engine/events/Event.h"

bool test_eventbus_basic(std::string& outFail) {
    EventBus bus;

    std::atomic<int> calls{0};

    const auto subId = bus.subscribe(EventType::KeyDown, [&](const Event& e) {
        (void)e;
        ++calls;
    });

    Event ev(EventType::KeyDown);
    bus.emit(ev);

    if (calls.load() != 1) {
        outFail = "listener not called exactly once after emit()";
        return false;
    }

    const bool removed = bus.unsubscribe(EventType::KeyDown, subId);
    if (!removed) {
        outFail = "unsubscribe() returned false";
        return false;
    }

    bus.emit(ev);
    if (calls.load() != 1) {
        outFail = "listener called after unsubscribe()";
        return false;
    }

    return true;
}
