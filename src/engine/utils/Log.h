// src/engine/utils/Log.h
#pragma once

#include <string>

namespace engine::log {

enum class Level {
    Debug,
    Info,
    Warn,
    Error,
};

// Set minimum level to print (default: Info).
void setMinLevel(Level lvl);
Level getMinLevel();

// Optional: route output to a file (append). Empty string disables file output.
bool setLogFile(const std::string& path);

// Log a message. Thread-safe.
void write(Level lvl, const std::string& msg);

// Convenience overload.
void write(Level lvl, const char* msg);

} // namespace engine::log

// Lightweight macros (avoid fmt dependency).
#define LOG_DEBUG(MSG) ::engine::log::write(::engine::log::Level::Debug, (MSG))
#define LOG_INFO(MSG)  ::engine::log::write(::engine::log::Level::Info,  (MSG))
#define LOG_WARN(MSG)  ::engine::log::write(::engine::log::Level::Warn,  (MSG))
#define LOG_ERROR(MSG) ::engine::log::write(::engine::log::Level::Error, (MSG))
