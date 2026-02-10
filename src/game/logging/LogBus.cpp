// src/game/logging/LogBus.cpp
#include "LogBus.h"

#include "engine/ui/BattleFeed.h"
#include <iostream>

namespace LogBus {

void Logger::push(const std::string& s, const glm::vec3& c, float life) {
    if (feed_enabled_ && feed_) {
        feed_->push(s, c, life);
    }
    if (echo_) {
        std::cout << s << "\n";
    }
}

void Logger::catchInfo(const std::string& s, const glm::vec3& c, float life) {
    if (feed_enabled_ && catch_feed_) {
        catch_feed_->push(s, c, life);
    }
    if (echo_) {
        std::cout << s << "\n";
    }
}

void Logger::economyInfo(const std::string& s, const glm::vec3& c, float life) {
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

} // namespace LogBus
