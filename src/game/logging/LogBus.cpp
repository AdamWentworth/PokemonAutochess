// src/game/logging/LogBus.cpp
#include "LogBus.h"

#include "engine/ui/BattleFeed.h"
#include <algorithm>
#include <iostream>

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
    if (feed_enabled_ && feed_) {
        feed_->push(s, c, life);
    }
    if (echo_) {
        std::cout << s << "\n";
    }
}

void Logger::catchInfo(const std::string& s, const glm::vec3& c, float life) {
    pushRecent(recent_catch_, s, c);
    if (feed_enabled_ && catch_feed_) {
        catch_feed_->push(s, c, life);
    }
    if (echo_) {
        std::cout << s << "\n";
    }
}

void Logger::economyInfo(const std::string& s, const glm::vec3& c, float life) {
    pushRecent(recent_economy_, s, c);
    if (feed_enabled_ && economy_feed_) {
        economy_feed_->push(s, c, life);
    }
    if (echo_) {
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

} // namespace LogBus
