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
#include "game/runtime/shared/projected/core/SharedProjectedDebugVfx.h"
#include "game/runtime/shared/projected/core/SharedProjectedRenderItems.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshPrep.h"
#include "game/runtime/shared/projected/unit/SharedProjectedUnitModelRenderer.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshSupport.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshTransforms.h"
#include "game/runtime/shared/projected/world_scene/SharedProjectedUnitWorldSceneRenderer.h"
#include "game/runtime/shared/scene/SharedWorldScene.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFirePlaybackPolicy.h"

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

game::runtime::render_model::MeshData makeCharmanderHybridWorldSceneMesh() {
    game::runtime::render_model::MeshData mesh;
    mesh.modelScaleFactor = 1.0f;
    mesh.boundsMin = glm::vec3(-0.5f, 0.0f, -0.8f);
    mesh.boundsMax = glm::vec3(0.5f, 1.2f, 0.5f);
    mesh.vertices.resize(6u);
    mesh.vertices[0].position = glm::vec3(-0.3f, 0.0f, 0.0f);
    mesh.vertices[1].position = glm::vec3(0.3f, 0.0f, 0.0f);
    mesh.vertices[2].position = glm::vec3(0.0f, 0.8f, 0.0f);
    mesh.vertices[3].position = glm::vec3(-0.06f, 0.0f, 0.0f);
    mesh.vertices[4].position = glm::vec3(0.06f, 0.0f, 0.0f);
    mesh.vertices[5].position = glm::vec3(0.0f, 0.18f, -0.08f);
    mesh.indices = {0u, 1u, 2u, 3u, 4u, 5u};
    mesh.triangleSubmesh = {0u, 1u};
    mesh.triangleNodeIndex = {1, 5};
    mesh.submeshBaseTextures.resize(2u);
    for (auto& texture : mesh.submeshBaseTextures) {
        texture.width = 1;
        texture.height = 1;
        texture.rgba = {255u, 255u, 255u, 255u};
    }
    mesh.submeshMeshIndex = {0, 1};
    mesh.nodeNames = {
        "root",
        "PM0004_Charmander",
        "tail_06",
        "fire_anchor_base",
        "fire_anchor_tip",
        "tail_fire_mesh",
    };
    mesh.nodeMesh = {-1, 0, -1, -1, -1, 1};
    mesh.meshIndexToNode = {1, 5};
    mesh.nodesDefault.resize(6u);
    mesh.nodeChildren.resize(6u);
    mesh.nodeChildren[0] = {1};
    mesh.nodeChildren[1] = {2, 5};
    mesh.nodeChildren[2] = {3};
    mesh.nodeChildren[3] = {4};
    mesh.nodeParent = {-1, 0, 1, 2, 3, 1};
    mesh.nodeSkin = {-1, -1, -1, -1, -1, -1};
    mesh.sceneRoots = {0};
    mesh.bindNodeGlobals.resize(6u, glm::mat4(1.0f));
    return mesh;
}

game::runtime::shared_backend_pose::PoseEval makeCharmanderHybridScenePose() {
    game::runtime::shared_backend_pose::PoseEval poseEval;
    poseEval.hasScenePose = true;
    poseEval.nodeLocals.resize(6u);

    const glm::vec3 bodyPos(0.15f, 0.08f, -0.05f);
    poseEval.nodeGlobals = {
        glm::mat4(1.0f),
        glm::translate(glm::mat4(1.0f), bodyPos),
        glm::translate(glm::mat4(1.0f), bodyPos + glm::vec3(-0.10f, 0.35f, -0.42f)),
        glm::translate(glm::mat4(1.0f), bodyPos + glm::vec3(-0.11f, 0.43f, -0.52f)),
        glm::translate(glm::mat4(1.0f), bodyPos + glm::vec3(-0.12f, 0.64f, -0.62f)),
        glm::translate(glm::mat4(1.0f), bodyPos + glm::vec3(-0.11f, 0.43f, -0.52f)),
    };
    return poseEval;
}

