// LogBus.cpp
#include "LogBus.h"

#include "engine/ui/BattleFeed.h"
#include <iostream>
#include <atomic>

namespace LogBus {

// Atomic active logger pointer to avoid data races if logger is swapped while logging.
static std::atomic<Logger*> g_active{nullptr};

// Default instance (used only if no active logger is set).
static Logger& defaultLogger() {
    static Logger inst;
    return inst;
}

static Logger& active() {
    Logger* p = g_active.load(std::memory_order_acquire);
    return p ? *p : defaultLogger();
}

void setActive(Logger* logger) {
    g_active.store(logger, std::memory_order_release);
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
