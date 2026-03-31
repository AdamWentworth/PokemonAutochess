#include "game/runtime/GameRunner.h"

#include "game/runtime/RuntimeGameRunnerSession.h"
#include "game/runtime/RuntimeRelaunchLoop.h"
#include "game/runtime/video/VideoPreferences.h"

#include <iostream>
#include <string>

namespace game {

int runGame() {
    const std::string prefsPath = game::video::defaultPreferencesPath();
    return game::runtime::relaunch_loop::runWithRestartPolicy(
        prefsPath,
        []() { return game::runtime::runner_session::runSingleSession(); },
        std::cout,
        std::cerr);
}

} // namespace game
