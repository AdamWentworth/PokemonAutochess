#pragma once

#include <algorithm>

#include <glm/glm.hpp>

#include "engine/render/Model.h"
#include "game/PokemonInstance.h"

inline float computeModelWorldScaleForMoveImpact(const PokemonInstance& instance) {
    return (instance.model ? instance.model->getScaleFactor() : 1.0f) *
           std::max(0.0f, instance.modelScaleCorrection) *
           std::max(0.0f, instance.speciesScale) *
           std::max(0.0f, instance.visualScale) *
           std::max(0.0f, instance.captureScale);
}
