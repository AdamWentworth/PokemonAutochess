#pragma once

#include <functional>
#include <iosfwd>
#include <string>

namespace game::runtime::relaunch_loop {

int runWithRestartPolicy(const std::string& prefsPath,
                         const std::function<int()>& launchOnce,
                         std::ostream& logOut,
                         std::ostream& errOut);

} // namespace game::runtime::relaunch_loop
