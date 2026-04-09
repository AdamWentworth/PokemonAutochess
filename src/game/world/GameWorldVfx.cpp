#include "game/world/GameWorld.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireCoordinator.h"
#include "vfx/effects/growl/GrowlWaveVfxConfig.h"

#include <string>

void GameWorld::updateRenderVfx(float dt) {
    // Tail fire VFX: init once, then update every frame.
    if (!tailFireVfxInitialized) {
        tailFireVfx.setFilter(
            [](const PokemonInstance& unit) {
                return game::runtime::shared_tail_fire_coordinator::unitUsesTailFireMeshPlayback(unit);
            });
        tailFireVfx.setConfig(
            game::runtime::shared_tail_fire_coordinator::resolvePrimaryPlaybackConfig());
        tailFireVfx.setUsePlaybackSpeciesConfigs(true);
        tailFireVfxInitialized = true;
    }

    tailFireVfx.update(dt, pokemons, benchPokemons);

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

    grassImpactVfx.update(dt);
    tackleSmokeVfx.update(dt);

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
