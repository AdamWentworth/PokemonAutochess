#include "game/runtime/RuntimeRestartPolicy.h"

#include <ostream>

namespace game::runtime::restart_policy {

bool clearStaleRestartRequest(game::video::Preferences& prefs) {
    if (!prefs.restartOnExit) {
        return false;
    }
    prefs.restartOnExit = false;
    prefs.bootMenuScreen.clear();
    return true;
}

bool consumeRestartRequestForRelaunch(game::video::Preferences& prefs) {
    if (!prefs.restartOnExit) {
        return false;
    }
    prefs.restartOnExit = false;
    return true;
}

bool clearStaleRestartRequest(const std::string& prefsPath, std::ostream& err) {
    game::video::Preferences prefs = game::video::loadPreferences(prefsPath);
    if (!clearStaleRestartRequest(prefs)) {
        return true;
    }

    std::string saveErr;
    if (game::video::savePreferences(prefs, prefsPath, &saveErr)) {
        return true;
    }

    err << "[Video] Failed to clear stale restart request: " << saveErr << "\n";
    return false;
}

bool consumeRestartRequestForRelaunch(const std::string& prefsPath,
                                      std::ostream& err,
                                      bool& outShouldRelaunch) {
    game::video::Preferences prefs = game::video::loadPreferences(prefsPath);
    outShouldRelaunch = consumeRestartRequestForRelaunch(prefs);
    if (!outShouldRelaunch) {
        return true;
    }

    std::string saveErr;
    if (game::video::savePreferences(prefs, prefsPath, &saveErr)) {
        return true;
    }

    err << "[Video] Failed to consume restart request: " << saveErr << "\n";
    return false;
}

} // namespace game::runtime::restart_policy
