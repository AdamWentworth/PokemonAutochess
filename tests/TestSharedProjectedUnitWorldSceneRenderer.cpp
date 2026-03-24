#include <string>
#include <unordered_map>
#include <vector>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "engine/render/IRenderBackend.h"
#include "game/GameConfig.h"
#include "game/PokemonInstance.h"
#include "game/config/GameDataDb.h"
#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "game/runtime/shared/backend/SharedBackendPoseEval.h"
#include "game/runtime/shared/projected/SharedProjectedDebugVfx.h"
#include "game/runtime/shared/projected/SharedProjectedRenderItems.h"
#include "game/runtime/shared/projected/SharedProjectedUnitModelRenderer.h"
#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshSupport.h"
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
    bool getWorldSceneFastPathCaps(WorldSceneFastPathCaps& outCaps) const override {
        outCaps = WorldSceneFastPathCaps{};
        outCaps.supported = true;
        outCaps.supportsSkinnedInstancing = true;
        return true;
    }
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

bool test_shared_projected_unit_world_scene_rigid_node_transform(std::string& outFail) {
    FakeWorldSceneBackend backend;
    GameDataDb dataDb;
    PokemonInstance unit;
    unit.id = 11;
    unit.name = "testmon";
    unit.alive = true;
    unit.speciesScale = 1.0f;
    unit.modelScaleCorrection = 1.0f;

    game::runtime::render_model::MeshData mesh;
    mesh.modelScaleFactor = 1.0f;
    mesh.boundsMin = glm::vec3(-0.5f, 0.0f, -0.5f);
    mesh.boundsMax = glm::vec3(0.5f, 1.0f, 0.5f);
    mesh.vertices.resize(3u);
    mesh.vertices[0].position = glm::vec3(0.0f, 0.0f, 0.0f);
    mesh.vertices[1].position = glm::vec3(1.0f, 0.0f, 0.0f);
    mesh.vertices[2].position = glm::vec3(0.0f, 1.0f, 0.0f);
    mesh.indices = {0u, 1u, 2u};
    mesh.triangleSubmesh = {0u};
    mesh.triangleNodeIndex = {1};
    mesh.submeshBaseTextures.resize(1u);
    mesh.submeshBaseTextures[0].width = 1;
    mesh.submeshBaseTextures[0].height = 1;
    mesh.submeshBaseTextures[0].rgba = {255u, 255u, 255u, 255u};
    mesh.submeshMeshIndex = {0};
    mesh.meshIndexToNode = {1};
    mesh.nodesDefault.resize(2u);
    mesh.nodeChildren.resize(2u);
    mesh.nodeChildren[0].push_back(1);
    mesh.nodeParent = {-1, 0};
    mesh.nodeSkin = {-1, -1};
    mesh.sceneRoots = {0};
    mesh.bindNodeGlobals = {glm::mat4(1.0f), glm::mat4(1.0f)};

    game::runtime::shared_backend_pose::PoseEval poseEval;
    poseEval.hasScenePose = true;
    poseEval.nodeLocals = mesh.nodesDefault;
    poseEval.nodeGlobals = {
        glm::mat4(1.0f),
        glm::translate(glm::mat4(1.0f), glm::vec3(0.75f, 0.25f, -0.5f)),
    };

    IRenderBackend::DebugQuad tint{};
    tint.r = 1.0f;
    tint.g = 1.0f;
    tint.b = 1.0f;
    tint.a = 1.0f;

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
    args.meshForUnit = &mesh;
    args.scenePose = &poseEval;
    args.scenePoseReady = true;
    args.backendId = "d3d12";
    args.enableGpuClipSkinning = true;
    args.tint = &tint;
    args.supportsWorldTriangles3D = true;
    args.supportsWorldIndexedMeshes = true;
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

    if (!expect(usedWorldScene,
                "Rigid submeshes attached to a scene node should still use the world-scene fast path.",
                outFail)) {
        return false;
    }
    if (!expect(result.drewModelMesh && !result.skipUnit,
                "Rigid-node world-scene rendering should produce a model draw without skipping the unit.",
                outFail)) {
        return false;
    }
    if (!expect(worldSceneFrame.drawClasses.size() == 1u &&
                    worldSceneFrame.drawClasses[0].instances.size() == 1u,
                "Rigid-node test should produce exactly one world-scene instance.",
                outFail)) {
        return false;
    }

    const auto& instance = worldSceneFrame.drawClasses[0].instances[0];
    if (!expect(instance.gpuSkinning == 0u,
                "Rigid-node test mesh should stay on the rigid instance path.",
                outFail)) {
        return false;
    }
    if (!expect(std::abs(instance.modelMatrix[12] - 0.75f) < 0.0001f &&
                    std::abs(instance.modelMatrix[13] - 0.2525f) < 0.0001f &&
                    std::abs(instance.modelMatrix[14] + 0.5f) < 0.0001f,
                "Rigid-node batches should carry their scene-node transform into the world-scene instance matrix. got=(" +
                    std::to_string(instance.modelMatrix[12]) + "," +
                    std::to_string(instance.modelMatrix[13]) + "," +
                    std::to_string(instance.modelMatrix[14]) + ")",
                outFail)) {
        return false;
    }

    return true;
}

