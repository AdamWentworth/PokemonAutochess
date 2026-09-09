#pragma once

#include "engine/assets/phlosion/PhlosionAuthoredScene.h"
#include "engine/assets/phlosion/PhlosionEnvironmentPatch.h"
#include "game/assets/environment/PublishedEnvironmentScene.h"
#include "game/runtime/shared/scene/AuthoredGroundSurface.h"
#include "game/runtime/shared/scene/PublishedEnvironmentSceneAdapter.h"

namespace game::runtime::authored_environment {

struct PreparedPatch {
    std::string nodeId;
    game::assets::published_environment::CanonicalScene geometry;
    published_environment_scene::PreparedScene scene;
    GroundSurface ground;
    std::array<float, 3> boundsMinimum{};
    std::array<float, 3> boundsMaximum{};
};

// Prepare complete authored meshes against an already published material
// library. This module has no source-tile repair, contour, or ledge generation.
bool preparePatch(
    const game::assets::published_environment::CanonicalScene &source,
    const published_environment_scene::PreparedScene &sourceScene,
    const engine::assets::phlosion::EnvironmentPatchDocument &patch,
    const engine::assets::phlosion::AuthoredSceneNode &node,
    bool independentTerrain,
    std::uint32_t groundMaterialIndex,
    PreparedPatch &prepared,
    std::string *outError);

} // namespace game::runtime::authored_environment
