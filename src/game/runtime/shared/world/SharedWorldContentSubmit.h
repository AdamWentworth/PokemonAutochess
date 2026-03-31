#pragma once

#include "engine/render/IRenderBackend.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"

#include <vector>

class Camera3D;
struct EngineRenderBuildBreakdown;

namespace game::runtime::shared_world_content_submit {

struct Args {
    IRenderBackend* renderer = nullptr;
    const Camera3D* camera = nullptr;
    int drawableW = 0;
    int drawableH = 0;
    bool hasWorldViewProj = false;
    bool supportsWorldTriangles3D = false;
    bool supportsWorldIndexedMeshes = false;
    const float* worldViewProj = nullptr;
    std::vector<IRenderBackend::DebugQuad>* worldBackgroundQuads = nullptr;
    std::vector<IRenderBackend::WorldTriangle>* world3DTriangles = nullptr;
    const IRenderBackend::WorldSceneView* worldSceneView = nullptr;
    const IRenderBackend::WorldSceneFrame* worldSceneFrame = nullptr;
    std::vector<shared_world_batches::WorldIndexedBatch>* worldIndexedBatches = nullptr;
    EngineRenderBuildBreakdown* renderBuildBreakdown = nullptr;
};

void submitOpaqueAndIndexedWorldContent(const Args& args);

} // namespace game::runtime::shared_world_content_submit