bool test_shared_projected_unit_world_scene_rigid_under_skin_transform(std::string& outFail) {
    FakeWorldSceneBackend backend;
    GameDataDb dataDb;
    PokemonInstance unit;
    unit.id = 12;
    unit.name = "testmon";
    unit.alive = true;
    unit.speciesScale = 1.0f;
    unit.modelScaleCorrection = 1.0f;

    game::runtime::render_model::MeshData mesh;
    mesh.modelScaleFactor = 1.0f;
    mesh.boundsMin = glm::vec3(-0.5f, 0.0f, -0.5f);
    mesh.boundsMax = glm::vec3(0.5f, 1.0f, 0.5f);
    mesh.vertices.resize(3u);
    mesh.vertices[0].position = glm::vec3(0.0f, 0.0f, 0.0f);
    mesh.vertices[1].position = glm::vec3(1.0f, 0.0f, 0.0f);
    mesh.vertices[2].position = glm::vec3(0.0f, 1.0f, 0.0f);
    mesh.indices = {0u, 1u, 2u};
    mesh.triangleSubmesh = {0u};
    mesh.triangleNodeIndex = {1};
    mesh.submeshBaseTextures.resize(1u);
    mesh.submeshBaseTextures[0].width = 1;
    mesh.submeshBaseTextures[0].height = 1;
    mesh.submeshBaseTextures[0].rgba = {255u, 255u, 255u, 255u};
    mesh.submeshMeshIndex = {0};
    mesh.meshIndexToNode = {1};
    mesh.nodesDefault.resize(2u);
    mesh.nodeChildren.resize(2u);
    mesh.nodeChildren[0].push_back(1);
    mesh.nodeParent = {-1, 0};
    mesh.nodeSkin = {-1, 0};
    mesh.sceneRoots = {0};
    mesh.bindNodeGlobals = {glm::mat4(1.0f), glm::mat4(1.0f)};
    mesh.skins.resize(1u);
    mesh.skins[0].joints = {1};
    mesh.skins[0].inverseBind = {glm::mat4(1.0f)};

    game::runtime::shared_backend_pose::PoseEval poseEval;
    poseEval.hasScenePose = true;
    poseEval.nodeLocals = mesh.nodesDefault;
    poseEval.nodeGlobals = {
        glm::mat4(1.0f),
        glm::translate(glm::mat4(1.0f), glm::vec3(0.40f, 0.60f, -0.25f)),
    };

    IRenderBackend::DebugQuad tint{};
    tint.r = 1.0f;
    tint.g = 1.0f;
    tint.b = 1.0f;
    tint.a = 1.0f;

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
    args.meshForUnit = &mesh;
    args.scenePose = &poseEval;
    args.scenePoseReady = true;
    args.backendId = "d3d12";
    args.enableGpuClipSkinning = true;
    args.tint = &tint;
    args.supportsWorldTriangles3D = true;
    args.supportsWorldIndexedMeshes = true;
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

    const auto fallback =
        game::runtime::shared_projected_unit_backend_mesh_support::buildSubmeshNodeFallback(mesh);
    const auto* fastCache =
        game::runtime::shared_projected_unit_backend_mesh_support::ensureFastTexturedMeshTemplateCache(
            &mesh,
            fallback,
            1u,
            true);

    game::runtime::shared_projected_unit_models::Result result{};
    const bool usedWorldScene =
        game::runtime::shared_projected_unit_world_scene::tryRenderProjectedUnitModelWorldScene(
            args,
            result);

    if (!usedWorldScene) {
        if (!fastCache || fastCache->batches.empty()) {
            outFail = "Rigid triangles under a skinned node should still use the world-scene fast path. fastCache was empty.";
        } else {
            const auto& batch = fastCache->batches[0];
            outFail = "Rigid triangles under a skinned node should still use the world-scene fast path. fastCache[0]: skinned=" +
                std::to_string(batch.skinnedBatch ? 1 : 0) +
                " palette=" + std::to_string(batch.gpuJointPalette.size()) +
                " indices=" + std::to_string(batch.indices.size()) +
                " verts=" + std::to_string(batch.gpuTemplateVertices.size()) +
                " batches=" + std::to_string(fastCache->batches.size());
        }
        return false;
    }
    if (!expect(result.drewModelMesh && !result.skipUnit,
                "Rigid-under-skin world-scene rendering should still produce a model draw.",
                outFail)) {
        return false;
    }
    if (!expect(worldSceneFrame.drawClasses.size() == 1u &&
                    worldSceneFrame.drawClasses[0].instances.size() == 1u,
                "Rigid-under-skin test should produce exactly one world-scene instance.",
                outFail)) {
        return false;
    }

    const auto& instance = worldSceneFrame.drawClasses[0].instances[0];
    if (!expect(instance.gpuSkinning == 0u,
                "Zero-weight triangles under a skinned node should stay rigid so they inherit the animated node transform instead of root-space full skinning.",
                outFail)) {
        return false;
    }
    if (!expect(std::abs(instance.modelMatrix[12] - 0.40f) < 0.0001f &&
                    std::abs(instance.modelMatrix[13] - 0.6025f) < 0.0001f &&
                    std::abs(instance.modelMatrix[14] + 0.25f) < 0.0001f,
                "Rigid-under-skin batches should carry their animated node transform into the world-scene instance matrix. got=(" +
                    std::to_string(instance.modelMatrix[12]) + "," +
                    std::to_string(instance.modelMatrix[13]) + "," +
                    std::to_string(instance.modelMatrix[14]) + ")",
                outFail)) {
        return false;
    }

    return true;
}