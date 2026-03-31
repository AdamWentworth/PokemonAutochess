#include "game/runtime/RuntimeRelaunchLoop.h"

#include "engine/utils/LogSink.h"
#include "game/runtime/RuntimeRestartPolicy.h"

#include <ostream>

namespace game::runtime::relaunch_loop {

int runWithRestartPolicy(const std::string& prefsPath,
                         const std::function<int()>& launchOnce,
                         std::ostream& logOut,
                         std::ostream& errOut) {
    engine::log::Sink log("Run", &logOut, &errOut);
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

        log.info("[Run] Restart requested. Re-launching game session...");
    }
}

} // namespace game::runtime::relaunch_loop
