#include "game/world/GameWorld.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireCoordinator.h"
#include "vfx/effects/growl/GrowlWaveVfxConfig.h"

#include <algorithm>
#include <string>
#include <unordered_set>

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

    if (!encounterGrassRustleVfxInitialized) {
        // Direct ROMFS evidence:
        // bin/field/effect/particle/chara_walk_grass/
        // ef_chara_grass_run_dast.ptcl. Its VFXB names leaf, kona_up, and
        // kona emitters. This first pass preserves the leaf role and ground
        // contact cadence while exact PTCL texture/curve cooking remains a
        // separately documented importer boundary.
        GrassImpactVFX::Config configData;
        configData.minParticles = 1;
        configData.maxParticles = 2;
        configData.spawnRadius = 0.10f;
        configData.impactYOffset = 0.10f;
        configData.minSpeed = 0.22f;
        configData.maxSpeed = 0.62f;
        configData.minLifeSec = 0.28f;
        configData.maxLifeSec = 0.58f;
        configData.minSize = 0.055f;
        configData.maxSize = 0.105f;
        configData.minUpward = 0.32f;
        configData.maxUpward = 0.82f;
        configData.acceleration = glm::vec3(0.0f, -1.15f, 0.0f);
        configData.dampingBase = 0.48f;
        configData.pointScale = 360.0f;
        encounterGrassRustleVfx.setConfig(configData);
        encounterGrassRustleVfxInitialized = true;
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
    encounterGrassRustleVfx.update(dt);
    tackleSmokeVfx.update(dt);
    scratchGlowVfx.update(dt);

    std::unordered_set<int> presentUnitIds;
    presentUnitIds.reserve(pokemons.size());
    for (const auto& unit : pokemons) {
        presentUnitIds.insert(unit.id);
        float& cooldown =
            encounterGrassRustleCooldownSec[unit.id];
        cooldown = std::max(0.0f, cooldown - dt);
        if (!unit.alive || unit.fainting || unit.captureInProgress ||
            !unit.isMoving ||
            unit.airState != AirLocomotionState::Grounded ||
            cooldown > 0.0f ||
            !isWorldPositionInEncounterGrass(unit.position)) {
            continue;
        }
        encounterGrassRustleVfx.emitAt(unit.position);
        const float speed = std::max(0.25f, unit.movementSpeed);
        cooldown = std::clamp(0.15f / speed, 0.075f, 0.18f);
    }
    std::erase_if(
        encounterGrassRustleCooldownSec,
        [&](const auto& entry) {
            return !presentUnitIds.contains(entry.first);
        });

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
