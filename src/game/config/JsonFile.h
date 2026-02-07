// JsonFile.h
#pragma once

#include <string>
#include <fstream>

#include <nlohmann/json.hpp>

#include "game/logging/LoggerUtil.h"

namespace ConfigIO {

// Loads and parses a JSON file.
// - Returns true on success.
// - On failure, logs an error (unless silentIfMissing==true and the file can't be opened).
inline bool loadJsonFile(const std::string& filePath,
                         nlohmann::json& out,
                         const char* tag,
                         bool silentIfMissing = false,
                         LogBus::Logger* logger = nullptr) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        if (!silentIfMissing) {
            game::log::error(logger, std::string("[") + tag + "] Failed to open: " + filePath);
        }
        return false;
    }

    try {
        file >> out;
    } catch (const std::exception& e) {
        game::log::error(logger, std::string("[") + tag + "] Failed to parse JSON: " + filePath + " (" + e.what() + ")");
        return false;
    } catch (...) {
        game::log::error(logger, std::string("[") + tag + "] Failed to parse JSON: " + filePath + " (unknown exception)");
        return false;
    }

    return true;
}

} // namespace ConfigIO
