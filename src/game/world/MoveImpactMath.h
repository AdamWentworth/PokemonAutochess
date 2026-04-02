#pragma once

#include <algorithm>

#include <glm/glm.hpp>

#include "engine/render/Model.h"
#include "game/PokemonInstance.h"
#include "game/runtime/render_model_cache/RenderModelCache.h"

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

struct MoveImpactSurfacePoint {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec3 forward{0.0f, 0.0f, 1.0f};
    bool usedMeshSurface = false;
};

glm::vec3 computeMoveImpactRenderOrigin(const PokemonInstance& instance);

glm::vec3 computeMoveImpactWorldCenter(
    const PokemonInstance& instance,
    const game::runtime::render_model::MeshData* mesh = nullptr,
    float backendModelScaleFactor = 1.0f);

MoveImpactSurfacePoint computeTargetSurfaceImpactPoint(
    const PokemonInstance& target,
    const PokemonInstance* attacker,
    const game::runtime::render_model::MeshData* targetMesh = nullptr,
    const game::runtime::render_model::MeshData* attackerMesh = nullptr);
