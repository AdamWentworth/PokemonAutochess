#include <filesystem>
#include <sstream>
#include <string>

#include "game/runtime/RuntimeRestartPolicy.h"

bool test_runtime_restart_policy_contract(std::string& outFail) {
    using game::runtime::restart_policy::clearStaleRestartRequest;
    using game::runtime::restart_policy::consumeRestartRequestForRelaunch;
    using game::video::Preferences;

    {
        Preferences prefs;
        prefs.restartOnExit = true;
        prefs.bootMenuScreen = "video";
        if (!clearStaleRestartRequest(prefs) ||
            prefs.restartOnExit ||
            !prefs.bootMenuScreen.empty()) {
            outFail = "clearStaleRestartRequest should clear restartOnExit and bootMenuScreen.";
            return false;
        }
    }

    {
        Preferences prefs;
        prefs.bootMenuScreen = "video";
        if (clearStaleRestartRequest(prefs) ||
            prefs.restartOnExit ||
            prefs.bootMenuScreen != "video") {
            outFail = "clearStaleRestartRequest should leave non-restart preferences unchanged.";
            return false;
        }
    }

    {
        Preferences prefs;
        prefs.restartOnExit = true;
        prefs.bootMenuScreen = "video";
        if (!consumeRestartRequestForRelaunch(prefs) ||
            prefs.restartOnExit ||
            prefs.bootMenuScreen != "video") {
            outFail = "consumeRestartRequestForRelaunch should preserve bootMenuScreen for relaunch.";
            return false;
        }
    }

    const std::filesystem::path tempPath =
        std::filesystem::temp_directory_path() / "pac_runtime_restart_policy_test.json";
    const std::string path = tempPath.string();
    std::error_code ec;

    {
        Preferences prefs;
        prefs.restartOnExit = true;
        prefs.bootMenuScreen = "video";

        std::string saveErr;
        if (!game::video::savePreferences(prefs, path, &saveErr)) {
            outFail = "failed to seed stale restart preferences: " + saveErr;
            return false;
        }

        std::ostringstream errs;
        if (!clearStaleRestartRequest(path, errs) || !errs.str().empty()) {
            outFail = "clearStaleRestartRequest(path) should succeed without diagnostics.";
            return false;
        }

        const Preferences loaded = game::video::loadPreferences(path);
        if (loaded.restartOnExit || !loaded.bootMenuScreen.empty()) {
            outFail = "clearStaleRestartRequest(path) should clear stale relaunch state from disk.";
            return false;
        }
    }

    {
        Preferences prefs;
        prefs.restartOnExit = true;
        prefs.bootMenuScreen = "video";

        std::string saveErr;
        if (!game::video::savePreferences(prefs, path, &saveErr)) {
            outFail = "failed to seed relaunch preferences: " + saveErr;
            return false;
        }

        bool shouldRelaunch = false;
        std::ostringstream errs;
        if (!consumeRestartRequestForRelaunch(path, errs, shouldRelaunch) ||
            !shouldRelaunch ||
            !errs.str().empty()) {
            outFail = "consumeRestartRequestForRelaunch(path) should request relaunch without diagnostics.";
            return false;
        }

        const Preferences loaded = game::video::loadPreferences(path);
        if (loaded.restartOnExit || loaded.bootMenuScreen != "video") {
            outFail = "consumeRestartRequestForRelaunch(path) should preserve bootMenuScreen on disk.";
            return false;
        }
    }

    {
        Preferences prefs;
        prefs.restartOnExit = false;
        prefs.bootMenuScreen = "audio";

        std::string saveErr;
        if (!game::video::savePreferences(prefs, path, &saveErr)) {
            outFail = "failed to seed non-relaunch preferences: " + saveErr;
            return false;
        }

        bool shouldRelaunch = true;
        std::ostringstream errs;
        if (!consumeRestartRequestForRelaunch(path, errs, shouldRelaunch) ||
            shouldRelaunch ||
            !errs.str().empty()) {
            outFail = "consumeRestartRequestForRelaunch(path) should no-op when no restart is pending.";
            return false;
        }

        const Preferences loaded = game::video::loadPreferences(path);
        if (loaded.restartOnExit || loaded.bootMenuScreen != "audio") {
            outFail = "consumeRestartRequestForRelaunch(path) should leave non-restart preferences intact.";
            return false;
        }
    }

    std::filesystem::remove(tempPath, ec);
    return true;
}
