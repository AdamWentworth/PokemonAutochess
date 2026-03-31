#include <string>
#include <unordered_map>
#include <vector>

#include "game/runtime/shared/projected/SharedProjectedBodyPresentation.h"

bool test_shared_projected_body_presentation_contract(std::string& outFail) {
    namespace body_presentation = game::runtime::shared_projected_body_presentation;
    namespace projected_models = game::runtime::shared_projected_unit_models;
    namespace tail_fire = game::runtime::shared_tail_fire_fallback;
    namespace world_batches = game::runtime::shared_world_batches;

    projected_models::Result renderResult{};
    IRenderBackend::WorldSceneFrame worldSceneFrame{};
    std::vector<world_batches::WorldIndexedBatch> worldIndexedBatches;
    std::unordered_map<int, tail_fire::Anchor> sharedTailFireAnchors;

    auto summary = body_presentation::summarizeProjectedBodyPresentation(
        renderResult,
        &worldSceneFrame,
        &worldIndexedBatches,
        &sharedTailFireAnchors);
    if (summary.producedScratch) {
        outFail = "Empty projected body presentation should not report scratch output.";
        return false;
    }

    renderResult.drewModelMesh = true;
    summary = body_presentation::summarizeProjectedBodyPresentation(
        renderResult,
        &worldSceneFrame,
        &worldIndexedBatches,
        &sharedTailFireAnchors);
    if (!summary.producedScratch) {
        outFail = "A rendered projected model mesh should count as body presentation scratch.";
        return false;
    }

    renderResult = {};
    worldSceneFrame.drawClasses.emplace_back();
    summary = body_presentation::summarizeProjectedBodyPresentation(
        renderResult,
        &worldSceneFrame,
        &worldIndexedBatches,
        &sharedTailFireAnchors);
    if (!summary.producedScratch) {
        outFail = "World-scene draw classes should count as projected body presentation scratch.";
        return false;
    }

    worldSceneFrame.drawClasses.clear();
    worldIndexedBatches.emplace_back();
    summary = body_presentation::summarizeProjectedBodyPresentation(
        renderResult,
        &worldSceneFrame,
        &worldIndexedBatches,
        &sharedTailFireAnchors);
    if (!summary.producedScratch) {
        outFail = "Indexed body batches should count as projected body presentation scratch.";
        return false;
    }

    worldIndexedBatches.clear();
    sharedTailFireAnchors.emplace(7, tail_fire::Anchor{});
    summary = body_presentation::summarizeProjectedBodyPresentation(
        renderResult,
        &worldSceneFrame,
        &worldIndexedBatches,
        &sharedTailFireAnchors);
    if (!summary.producedScratch) {
        outFail = "Tail Fire anchor scratch should count as projected presentation scratch.";
        return false;
    }

    return true;
}
