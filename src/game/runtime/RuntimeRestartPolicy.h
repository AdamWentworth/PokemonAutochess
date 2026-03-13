#pragma once

#include <iosfwd>
#include <string>

#include "game/runtime/video/VideoPreferences.h"

namespace game::runtime::restart_policy {

bool clearStaleRestartRequest(game::video::Preferences& prefs);

bool consumeRestartRequestForRelaunch(game::video::Preferences& prefs);

bool clearStaleRestartRequest(const std::string& prefsPath, std::ostream& err);

bool consumeRestartRequestForRelaunch(const std::string& prefsPath,
                                      std::ostream& err,
                                      bool& outShouldRelaunch);

} // namespace game::runtime::restart_policy

