#include "game/world/GameWorld.h"
#include "vfx/effects/growl/GrowlWaveVfxConfig.h"

#include <string>

void GameWorld::updateRenderVfx(float dt) {
    if (!grassImpactVfxInitialized) {
        GrassImpactVFX::Config configData;  // defaults
        grassImpactVfx.setConfig(configData);
        grassImpactVfxInitialized = true;
    }

    if (!tackleSmokeVfxInitialized) {
        TackleSmokeVFX::Config configData = TackleSmokeVFX::makeGameplayConfig();
        tackleSmokeVfx.setConfig(configData);
        tackleSmokeVfxInitialized = true;
    }

    if (!scratchGlowVfxInitialized) {
        ScratchGlowVFX::Config configData = ScratchGlowVFX::makeGameplayConfig();
        scratchGlowVfx.setConfig(configData);
        scratchGlowVfxInitialized = true;
    }

    grassImpactVfx.update(dt);
    tackleSmokeVfx.update(dt);
    scratchGlowVfx.update(dt);

    if (!leechSeedVfxInitialized) {
        LeechSeedProjectileVFX::Config configData;  // defaults
        leechSeedVfx.setConfig(configData);
        leechSeedVfxInitialized = true;
    }

    leechSeedVfx.update(dt);

    if (!healPlusVfxInitialized) {
        HealPlusVFX::Config configData;  // defaults
        healPlusVfx.setConfig(configData);
        healPlusVfxInitialized = true;
    }

    if (!leechSeedDrainVfxInitialized) {
        LeechSeedDrainVFX::Config configData;  // defaults
        leechSeedDrainVfx.setConfig(configData);
        leechSeedDrainVfxInitialized = true;
    }

    if (!growlWaveVfxInitialized) {
        GrowlWaveVFX::Config configData = vfx::growl_wave_config::makeSourceAlignedConfig();
        growlWaveVfx.setConfig(configData);
        growlWaveVfxInitialized = true;
    }

    if (!clawSwipeVfxInitialized) {
        ClawSwipeVFX::Config configData;  // defaults
        clawSwipeVfx.setConfig(configData);
        clawSwipeVfxInitialized = true;
    }

    if (!aquaSwooshVfxInitialized) {
        AquaSwooshVFX::Config configData;  // defaults
        aquaSwooshVfx.setConfig(configData);
        aquaSwooshVfxInitialized = true;
    }

    healPlusVfx.update(dt);
    leechSeedDrainVfx.update(dt);
    growlWaveVfx.update(dt);
    clawSwipeVfx.update(dt);
    aquaSwooshVfx.update(dt);
}
