// src/game/vfx/CharmanderTailFireVFX.cpp
#include "CharmanderTailFireVFX.h"

CharmanderTailFireVFX::CharmanderTailFireVFX() {
    // Apply to Charmander only (same approach as before)
    tailFire.setNameFilterCaseInsensitive("charmander");

    TailFireVFX::Config c;

    // Enable hybrid flipbook+procedural fire tail
    c.useFlipbook = true;

    // Keep your existing Charmander tuning (from earlier versions)
    c.emitRatePerSec   = 90.0f;
    c.spawnRadius      = 0.010f;
    c.tailTipNodeIndex = 45;
    c.tailWorldYOffset = 0.2f;
    c.backDir          = glm::vec3(0.0f, 0.0f, 1.0f);

    // Fire setup lives in TailFireVFX now:
    // shaders / flipbook / render settings / accel / damping / pointScale.

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
