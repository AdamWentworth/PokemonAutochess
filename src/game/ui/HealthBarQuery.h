// src/game/ui/HealthBarQuery.h
#pragma once

#include <vector>

#include "engine/ui/HealthBarData.h"

class Camera3D;
struct PokemonInstance;

// Computes screen-space health/energy bar data for all visible, alive units.
std::vector<HealthBarData> BuildHealthBarData(
    const std::vector<PokemonInstance>& boardUnits,
    const std::vector<PokemonInstance>& benchUnits,
    const Camera3D& camera,
    int screenWidth,
    int screenHeight
);
