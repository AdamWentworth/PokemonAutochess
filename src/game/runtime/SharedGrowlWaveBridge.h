#pragma once

#include <functional>
#include <vector>

#include <glm/glm.hpp>

#include "game/runtime/backend_model_cache/BackendModelCache.h"
#include "game/runtime/SharedGrowlVfxHelpers.h"
#include "game/runtime/SharedGrowlWaveBatches.h"
#include "game/runtime/SharedWorldIndexedBatches.h"
#include "game/vfx/GrowlWaveVFX.h"

namespace game::runtime::shared_growl_bridge {

using MeshResolver = std::function<backend_model::MeshData*(const std::string&)>;
using TextureResolver = std::function<bool(
    const GrowlWaveVFX::Config::DrawPass&,
    const shared_growl::TevState&,
    shared_growl_batches::TextureView&)>;

bool appendBatches(const GrowlWaveVFX::RenderSnapshot& snapshot,
                   std::vector<shared_world_batches::WorldIndexedBatch>& outBatches,
                   const glm::vec3& cameraWorldPos,
                   const MeshResolver& resolveMesh,
                   const TextureResolver& resolveTexture);

} // namespace game::runtime::shared_growl_bridge

