#pragma once

#include "game/runtime/shared/projected/unit/SharedProjectedUnitModelRenderer.h"

namespace game::runtime::shared_projected_body_presentation {

struct Result {
    shared_projected_unit_models::Result renderResult{};
    bool producedScratch = false;
};

Result summarizeProjectedBodyPresentation(
    const shared_projected_unit_models::Result& renderResult,
    const IRenderBackend::WorldSceneFrame* worldSceneFrame,
    const std::vector<shared_world_batches::WorldIndexedBatch>* worldIndexedBatches);

Result buildProjectedBodyPresentation(const shared_projected_unit_models::Args& args);

} // namespace game::runtime::shared_projected_body_presentation

