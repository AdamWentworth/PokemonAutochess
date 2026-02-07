#include "game/scripting/ScriptEventBus.h"

void ScriptEventBus::emit(const std::string& type, const std::optional<std::string>& payload) {
    ScriptEvent evt;
    evt.type = type;
    if (payload.has_value() && !payload->empty()) {
        evt.payload = *payload;
        evt.hasPayload = true;
    }
    queue_.push_back(std::move(evt));
}

std::vector<ScriptEvent> ScriptEventBus::drain() {
    std::vector<ScriptEvent> out;
    out.swap(queue_);
    return out;
}

void ScriptEventBus::clear() {
    queue_.clear();
}