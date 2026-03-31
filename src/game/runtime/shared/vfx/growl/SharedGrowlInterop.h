#pragma once

#include <vector>

#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"
#include "vfx/runtime/growl/SharedGrowlWaveBatches.h"

namespace game::runtime::shared_growl_interop {

vfx::runtime::growl_batches::MeshData toReusableMeshData(
    const render_model::MeshData& mesh);

shared_world_batches::WorldIndexedBatch toWorldIndexedBatch(
    const vfx::runtime::growl_batches::WorldIndexedBatch& src);

void appendWorldIndexedBatches(
    const std::vector<vfx::runtime::growl_batches::WorldIndexedBatch>& src,
    std::vector<shared_world_batches::WorldIndexedBatch>& dst);

} // namespace game::runtime::shared_growl_interop
