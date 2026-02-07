// src/game/logging/LogBus.cpp
#include "LogBus.h"

#include "engine/ui/BattleFeed.h"
#include <iostream>

namespace LogBus {

// Stage 3 prep: thread-local active logger pointer, so parallel tests/sims don't collide.
static thread_local Logger* t_active = nullptr;

// Thread-local default instance (used only if no active logger is set in this thread).
static Logger& defaultLogger() {
    static thread_local Logger inst;
    return inst;
}

static Logger& active() {
    return t_active ? *t_active : defaultLogger();
}

void setActive(Logger* logger) {
    t_active = logger;
}

void Logger::push(const std::string& s, const glm::vec3& c, float life) {
    if (feed_enabled_ && feed_) {
        feed_->push(s, c, life);
    }
    if (echo_) {
        std::cout << s << "\n";
    }
}

void Logger::infoTerminalOnly(const std::string& s) {
    std::cout << s << "\n";
}

// ---- Compatibility functions (delegate to active logger) ----
void attach(BattleFeed* feed)                { active().attach(feed); }
void info(const std::string& s)              { active().info(s); }
void warn(const std::string& s)              { active().warn(s); }
void error(const std::string& s)             { active().error(s); }
void colored(const std::string& s, const glm::vec3& rgb, float lifetime) { active().colored(s, rgb, lifetime); }
void setEchoToStdout(bool enabled)           { active().setEchoToStdout(enabled); }
void setFeedEnabled(bool enabled)            { active().setFeedEnabled(enabled); }
void infoTerminalOnly(const std::string& s)  { active().infoTerminalOnly(s); }

} // namespace LogBus
