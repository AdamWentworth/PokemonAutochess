#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "engine/vfx/ParticleSystem.h"
#include "game/runtime/shared/SharedWorldIndexedBatches.h"

namespace game::runtime::shared_particle_billboards {

struct BuildContext {
    glm::mat4 viewProj{1.0f};
    glm::mat4 invViewProj{1.0f};
    glm::vec3 cameraWorldPos{0.0f};
    int drawableW = 1;
    int drawableH = 1;
};

bool appendGenericBatch(const ParticleSystem::RenderSnapshot& snapshot,
                        const BuildContext& ctx,
                        game::runtime::shared_world_batches::WorldIndexedBatch batchTemplate,
                        std::vector<game::runtime::shared_world_batches::WorldIndexedBatch>& outBatches);

} // namespace game::runtime::shared_particle_billboards
