#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "engine/render/IRenderBackend.h"
#include "game/GameConfig.h"
#include "game/PokemonInstance.h"
#include "game/config/GameDataDb.h"
#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "game/runtime/shared/backend/SharedBackendPoseEval.h"
#include "game/runtime/shared/projected/SharedProjectedDebugVfx.h"
#include "game/runtime/shared/projected/SharedProjectedRenderItems.h"
#include "game/runtime/shared/projected/SharedProjectedUnitModelRenderer.h"
#include "game/runtime/shared/projected/SharedProjectedUnitWorldSceneRenderer.h"
#include "game/runtime/shared/scene/SharedWorldScene.h"

namespace {

class FakeWorldSceneBackend final : public IRenderBackend {
public:
    const char* backendId() const override { return "test"; }
    void beginFrame(float, float, float, float) override {}
    void endFrame() override {}
    void onResize(int, int) override {}
    bool requiresOpenGLContext() const override { return false; }
    bool handlesPresentation() const override { return false; }
    bool supportsWorldSceneFastPath() const override { return true; }
    void shutdown() override {}
};

bool expect(bool condition, const std::string& message, std::string& outFail) {
    if (condition) {
        return true;
    }
    outFail = message;
    return false;
}

} // namespace

bool test_shared_projected_unit_world_scene_tail_fire_fallback(std::string& outFail) {
    FakeWorldSceneBackend backend;
    GameDataDb dataDb;
    PokemonInstance unit;
    unit.id = 7;
    unit.name = "charmander";
    unit.alive = true;
    unit.fainting = false;

    game::runtime::render_model::MeshData mesh;
    game::runtime::shared_backend_pose::PoseEval poseEval;
    IRenderBackend::DebugQuad tint{};
    std::vector<IRenderBackend::DebugTriangle> worldTriangles;
    std::vector<IRenderBackend::WorldTriangle> world3DTriangles;
    std::vector<IRenderBackend::DebugLine> lines;
    game::runtime::shared_projected_debug::ProjectedDebugVfxBuilder projectedDebug(
        false,
        glm::mat4(1.0f),
        glm::mat4(1.0f),
        720,
        glm::vec4(0.0f, 0.0f, 1280.0f, 720.0f),
        worldTriangles,
        world3DTriangles,
        lines);
    game::runtime::shared_projected_render_items::ProjectedRenderItemRegistry projectedRenderItems;
    game::runtime::shared_world_scene::WorldSceneRegistry worldSceneRegistry;
    IRenderBackend::WorldSceneFrame worldSceneFrame;
    std::vector<game::runtime::shared_projected_scene::DepthTri> modelDepthTris;
    std::vector<game::runtime::shared_projected_scene::DepthWorldTri> modelDepthWorldTris;
    std::size_t remainingModelTrianglesBudget = 4096u;

    game::runtime::shared_projected_unit_models::Args args;
    args.renderer = &backend;
    args.dataDb = &dataDb;
    args.unit = &unit;
    args.pose = nullptr;
    args.meshForUnit = &mesh;
    args.scenePose = &poseEval;
    args.tint = &tint;
    args.projectedDebug = &projectedDebug;
    args.projectedRenderItems = &projectedRenderItems;
    args.worldSceneRegistry = &worldSceneRegistry;
    args.worldSceneFrame = &worldSceneFrame;
    args.modelDepthTris = &modelDepthTris;
    args.modelDepthWorldTris = &modelDepthWorldTris;
    args.remainingModelTrianglesBudget = &remainingModelTrianglesBudget;
    args.world3DTriangles = &world3DTriangles;
    args.backendModelTriangleLimit = []() { return static_cast<std::size_t>(4096u); };
    args.backendModelFullMeshEnabled = []() { return true; };
    args.backendModelFastTexturedPathEnabled = []() { return true; };
    args.backendModelBackfaceCullingEnabled = []() { return true; };

    game::runtime::shared_projected_unit_models::Result result{};
    const bool usedWorldScene =
        game::runtime::shared_projected_unit_world_scene::tryRenderProjectedUnitModelWorldScene(
            args,
            result);

    if (!expect(!usedWorldScene,
                "Charmander should stay on the legacy projected-model path so authored tail-fire playback remains intact.",
                outFail)) {
        return false;
    }
    if (!expect(!result.drewModelMesh && !result.skipUnit,
                "Tail-fire fallback should decline the world-scene path without consuming the model render.",
                outFail)) {
        return false;
    }
    if (!expect(worldSceneFrame.drawClasses.empty() && worldSceneRegistry.renderObjects.empty(),
                "Tail-fire fallback should not populate persistent world-scene state for Charmander.",
                outFail)) {
        return false;
    }

    return true;
}
