// src/game/logging/LogBus.h
#pragma once

#include <chrono>
#include <string>
#include <vector>
#include <glm/glm.hpp>

class BattleFeed;

namespace LogBus {

// Instance-based logger (preferred).
// Composition root should own a Logger and wire it explicitly.
class Logger {
public:
    struct LineSnapshot {
        std::string text;
        glm::vec3 color{1.0f, 1.0f, 1.0f};
    };

    void attach(BattleFeed* feed) { feed_ = feed; }
    void attachCatchFeed(BattleFeed* feed) { catch_feed_ = feed; }
    void attachEconomyFeed(BattleFeed* feed) { economy_feed_ = feed; }

    void info(const std::string& s)  { push(s, {1,1,1}); }
    void warn(const std::string& s)  { push(std::string("[WARN] ")  + s, {1,0.9f,0.2f}); }
    void error(const std::string& s) { push(std::string("[ERROR] ") + s, {1,0.3f,0.3f}); }

    void colored(const std::string& s, const glm::vec3& rgb, float lifetime = 3.f) {
        push(s, rgb, lifetime);
    }

    void catchInfo(const std::string& s, const glm::vec3& rgb = {1,1,1}, float lifetime = 3.f);
    void economyInfo(const std::string& s, const glm::vec3& rgb = {1,1,1}, float lifetime = 4.f);

    // stdout mirroring toggle (default: on)
    void setEchoToStdout(bool enabled) { echo_ = enabled; }

    // on-screen feed toggle (default: on)
    void setFeedEnabled(bool enabled) { feed_enabled_ = enabled; }

    // stdout only
    void infoTerminalOnly(const std::string& s);

    std::vector<LineSnapshot> recentMainLines(std::size_t maxCount) const;
    std::vector<LineSnapshot> recentCatchLines(std::size_t maxCount) const;
    std::vector<LineSnapshot> recentEconomyLines(std::size_t maxCount) const;

private:
    struct StoredLine {
        std::string text;
        glm::vec3 color{1.0f, 1.0f, 1.0f};
    };

    static constexpr std::size_t kRecentLineCap = 24;

    static void pushRecent(std::vector<StoredLine>& bucket, const std::string& s, const glm::vec3& c);
    static std::vector<LineSnapshot> snapshotRecent(const std::vector<StoredLine>& bucket, std::size_t maxCount);
    bool shouldEchoStdoutNow();
    void push(const std::string& s, const glm::vec3& c, float life = 3.f);

private:
    BattleFeed* feed_ = nullptr;
    BattleFeed* catch_feed_ = nullptr;
    BattleFeed* economy_feed_ = nullptr;
    bool echo_ = true;
    bool feed_enabled_ = true;
    std::chrono::steady_clock::time_point echoWindowStart_{};
    int echoWindowLineCount_ = 0;
    int echoWindowDroppedCount_ = 0;
    std::vector<StoredLine> recent_main_;
    std::vector<StoredLine> recent_catch_;
    std::vector<StoredLine> recent_economy_;
};

} // namespace LogBus
