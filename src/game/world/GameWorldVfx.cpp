#include "game/world/GameWorld.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireCoordinator.h"

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

    if (!tackleImpactVfxInitialized) {
        TackleImpactVFX::Config configData;  // defaults
        tackleImpactVfx.setConfig(configData);
        tackleImpactVfxInitialized = true;
    }

    grassImpactVfx.update(dt);
    tackleImpactVfx.update(dt);

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
        GrowlWaveVFX::Config configData;  // defaults
        // We now resolve the origin from head/jaw nodes, so keep built-in offsets minimal.
        configData.spawnForwardOffset = 0.0f;
        configData.spawnHeightOffset = 0.0f;
        configData.drawManifestPath = "config/vfx/moves/growl_draw_passes.json";
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
