#include "game/runtime/RuntimeRelaunchLoop.h"

#include "game/runtime/RuntimeRestartPolicy.h"

#include <ostream>

namespace game::runtime::relaunch_loop {

int runWithRestartPolicy(const std::string& prefsPath,
                         const std::function<int()>& launchOnce,
                         std::ostream& logOut,
                         std::ostream& errOut) {
    int lastResult = 0;
    for (;;) {
        if (!game::runtime::restart_policy::clearStaleRestartRequest(prefsPath, errOut)) {
            return 1;
        }

        lastResult = launchOnce();

        bool shouldRelaunch = false;
        if (!game::runtime::restart_policy::consumeRestartRequestForRelaunch(
                prefsPath,
                errOut,
                shouldRelaunch)) {
            return lastResult;
        }
        if (!shouldRelaunch) {
            return lastResult;
        }

        logOut << "[Run] Restart requested. Re-launching game session...\n";
    }
}

} // namespace game::runtime::relaunch_loop
