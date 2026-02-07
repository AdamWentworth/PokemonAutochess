#pragma once

#include <optional>
#include <string>
#include <vector>

struct ScriptEvent {
    std::string type;
    std::string payload;
    bool hasPayload = false;
};

class ScriptEventBus {
public:
    void emit(const std::string& type, const std::optional<std::string>& payload);
    std::vector<ScriptEvent> drain();
    void clear();
    const std::vector<ScriptEvent>& peek() const { return queue_; }

private:
    std::vector<ScriptEvent> queue_;
};