game::runtime::SharedBackendTextureCacheEntry makeAtlasEntry() {
    game::runtime::SharedBackendTextureCacheEntry entry;
    entry.valid = true;
    entry.width = 1;
    entry.height = 1;
    entry.rgba = {255u, 255u, 255u, 255u};
    return entry;
}

void populateCharmanderWorldSceneArgs(
    FakeWorldSceneBackend& backend,
    GameDataDb& dataDb,
    PokemonInstance& unit,
    game::runtime::render_model::MeshData& mesh,
    game::runtime::shared_backend_pose::PoseEval& poseEval,
    IRenderBackend::DebugQuad& tint,
    game::runtime::shared_projected_debug::ProjectedDebugVfxBuilder& projectedDebug,
    game::runtime::shared_projected_render_items::ProjectedRenderItemRegistry& projectedRenderItems,
    game::runtime::shared_world_scene::WorldSceneRegistry& worldSceneRegistry,
    IRenderBackend::WorldSceneFrame& worldSceneFrame,
    std::unordered_map<int, game::runtime::shared_tail_fire_fallback::Anchor>& sharedTailFireAnchors,
    std::vector<game::runtime::shared_world_batches::WorldIndexedBatch>& worldIndexedBatches,
    std::unordered_map<std::string, game::runtime::SharedBackendTextureCacheEntry>& backendTextureByPath,
    std::vector<game::runtime::shared_projected_scene::DepthTri>& modelDepthTris,
    std::vector<game::runtime::shared_projected_scene::DepthWorldTri>& modelDepthWorldTris,
    std::size_t& remainingModelTrianglesBudget,
    std::vector<IRenderBackend::WorldTriangle>& world3DTriangles,
    game::runtime::shared_projected_unit_models::Args& args) {
    unit.id = 7;
    unit.name = "charmander";
    unit.alive = true;
    unit.fainting = false;
    unit.speciesScale = 1.0f;
    unit.modelScaleCorrection = 1.0f;

    tint.r = 1.0f;
    tint.g = 1.0f;
    tint.b = 1.0f;
    tint.a = 1.0f;

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
    args.sharedTailFireAnchors = &sharedTailFireAnchors;
    args.worldIndexedBatches = &worldIndexedBatches;
    args.backendTextureByPath = &backendTextureByPath;
    args.modelDepthTris = &modelDepthTris;
    args.modelDepthWorldTris = &modelDepthWorldTris;
    args.remainingModelTrianglesBudget = &remainingModelTrianglesBudget;
    args.world3DTriangles = &world3DTriangles;
    args.backendModelTriangleLimit = []() { return static_cast<std::size_t>(4096u); };
    args.backendModelFullMeshEnabled = []() { return true; };
    args.backendModelFastTexturedPathEnabled = []() { return true; };
    args.backendModelBackfaceCullingEnabled = []() { return true; };
}

} // namespace

bool test_shared_projected_unit_world_scene_tail_fire_fallback(std::string& outFail) {
    FakeWorldSceneBackend backend;
    GameDataDb dataDb;
    PokemonInstance unit;
    game::runtime::render_model::MeshData mesh = makeCharmanderHybridWorldSceneMesh();
    mesh.assetCacheIdentity = "test/charmander-tail-fire-fallback";
    game::runtime::shared_backend_pose::PoseEval poseEval = makeCharmanderHybridScenePose();
    IRenderBackend::DebugQuad tint{};
    std::unordered_map<int, game::runtime::shared_tail_fire_fallback::Anchor> sharedTailFireAnchors;
    std::vector<game::runtime::shared_world_batches::WorldIndexedBatch> worldIndexedBatches;
    std::unordered_map<std::string, game::runtime::SharedBackendTextureCacheEntry> backendTextureByPath;
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
    populateCharmanderWorldSceneArgs(
        backend,
        dataDb,
        unit,
        mesh,
        poseEval,
        tint,
        projectedDebug,
        projectedRenderItems,
        worldSceneRegistry,
        worldSceneFrame,
        sharedTailFireAnchors,
        worldIndexedBatches,
        backendTextureByPath,
        modelDepthTris,
        modelDepthWorldTris,
        remainingModelTrianglesBudget,
        world3DTriangles,
        args);

    game::runtime::shared_projected_unit_models::Result result{};
    const bool usedWorldScene =
        game::runtime::shared_projected_unit_world_scene::tryRenderProjectedUnitModelWorldScene(
            args,
            result);

    if (!expect(!usedWorldScene,
                "World-scene Charmander should decline cleanly when the dedicated tail-fire sidecar cannot be built.",
                outFail)) {
        return false;
    }
    if (!expect(!result.drewModelMesh && !result.skipUnit,
                "Tail-fire sidecar failure should leave the model render untouched so the caller can fall back.",
                outFail)) {
        return false;
    }
    if (!expect(worldSceneFrame.drawClasses.empty() &&
                    worldSceneRegistry.renderObjects.empty() &&
                    worldIndexedBatches.empty(),
                "Tail-fire sidecar failure should not populate world-scene or indexed render state.",
                outFail)) {
        return false;
    }

    return true;
}

