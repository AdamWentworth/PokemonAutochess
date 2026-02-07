// JsonFile.h
#pragma once

#include <string>
#include <fstream>
#include <algorithm>

#include <nlohmann/json.hpp>

#include "engine/core/IAssetStore.h"
#include "engine/core/Paths.h"
#include "game/logging/LoggerUtil.h"

namespace ConfigIO {

// Loads and parses a JSON file.
// - Returns true on success.
// - On failure, logs an error (unless silentIfMissing==true and the file can't be opened).
inline bool loadJsonFile(const std::string& filePath,
                         nlohmann::json& out,
                         const char* tag,
                         bool silentIfMissing = false,
                         LogBus::Logger* logger = nullptr,
                         const engine::IAssetStore* store = nullptr) {
    auto parseText = [&](const std::string& text) -> bool {
        try {
            out = nlohmann::json::parse(text);
        } catch (const std::exception& e) {
            game::log::error(logger, std::string("[") + tag + "] Failed to parse JSON: " + filePath + " (" + e.what() + ")");
            return false;
        } catch (...) {
            game::log::error(logger, std::string("[") + tag + "] Failed to parse JSON: " + filePath + " (unknown exception)");
            return false;
        }
        return true;
    };

    if (store) {
        std::string text;
        std::string err;
        std::string virtualPath = filePath;

        // Normalize to a virtual path if a data-root prefix was provided.
        std::string root = engine::paths::dataRoot();
        std::replace(root.begin(), root.end(), '\\', '/');
        std::replace(virtualPath.begin(), virtualPath.end(), '\\', '/');
        if (!root.empty() && (root.back() == '/' || root.back() == '\\')) root.pop_back();
        if (!root.empty() && virtualPath.rfind(root + "/", 0) == 0) {
            virtualPath = virtualPath.substr(root.size() + 1);
        }
        while (!virtualPath.empty() && (virtualPath.front() == '/' || virtualPath.front() == '\\')) {
            virtualPath.erase(virtualPath.begin());
        }

        if (!store->readText(virtualPath, text, &err)) {
            if (!silentIfMissing) {
                game::log::error(logger, std::string("[") + tag + "] Failed to read from asset store: " +
                    virtualPath + (err.empty() ? "" : (" (" + err + ")")));
            }
            return false;
        }
        return parseText(text);
    }

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
