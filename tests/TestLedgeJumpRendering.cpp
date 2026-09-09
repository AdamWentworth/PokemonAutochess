#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "engine/core/Paths.h"
#include "game/GameConfig.h"
#include "game/animation/LedgeJump.h"
#include "game/config/AnimSetLoader.h"
#include "game/runtime/session/SessionBackendUnitHydration.h"
#include "game/runtime/shared/projected/unit/SharedProjectedUnitRenderer.h"

namespace {
class SkeletonBackend final : public IRenderBackend {
  public:
    const char *backendId() const override { return "opengl"; }
    void beginFrame(float, float, float, float) override {}
    void endFrame() override {}
    void onResize(int, int) override {}
    bool requiresOpenGLContext() const override { return false; }
    bool handlesPresentation() const override { return false; }
    bool supportsWorldSceneFastPath() const override { return true; }
    bool getWorldSceneFastPathCaps(WorldSceneFastPathCaps &caps) const override {
        caps = {};
        caps.supported = true;
        caps.supportsSkinnedInstancing = true;
        return true;
    }
    void shutdown() override {}
};
} // namespace

// Exercise the public unit renderer, including its choice of skinning path,
// using the same modern and LGPE model families as the editor ledge scenario.
bool test_ledge_jump_rendering(std::string &outFail) {
    namespace runtime = game::runtime;
    GameDataDb db;
    if (!db.pokemon.loadConfig(engine::paths::data("config/pokemon_config.json"), nullptr) ||
        !db.flyers.loadConfig(engine::paths::data("config/flyers_config.json"), nullptr)) return false;
    GameConfigData config;
    GameWorld world(config);
    SkeletonBackend backend;
    runtime::session_backend_unit_hydration::BackendAnimRoleCache rolesCache;
    // Renderer pose/geometry caches key by mesh address, as they do for the
    // runtime's persistent model cache. Keep both model identities alive.
    std::array<runtime::render_model::MeshData, 2> meshes;
    std::size_t modelIndex = 0;
    for (const std::string species : {"bulbasaur", "rattata"}) {
        const auto *stats = db.pokemon.getStats(species);
        const std::string path = "assets/models/" + stats->model;
        auto& mesh = meshes[modelIndex++];
        if (!runtime::render_model::loadMeshFromCache(path, mesh, &outFail)) return false;
        const auto &roles = runtime::session_backend_unit_hydration::ensureBackendAnimRoles(path, &mesh, rolesCache);
        PokemonInstance unit;
        unit.id = PokemonInstance::getNextUnitID();
        unit.name = species;
        unit.backendModelPath = path;
        unit.alive = true;
        unit.isMoving = true;
        unit.moveFrom = {0.0f, 0.5f, 0.0f};
        unit.moveTo = {0.0f, 0.0f, 1.0f};
        unit.movementSpeed = 1.0f;
        AnimSet::applyAnimSetOverrides(unit, path, &db.flyers);
        unit.animIdleIndex = roles.idleIndex;
        unit.animMoveIndex = roles.moveIndex;
        unit.animJumpStartIndex = roles.jumpStartIndex;
        unit.animJumpLoopIndex = roles.jumpLoopIndex;
        unit.animJumpLandIndex = roles.jumpLandIndex;
        for (const auto &clip : mesh.animations)
            unit.backendAnimDurationsSec.push_back(clip.durationSec);

        std::vector<IRenderBackend::DebugTriangle> triangles;
        std::vector<IRenderBackend::WorldTriangle> worldTriangles;
        std::vector<IRenderBackend::DebugLine> lines, textLines;
        std::vector<IRenderBackend::DebugQuad> quads;
        std::vector<IRenderBackend::DebugSprite> sprites;
        const glm::mat4 view = glm::lookAt(glm::vec3(0, 4, 6), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
        const glm::mat4 proj = glm::perspective(glm::radians(50.0f), 1.0f, 0.01f, 50.0f);
        const glm::mat4 viewProj = proj * view;
        runtime::shared_projected_debug::ProjectedDebugVfxBuilder debug(true, view, proj, 800, {0, 0, 800, 800}, triangles, worldTriangles, lines);
        runtime::shared_projected_render_items::ProjectedRenderItemRegistry items;
        runtime::shared_world_scene::WorldSceneRegistry registry;
        IRenderBackend::WorldSceneFrame frame;
        runtime::shared_capture::SnapshotCache capture;
        std::vector<runtime::shared_world_batches::WorldIndexedBatch> batches;
        std::unordered_map<std::string, runtime::SharedBackendTextureCacheEntry> textures;
        std::vector<runtime::shared_projected_scene::DepthTri> depth;
        std::vector<runtime::shared_projected_scene::DepthWorldTri> worldDepth;
        std::size_t budget = 1000000;
        runtime::shared_unit_hud::Config hud;
        runtime::shared_projected_units::Args args;
        args.renderer = &backend;
        args.dataDb = &db;
        args.gameWorld = &world;
        args.minDim = 800;
        args.drawableW = args.drawableH = 800;
        args.cameraWorldPos = {0, 4, 6};
        args.supportsWorldTriangles3D = args.supportsWorldIndexedMeshes = true;
        args.enableGpuClipSkinning = true;
        args.rendererBackendId = "opengl";
        args.hasWorldViewProj = true;
        args.worldViewProj = glm::value_ptr(viewProj);
        args.projectedDebug = &debug;
        args.projectedRenderItems = &items;
        args.worldSceneRegistry = &registry;
        args.worldSceneFrame = &frame;
        args.sharedCaptureAttemptCache = &capture;
        args.worldIndexedBatches = &batches;
        args.backendTextureByPath = &textures;
        args.modelDepthTris = &depth;
        args.modelDepthWorldTris = &worldDepth;
        args.remainingModelTrianglesBudget = &budget;
        args.worldQuads = &quads;
        args.lines = &lines;
        args.textLines = &textLines;
        args.sprites = &sprites;
        args.worldTriangles = &triangles;
        args.world3DTriangles = &worldTriangles;
        args.sharedUnitHudCfg = &hud;
        args.resolveModelMesh = [&](const PokemonInstance &) { return &mesh; };
        args.ensureBackendTextureLoaded = [&](const std::string &key, bool) {
            auto &entry = textures[key];
            entry.valid = true;
            entry.width = entry.height = 1;
            entry.rgba = {255, 255, 255, 255};
            return &entry;
        };
        args.backendModelTriangleLimit = [] { return std::size_t(1000000); };
        args.backendModelFullMeshEnabled = [] { return true; };
        args.backendModelFastTexturedPathEnabled = [] { return true; };
        args.backendModelBackfaceCullingEnabled = [] { return true; };

        for (const auto phase : {LedgeJumpPhase::None, LedgeJumpPhase::Start, LedgeJumpPhase::Airborne, LedgeJumpPhase::Landing}) {
            std::vector<float> previousPalette;
            for (const float progress : {0.2f, 0.7f}) {
                LedgeJump::begin(unit, 1.0f);
                if (phase == LedgeJumpPhase::None) {
                    unit.ledgeJump = {};
                    unit.activeAnimIndex = unit.animMoveIndex;
                    unit.animTimeSec = progress * mesh.animations[unit.animMoveIndex].durationSec;
                } else {
                    const auto timing = unit.ledgeJump;
                    const float t = phase == LedgeJumpPhase::Start ? progress * timing.startSec : phase == LedgeJumpPhase::Airborne ? timing.startSec + progress * timing.airborneSec
                                                                                                                                    : timing.startSec + timing.airborneSec + progress * timing.landingSec;
                    LedgeJump::advance(unit, t);
                }
                frame.clear();
                batches.clear();
                runtime::shared_projected_render_items::beginProjectedRenderItemsFrame(items);
                budget = 1000000;
                runtime::shared_projected_units::drawProjectedUnits(args, {unit});
                std::vector<float> palette;
                for (const auto &draw : frame.drawClasses) {
                    for (const auto &instance : draw.instances) {
                        if (instance.gpuSkinning && instance.skinMatrices) {
                            const std::size_t count = instance.skinMatrixCount * (instance.gpuSkinningMode == 1 ? 32u : 16u);
                            palette.insert(palette.end(), instance.skinMatrices, instance.skinMatrices + count);
                        }
                    }
                }
                for (const auto &batch : batches) {
                    const float *matrices = batch.sharedSkinMatrices ? batch.sharedSkinMatrices : batch.skinMatrices.data();
                    if (batch.gpuSkinning && matrices && batch.skinMatrixCount) {
                        const std::size_t count = batch.skinMatrixCount * (batch.gpuSkinningMode == 1 ? 32u : 16u);
                        palette.insert(palette.end(), matrices, matrices + count);
                    }
                }
                if (palette.empty()) {
                    outFail = species + " lost its skeletal draw submission in phase " + std::to_string(static_cast<int>(phase)) +
                              " world draws=" + std::to_string(frame.drawClasses.size()) + " indexed batches=" + std::to_string(batches.size());
                    return false;
                }
                if (!previousPalette.empty()) {
                    float delta = 0;
                    for (std::size_t i = 0; i < std::min(palette.size(), previousPalette.size()); ++i)
                        delta = std::max(delta, std::abs(palette[i] - previousPalette[i]));
                    std::cout << species << " phase=" << static_cast<int>(phase) << " clip=" << mesh.animations[unit.activeAnimIndex].name << " palette delta=" << delta << '\n';
                    // Airborne loops may deliberately hold a tucked pose. Run,
                    // takeoff and landing must visibly articulate the skeleton.
                    if (phase != LedgeJumpPhase::Airborne && delta < 0.0001f) {
                        outFail = species + " has a frozen pose in phase " + std::to_string(static_cast<int>(phase));
                        return false;
                    }
                }
                previousPalette = std::move(palette);
            }
        }
    }
    return true;
}
