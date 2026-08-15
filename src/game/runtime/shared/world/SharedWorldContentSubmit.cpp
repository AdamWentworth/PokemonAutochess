#include "game/runtime/shared/world/SharedWorldContentSubmit.h"

#include "engine/core/EngineServices.h"
#include "engine/render/Camera3D.h"

#include <algorithm>
#include <chrono>

namespace game::runtime::shared_world_content_submit {

namespace {

using Clock = std::chrono::steady_clock;

float toMs(const Clock::time_point& start, const Clock::time_point& end) {
    return static_cast<float>(
        std::chrono::duration<double, std::milli>(end - start).count());
}

} // namespace

void submitOpaqueAndIndexedWorldContent(const Args& args) {
    if (!args.renderer || args.drawableW <= 0 || args.drawableH <= 0) {
        return;
    }

    auto* renderer = args.renderer;
    auto* renderBuildBreakdown = args.renderBuildBreakdown;
    renderer->beginWorldSceneColorPass(args.drawableW, args.drawableH);

    if (args.worldBackgroundQuads && !args.worldBackgroundQuads->empty()) {
        const auto stageStart = Clock::now();
        renderer->drawDebugQuads(
            args.worldBackgroundQuads->data(),
            args.worldBackgroundQuads->size(),
            args.drawableW,
            args.drawableH);
        if (renderBuildBreakdown) {
            renderBuildBreakdown->worldBackgroundMs += toMs(stageStart, Clock::now());
        }
    }

    if (args.world3DTriangles &&
        !args.world3DTriangles->empty() &&
        args.hasWorldViewProj &&
        args.supportsWorldTriangles3D &&
        args.worldViewProj) {
        const auto stageStart = Clock::now();
        renderer->drawWorldTriangles(
            args.world3DTriangles->data(),
            args.world3DTriangles->size(),
            args.worldViewProj,
            args.drawableW,
            args.drawableH);
        if (renderBuildBreakdown) {
            renderBuildBreakdown->worldTriangles3dMs += toMs(stageStart, Clock::now());
        }
    }

    if (args.worldSceneView &&
        args.worldSceneFrame &&
        !args.worldSceneFrame->drawClasses.empty() &&
        args.hasWorldViewProj &&
        renderer->supportsWorldSceneFastPath()) {
        const auto stageStart = Clock::now();
        renderer->submitWorldScene(*args.worldSceneFrame, *args.worldSceneView);
        if (renderBuildBreakdown) {
            const float elapsedMs = toMs(stageStart, Clock::now());
            renderBuildBreakdown->worldSceneSubmitMs += elapsedMs;
            renderBuildBreakdown->worldIndexedMs += elapsedMs;
        }
    }

    if (args.worldIndexedBatches &&
        !args.worldIndexedBatches->empty() &&
        args.hasWorldViewProj &&
        args.supportsWorldIndexedMeshes &&
        args.worldViewProj) {
        float cameraWorldPos3[3] = {0.0f, 7.0f, 9.0f};
        float cameraForward3[3] = {0.0f, -0.6139406f, -0.7893522f};
        float cameraTarget3[3] = {0.0f, -1.0f, 0.0f};
        if (args.camera) {
            const glm::vec3 camPos = args.camera->getPosition();
            const glm::vec3 camForward = args.camera->getDirection();
            const glm::vec3 camTarget = args.camera->getTarget();
            cameraWorldPos3[0] = camPos.x;
            cameraWorldPos3[1] = camPos.y;
            cameraWorldPos3[2] = camPos.z;
            cameraForward3[0] = camForward.x;
            cameraForward3[1] = camForward.y;
            cameraForward3[2] = camForward.z;
            cameraTarget3[0] = camTarget.x;
            cameraTarget3[1] = camTarget.y;
            cameraTarget3[2] = camTarget.z;
        }
        const float cameraForwardScale =
            std::clamp(args.cameraForwardScale, 1.0f, 4.0f);
        cameraForward3[0] *= cameraForwardScale;
        cameraForward3[1] *= cameraForwardScale;
        cameraForward3[2] *= cameraForwardScale;

        const auto stageStart = Clock::now();
        shared_world_batches::submitWorldIndexedBatches(
            *renderer,
            *args.worldIndexedBatches,
            args.worldViewProj,
            args.drawableW,
            args.drawableH,
            cameraWorldPos3,
            cameraForward3,
            cameraTarget3);
        if (renderBuildBreakdown) {
            const float elapsedMs = toMs(stageStart, Clock::now());
            renderBuildBreakdown->worldIndexedBatchSubmitMs += elapsedMs;
            renderBuildBreakdown->worldIndexedMs += elapsedMs;
        }
    }
    renderer->endWorldSceneColorPass();
}

} // namespace game::runtime::shared_world_content_submit
