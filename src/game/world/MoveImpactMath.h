#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

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

struct MoveImpactModelPrewarmStats {
    std::size_t meshesWarmed = 0u;
    std::size_t growlAnchorModelsWarmed = 0u;
};

glm::vec3 computeMoveImpactRenderOrigin(const PokemonInstance& instance);

glm::vec3 computeMoveImpactWorldCenter(
    const PokemonInstance& instance,
    const game::runtime::render_model::MeshData* mesh = nullptr,
    float backendModelScaleFactor = 1.0f);

const game::runtime::render_model::MeshData* resolveMoveImpactMeshForModelPath(
    const std::string& modelPath);

bool prewarmMoveImpactMeshForModelPath(
    const std::string& modelPath,
    const game::runtime::render_model::MeshData* preloadedMesh = nullptr);

const std::vector<int>* resolveGrowlAnchorNodeIndicesForModelPath(
    const std::string& modelPath);

MoveImpactModelPrewarmStats prewarmMoveImpactModelPaths(
    const std::vector<std::pair<std::string, const game::runtime::render_model::MeshData*>>&
        modelPaths);

MoveImpactSurfacePoint computeApproximateTargetSurfaceImpactPoint(
    const PokemonInstance& target,
    const PokemonInstance* attacker,
    const game::runtime::render_model::MeshData* targetMesh = nullptr,
    const game::runtime::render_model::MeshData* attackerMesh = nullptr);

MoveImpactSurfacePoint computeTargetSurfaceImpactPoint(
    const PokemonInstance& target,
    const PokemonInstance* attacker,
    const game::runtime::render_model::MeshData* targetMesh = nullptr,
    const game::runtime::render_model::MeshData* attackerMesh = nullptr);
