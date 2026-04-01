#pragma once

#include "game/runtime/shared/backend/SharedBackendPoseEval.h"
#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshPrep.h"
#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshSupport.h"
#include "game/runtime/shared/projected/SharedProjectedUnitModelRenderer.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace game::runtime::shared_projected_unit_world_scene_trace {

bool shouldTraceUnit(const PokemonInstance& unit);
bool shouldDisableUnit(const PokemonInstance& unit);
void appendTraceLine(std::string_view line);

std::uint64_t hashPoseEval(const game::runtime::shared_backend_pose::PoseEval* scenePose);
std::uint64_t hashSkinPayload(
    const game::runtime::shared_projected_unit_backend_mesh_support::GpuSkinBatchState& state);

void traceEnter(const game::runtime::shared_projected_unit_models::Args& args);
void traceSkip(const game::runtime::shared_projected_unit_models::Args& args, const char* reason);
void traceFrameSummary(
    const game::runtime::shared_projected_unit_models::Args& args,
    const game::runtime::shared_projected_unit_backend_mesh_prep::PreparedState& prepared,
    std::size_t rigidBatchCount,
    std::size_t skinnedBatchCount,
    std::uint64_t batchHash,
    std::uint64_t poseHash);

} // namespace game::runtime::shared_projected_unit_world_scene_trace
