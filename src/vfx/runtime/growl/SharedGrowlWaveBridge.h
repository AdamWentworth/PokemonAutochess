#pragma once

#include <functional>
#include <vector>

#include <glm/glm.hpp>

#include "vfx/runtime/growl/SharedGrowlVfxHelpers.h"
#include "vfx/runtime/growl/SharedGrowlWaveBatches.h"
#include "vfx/effects/growl/GrowlWaveVFX.h"

namespace vfx::runtime::growl_bridge {

using MeshResolver = std::function<growl_batches::MeshData*(const std::string&)>;
using TextureResolver = std::function<bool(
    const GrowlWaveVFX::Config::DrawPass&,
    const growl::TevState&,
    growl_batches::TextureView&)>;

bool appendBatches(const GrowlWaveVFX::RenderSnapshot& snapshot,
                   std::vector<growl_batches::WorldIndexedBatch>& outBatches,
                   const glm::vec3& cameraWorldPos,
                   const MeshResolver& resolveMesh,
                   const TextureResolver& resolveTexture);

} // namespace vfx::runtime::growl_bridge

