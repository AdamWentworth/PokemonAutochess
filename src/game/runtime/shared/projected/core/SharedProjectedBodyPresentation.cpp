#include "game/runtime/shared/projected/core/SharedProjectedBodyPresentation.h"

namespace game::runtime::shared_projected_body_presentation {

Result summarizeProjectedBodyPresentation(
    const shared_projected_unit_models::Result& renderResult,
    const IRenderBackend::WorldSceneFrame* worldSceneFrame,
    const std::vector<shared_world_batches::WorldIndexedBatch>* worldIndexedBatches) {
    Result out{};
    out.renderResult = renderResult;
    out.producedScratch =
        renderResult.drewModelMesh ||
        (worldSceneFrame && !worldSceneFrame->drawClasses.empty()) ||
        (worldIndexedBatches && !worldIndexedBatches->empty());
    return out;
}

Result buildProjectedBodyPresentation(const shared_projected_unit_models::Args& args) {
    return summarizeProjectedBodyPresentation(
        shared_projected_unit_models::renderProjectedUnitModel(args),
        args.worldSceneFrame,
        args.worldIndexedBatches);
}

} // namespace game::runtime::shared_projected_body_presentation

