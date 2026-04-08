#pragma once

#include <vector>

#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"
#include "vfx/runtime/shared/SharedAuthoredVfxBatches.h"

namespace game::runtime::shared_authored_vfx_interop {

vfx::runtime::authored_batches::MeshData toReusableMeshData(
    const render_model::MeshData& mesh);

const vfx::runtime::authored_batches::MeshData& cachedReusableMeshData(
    const render_model::MeshData& mesh);

shared_world_batches::WorldIndexedBatch toWorldIndexedBatch(
    const vfx::runtime::authored_batches::WorldIndexedBatch& src);

void appendWorldIndexedBatches(
    const std::vector<vfx::runtime::authored_batches::WorldIndexedBatch>& src,
    std::vector<shared_world_batches::WorldIndexedBatch>& dst);

} // namespace game::runtime::shared_authored_vfx_interop
