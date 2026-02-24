#include "game/world/GameWorld.h"

#include <algorithm>

#include <glm/gtc/matrix_transform.hpp>

#include "engine/render/BoardRenderer.h"
#include "engine/render/Camera3D.h"
#include "engine/render/Model.h"

#include "game/ui/HealthBarQuery.h"

void GameWorld::drawAll(const Camera3D& camera, BoardRenderer& boardRenderer) {
    lastViewMatrix = camera.getViewMatrix();
    hasLastViewMatrix = true;

    boardRenderer.draw(camera);
    boardRenderer.drawBench(camera);

    auto drawPokemonList = [&](const std::vector<PokemonInstance>& list) {
        for (const auto& instance : list) {
            if (!instance.model) continue;
            if (!instance.alive && !instance.fainting) continue;

            float scaleFactor = instance.model->getScaleFactor() *
                                std::max(0.0f, instance.modelScaleCorrection) *
                                std::max(0.0f, instance.speciesScale) *
                                std::max(0.0f, instance.visualScale) *
                                std::max(0.0f, instance.captureScale);

            glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(scaleFactor));
            glm::mat4 rotationX = glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.x), glm::vec3(1, 0, 0));
            glm::mat4 rotationY = glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.y), glm::vec3(0, 1, 0));
            glm::mat4 rotationZ = glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.z), glm::vec3(0, 0, 1));
            glm::vec3 renderPos = instance.position + glm::vec3(0.0f, instance.visualYOffset, 0.0f);
            glm::mat4 translation = glm::translate(glm::mat4(1.0f), renderPos);

            glm::mat4 instanceTransform = translation * rotationY * rotationX * rotationZ * scale;

            const float tintStrength = std::clamp(instance.captureTintStrength, 0.0f, 1.0f);
            instance.model->drawAnimated(camera, instanceTransform, instance.animTimeSec, instance.activeAnimIndex,
                                         glm::vec3(1.0f, 0.1f, 0.1f), tintStrength);
        }
    };

    drawPokemonList(pokemons);
    drawPokemonList(benchPokemons);

    if (pokeballModelLoaded && pokeballModel) {
        for (const auto& attempt : captureAttempts) {
            if (attempt.timeLeftSec <= 0.0f) continue;
            float scaleFactor = pokeballModel->getScaleFactor() * std::max(0.0f, attempt.ballScale);
            glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(scaleFactor));
            glm::mat4 rotationY = glm::rotate(glm::mat4(1.0f), glm::radians(attempt.ballYawDeg), glm::vec3(0, 1, 0));
            glm::mat4 translation = glm::translate(glm::mat4(1.0f), attempt.ballPos);
            glm::mat4 instanceTransform = translation * rotationY * scale;
            pokeballModel->drawAnimated(camera, instanceTransform, 0.0f, 0);
        }
    }

    // draw particles AFTER opaque models
    tailFireVfx.render(camera);
    grassImpactVfx.render(camera);
    tackleImpactVfx.render(camera);
    leechSeedVfx.render(camera);
    healPlusVfx.render(camera);
    leechSeedDrainVfx.render(camera);
    growlWaveVfx.render(camera);
    clawSwipeVfx.render(camera);
    aquaSwooshVfx.render(camera);
}

std::vector<HealthBarData> GameWorld::getHealthBarData(const Camera3D& camera,
                                                       int screenWidth,
                                                       int screenHeight) const {
    return BuildHealthBarData(pokemons, benchPokemons, camera, screenWidth, screenHeight, config);
}

bool GameWorld::buildGrowlWaveSnapshot(GrowlWaveVFX::RenderSnapshot& out) const {
    return growlWaveVfx.buildRenderSnapshot(out);
}

bool GameWorld::buildParticleVfxSnapshots(ParticleVfxSnapshots& out) const {
    bool any = false;
    any = tailFireVfx.getParticles().buildRenderSnapshot(out.tailFire) || any;
    any = grassImpactVfx.getParticles().buildRenderSnapshot(out.grassImpact) || any;
    any = tackleImpactVfx.getBurstParticles().buildRenderSnapshot(out.tackleBurst) || any;
    any = tackleImpactVfx.getSparkParticles().buildRenderSnapshot(out.tackleSpark) || any;
    any = leechSeedVfx.getParticles().buildRenderSnapshot(out.leechSeedProjectile) || any;
    any = healPlusVfx.getParticles().buildRenderSnapshot(out.healPlus) || any;
    any = leechSeedDrainVfx.getParticles().buildRenderSnapshot(out.leechSeedDrain) || any;
    any = clawSwipeVfx.getParticles().buildRenderSnapshot(out.clawSwipe) || any;
    any = aquaSwooshVfx.getParticles().buildRenderSnapshot(out.aquaSwoosh) || any;
    return any;
}

