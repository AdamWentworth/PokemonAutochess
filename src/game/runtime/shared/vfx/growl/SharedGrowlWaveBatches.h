#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "game/runtime/backend_model_cache/BackendModelCache.h"
#include "game/runtime/shared/vfx/growl/SharedGrowlVfxHelpers.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"
#include "game/vfx/GrowlWaveVFX.h"

namespace game::runtime::shared_growl_batches {

struct TextureView {
    const unsigned char* rgba = nullptr;
    int width = 0;
    int height = 0;
};

bool appendPassBatch(std::vector<shared_world_batches::WorldIndexedBatch>& outBatches,
                     const GrowlWaveVFX::RenderSnapshot& snapshot,
                     const GrowlWaveVFX::Config::DrawPass& pass,
                     const shared_growl::TevState& passTev,
                     const backend_model::MeshData* passMesh,
                     const TextureView& texture,
                     const glm::vec3& cameraWorldPos);

} // namespace game::runtime::shared_growl_batches

