// src/game/logging/LogBus.h
#pragma once

#include <string>
#include <glm/glm.hpp>

class BattleFeed;

namespace LogBus {

// Instance-based logger (preferred).
// Composition root should own a Logger and wire it explicitly.
class Logger {
public:
    void attach(BattleFeed* feed) { feed_ = feed; }

    void info(const std::string& s)  { push(s, {1,1,1}); }
    void warn(const std::string& s)  { push(std::string("[WARN] ")  + s, {1,0.9f,0.2f}); }
    void error(const std::string& s) { push(std::string("[ERROR] ") + s, {1,0.3f,0.3f}); }

    void colored(const std::string& s, const glm::vec3& rgb, float lifetime = 3.f) {
        push(s, rgb, lifetime);
    }

    // stdout mirroring toggle (default: on)
    void setEchoToStdout(bool enabled) { echo_ = enabled; }

    // on-screen feed toggle (default: on)
    void setFeedEnabled(bool enabled) { feed_enabled_ = enabled; }

    // stdout only
    void infoTerminalOnly(const std::string& s);

private:
    void push(const std::string& s, const glm::vec3& c, float life = 3.f);

private:
    BattleFeed* feed_ = nullptr;
    bool echo_ = true;
    bool feed_enabled_ = true;
};

} // namespace LogBus
