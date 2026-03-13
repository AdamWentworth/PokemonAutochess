#include <filesystem>
#include <sstream>
#include <string>

#include "game/runtime/RuntimeRelaunchLoop.h"
#include "game/runtime/video/VideoPreferences.h"

bool test_runtime_relaunch_loop_contract(std::string& outFail) {
    namespace fs = std::filesystem;

    const fs::path prefsPath =
        fs::temp_directory_path() / "pac_runtime_relaunch_loop_contract_video_settings.json";
    std::error_code removeError;
    fs::remove(prefsPath, removeError);

    {
        game::video::Preferences prefs;
        std::string saveErr;
        if (!game::video::savePreferences(prefs, prefsPath.string(), &saveErr)) {
            outFail = "failed to write the initial relaunch-loop test preferences file: " + saveErr;
            return false;
        }
    }

    int launchCount = 0;
    std::ostringstream logs;
    std::ostringstream errs;
    const int result = game::runtime::relaunch_loop::runWithRestartPolicy(
        prefsPath.string(),
        [&]() {
            ++launchCount;
            if (launchCount == 1) {
                game::video::Preferences prefs = game::video::loadPreferences(prefsPath.string());
                prefs.restartOnExit = true;
                prefs.bootMenuScreen = "video";
                std::string saveErr;
                if (!game::video::savePreferences(prefs, prefsPath.string(), &saveErr)) {
                    outFail = "failed to persist the restart request for the relaunch-loop contract: " + saveErr;
                    return 91;
                }
                return 7;
            }
            return 9;
        },
        logs,
        errs);

    fs::remove(prefsPath, removeError);
    if (!outFail.empty()) {
        return false;
    }
    if (result != 9 ||
        launchCount != 2 ||
        logs.str().find("Restart requested. Re-launching") == std::string::npos ||
        !errs.str().empty()) {
        outFail = "runWithRestartPolicy should relaunch once after consuming a persisted restart request and return the final launch result.";
        return false;
    }

    return true;
}

