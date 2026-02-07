// src/engine/events/EventManager.h
//
// Drop-in replacement (keeps legacy API):
//   - EventManager::getInstance()
//   - subscribe(EventType, Listener)
//   - emit(const Event&)
//
// Adds:
//   - subscribeToken(.), unsubscribe(.)
//   - subscribeScoped(.): returns RAII Subscription
//   - setDefaultBus(EventBus*): allow Application to inject the engine-owned bus
//
// Stage 3 prep (Remove globals):
// - Legacy calls should be routed to an injected bus.
// - To avoid shared-state collisions in parallel tests before full migration,
//   the fallback bus (when not injected) is **thread_local** instead of process-global.
//
// NOTE: Stage 3 exit criteria still requires removing getInstance() usage from core gameplay.

#pragma once

#include "Event.h"
#include "EventBus.h"
#include "Subscription.h"

#include <functional>
#include <utility>

class EventManager {
public:
    using Listener = std::function<void(const Event&)>;
    using SubscriptionId = EventBus::SubscriptionId;

    static EventManager& getInstance() {
        static EventManager instance;
        return instance;
    }

    // Application should call this once during startup to route legacy calls to the engine-owned bus.
    static void setDefaultBus(EventBus* bus) {
        getInstance().externalBus_ = bus;
    }

    // Legacy API: subscribe without a handle.
    void subscribe(EventType type, const Listener& listener) {
        (void)bus().subscribe(type, listener);
    }

    // New API: get a token you can later unsubscribe with.
    SubscriptionId subscribeToken(EventType type, Listener listener) {
        return bus().subscribe(type, std::move(listener));
    }

    // New API: scoped subscription (auto-unsubscribe).
    Subscription subscribeScoped(EventType type, Listener listener) {
        const auto id = bus().subscribe(type, std::move(listener));
        return Subscription(&bus(), type, id);
    }

    bool unsubscribe(EventType type, SubscriptionId id) {
        return bus().unsubscribe(type, id);
    }

    void emit(const Event& event) const {
        bus().emit(event);
    }

    void clear() {
        bus().clear();
    }

private:
    EventManager() = default;
    EventManager(const EventManager&) = delete;
    EventManager& operator=(const EventManager&) = delete;

    // Thread-local fallback bus: avoids cross-test / cross-sim shared state.
    static EventBus& threadLocalFallback() {
        static thread_local EventBus bus;
        return bus;
    }

    EventBus& bus() const {
        return externalBus_ ? *externalBus_ : threadLocalFallback();
    }

    mutable EventBus* externalBus_ = nullptr; // not owning
};
