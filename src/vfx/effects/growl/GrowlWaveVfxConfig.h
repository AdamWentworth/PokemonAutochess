#pragma once

#include "vfx/effects/growl/GrowlWaveVFX.h"

namespace vfx::growl_wave_config {

inline GrowlWaveVFX::Config makeSourceAlignedConfig() {
    GrowlWaveVFX::Config config;
    // The Growl FSYS control block's strongest global cue is a 0.94-like persistence
    // window plus a 10-degree angular term. Keep the spawn offsets neutral, but make the
    // shared lifetime deterministic so authored pass timing is no longer riding on the
    // generic randomized shared-authored defaults.
    config.spawnForwardOffset = 0.0f;
    config.spawnHeightOffset = 0.0f;
    config.drawManifestPath = "config/vfx/moves/growl_draw_passes.json";
    config.ringMinLifeSec = 0.94f;
    config.ringMaxLifeSec = 0.94f;
    config.fadeStart = 0.94f;
    return config;
}

} // namespace vfx::growl_wave_config
