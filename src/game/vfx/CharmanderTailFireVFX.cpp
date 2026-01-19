// src/game/vfx/CharmanderTailFireVFX.cpp
#include "CharmanderTailFireVFX.h"

CharmanderTailFireVFX::CharmanderTailFireVFX() {
    // Apply to Charmander only
    tailFire.setNameFilterCaseInsensitive("charmander");

    TailFireVFX::Config c;

    // Hybrid flipbook + procedural
    c.useFlipbook = true;

    // Slower emission (torch/candle-like)
    c.emitRatePerSec = 35.0f;

    // Less wide / tighter spawn
    c.spawnRadius = 0.0085f;

    c.tailTipNodeIndex = 45;
    c.tailWorldYOffset = 0.185f;
    c.backDir          = glm::vec3(0.0f, 0.0f, 1.0f);

    // Keep premultiplied (matches shader output)
    c.blend = ParticleSystem::BlendMode::Premultiplied;

    // Taller feel: more upward drift
    c.acceleration = glm::vec3(0.0f, 1.9f, 0.0f);
    c.dampingBase  = 0.07f;

    // Slightly larger/brighter read
    c.pointScale = 980.0f;

    tailFire.setConfig(c);
}

void CharmanderTailFireVFX::update(float dt,
                                  const std::vector<PokemonInstance>& boardUnits,
                                  const std::vector<PokemonInstance>& benchUnits)
{
    tailFire.update(dt, boardUnits, benchUnits);
}

void CharmanderTailFireVFX::render(const Camera3D& camera) {
    tailFire.render(camera);
}
