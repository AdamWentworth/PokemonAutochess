#pragma once

#include <functional>
#include <vector>

#include <glm/glm.hpp>

#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "vfx/runtime/growl/SharedGrowlVfxHelpers.h"
#include "vfx/runtime/growl/SharedGrowlWaveBatches.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"
#include "vfx/effects/growl/GrowlWaveVFX.h"

namespace vfx::runtime::growl_bridge {

namespace render_model = game::runtime::render_model;
namespace shared_world_batches = game::runtime::shared_world_batches;

using MeshResolver = std::function<render_model::MeshData*(const std::string&)>;
using TextureResolver = std::function<bool(
    const GrowlWaveVFX::Config::DrawPass&,
    const growl::TevState&,
    growl_batches::TextureView&)>;

bool appendBatches(const GrowlWaveVFX::RenderSnapshot& snapshot,
                   std::vector<shared_world_batches::WorldIndexedBatch>& outBatches,
                   const glm::vec3& cameraWorldPos,
                   const MeshResolver& resolveMesh,
                   const TextureResolver& resolveTexture);

} // namespace vfx::runtime::growl_bridge