bool test_shared_projected_unit_world_scene_tail_fire_hybrid_path(std::string& outFail) {
    FakeWorldSceneBackend backend;
    GameDataDb dataDb;
    PokemonInstance unit;
    game::runtime::render_model::MeshData mesh = makeCharmanderHybridWorldSceneMesh();
    mesh.assetCacheIdentity = "test/charmander-tail-fire-hybrid";
    game::runtime::shared_backend_pose::PoseEval poseEval = makeCharmanderHybridScenePose();
    IRenderBackend::DebugQuad tint{};
    std::unordered_map<int, game::runtime::shared_tail_fire_fallback::Anchor> sharedTailFireAnchors;
    std::vector<game::runtime::shared_world_batches::WorldIndexedBatch> worldIndexedBatches;
    std::unordered_map<std::string, game::runtime::SharedBackendTextureCacheEntry> backendTextureByPath;
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
    populateCharmanderWorldSceneArgs(
        backend,
        dataDb,
        unit,
        mesh,
        poseEval,
        tint,
        projectedDebug,
        projectedRenderItems,
        worldSceneRegistry,
        worldSceneFrame,
        sharedTailFireAnchors,
        worldIndexedBatches,
        backendTextureByPath,
        modelDepthTris,
        modelDepthWorldTris,
        remainingModelTrianglesBudget,
        world3DTriangles,
        args);
    args.ensureBackendTextureLoaded =
        [&](const std::string& path, bool) -> game::runtime::SharedBackendTextureCacheEntry* {
            auto [it, inserted] = backendTextureByPath.emplace(path, makeAtlasEntry());
            if (!inserted) {
                it->second = makeAtlasEntry();
            }
            return &it->second;
        };

    game::runtime::shared_projected_unit_models::Result result{};
    const bool usedWorldScene =
        game::runtime::shared_projected_unit_world_scene::tryRenderProjectedUnitModelWorldScene(
            args,
            result);

    if (!expect(usedWorldScene,
                "Charmander should use the hybrid world-scene path when the dedicated tail-fire sidecar is available.",
                outFail)) {
        return false;
    }
    if (!expect(result.drewModelMesh && !result.skipUnit,
                "Hybrid Charmander rendering should consume the model render without skipping the unit.",
                outFail)) {
        return false;
    }
    if (!expect(worldSceneFrame.drawClasses.size() == 1u &&
                    worldSceneFrame.drawClasses[0].instances.size() == 1u &&
                    worldSceneRegistry.renderObjects.size() == 1u,
                "Hybrid Charmander rendering should send the body through one world-scene draw class while keeping the fire sidecar separate.",
                outFail)) {
        return false;
    }
    if (!expect(worldSceneRegistry.materials.size() == 1u &&
                    worldSceneRegistry.materials[0].textureRgba ==
                        mesh.submeshBaseTextures[0].rgba.data() &&
                    worldSceneRegistry.materials[0].textureWidth ==
                        mesh.submeshBaseTextures[0].width &&
                    worldSceneRegistry.materials[0].textureHeight ==
                        mesh.submeshBaseTextures[0].height &&
                    worldSceneRegistry.materials[0].textureKey !=
                        "__fallback_white_1x1__" &&
                    worldSceneRegistry.materials[0].materialMode == 2u,
                "Hybrid character body submission must preserve its authored base-color texture payload and lit material mode.",
                outFail)) {
        return false;
    }
    if (!expect(worldIndexedBatches.size() == 1u &&
                    game::runtime::shared_tail_fire_playback_policy::hasAuthoredFireMeshBatches(
                        worldIndexedBatches),
                "Hybrid Charmander rendering should emit exactly one authored tail-fire sidecar batch.",
                outFail)) {
        return false;
    }

    const auto anchorIt = sharedTailFireAnchors.find(unit.id);
    if (!expect(anchorIt != sharedTailFireAnchors.end() &&
                    anchorIt->second.valid &&
                    anchorIt->second.meshCarrierActive &&
                    anchorIt->second.exactFireAnchor,
                "Hybrid Charmander rendering should export a mesh-carrier tail-fire anchor from the authored fire rig.",
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

bool test_shared_projected_unit_gpu_bind_pose_skinning(std::string& outFail) {
    namespace backend_mesh =
        game::runtime::shared_projected_unit_backend_mesh;
    namespace prep =
        game::runtime::shared_projected_unit_backend_mesh_prep;
    namespace transforms =
        game::runtime::shared_projected_unit_backend_mesh_transforms;

    PokemonInstance unit;
    game::runtime::render_prep_pose::ProceduralPose proceduralPose;
    game::runtime::render_model::MeshData mesh;
    mesh.vertices.resize(3u);
    for (auto& vertex : mesh.vertices) {
        vertex.j0 = 0u;
        vertex.w0 = 1.0f;
    }
    mesh.nodesDefault.resize(2u);
    mesh.nodeSkin = {-1, 0};
    mesh.bindNodeGlobals = {
        glm::mat4(1.0f),
        glm::translate(
            glm::mat4(1.0f),
            glm::vec3(0.25f, 0.50f, -0.75f))};
    mesh.skins.resize(1u);
    mesh.skins[0].joints = {1};
    mesh.skins[0].inverseBind = {glm::mat4(1.0f)};

    game::runtime::shared_backend_pose::PoseEval bindPose;
    bindPose.hasScenePose = true;
    bindPose.hasClipPose = false;
    bindPose.nodeLocals = mesh.nodesDefault;
    bindPose.nodeGlobals = mesh.bindNodeGlobals;

    backend_mesh::Args args;
    args.unit = &unit;
    args.pose = &proceduralPose;
    args.scenePose = &bindPose;
    args.scenePoseReady = true;
    args.backendId = "d3d12";
    args.enableClipSkinning = true;
    args.enableGpuClipSkinning = true;

    prep::PreparedState prepared;
    prepared.mesh = &mesh;
    prepared.scenePose = &bindPose;
    prepared.modelM = glm::mat4(1.0f);
    prepared.usePositionOnlyVertexPath = true;

    transforms::Resolver resolver;
    resolver.initialize(args, prepared);
    std::array<float, 16> modelMatrix{};
    std::uint8_t skinningMode = 0u;
    std::vector<float> skinMatrices;
    std::uint32_t skinMatrixCount = 0u;
    if (!expect(
            !resolver.configureGpuClipSkinningBatch(
                1,
                nullptr,
                modelMatrix,
                skinningMode,
                skinMatrices,
                skinMatrixCount),
            "Bind-pose GPU skinning should remain opt-in for runtime callers that still require procedural CPU deformation.",
            outFail)) {
        return false;
    }

    args.enableGpuBindPoseSkinning = true;
    resolver.initialize(args, prepared);
    if (!expect(
            resolver.configureGpuClipSkinningBatch(
                1,
                nullptr,
                modelMatrix,
                skinningMode,
                skinMatrices,
                skinMatrixCount),
            "An explicitly requested bind pose should use the same textured GPU skinning path as an animated pose.",
            outFail)) {
        return false;
    }
    if (!expect(
            skinMatrixCount == 1u &&
                skinMatrices.size() >= 16u,
            "Bind-pose GPU skinning should publish the authored joint palette.",
            outFail)) {
        return false;
    }

    return true;
}

