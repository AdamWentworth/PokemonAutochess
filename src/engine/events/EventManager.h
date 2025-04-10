// EventManager.h

#pragma once
#include "Event.h"
#include <functional>
#include <unordered_map>
#include <vector>
#include <memory>

class EventManager {
public:
    // Define a listener type that takes an Event reference.
    using Listener = std::function<void(const Event&)>;

    // Get the singleton instance.
    static EventManager& getInstance() {
        static EventManager instance;
        return instance;
    }

    // Subscribe a listener to a given event type.
    void subscribe(EventType type, const Listener& listener) {
        listeners[type].push_back(listener);
    }

    // Emit an event to all registered listeners for its type.
    void emit(const Event& event) const {
        auto it = listeners.find(event.getType());
        if (it != listeners.end()) {
            for (const auto& listener : it->second) {
                listener(event);
            }
        }
    }

private:
    // Private constructor for singleton pattern.
    EventManager() = default;
    EventManager(const EventManager&) = delete;
    EventManager& operator=(const EventManager&) = delete;

    // Map event type to list of listeners.
    std::unordered_map<EventType, std::vector<Listener>> listeners;
};
