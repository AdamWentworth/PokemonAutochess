// src/game/logging/LoggerUtil.h
#pragma once

#include "game/logging/LogBus.h"

#include <iostream>
#include <string>

namespace game::log {

inline void info(LogBus::Logger* logger, const std::string& s) {
    if (logger) {
        logger->info(s);
    } else {
        std::cout << s << "\n";
    }
}

inline void warn(LogBus::Logger* logger, const std::string& s) {
    if (logger) {
        logger->warn(s);
    } else {
        std::cerr << "[WARN] " << s << "\n";
    }
}

inline void error(LogBus::Logger* logger, const std::string& s) {
    if (logger) {
        logger->error(s);
    } else {
        std::cerr << "[ERROR] " << s << "\n";
    }
}

inline void infoTerminalOnly(LogBus::Logger* logger, const std::string& s) {
    if (logger) {
        logger->infoTerminalOnly(s);
    } else {
        std::cout << s << "\n";
    }
}

} // namespace game::log
