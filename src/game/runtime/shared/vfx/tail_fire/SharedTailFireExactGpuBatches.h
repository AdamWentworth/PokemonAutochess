#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "engine/vfx/ParticleSystem.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"

namespace game::runtime::shared_tail_fire_exact_gpu {

struct AtlasView {
    const unsigned char* rgba = nullptr;
    int width = 0;
    int height = 0;
    std::string cacheKey;
    glm::vec4 rect0{0.0f, 0.0f, 1.0f, 1.0f};
    glm::vec4 rect1{0.0f, 0.0f, 1.0f, 1.0f};
    bool hasSecondary = false;
};

struct BuildContext {
    glm::mat4 viewProj{1.0f};
    glm::mat4 invViewProj{1.0f};
    glm::vec3 cameraWorldPos{0.0f};
    int drawableW = 1;
    int drawableH = 1;
    std::uint8_t blendMode = 0u;
};

bool appendBatch(const char* label,
                 const ParticleSystem::RenderSnapshot& snapshot,
                 const BuildContext& ctx,
                 const AtlasView& atlas,
                 std::vector<shared_world_batches::WorldIndexedBatch>& outBatches);

} // namespace game::runtime::shared_tail_fire_exact_gpu

