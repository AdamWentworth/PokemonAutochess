#pragma once

#include "game/runtime/shared/vfx/authored/SharedAuthoredVfxPrewarm.h"
#include "game/runtime/startup/RuntimeStartupAssetPrewarm.h"

namespace game::runtime::growl_vfx_prewarm {

using Args = shared_authored_vfx_prewarm::Args;

startup_asset_prewarm::GrowlStats prewarm(const Args& args);

} // namespace game::runtime::growl_vfx_prewarm
