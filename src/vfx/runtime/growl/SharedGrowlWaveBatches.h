#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "vfx/runtime/growl/SharedGrowlVfxHelpers.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"
#include "vfx/effects/growl/GrowlWaveVFX.h"

namespace vfx::runtime::growl_batches {

namespace render_model = game::runtime::render_model;
namespace shared_world_batches = game::runtime::shared_world_batches;

struct TextureView {
    const unsigned char* rgba = nullptr;
    int width = 0;
    int height = 0;
};

bool appendPassBatch(std::vector<shared_world_batches::WorldIndexedBatch>& outBatches,
                     const GrowlWaveVFX::RenderSnapshot& snapshot,
                     const GrowlWaveVFX::Config::DrawPass& pass,
                     const growl::TevState& passTev,
                     const render_model::MeshData* passMesh,
                     const TextureView& texture,
                     const glm::vec3& cameraWorldPos);

} // namespace vfx::runtime::growl_batches

