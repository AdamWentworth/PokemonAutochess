// LogBus.h
#pragma once

#include <string>
#include <glm/glm.hpp>

class BattleFeed;

namespace LogBus {

// Instance-based logger (preferred).
// This replaces file-scope globals with explicit ownership (e.g., GameApp owns a Logger).
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

    // stdout only; never touches BattleFeed
    void infoTerminalOnly(const std::string& s);

private:
    void push(const std::string& s, const glm::vec3& c, float life = 3.f);

private:
    BattleFeed* feed_ = nullptr;
    bool echo_ = true;
    bool feed_enabled_ = true;
};

// Active logger selection.
// During migration, the global LogBus::* functions delegate to the active logger.
// If none is set, a process-wide default Logger is used.
void setActive(Logger* logger);

// Compatibility API (existing call sites).
void attach(BattleFeed* feed);
void info(const std::string& s);
void warn(const std::string& s);
void error(const std::string& s);
void colored(const std::string& s, const glm::vec3& rgb, float lifetime = 3.f);

void setEchoToStdout(bool enabled);
void setFeedEnabled(bool enabled);
void infoTerminalOnly(const std::string& s);

} // namespace LogBus
