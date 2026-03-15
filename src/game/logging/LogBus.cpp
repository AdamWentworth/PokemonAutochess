// src/game/logging/LogBus.cpp
#include "LogBus.h"

#include "engine/ui/BattleFeed.h"
#include "engine/core/Environment.h"
#include <algorithm>
#include <chrono>
#include <iostream>

namespace {

int stdoutEchoLineCapPerSecond() {
    static const int cap = []() -> int {
        constexpr int kDefault = 72;
        constexpr int kMin = 0;
        constexpr int kMax = 2000;
        const auto raw = engine::env::get("PAC_LOG_ECHO_MAX_LINES_PER_SEC");
        if (!raw.has_value()) return kDefault;
        try {
            const int parsed = std::stoi(*raw);
            return std::clamp(parsed, kMin, kMax);
        } catch (...) {
            return kDefault;
        }
    }();
    return cap;
}

} // namespace

namespace LogBus {

void Logger::pushRecent(std::vector<StoredLine>& bucket, const std::string& s, const glm::vec3& c) {
    bucket.push_back(StoredLine{s, c});
    if (bucket.size() > kRecentLineCap) {
        const std::size_t drop = bucket.size() - kRecentLineCap;
        bucket.erase(bucket.begin(), bucket.begin() + static_cast<std::ptrdiff_t>(drop));
    }
}

std::vector<Logger::LineSnapshot> Logger::snapshotRecent(const std::vector<StoredLine>& bucket, std::size_t maxCount) {
    std::vector<LineSnapshot> out;
    if (bucket.empty() || maxCount == 0) return out;

    const std::size_t start = (bucket.size() > maxCount) ? (bucket.size() - maxCount) : 0;
    out.reserve(bucket.size() - start);
    for (std::size_t i = start; i < bucket.size(); ++i) {
        LineSnapshot line;
        line.text = bucket[i].text;
        line.color = bucket[i].color;
        out.push_back(std::move(line));
    }
    return out;
}

void Logger::push(const std::string& s, const glm::vec3& c, float life) {
    pushRecent(recent_main_, s, c);
    ++recent_main_revision_;
    if (feed_enabled_ && feed_) {
        feed_->push(s, c, life);
    }
    if (shouldEchoStdoutNow()) {
        std::cout << s << "\n";
    }
}

void Logger::catchInfo(const std::string& s, const glm::vec3& c, float life) {
    pushRecent(recent_catch_, s, c);
    ++recent_catch_revision_;
    if (feed_enabled_ && catch_feed_) {
        catch_feed_->push(s, c, life);
    }
    if (shouldEchoStdoutNow()) {
        std::cout << s << "\n";
    }
}

void Logger::economyInfo(const std::string& s, const glm::vec3& c, float life) {
    pushRecent(recent_economy_, s, c);
    ++recent_economy_revision_;
    if (feed_enabled_ && economy_feed_) {
        economy_feed_->push(s, c, life);
    }
    if (shouldEchoStdoutNow()) {
        std::cout << s << "\n";
    }
}

void Logger::infoTerminalOnly(const std::string& s) {
    std::cout << s << "\n";
}

std::vector<Logger::LineSnapshot> Logger::recentMainLines(std::size_t maxCount) const {
    return snapshotRecent(recent_main_, maxCount);
}

std::vector<Logger::LineSnapshot> Logger::recentCatchLines(std::size_t maxCount) const {
    return snapshotRecent(recent_catch_, maxCount);
}

std::vector<Logger::LineSnapshot> Logger::recentEconomyLines(std::size_t maxCount) const {
    return snapshotRecent(recent_economy_, maxCount);
}

bool Logger::shouldEchoStdoutNow() {
    if (!echo_) return false;

    const int lineCap = stdoutEchoLineCapPerSecond();
    if (lineCap <= 0) return false;

    const auto now = std::chrono::steady_clock::now();
    if (echoWindowStart_.time_since_epoch().count() == 0) {
        echoWindowStart_ = now;
    }

    constexpr auto kWindowDuration = std::chrono::seconds(1);
    if ((now - echoWindowStart_) >= kWindowDuration) {
        if (echoWindowDroppedCount_ > 0) {
            std::cout << "[LogBus] Suppressed " << echoWindowDroppedCount_
                      << " stdout lines in last second\n";
        }
        echoWindowStart_ = now;
        echoWindowLineCount_ = 0;
        echoWindowDroppedCount_ = 0;
    }

    if (echoWindowLineCount_ >= lineCap) {
        ++echoWindowDroppedCount_;
        return false;
    }

    ++echoWindowLineCount_;
    return true;
}

} // namespace LogBus
