// src/game/vfx/CharmanderTailFireVFX.h
#pragma once

#include <vector>

#include "game/PokemonInstance.h"
#include "TailFireVFX.h"

class Camera3D;

class CharmanderTailFireVFX {
public:
    CharmanderTailFireVFX();

    void update(float dt,
                const std::vector<PokemonInstance>& boardUnits,
                const std::vector<PokemonInstance>& benchUnits);

    void render(const Camera3D& camera);

private:
    TailFireVFX tailFire;
};
