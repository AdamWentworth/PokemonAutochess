#pragma once

#include <algorithm>

#include <glm/glm.hpp>

#include "engine/render/Model.h"
#include "game/PokemonInstance.h"

inline float computeModelWorldScaleForMoveImpact(const PokemonInstance& instance,
                                                 float backendModelScaleFactor = 1.0f) {
    const float modelScaleFactor =
        instance.model ? instance.model->getScaleFactor() : backendModelScaleFactor;
    return modelScaleFactor *
           std::max(0.0f, instance.modelScaleCorrection) *
           std::max(0.0f, instance.speciesScale) *
           std::max(0.0f, instance.visualScale) *
           std::max(0.0f, instance.captureScale);
}
