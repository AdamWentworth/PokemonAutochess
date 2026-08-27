#include "engine/assets/phlosion/PhlosionSceneArchive.h"
#include "engine/core/EngineServices.h"
#include "engine/core/Environment.h"
#include "engine/core/GameContext.h"
#include "engine/editor/EditorProjectPlugin.h"
#include "engine/events/EventBus.h"
#include "engine/render/Camera3D.h"
#include "engine/utils/ResourceManager.h"
#include "engine/utils/ShaderCache.h"
#include "game/GameConfig.h"
#include "game/assets/DevAssetStore.h"
#include "game/editor/PokemonAutochessEditorAssetCatalog.h"
#include "game/editor/PokemonAutochessEditorCommands.h"
#include "game/editor/PokemonAutochessEditorHierarchy.h"
#include "game/editor/PokemonAutochessEditorLayoutTransactions.h"
#include "game/editor/PokemonAutochessEditorPersistence.h"
#include "game/editor/PokemonAutochessEditorPreviewCatalog.h"
#include "game/editor/PokemonAutochessEditorSceneMutationSession.h"
#include "game/editor/PokemonAutochessEditorSceneMutations.h"
#include "game/editor/PokemonAutochessEditorViewportProjection.h"
#include "game/editor/PokemonPrefabPreview.h"
#include "game/editor/PokemonVfxPrefabPreview.h"
#include "game/editor/Route1EnvironmentPrefabPreview.h"
#include "game/runtime/GameRuntime.h"
#include "game/runtime/RuntimeBootLoading.h"
#include "game/runtime/video/VideoPreferences.h"
#include "game/runtime/shared/scene/Route1RuntimeEnvironment.h"
#include "game/runtime/shared/scene/Route1SceneVariants.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"

#include <SDL2/SDL_ttf.h>
#include <glad/glad.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#ifndef PHLOSION_EDITOR_PLUGIN_BUILD_CONFIGURATION
#define PHLOSION_EDITOR_PLUGIN_BUILD_CONFIGURATION "Unknown"
#endif

namespace {

namespace asset_catalog = game::editor::asset_catalog;
namespace editor_commands = game::editor::commands;
namespace editor_hierarchy = game::editor::hierarchy;
namespace layout_transactions =
    game::editor::layout_transactions;
namespace editor_persistence =
    game::editor::persistence;
namespace preview_catalog = game::editor::preview_catalog;
namespace scene_mutation_session =
    game::editor::scene_mutation_session;
namespace scene_mutations =
    game::editor::scene_mutations;
namespace route1_scene_variants =
    game::runtime::route1_scene_variants;
namespace viewport_projection =
    game::editor::viewport_projection;

constexpr std::string_view kBoardGroundPrototypeStableId =
    "gameplay-board/ground-patch-prototype";
constexpr std::string_view kBoardGroundPrefabAssetId =
    "route1/autochess_board_ground_patch";
constexpr std::string_view kBoardGroundInstanceStableId =
    "authored-prefab/autochess-board-ground-patch/board-clearance";
constexpr std::string_view kTerrainTileSetAssetId =
    "route1/terrain_tileset";
constexpr float kTerrainTileSizeCm = 100.0f;
constexpr float kTerrainElevationStepCm = 50.0f;
constexpr std::string_view kGameplayBoardStableId =
    "gameplay/autochess-board";
constexpr float kDefaultBoardCellSizeWorld = 1.0f;

constexpr std::array<std::int32_t, 2>
defaultBoardTerrainGridOrigin(
    std::string_view sceneId) noexcept {
    return sceneId == route1_scene_variants::kRoute1_5.sceneId
        ? std::array<std::int32_t, 2>{17, -19}
        : std::array<std::int32_t, 2>{17, -10};
}

constexpr std::array<float, 3> defaultBoardSourceAnchorCm(
    std::string_view sceneId) noexcept {
    const auto origin = defaultBoardTerrainGridOrigin(sceneId);
    return {
        (static_cast<float>(origin[0]) + 4.0f) *
            kTerrainTileSizeCm,
        0.0f,
        (static_cast<float>(origin[1]) + 4.0f) *
            kTerrainTileSizeCm};
}

// Inspector swatches are deliberately authored by the project. They are
// display metadata for the recovered Route 1 surfaces; runtime rendering
// continues to use the source material and texture contracts.
constexpr std::array<std::uint32_t, 3> kLightLawnPreview{
    0x78bd4affu, 0x3f742dffu, 0xb6dc73ffu};
constexpr std::array<std::uint32_t, 3> kDirtPathPreview{
    0xc38b43ffu, 0x764823ffu, 0xe4bb68ffu};
constexpr std::array<std::uint32_t, 3> kDarkLawnPreview{
    0x2e7b6effu, 0x17463effu, 0x61a391ffu};
constexpr std::array<std::uint32_t, 3> kEmptyTilePreview{
    0x343b40ffu, 0x171b1effu, 0x8b969dffu};
constexpr std::uint32_t kRaisedPlatformSidePreview = 0x6f482affu;

constexpr std::array<
    engine::editor::EditorProjectTerrainPrefab,
    35> kTerrainPrefabs{{
        {"light_lawn", "Light Lawn", "Ground Surfaces",
         "light_lawn", "flat", "auto", kLightLawnPreview[0],
         kLightLawnPreview[1], kLightLawnPreview[2]},
        {"dark_lawn", "Dark Lawn", "Ground Surfaces",
         "dark_lawn", "flat", "auto", kDarkLawnPreview[0],
         kDarkLawnPreview[1], kDarkLawnPreview[2]},
        {"dirt_path", "Dirt Path", "Ground Surfaces",
         "dirt_path", "flat", "auto", kLightLawnPreview[0],
         kDirtPathPreview[1], kDirtPathPreview[0], 0x0fu},
        {"dirt_path_full", "Full Dirt (No Grass)",
         "Dirt Path / Full", "dirt_path", "flat", "path_15",
         kLightLawnPreview[0], kDirtPathPreview[1],
         kDirtPathPreview[0], 0x0fu},
        {"dirt_path_grass_north", "North Grass Edge",
         "Dirt Path / Grass Edges", "dirt_path", "flat", "path_14",
         kLightLawnPreview[0], kDirtPathPreview[1],
         kDirtPathPreview[0], 0x0eu},
        {"dirt_path_grass_east", "East Grass Edge",
         "Dirt Path / Grass Edges", "dirt_path", "flat", "path_13",
         kLightLawnPreview[0], kDirtPathPreview[1],
         kDirtPathPreview[0], 0x0du},
        {"dirt_path_grass_south", "South Grass Edge",
         "Dirt Path / Grass Edges", "dirt_path", "flat", "path_11",
         kLightLawnPreview[0], kDirtPathPreview[1],
         kDirtPathPreview[0], 0x0bu},
        {"dirt_path_grass_west", "West Grass Edge",
         "Dirt Path / Grass Edges", "dirt_path", "flat", "path_7",
         kLightLawnPreview[0], kDirtPathPreview[1],
         kDirtPathPreview[0], 0x07u},
        {"dirt_path_grass_north_east", "North-East Grass Corner",
         "Dirt Path / Grass Corners", "dirt_path", "flat", "path_12",
         kLightLawnPreview[0], kDirtPathPreview[1],
         kDirtPathPreview[0], 0x0cu},
        {"dirt_path_grass_east_south", "East-South Grass Corner",
         "Dirt Path / Grass Corners", "dirt_path", "flat", "path_9",
         kLightLawnPreview[0], kDirtPathPreview[1],
         kDirtPathPreview[0], 0x09u},
        {"dirt_path_grass_south_west", "South-West Grass Corner",
         "Dirt Path / Grass Corners", "dirt_path", "flat", "path_3",
         kLightLawnPreview[0], kDirtPathPreview[1],
         kDirtPathPreview[0], 0x03u},
        {"dirt_path_grass_west_north", "West-North Grass Corner",
         "Dirt Path / Grass Corners", "dirt_path", "flat", "path_6",
         kLightLawnPreview[0], kDirtPathPreview[1],
         kDirtPathPreview[0], 0x06u},
        {"dirt_path_corridor_north_south", "North-South Dirt Corridor",
         "Dirt Path / Corridors", "dirt_path", "flat", "path_5",
         kLightLawnPreview[0], kDirtPathPreview[1],
         kDirtPathPreview[0], 0x05u},
        {"dirt_path_corridor_east_west", "East-West Dirt Corridor",
         "Dirt Path / Corridors", "dirt_path", "flat", "path_10",
         kLightLawnPreview[0], kDirtPathPreview[1],
         kDirtPathPreview[0], 0x0au},
        {"dirt_path_end_north", "North Dirt End",
         "Dirt Path / Ends", "dirt_path", "flat", "path_1",
         kLightLawnPreview[0], kDirtPathPreview[1],
         kDirtPathPreview[0], 0x01u},
        {"dirt_path_end_east", "East Dirt End",
         "Dirt Path / Ends", "dirt_path", "flat", "path_2",
         kLightLawnPreview[0], kDirtPathPreview[1],
         kDirtPathPreview[0], 0x02u},
        {"dirt_path_end_south", "South Dirt End",
         "Dirt Path / Ends", "dirt_path", "flat", "path_4",
         kLightLawnPreview[0], kDirtPathPreview[1],
         kDirtPathPreview[0], 0x04u},
        {"dirt_path_end_west", "West Dirt End",
         "Dirt Path / Ends", "dirt_path", "flat", "path_8",
         kLightLawnPreview[0], kDirtPathPreview[1],
         kDirtPathPreview[0], 0x08u},
        {"dirt_path_isolated", "Isolated Dirt (Grass All Sides)",
         "Dirt Path / Isolated", "dirt_path", "flat", "path_0",
         kLightLawnPreview[0], kDirtPathPreview[1],
         kDirtPathPreview[0], 0x00u},
        {"empty_flat", "Erase / Empty", "Ground Surfaces",
         "empty", "flat", "auto", kEmptyTilePreview[0],
         kEmptyTilePreview[1], kEmptyTilePreview[2]},
        {"light_lawn_ramp_north", "North", "Light Lawn Ramps",
         "light_lawn", "ramp_north", "auto", kLightLawnPreview[0],
         kLightLawnPreview[1], kLightLawnPreview[2], 0xffffffffu,
         engine::editor::EditorProjectTerrainPrefabKind::Ramp},
        {"light_lawn_ramp_east", "East", "Light Lawn Ramps",
         "light_lawn", "ramp_east", "auto", kLightLawnPreview[0],
         kLightLawnPreview[1], kLightLawnPreview[2], 0xffffffffu,
         engine::editor::EditorProjectTerrainPrefabKind::Ramp},
        {"light_lawn_ramp_south", "South", "Light Lawn Ramps",
         "light_lawn", "ramp_south", "auto", kLightLawnPreview[0],
         kLightLawnPreview[1], kLightLawnPreview[2], 0xffffffffu,
         engine::editor::EditorProjectTerrainPrefabKind::Ramp},
        {"light_lawn_ramp_west", "West", "Light Lawn Ramps",
         "light_lawn", "ramp_west", "auto", kLightLawnPreview[0],
         kLightLawnPreview[1], kLightLawnPreview[2], 0xffffffffu,
         engine::editor::EditorProjectTerrainPrefabKind::Ramp},
        {"dirt_path_ramp_north", "North", "Dirt Path Ramps",
         "dirt_path", "ramp_north", "auto", kDirtPathPreview[0],
         kDirtPathPreview[1], kDirtPathPreview[2], 0xffffffffu,
         engine::editor::EditorProjectTerrainPrefabKind::Ramp},
        {"dirt_path_ramp_east", "East", "Dirt Path Ramps",
         "dirt_path", "ramp_east", "auto", kDirtPathPreview[0],
         kDirtPathPreview[1], kDirtPathPreview[2], 0xffffffffu,
         engine::editor::EditorProjectTerrainPrefabKind::Ramp},
        {"dirt_path_ramp_south", "South", "Dirt Path Ramps",
         "dirt_path", "ramp_south", "auto", kDirtPathPreview[0],
         kDirtPathPreview[1], kDirtPathPreview[2], 0xffffffffu,
         engine::editor::EditorProjectTerrainPrefabKind::Ramp},
        {"dirt_path_ramp_west", "West", "Dirt Path Ramps",
         "dirt_path", "ramp_west", "auto", kDirtPathPreview[0],
         kDirtPathPreview[1], kDirtPathPreview[2], 0xffffffffu,
         engine::editor::EditorProjectTerrainPrefabKind::Ramp},
        {"dark_lawn_ramp_north", "North", "Dark Lawn Ramps",
         "dark_lawn", "ramp_north", "auto", kDarkLawnPreview[0],
         kDarkLawnPreview[1], kDarkLawnPreview[2], 0xffffffffu,
         engine::editor::EditorProjectTerrainPrefabKind::Ramp},
        {"dark_lawn_ramp_east", "East", "Dark Lawn Ramps",
         "dark_lawn", "ramp_east", "auto", kDarkLawnPreview[0],
         kDarkLawnPreview[1], kDarkLawnPreview[2], 0xffffffffu,
         engine::editor::EditorProjectTerrainPrefabKind::Ramp},
        {"dark_lawn_ramp_south", "South", "Dark Lawn Ramps",
         "dark_lawn", "ramp_south", "auto", kDarkLawnPreview[0],
         kDarkLawnPreview[1], kDarkLawnPreview[2], 0xffffffffu,
         engine::editor::EditorProjectTerrainPrefabKind::Ramp},
        {"dark_lawn_ramp_west", "West", "Dark Lawn Ramps",
         "dark_lawn", "ramp_west", "auto", kDarkLawnPreview[0],
         kDarkLawnPreview[1], kDarkLawnPreview[2], 0xffffffffu,
         engine::editor::EditorProjectTerrainPrefabKind::Ramp},
        {"light_lawn_platform_raise", "Light Lawn Platform",
         "Raised Platform Tiles", "light_lawn", "flat", "auto",
         kLightLawnPreview[0], kRaisedPlatformSidePreview,
         kLightLawnPreview[2], 0xffffffffu,
         engine::editor::EditorProjectTerrainPrefabKind::Platform, 1},
        {"dirt_path_platform_raise", "Dirt Path Platform",
         "Raised Platform Tiles", "dirt_path", "flat", "auto",
         kDirtPathPreview[0], kRaisedPlatformSidePreview,
         kLightLawnPreview[2], 0x0fu,
         engine::editor::EditorProjectTerrainPrefabKind::Platform, 1},
        {"dark_lawn_platform_raise", "Dark Lawn Platform",
         "Raised Platform Tiles", "dark_lawn", "flat", "auto",
         kDarkLawnPreview[0], kRaisedPlatformSidePreview,
         kDarkLawnPreview[2], 0xffffffffu,
         engine::editor::EditorProjectTerrainPrefabKind::Platform, 1},
    }};

constexpr const auto& terrainPrefabs() noexcept {
    return kTerrainPrefabs;
}

void setProcessEnvironment(
    const std::string& name,
    const std::optional<std::string>& value) {
#if defined(_WIN32)
    _putenv_s(name.c_str(), value ? value->c_str() : "");
#else
    if (value) {
        setenv(name.c_str(), value->c_str(), 1);
    } else {
        unsetenv(name.c_str());
    }
#endif
}

void appendProjectedEditorLine(
    const engine::editor::EditorProjectRenderContext& context,
    const glm::vec3& start,
    const glm::vec3& end,
    float r,
    float g,
    float b,
    float a,
    float thickness,
    std::vector<IRenderBackend::DebugLine>& out) {
    IRenderBackend::DebugLine line{};
    const viewport_projection::Context projectionContext{
        .viewProjectionMatrix4x4 =
            context.viewProjectionMatrix4x4,
        .surfaceWidth = context.surfaceWidth,
        .surfaceHeight = context.surfaceHeight};
    std::array<float, 2> viewportStart{};
    std::array<float, 2> viewportEnd{};
    if (!viewport_projection::projectPoint(
            projectionContext,
            {start.x, start.y, start.z},
            viewportStart) ||
        !viewport_projection::projectPoint(
            projectionContext,
            {end.x, end.y, end.z},
            viewportEnd)) {
        return;
    }
    line.x1 = viewportStart[0];
    line.y1 = viewportStart[1];
    line.x2 = viewportEnd[0];
    line.y2 = viewportEnd[1];
    line.r = r;
    line.g = g;
    line.b = b;
    line.a = a;
    line.thickness = thickness;
    out.push_back(line);
}

class PokemonAutochessEditorProject final
    : public engine::editor::IEditorProjectRuntime {
public:
    ~PokemonAutochessEditorProject() override {
        if (gameRuntime_) {
            gameRuntime_->shutdown();
            gameRuntime_.reset();
        }
        if (ownsTtf_) {
            TTF_Quit();
        }
        restoreEnvironment();
        restoreWorkingDirectory();
    }

    bool open(
        const engine::editor::EditorProjectOpenContext& context,
        std::string* outError) override {
        if (!context.descriptor || !context.projectRoot ||
            !context.startupScenePath) {
            if (outError) {
                *outError =
                    "Pokemon Autochess editor plugin received an incomplete project context.";
            }
            return false;
        }

        const std::filesystem::path projectRoot(context.projectRoot);
        projectRoot_ = projectRoot;
        persistence_.setProjectRoot(projectRoot_);
        game::assets::DevAssetStore projectStore(
            projectRoot_.string());
        const GameConfigData gameConfig =
            GameConfig::load(
                nullptr,
                &projectStore);
        boardCellSize_ =
            std::max(
                0.05f,
                gameConfig.cellSize);
        const auto startupScene = std::find_if(
            context.descriptor->scenes.begin(),
            context.descriptor->scenes.end(),
            [&](const engine::editor::ProjectScene& scene) {
                return scene.sceneId ==
                       context.descriptor->startupSceneId;
            });
        if (startupScene ==
            context.descriptor->scenes.end()) {
            if (outError) {
                *outError =
                    "Pokemon Autochess could not find the startup game scene.";
            }
            return false;
        }
        const auto environment = std::find_if(
            context.descriptor->environments.begin(),
            context.descriptor->environments.end(),
            [&](const engine::editor::ProjectEnvironment&
                    candidate) {
                return candidate.assetId ==
                       startupScene->environmentAssetId;
            });
        if (environment ==
            context.descriptor->environments.end()) {
            if (outError) {
                *outError =
                    "Pokemon Autochess could not find the startup environment backdrop.";
            }
            return false;
        }
        const std::filesystem::path startupScenePath(
            context.startupScenePath);
        return activateScene(
            startupScene->sceneId,
            startupScene->displayName,
            environment->assetId,
            environment->kind,
            startupScenePath,
            startupScene->authoredScenePath.empty()
                ? std::filesystem::path{}
                : projectRoot_ /
                      startupScene->authoredScenePath,
            startupScene->runtimePath.generic_string(),
            startupScene->status,
            outError);
    }

    bool openScene(
        const engine::editor::EditorProjectSceneContext&
            context,
        std::string* outError) override {
        if (!context.sceneId ||
            !context.environmentAssetId ||
            !context.environmentKind) {
            if (outError) {
                *outError =
                    "Pokemon Autochess received an incomplete game scene context.";
            }
            return false;
        }
        return activateScene(
            context.sceneId,
            context.displayName ? context.displayName : "",
            context.environmentAssetId,
            context.environmentKind,
            context.environmentPath
                ? std::filesystem::path(
                      context.environmentPath)
                : std::filesystem::path{},
            context.authoredScenePath
                ? std::filesystem::path(
                      context.authoredScenePath)
                : std::filesystem::path{},
            context.runtimePath ? context.runtimePath : "",
            context.status ? context.status : "",
            outError);
    }

    void prewarm(
        IRenderBackend& renderer,
            const engine::editor::EditorProjectCameraContext&
            camera) override {
        batches_.clear();
        if (!sceneViewReady_) {
            return;
        }
        environment_.appendIndexedBatches(0.0f, batches_);
        game::runtime::shared_world_batches::
            prewarmWorldIndexedBatches(
                renderer,
                batches_,
                camera.cameraWorldPosition3,
                camera.cameraForward3,
                camera.cameraTarget3);
    }

    void update(float simulationSeconds) override {
        simulationSeconds_ = simulationSeconds;
        if (sceneViewReady_) {
            environment_.updateAnimation(simulationSeconds);
        }
    }

    void render(
        const engine::editor::EditorProjectRenderContext&
            context) override {
        if (!context.renderer ||
            !context.viewProjectionMatrix4x4 ||
            !sceneViewReady_) {
            return;
        }
        layoutViewProjection_ =
            glm::make_mat4(
                context.viewProjectionMatrix4x4);
        layoutProjectionWidth_ =
            context.surfaceWidth;
        layoutProjectionHeight_ =
            context.surfaceHeight;
        layoutProjectionReady_ = true;
        batches_.clear();
        environment_.appendIndexedBatches(
            simulationSeconds_,
            batches_);
        if (!terrainBatchDiagnosticsWritten_ &&
            engine::env::get(
                "PAC_ROUTE1_TERRAIN_BATCH_DIAGNOSTICS")) {
            terrainBatchDiagnosticsWritten_ = true;
            for (const auto& batch : batches_) {
                const auto& key = batch.geometryCacheKey;
                if (key.find("route1:terrain-") ==
                    std::string::npos) {
                    continue;
                }
                const auto* vertices = batch.sharedVertices
                    ? batch.sharedVertices
                    : batch.vertices.data();
                const std::size_t vertexCount = batch.sharedVertices
                    ? batch.sharedVertexCount
                    : batch.vertices.size();
                if (!vertices || vertexCount == 0u) {
                    continue;
                }
                std::array<float, 3> minimum{
                    vertices[0].x, vertices[0].y, vertices[0].z};
                std::array<float, 3> maximum = minimum;
                for (std::size_t vertexIndex = 1u;
                     vertexIndex < vertexCount;
                     ++vertexIndex) {
                    minimum[0] = std::min(
                        minimum[0], vertices[vertexIndex].x);
                    minimum[1] = std::min(
                        minimum[1], vertices[vertexIndex].y);
                    minimum[2] = std::min(
                        minimum[2], vertices[vertexIndex].z);
                    maximum[0] = std::max(
                        maximum[0], vertices[vertexIndex].x);
                    maximum[1] = std::max(
                        maximum[1], vertices[vertexIndex].y);
                    maximum[2] = std::max(
                        maximum[2], vertices[vertexIndex].z);
                }
                std::cerr
                    << "[PokemonAutochessEditor][TerrainBatch] key="
                    << key
                    << " vertices=" << vertexCount
                    << " local_bounds_cm=["
                    << minimum[0] << ',' << minimum[1] << ','
                    << minimum[2] << "]-["
                    << maximum[0] << ',' << maximum[1] << ','
                    << maximum[2] << "]\n";
            }
        }
        game::runtime::shared_world_batches::
            submitWorldIndexedBatches(
                *context.renderer,
                batches_,
                context.viewProjectionMatrix4x4,
                context.surfaceWidth,
                context.surfaceHeight,
                context.cameraWorldPosition3,
                context.cameraForward3,
                context.cameraTarget3);
        renderLayoutOverlay(context);
    }

    engine::editor::EditorProjectStats stats() const override {
        if (!sceneViewReady_) {
            return {};
        }
        const auto& source = environment_.stats();
        return {
            .sceneCount = source.sceneCount,
            .materialCount = source.materialCount,
            .drawClassCount = source.drawClassCount,
            .visibleTriangleCount =
                source.visibleTriangleCount,
            .shadowTriangleCount =
                source.shadowTriangleCount,
            .archiveFileCount = sceneStore_.fileCount()};
    }

    const char* status() const noexcept override {
        return status_.c_str();
    }

    std::size_t gamePreviewCount() const noexcept override {
        return preview_catalog::all().size();
    }

    engine::editor::EditorProjectGamePreview gamePreview(
        std::size_t index) const noexcept override {
        const auto& previews = preview_catalog::all();
        if (index >= previews.size()) {
            return {};
        }
        const auto& preview = previews[index];
        return {
            .id = preview.id,
            .displayName = preview.displayName,
            .group = preview.group,
            .description = preview.description,
            .sceneId = preview.sceneId,
        };
    }

    bool initializeGamePreview(
        const engine::editor::EditorProjectGamePreviewContext&
            context,
        std::string* outError) override {
        if (gameRuntime_) {
            if (outError) {
                outError->clear();
            }
            return true;
        }
        if (!context.renderer || !context.camera ||
            projectRoot_.empty()) {
            if (outError) {
                *outError =
                    "Embedded game preview received an incomplete host context.";
            }
            return false;
        }

        rememberAndSetEnvironment(
            "PHLOSION_DATA_ROOT",
            projectRoot_.string());
        rememberAndSetEnvironment(
            "PHLOSION_ASSET_ROOT",
            (projectRoot_ / "assets").string());
        rememberAndSetEnvironment(
            "PAC_AUTO_LOAD_DEBUG_SNAPSHOT",
            "0");
        rememberAndSetEnvironment(
            "PAC_EDITOR_START_STATE",
            std::nullopt);
        rememberAndSetEnvironment(
            "PAC_EDITOR_GAME_MODE",
            std::nullopt);
        if (!adoptProjectWorkingDirectory(outError)) {
            return false;
        }
        if (context.renderer->requiresOpenGLContext() &&
            gladLoadGLLoader(
                reinterpret_cast<GLADloadproc>(
                    SDL_GL_GetProcAddress)) == 0) {
            if (outError) {
                *outError =
                    "The Pokemon Autochess editor plugin could not bind to the editor's active OpenGL context.";
            }
            return false;
        }

        if (TTF_WasInit() == 0) {
            if (TTF_Init() == 0) {
                ownsTtf_ = true;
            }
        }

        renderer_ = context.renderer;
        gameCamera_ = context.camera;
        previewWidth_ = std::max(1, context.surfaceWidth);
        previewHeight_ = std::max(1, context.surfaceHeight);
        services_.resources = &resources_;
        services_.shaders = &shaders_;
        services_.events = &events_;
        services_.activeRendererBackend =
            renderer_->backendId()
                ? renderer_->backendId()
                : "opengl";
        services_.videoPreferencesPath =
            game::video::defaultPreferencesPath();

        GameContext gameContext;
        gameContext.renderer = renderer_;
        gameContext.camera = gameCamera_;
        gameContext.services = &services_;
        gameContext.drawableW = previewWidth_;
        gameContext.drawableH = previewHeight_;
        gameContext.deferBulkModelPrewarm = true;
        gameContext.deferStartupFramePrewarm = true;
        gameContext.setTitle =
            [&](const std::string& title) {
                runtimeTitle_ = title;
            };
        gameContext.swapBuffers = []() {};
        gameContext.requestQuit =
            [&]() {
                runtimeRequestedQuit_ = true;
            };
        gameContext.pumpPreloadEvents =
            []() {
                return true;
            };
        gameContext.renderBootLoading =
            [&](float progress) {
                latestBootProgress_ = progress;
            };
        gameContext.applyVideoMode =
            [&](int width, int height, bool fullscreen) {
                previewWidth_ = std::max(1, width);
                previewHeight_ = std::max(1, height);
                previewFullscreen_ = fullscreen;
                return true;
            };
        gameContext.queryVideoMode =
            [&]() {
                return GameContext::VideoMode{
                    .width = previewWidth_,
                    .height = previewHeight_,
                    .fullscreen = previewFullscreen_,
                };
            };

        if (!persistence_.loadPreviewUnitOverrides(outError)) {
            return false;
        }
        gameRuntime_ = std::make_unique<GameRuntime>();
        gameRuntime_->init(gameContext);
        if (runtimeRequestedQuit_) {
            gameRuntime_->shutdown();
            gameRuntime_.reset();
            if (outError) {
                *outError =
                    "Pokemon Autochess requested shutdown while initializing its embedded preview.";
            }
            return false;
        }
        activePreviewId_ = "main-menu";
        status_ =
            "Route 1 scene mounted; embedded Pokemon Autochess runtime is warm.";
        return true;
    }

    bool selectGamePreview(
        const char* id,
        std::string* outError) override {
        if (!gameRuntime_) {
            if (outError) {
                *outError =
                    "Embedded game preview is not initialized.";
            }
            return false;
        }
        const std::string_view requested =
            id ? std::string_view(id) : std::string_view{};
        const auto* found = preview_catalog::find(requested);
        if (!found) {
            if (outError) {
                *outError =
                    "Unknown Pokemon Autochess game preview: " +
                    std::string(requested);
            }
            return false;
        }

        const std::filesystem::path snapshotPath =
            found->snapshot[0] == '\0'
                ? std::filesystem::path{}
                : projectRoot_ / found->snapshot;
        if (!gameRuntime_->activateEditorPreview(
                found->state,
                found->gameMode,
                snapshotPath.string(),
                outError)) {
            return false;
        }
        activePreviewId_ = found->id;
        editSession_.clearPreviewUnitLiveEdit();
        previewUnitSourceTransforms_.clear();
        refreshPreviewUnitLayoutObjects(true);
        applySavedPreviewUnitOverrides();
        refreshPreviewUnitLayoutObjects(false);
        bootReplayActive_ = requested == "boot";
        bootReplaySeconds_ = 0.0f;
        status_ =
            "Game preview selected: " +
            std::string(found->displayName) +
            " (warm runtime; no process restart).";
        if (outError) {
            outError->clear();
        }
        return true;
    }

    void resetGamePreview() override {
        std::string ignored;
        selectGamePreview(activePreviewId_.c_str(), &ignored);
    }

    void fixedUpdateGamePreview(
        float deltaSeconds) override {
        if (!gameRuntime_) {
            return;
        }
        if (bootReplayActive_) {
            bootReplaySeconds_ +=
                std::max(0.0f, deltaSeconds);
            if (bootReplaySeconds_ < kBootReplayDurationSeconds) {
                return;
            }
            bootReplayActive_ = false;
        }
        gameRuntime_->fixedUpdate(deltaSeconds);
    }

    void renderGamePreview(
        const engine::editor::EditorProjectRenderContext&
            context) override {
        if (!gameRuntime_ || !context.renderer) {
            return;
        }
        previewWidth_ = std::max(1, context.surfaceWidth);
        previewHeight_ = std::max(1, context.surfaceHeight);
        if (bootReplayActive_) {
            std::array<
                IRenderBackend::DebugQuad,
                game::runtime::boot_loading::
                    kFallbackLoadingQuadCount>
                quads{};
            const float progress = std::clamp(
                bootReplaySeconds_ /
                    kBootReplayDurationSeconds,
                0.0f,
                1.0f);
            if (game::runtime::boot_loading::
                    buildFallbackLoadingQuads(
                        previewWidth_,
                        previewHeight_,
                        progress,
                        quads)) {
                context.renderer->drawDebugQuads(
                    quads.data(),
                    quads.size(),
                    previewWidth_,
                    previewHeight_);
            }
            return;
        }
        gameRuntime_->render(
            previewWidth_,
            previewHeight_);
        if (gameCamera_) {
            gameLayoutViewProjection_ =
                gameCamera_->getProjectionMatrix() *
                gameCamera_->getViewMatrix();
            gameLayoutProjectionWidth_ = previewWidth_;
            gameLayoutProjectionHeight_ = previewHeight_;
            gameLayoutProjectionReady_ = true;
        }
        refreshPreviewUnitLayoutObjects(false);
    }

    void handleGamePreviewInput(
        const InputEvent& event) override {
        if (gameRuntime_ && !bootReplayActive_) {
            gameRuntime_->handleEvent(event);
        }
    }

    bool gamePreviewReady() const noexcept override {
        return gameRuntime_ != nullptr;
    }

    std::size_t assetCount() const noexcept override {
        return vfxPreview_.assetCount() +
            environmentPrefabCatalog_.size();
    }

    engine::editor::EditorProjectAsset asset(
        std::size_t index) const noexcept override {
        const std::size_t vfxCount =
            vfxPreview_.assetCount();
        if (index < vfxCount) {
            return vfxPreview_.asset(index);
        }
        index -= vfxCount;
        return environmentPrefabCatalog_.asset(index);
    }

    bool instantiateAsset(
        const char* assetId,
        std::string* outCreatedStableId,
        std::string* outError) override {
        if (!assetId) {
            if (outError) {
                *outError =
                    "A scene-prefab asset must be selected.";
            }
            return false;
        }
        const auto* found =
            environmentPrefabCatalog_.find(assetId);
        if (!found) {
            if (outError) {
                *outError =
                    "The selected asset is not a Route 1 scene prefab.";
            }
            return false;
        }
        return duplicateLayoutObject(
            found->layoutStableId.c_str(),
            outCreatedStableId,
            outError);
    }

    bool selectAssetPreview(
        const char* assetId,
        const char* assetPath,
        std::string* outError) override {
        if (vfxPreview_.owns(
                assetId,
                assetPath)) {
            if (!vfxPreview_.select(
                    assetId,
                    assetPath,
                    outError)) {
                return false;
            }
            activeAssetPreview_ =
                ActiveAssetPreview::VisualEffect;
            return true;
        }
        if (environmentPrefabPreview_.owns(
                assetId,
                assetPath)) {
            if (!environmentPrefabPreview_.select(
                    assetId,
                    assetPath,
                    outError)) {
                return false;
            }
            activeAssetPreview_ =
                ActiveAssetPreview::Environment;
            return true;
        }
        if (!prefabPreview_.select(
                assetId,
                assetPath,
                outError)) {
            return false;
        }
        activeAssetPreview_ =
            ActiveAssetPreview::Model;
        return true;
    }

    engine::editor::EditorProjectAssetPreviewInfo
    assetPreviewInfo() const noexcept override {
        if (activeAssetPreview_ ==
            ActiveAssetPreview::VisualEffect) {
            return vfxPreview_.info();
        }
        if (activeAssetPreview_ ==
            ActiveAssetPreview::Environment) {
            return environmentPrefabPreview_.info();
        }
        return prefabPreview_.info();
    }

    engine::editor::EditorProjectAssetAnimation
    assetPreviewAnimation(
        std::size_t index) const noexcept override {
        if (activeAssetPreview_ ==
            ActiveAssetPreview::VisualEffect) {
            return vfxPreview_.animation(index);
        }
        if (activeAssetPreview_ ==
            ActiveAssetPreview::Environment) {
            return environmentPrefabPreview_.animation(index);
        }
        return prefabPreview_.animation(index);
    }

    void setAssetPreviewOptions(
        const engine::editor::
            EditorProjectAssetPreviewOptions&
                options) override {
        if (activeAssetPreview_ ==
            ActiveAssetPreview::VisualEffect) {
            vfxPreview_.setOptions(options);
        } else if (activeAssetPreview_ ==
                   ActiveAssetPreview::Environment) {
            environmentPrefabPreview_.setOptions(options);
        } else {
            prefabPreview_.setOptions(options);
        }
    }

    void updateAssetPreview(
        float deltaSeconds) override {
        if (activeAssetPreview_ ==
            ActiveAssetPreview::VisualEffect) {
            vfxPreview_.update(deltaSeconds);
        } else if (activeAssetPreview_ ==
                   ActiveAssetPreview::Environment) {
            environmentPrefabPreview_.update(deltaSeconds);
        } else {
            prefabPreview_.update(deltaSeconds);
        }
    }

    void renderAssetPreview(
        const engine::editor::
            EditorProjectRenderContext&
                context) override {
        if (activeAssetPreview_ ==
            ActiveAssetPreview::VisualEffect) {
            vfxPreview_.render(context);
        } else if (activeAssetPreview_ ==
                   ActiveAssetPreview::Environment) {
            environmentPrefabPreview_.render(context);
        } else {
            prefabPreview_.render(context);
        }
    }

    std::size_t layoutObjectCount() const noexcept override {
        return editor_hierarchy::objectCount(
            sceneViewReady_,
            environment_.layoutObjects().size(),
            previewUnitLayoutObjects_.size());
    }

    engine::editor::EditorProjectLayoutObject
    layoutObject(std::size_t index) const noexcept override {
        const auto& objects =
            environment_.layoutObjects();
        const auto address =
            editor_hierarchy::resolveObjectAddress(
                sceneViewReady_,
                objects.size(),
                previewUnitLayoutObjects_.size(),
                index);
        if (address.domain ==
            editor_hierarchy::ObjectDomain::GameplayPreviewUnit) {
            const auto& object =
                previewUnitLayoutObjects_[address.index];
            const auto& unit = object.unit;
            return {
                .stableId = object.stableId.c_str(),
                .displayName = object.displayName.c_str(),
                .typeName = "Gameplay Preview Unit",
                .coordinateSystem =
                    "Gameplay world metres; X/Z snap to board or bench slots",
                .reason = "game_preview_start_transform",
                .targetKind = "gameplay_preview_unit",
                .categoryPath = object.categoryPath.c_str(),
                .prefabAssetId = object.prefabAssetId.c_str(),
                .inspectorTitle = "Gameplay Preview Unit",
                .translationLabel = "Starting position",
                .viewportHint =
                    "Game viewport: click the unit marker and use Move [W] or Rotate [E]. Positions snap to legal board or bench slots and height follows Route 1 terrain.",
                .resetLabel = "Reset Starting Position",
                .scaleReadOnlyLabel = "Resolved gameplay scale",
                .scaleReadOnlyDescription =
                    "Scale is owned by PokemonAutochess runtime importer correction and species presentation data.",
                .capabilities =
                    engine::editor::EditorProjectLayoutTranslate |
                    engine::editor::EditorProjectLayoutRotate |
                    engine::editor::EditorProjectLayoutReset,
                .viewportMask =
                    engine::editor::EditorProjectLayoutViewportGame,
                .fineTranslationSnap = {0.05f, 0.05f, 0.05f},
                .sourceTranslation = object.source.position,
                .sourceRotationDegrees =
                    object.source.rotationDegrees,
                .sourceScale = {
                    unit.resolvedRenderScale,
                    unit.resolvedRenderScale,
                    unit.resolvedRenderScale},
                .translation = unit.position,
                .rotationDegrees = unit.rotationDegrees,
                .scale = {
                    unit.resolvedRenderScale,
                    unit.resolvedRenderScale,
                    unit.resolvedRenderScale},
                .terrainGridOrigin = {
                    unit.boardColumn,
                    unit.boardRow},
                .terrainGridExtent = {1u, 1u},
                .terrainGridBound = !unit.benchUnit,
                .boundsMinimum = object.boundsMinimum,
                .boundsMaximum = object.boundsMaximum,
                .viewportPosition = object.viewportPosition,
                .viewportAxisDirections =
                    object.viewportAxisDirections,
                .viewportSourceUnitsPerPixel =
                    object.viewportSourceUnitsPerPixel,
                .viewportVisible = object.viewportVisible,
                .suppressed = false,
                .hasOverride = object.hasOverride};
        }
        if (address.domain ==
            editor_hierarchy::ObjectDomain::None) {
            return {};
        }
        if (address.domain ==
            editor_hierarchy::ObjectDomain::GameplayBoard) {
            const auto& layout = environment_.layout();
            const auto defaultOrigin =
                defaultBoardTerrainGridOrigin(activeSceneId_);
            auto view = editor_hierarchy::gameplayBoardView(
                layout,
                editor_hierarchy::GameplayBoardViewConfig{
                    .stableId = kGameplayBoardStableId.data(),
                    .defaultSourceAnchorCm =
                        defaultBoardSourceAnchorCm(activeSceneId_),
                    .defaultTerrainGridOrigin =
                        defaultOrigin,
                    .defaultBoardCellSizeWorld =
                        kDefaultBoardCellSizeWorld,
                    .terrainTileSizeCm = kTerrainTileSizeCm,
                    .terrainElevationStepCm =
                        kTerrainElevationStepCm});
            if (!layoutProjectionReady_) {
                return view;
            }
            const auto worldFromSource =
                game::runtime::route1_environment::
                    worldFromSourceMatrix(layout);
            const auto projection =
                viewport_projection::projectTransform(
                    viewport_projection::Context{
                        .viewProjectionMatrix4x4 =
                            glm::value_ptr(
                                layoutViewProjection_),
                        .sourceToWorldMatrix4x4 =
                            worldFromSource.data(),
                        .surfaceWidth =
                            layoutProjectionWidth_,
                        .surfaceHeight =
                            layoutProjectionHeight_},
                    viewport_projection::TransformInput{
                        .sourcePosition =
                            layout.sourceAnchorCm,
                        .sourceAxisLength = 100.0f,
                        .fallbackAxisDirections = {
                            1.0f, 0.0f,
                            0.0f, -1.0f,
                            0.70710678f, 0.70710678f}});
            if (projection.visible) {
                view.viewportPosition =
                    projection.viewportPosition;
                view.viewportAxisDirections =
                    projection.viewportAxisDirections;
                view.viewportSourceUnitsPerPixel =
                    projection.viewportSourceUnitsPerPixel;
                view.viewportVisible = true;
            }
            return view;
        }
        const auto& object = objects[address.index];
        auto view =
            editor_hierarchy::environmentObjectView(object);
        if (!layoutProjectionReady_) {
            return view;
        }
        const auto worldFromSourceArray =
            game::runtime::route1_environment::
                worldFromSourceMatrix(
                    environment_.layout());
        const auto projection =
            viewport_projection::projectTransform(
                viewport_projection::Context{
                    .viewProjectionMatrix4x4 =
                        glm::value_ptr(
                            layoutViewProjection_),
                    .sourceToWorldMatrix4x4 =
                        worldFromSourceArray.data(),
                    .surfaceWidth =
                        layoutProjectionWidth_,
                    .surfaceHeight =
                        layoutProjectionHeight_},
                viewport_projection::TransformInput{
                    .sourcePosition =
                        object.translationCm,
                    .sourceAxisLength = 100.0f,
                    .fallbackAxisDirections = {
                        1.0f, 0.0f,
                        0.0f, -1.0f,
                        0.70710678f, 0.70710678f}});
        if (projection.visible) {
            view.viewportPosition =
                projection.viewportPosition;
            view.viewportAxisDirections =
                projection.viewportAxisDirections;
            view.viewportSourceUnitsPerPixel =
                projection.viewportSourceUnitsPerPixel;
            view.viewportVisible = true;
        }
        return view;
    }

    bool supportsTerrainTileEditing() const noexcept override {
        return sceneViewReady_ &&
            route1_scene_variants::editable(activeSceneId_);
    }

    std::size_t terrainTileCount() const noexcept override {
        return supportsTerrainTileEditing()
            ? environment_.terrainTiles().size()
            : 0u;
    }

    engine::editor::EditorProjectTerrainTile terrainTile(
        std::size_t index) const noexcept override {
        const auto& tiles = environment_.terrainTiles();
        if (!supportsTerrainTileEditing() ||
            index >= tiles.size()) {
            return {};
        }
        const auto& tile = tiles[index];
        engine::editor::EditorProjectTerrainTile view{
            .coordinate = {
                .gridX = tile.gridX,
                .gridZ = tile.gridZ},
            .sourceElevationLevel =
                tile.sourceElevationLevel,
            .elevationLevel = tile.elevationLevel,
            .sourceSurface = tile.sourceSurface.c_str(),
            .sourceShape = tile.sourceShape.c_str(),
            .surface = tile.surface.c_str(),
            .shape = tile.shape.c_str(),
            .visualVariant = tile.visualVariant.c_str(),
            .sourceReference = tile.sourceReference
                ? engine::editor::EditorProjectTerrainTileCoordinate{
                      .gridX = (*tile.sourceReference)[0],
                      .gridZ = (*tile.sourceReference)[1]}
                : engine::editor::
                      EditorProjectTerrainTileCoordinate{},
            .sourceOccupied = tile.sourceOccupied,
            .authored = tile.authored,
            .hasSourceReference =
                tile.sourceReference.has_value(),
            .receivesProjectedShadow =
                tile.receivesProjectedShadow,
            .normalizeSourceTint =
                tile.normalizeSourceTint,
            .suppressOverlappingVegetation =
                tile.suppressOverlappingVegetation};
        if (!layoutProjectionReady_ ||
            (!tile.sourceOccupied && !tile.authored)) {
            return view;
        }
        const auto worldFromSource =
            game::runtime::route1_environment::
                worldFromSourceMatrix(environment_.layout());
        const auto projection =
            viewport_projection::projectTerrainTile(
                viewport_projection::Context{
                    .viewProjectionMatrix4x4 =
                        glm::value_ptr(
                            layoutViewProjection_),
                    .sourceToWorldMatrix4x4 =
                        worldFromSource.data(),
                    .surfaceWidth =
                        layoutProjectionWidth_,
                    .surfaceHeight =
                        layoutProjectionHeight_},
                viewport_projection::TerrainTileInput{
                    .gridX = tile.gridX,
                    .gridZ = tile.gridZ,
                    .elevationLevel =
                        tile.elevationLevel,
                    .shape = tile.shape,
                    .tileSize = kTerrainTileSizeCm,
                    .elevationStep =
                        kTerrainElevationStepCm});
        view.viewportCorners =
            projection.viewportCorners;
        view.viewportFlatCorners =
            projection.viewportFlatCorners;
        view.viewportLevelStep =
            projection.viewportLevelStep;
        view.viewportVisible = projection.visible;
        return view;
    }

    std::size_t terrainSurfaceCount() const noexcept override {
        return supportsTerrainTileEditing() ? 3u : 0u;
    }

    engine::editor::EditorProjectTerrainSurface terrainSurface(
        std::size_t index) const noexcept override {
        static constexpr std::array<
            engine::editor::EditorProjectTerrainSurface,
            3> surfaces{{
                {"light_lawn", "Light Route Lawn"},
                {"dark_lawn", "Dark Raised Lawn"},
                {"dirt_path", "Route Dirt Path"},
            }};
        return index < surfaces.size() ? surfaces[index]
                                       : engine::editor::EditorProjectTerrainSurface{};
    }

    std::size_t terrainPrefabCount() const noexcept override {
        return supportsTerrainTileEditing() ? terrainPrefabs().size() : 0u;
    }

    engine::editor::EditorProjectTerrainPrefab terrainPrefab(
        std::size_t index) const noexcept override {
        const auto& prefabs = terrainPrefabs();
        return index < prefabs.size()
            ? prefabs[index]
            : engine::editor::EditorProjectTerrainPrefab{};
    }

    bool applyTerrainTileEdit(
        const engine::editor::EditorProjectTerrainTileEditRequest& request,
        std::string* outError) override {
        if (!supportsTerrainTileEditing()) {
            if (outError) {
                *outError =
                    "Terrain editing requires Route 1, an operation, and at least one selected tile.";
            }
            return false;
        }
        scene_mutations::TerrainTileEditResult mutation;
        if (!scene_mutations::buildTerrainTileEdit(
                request,
                environment_.terrainTiles(),
                environment_.layout(),
                kTerrainTileSetAssetId,
                mutation,
                outError)) {
            return false;
        }
        const auto previous = environment_.layout();
        std::string error;
        if (!sceneMutationSession().applyAuthoredLayout(
                mutation.layout,
                previous,
                &error)) {
            if (outError) {
                *outError =
                    "Could not apply and persist the terrain-tile edit: " +
                    error;
            }
            return false;
        }
        recordSceneEdit(previous);
        status_ = "Saved " +
            std::to_string(mutation.affectedTileCount) +
            " Route 1 terrain tile edit(s).";
        if (outError) {
            outError->clear();
        }
        return true;
    }
    bool setLayoutObjectOverride(
        const engine::editor::EditorProjectLayoutEdit& edit,
        std::string* outError) override {
        if (edit.stableId &&
            findPreviewUnitLayoutObject(edit.stableId)) {
            return previewPreviewUnitTransform(edit, outError) &&
                commitPreviewUnitTransform(
                    edit.stableId, outError);
        }
        if (!sceneViewReady_ ||
            !edit.stableId) {
            if (outError) {
                *outError =
                    "A mounted Route 1 scene and stable target are required.";
            }
            return false;
        }
        if (edit.stableId == kGameplayBoardStableId) {
            const auto previous = environment_.layout();
            const auto next =
                scene_mutations::boardRegistrationFromCenter(
                    previous,
                    edit.translation,
                    kTerrainTileSizeCm,
                    kTerrainElevationStepCm);
            std::string error;
            if (!sceneMutationSession().applyBoardRegistration(
                    next,
                    previous,
                    &error)) {
                synchronizeBoardCellSize(previous.boardCellSizeWorld);
                if (outError) {
                    *outError =
                        "Could not persist the gameplay board layout: " +
                        error;
                }
                return false;
            }
            synchronizeBoardCellSize(next.boardCellSizeWorld);
            layoutSelection_.select(edit.stableId);
            recordSceneEdit(previous);
            editSession_.clearSceneLiveEdit();
            status_ =
                "Gameplay board placement and cells are bound to the Route 1 grid.";
            if (outError) {
                outError->clear();
            }
            return true;
        }
        game::runtime::route1_environment::
            BoardLayoutTransform previous;
        if (!sceneMutationSession().setObjectOverride(
                edit,
                previous,
                outError)) {
            return false;
        }
        layoutSelection_.select(edit.stableId);
        recordSceneEdit(previous);
        editSession_.clearSceneLiveEdit();
        status_ =
            "Route 1 layout override saved and hot-reloaded: " +
            layoutSelection_.id() + ".";
        if (outError) {
            outError->clear();
        }
        return true;
    }

    bool previewLayoutObjectOverride(
        const engine::editor::EditorProjectLayoutEdit& edit,
        std::string* outError) override {
        if (edit.stableId &&
            findPreviewUnitLayoutObject(edit.stableId)) {
            return previewPreviewUnitTransform(edit, outError);
        }
        if (!sceneViewReady_ ||
            !edit.stableId) {
            if (outError) {
                *outError =
                    "A mounted Route 1 scene and stable target are required.";
            }
            return false;
        }
        if (edit.stableId == kGameplayBoardStableId) {
            editSession_.beginSceneLiveEdit(
                edit.stableId,
                environment_.layout());
            const auto next =
                scene_mutations::boardRegistrationFromCenter(
                    environment_.layout(),
                    edit.translation,
                    kTerrainTileSizeCm,
                    kTerrainElevationStepCm);
            if (scene_mutations::sameBoardRegistration(
                    next,
                    environment_.layout())) {
                layoutSelection_.select(edit.stableId);
                if (outError) {
                    outError->clear();
                }
                return true;
            }
            std::string error;
            if (!environment_.previewBoardLayout(next, &error)) {
                if (outError) {
                    *outError = std::move(error);
                }
                return false;
            }
            synchronizeBoardCellSize(next.boardCellSizeWorld);
            layoutSelection_.select(edit.stableId);
            status_ =
                "Live terrain-bound board preview (release to rebuild and autosave).";
            if (outError) {
                outError->clear();
            }
            return true;
        }
        editSession_.beginSceneLiveEdit(
            edit.stableId,
            environment_.layout());
        std::string error;
        if (!environment_.previewLayoutObjectOverride(
                edit.stableId,
                edit.translation,
                edit.rotationDegrees,
                edit.scale,
                edit.suppressed,
                edit.reason
                    ? edit.reason
                    : "autochess_board_clearance",
                &error)) {
            if (outError) {
                *outError = std::move(error);
            }
            return false;
        }
        layoutSelection_.select(edit.stableId);
        status_ =
            "Live Route 1 layout edit: " +
            layoutSelection_.id() +
            " (release to autosave).";
        if (outError) {
            outError->clear();
        }
        return true;
    }

    bool commitLayoutObjectOverride(
        const char* stableId,
        std::string* outError) override {
        if (stableId &&
            findPreviewUnitLayoutObject(stableId)) {
            return commitPreviewUnitTransform(
                stableId, outError);
        }
        if (!sceneViewReady_ ||
            !stableId) {
            if (outError) {
                *outError =
                    "A mounted Route 1 scene and stable target are required.";
            }
            return false;
        }
        if (stableId == kGameplayBoardStableId) {
            auto commitPlan =
                editSession_.prepareSceneCommit(stableId);
            if (commitPlan.targetConflict) {
                if (outError) {
                    *outError =
                        "The live board-layout target changed before commit.";
                }
                return false;
            }
            const auto liveLayout = environment_.layout();
            if (commitPlan.baseline &&
                liveLayout.terrainGridOrigin ==
                    commitPlan.baseline->terrainGridOrigin &&
                liveLayout.terrainElevationLevel ==
                    commitPlan.baseline->terrainElevationLevel) {
                editSession_.finishSceneLiveEdit();
                layoutSelection_.select(stableId);
                status_ =
                    "Gameplay board remained on its current Route 1 grid cell.";
                if (outError) {
                    outError->clear();
                }
                return true;
            }
            std::string error;
            if (!sceneMutationSession().applyBoardRegistration(
                    liveLayout,
                    commitPlan.rollbackOr(liveLayout),
                    &error)) {
                if (commitPlan.baseline) {
                    synchronizeBoardCellSize(
                        commitPlan.baseline->boardCellSizeWorld);
                }
                editSession_.finishSceneLiveEdit();
                if (outError) {
                    *outError =
                        "Could not autosave the gameplay board layout: " +
                        error;
                }
                return false;
            }
            const bool historyRecorded =
                commitPlan.baseline.has_value();
            editSession_.acceptSceneCommit(std::move(commitPlan));
            if (historyRecorded) {
                refreshEnvironmentPrefabAssets();
            }
            layoutSelection_.select(stableId);
            status_ =
                "Snapped gameplay board layout rebuilt and autosaved.";
            if (outError) {
                outError->clear();
            }
            return true;
        }
        auto commitPlan =
            editSession_.prepareSceneCommit(stableId);
        if (commitPlan.targetConflict) {
            if (outError) {
                *outError =
                    "The live layout target changed before commit.";
            }
            return false;
        }
        std::string error;
        const auto liveLayout =
            environment_.layout();
        scene_mutation_session::Session::FailureStage failureStage;
        if (!sceneMutationSession().applyAuthoredLayout(
                liveLayout,
                commitPlan.rollbackOr(liveLayout),
                &error,
                &failureStage)) {
            editSession_.finishSceneLiveEdit();
            if (outError) {
                *outError = failureStage ==
                        scene_mutation_session::Session::
                            FailureStage::Apply
                    ? "Could not finalize the live layout edit; it was rolled back: " +
                        error
                    : "Could not autosave the layout override; the live edit was rolled back: " +
                        error;
            }
            return false;
        }
        layoutSelection_.select(stableId);
        const bool historyRecorded =
            commitPlan.baseline.has_value();
        editSession_.acceptSceneCommit(std::move(commitPlan));
        if (historyRecorded) {
            refreshEnvironmentPrefabAssets();
        }
        status_ =
            "Route 1 layout override autosaved: " +
            layoutSelection_.id() + ".";
        if (outError) {
            outError->clear();
        }
        return true;
    }

    void cancelLayoutObjectOverride(
        const char* stableId) override {
        if ((stableId &&
             findPreviewUnitLayoutObject(stableId)) ||
            (!stableId &&
             editSession_.previewUnitLiveEditActive())) {
            cancelPreviewUnitTransform(stableId);
            return;
        }
        if (const auto baseline =
                editSession_.cancelSceneLiveEdit(stableId)) {
            std::string ignored;
            environment_.applyBoardLayout(
                *baseline,
                &ignored);
            synchronizeBoardCellSize(
                baseline->boardCellSizeWorld);
        }
        status_ =
            "Live Route 1 layout edit cancelled.";
    }

    bool resetLayoutObjectOverride(
        const char* stableId,
        std::string* outError) override {
        if (stableId &&
            findPreviewUnitLayoutObject(stableId)) {
            return resetPreviewUnitTransform(
                stableId, outError);
        }
        if (!sceneViewReady_ ||
            !stableId) {
            if (outError) {
                *outError =
                    "A mounted Route 1 scene and stable target are required.";
            }
            return false;
        }
        if (stableId == kGameplayBoardStableId) {
            const auto previous = environment_.layout();
            const auto next =
                scene_mutations::defaultBoardRegistration(
                    previous,
                    defaultBoardTerrainGridOrigin(
                        activeSceneId_));
            std::string error;
            if (!sceneMutationSession().applyBoardRegistration(
                    next,
                    previous,
                    &error)) {
                synchronizeBoardCellSize(previous.boardCellSizeWorld);
                if (outError) {
                    *outError =
                        "Could not restore the default gameplay board layout: " +
                        error;
                }
                return false;
            }
            synchronizeBoardCellSize(next.boardCellSizeWorld);
            recordSceneEdit(previous);
            editSession_.clearSceneLiveEdit();
            layoutSelection_.select(stableId);
            status_ = "Gameplay board layout restored to its default registration.";
            if (outError) {
                outError->clear();
            }
            return true;
        }
        game::runtime::route1_environment::
            BoardLayoutTransform previous;
        if (!sceneMutationSession().resetObjectOverride(
                stableId,
                previous,
                outError)) {
            return false;
        }
        layoutSelection_.select(stableId);
        recordSceneEdit(previous);
        editSession_.clearSceneLiveEdit();
        status_ =
            "Route 1 layout target restored to canonical source: " +
            layoutSelection_.id() + ".";
        if (outError) {
            outError->clear();
        }
        return true;
    }

    bool duplicateLayoutObject(
        const char* stableId,
        std::string* outCreatedStableId,
        std::string* outError) override {
        if (stableId &&
            findPreviewUnitLayoutObject(stableId)) {
            if (outCreatedStableId) {
                outCreatedStableId->clear();
            }
            if (outError) {
                *outError =
                    "Gameplay-preview units come from the selected preview roster and cannot be duplicated as environment objects.";
            }
            return false;
        }
        if (!sceneViewReady_ || !stableId) {
            if (outError) {
                *outError =
                    "A mounted Route 1 scene and selected object are required.";
            }
            return false;
        }
        game::runtime::route1_environment::
            BoardLayoutTransform previous;
        std::string createdStableId;
        if (!sceneMutationSession().duplicateObject(
                stableId,
                previous,
                createdStableId,
                outError)) {
            return false;
        }
        recordSceneEdit(previous);
        layoutSelection_.select(createdStableId);
        if (outCreatedStableId) {
            *outCreatedStableId = createdStableId;
        }
        if (outError) {
            outError->clear();
        }
        return true;
    }

    bool deleteLayoutObject(
        const char* stableId,
        std::string* outError) override {
        if (stableId &&
            findPreviewUnitLayoutObject(stableId)) {
            if (outError) {
                *outError =
                    "Gameplay-preview units are roster entries; edit the preview definition instead of deleting them from the environment.";
            }
            return false;
        }
        if (!sceneViewReady_ || !stableId) {
            if (outError) {
                *outError =
                    "A mounted Route 1 scene and selected object are required.";
            }
            return false;
        }
        game::runtime::route1_environment::
            BoardLayoutTransform previous;
        if (!sceneMutationSession().deleteObject(
                stableId,
                previous,
                outError)) {
            return false;
        }
        recordSceneEdit(previous);
        layoutSelection_.clear();
        if (outError) {
            outError->clear();
        }
        return true;
    }

    bool deleteLayoutObjects(
        const char* const* stableIds,
        std::size_t stableIdCount,
        std::string* outError) override {
        if (!sceneViewReady_ || !stableIds ||
            stableIdCount == 0u) {
            if (outError) {
                *outError =
                    "A mounted Route 1 scene and at least one selected object are required.";
            }
            return false;
        }
        game::runtime::route1_environment::
            BoardLayoutTransform previous;
        if (!sceneMutationSession().deleteObjects(
                stableIds,
                stableIdCount,
                previous,
                outError)) {
            return false;
        }
        recordSceneEdit(previous);
        layoutSelection_.clear();
        editSession_.clearSceneLiveEdit();
        refreshEnvironmentPrefabAssets();
        if (outError) {
            outError->clear();
        }
        return true;
    }

    bool boardClearanceAvailable() const noexcept {
        return sceneViewReady_ &&
            route1_scene_variants::editable(activeSceneId_);
    }

    std::size_t projectCommandCount() const noexcept override {
        return editor_commands::count(
            boardClearanceAvailable());
    }

    engine::editor::EditorProjectCommand projectCommand(
        std::size_t index) const noexcept override {
        return editor_commands::command(
            boardClearanceAvailable(),
            index);
    }

    bool executeProjectCommand(
        const char* commandId,
        const engine::editor::EditorProjectCommandValue* values,
        std::size_t valueCount,
        engine::editor::EditorProjectCommandResult& outResult,
        std::string* outError) override {
        outResult = {};
        const std::string_view id = commandId ? commandId : "";
        const auto kind = editor_commands::resolve(id);
        if (kind == editor_commands::Kind::ClearBoardFootprint) {
            editor_commands::BoardClearanceRequest request;
            if (!editor_commands::parseBoardClearanceRequest(
                    values,
                    valueCount,
                    request,
                    outError)) {
                return false;
            }
            editor_commands::BoardClearanceResult result;
            if (!applyBoardClearance(
                    request,
                    result,
                    outError)) {
                return false;
            }
            commandStatus_ =
                editor_commands::boardClearanceStatus(result);
            outResult = {
                .status = commandStatus_.c_str(),
                .sceneChanged = true,
                .assetsChanged = true};
            return true;
        }
        if (kind == editor_commands::Kind::ResetImportedScene) {
            if (!resetSceneToSource(outError)) {
                return false;
            }
            commandStatus_ =
                "Route 1 restored to its imported source baseline and autosaved.";
            outResult = {
                .status = commandStatus_.c_str(),
                .sceneChanged = true,
                .assetsChanged = true};
            return true;
        }
        if (kind ==
            editor_commands::Kind::ToggleTerrainSeamDiagnostics) {
            terrainSeamDiagnosticsVisible_ =
                !terrainSeamDiagnosticsVisible_;
            const auto& diagnostics = environment_.stats();
            commandStatus_ = terrainSeamDiagnosticsVisible_
                ? "Terrain seam diagnostics enabled: cyan outlines " +
                    std::to_string(
                        diagnostics.terrainContinuousFieldCellCount) +
                    " resolved cells; magenta marks " +
                    std::to_string(
                        diagnostics.
                            terrainProjectedShadowMismatchEdgeCount) +
                    " projected-shadow mismatch edges."
                : "Terrain seam diagnostics hidden.";
            outResult = {
                .status = commandStatus_.c_str(),
                .sceneChanged = false,
                .assetsChanged = false};
            return true;
        }
        if (kind ==
            editor_commands::Kind::ToggleTerrainPatchV2Preview) {
            const bool enabled =
                !environment_.terrainPatchV2PreviewEnabled();
            if (!environment_.setTerrainPatchV2PreviewEnabled(
                    enabled,
                    outError)) {
                return false;
            }
            terrainPatchV2PreviewEnabled_ = enabled;
            const auto& diagnostics = environment_.stats();
            commandStatus_ = enabled
                ? "Terrain Patch V2 preview enabled: " +
                    std::to_string(
                        diagnostics.terrainPatchV2RegionCount) +
                    " connected regions, " +
                    std::to_string(
                        diagnostics.terrainPatchV2CoreCellCount) +
                    " edited cells, " +
                    std::to_string(
                        diagnostics.terrainPatchV2TransitionCellCount) +
                    " source-transition cells; invalid boundaries: " +
                    std::to_string(
                        diagnostics.terrainPatchV2InvalidBoundaryCount) +
                    "."
                : "Terrain Patch V2 preview disabled; production tile cook restored.";
            outResult = {
                .status = commandStatus_.c_str(),
                .sceneChanged = false,
                .assetsChanged = false};
            return true;
        }
        if (outError) {
            *outError = editor_commands::unknownCommandError(id);
        }
        return false;
    }

    bool applyBoardClearance(
        const editor_commands::BoardClearanceRequest& request,
        editor_commands::BoardClearanceResult& outResult,
        std::string* outError) {
        outResult = {};
        if (!boardClearanceAvailable()) {
            if (outError) {
                *outError =
                    "Board clearing requires the mounted Route 1 scene.";
            }
            return false;
        }
        const auto config = boardClearanceConfig();
        scene_mutations::BoardClearancePlan plan;
        if (!scene_mutations::buildBoardClearancePlan(
                request,
                environment_.layout(),
                environment_.layoutObjects(),
                environment_.terrainTiles(),
                config,
                plan,
                outError)) {
            return false;
        }
        game::runtime::route1_environment::
            BoardLayoutTransform previous;
        if (!sceneMutationSession().applyBoardClearance(
                plan,
                config,
                previous,
                outError)) {
            return false;
        }
        outResult = plan.result;
        recordSceneEdit(previous);
        layoutSelection_.clear();
        editSession_.clearSceneLiveEdit();
        refreshEnvironmentPrefabAssets();
        if (outError) {
            outError->clear();
        }
        return true;
    }
    bool resetSceneToSource(
        std::string* outError) {
        if (!sceneViewReady_) {
            if (outError) {
                *outError =
                    "A mounted authored scene is required.";
            }
            return false;
        }
        const auto previous = environment_.layout();
        const auto baseline =
            scene_mutations::importedSceneBaseline(previous);
        std::string error;
        if (!sceneMutationSession().applyAuthoredLayout(
                baseline,
                previous,
                &error)) {
            if (outError) {
                *outError =
                    "Could not restore and persist the imported scene baseline: " +
                    error;
            }
            return false;
        }
        recordSceneEdit(previous);
        layoutSelection_.clear();
        editSession_.clearSceneLiveEdit();
        refreshEnvironmentPrefabAssets();
        if (outError) {
            outError->clear();
        }
        return true;
    }

    bool renameLayoutObject(
        const engine::editor::
            EditorProjectLayoutObjectCommand& command,
        std::string* outError) override {
        if (command.stableId &&
            findPreviewUnitLayoutObject(command.stableId)) {
            if (outError) {
                *outError =
                    "Gameplay-preview unit names are derived from their roster species.";
            }
            return false;
        }
        if (!sceneViewReady_ ||
            !command.stableId ||
            !command.value) {
            if (outError) {
                *outError =
                    "A selected object and non-empty name are required.";
            }
            return false;
        }
        game::runtime::route1_environment::
            BoardLayoutTransform previous;
        if (!sceneMutationSession().renameObject(
                command.stableId,
                command.value,
                previous,
                outError)) {
            return false;
        }
        recordSceneEdit(std::move(previous));
        layoutSelection_.select(command.stableId);
        if (outError) {
            outError->clear();
        }
        return true;
    }

    bool reparentLayoutObject(
        const engine::editor::
            EditorProjectLayoutObjectCommand& command,
        std::string* outError) override {
        if (command.stableId &&
            findPreviewUnitLayoutObject(command.stableId)) {
            if (outError) {
                *outError =
                    "Gameplay-preview units remain grouped by player, enemy, and bench placement.";
            }
            return false;
        }
        if (!sceneViewReady_ ||
            !command.stableId ||
            !command.value) {
            if (outError) {
                *outError =
                    "A selected object and hierarchy folder are required.";
            }
            return false;
        }
        game::runtime::route1_environment::
            BoardLayoutTransform previous;
        if (!sceneMutationSession().reparentObject(
                command.stableId,
                command.value,
                previous,
                outError)) {
            return false;
        }
        recordSceneEdit(std::move(previous));
        layoutSelection_.select(command.stableId);
        if (outError) {
            outError->clear();
        }
        return true;
    }

    bool canUndoSceneEdit() const noexcept override {
        return editSession_.canUndo();
    }

    bool canRedoSceneEdit() const noexcept override {
        return editSession_.canRedo();
    }

    bool undoSceneEdit(
        std::string* outError) override {
        const auto undoTarget =
            editSession_.undoTarget();
        if (!undoTarget) {
            if (outError) {
                *outError = "There is no scene edit to undo.";
            }
            return false;
        }
        const auto current = environment_.layout();
        const auto& target = *undoTarget;
        std::string error;
        if (!sceneMutationSession().applyHistoryLayout(
                target,
                current,
                &error)) {
            if (outError) {
                *outError =
                    "Could not undo and persist the scene edit: " +
                    error;
            }
            return false;
        }
        synchronizeBoardCellSize(
            target.boardCellSizeWorld);
        editSession_.acceptUndo(current);
        refreshEnvironmentPrefabAssets();
        if (outError) {
            outError->clear();
        }
        return true;
    }

    bool redoSceneEdit(
        std::string* outError) override {
        const auto redoTarget =
            editSession_.redoTarget();
        if (!redoTarget) {
            if (outError) {
                *outError = "There is no scene edit to redo.";
            }
            return false;
        }
        const auto current = environment_.layout();
        const auto& target = *redoTarget;
        std::string error;
        if (!sceneMutationSession().applyHistoryLayout(
                target,
                current,
                &error)) {
            if (outError) {
                *outError =
                    "Could not redo and persist the scene edit: " +
                    error;
            }
            return false;
        }
        synchronizeBoardCellSize(
            target.boardCellSizeWorld);
        editSession_.acceptRedo(current);
        refreshEnvironmentPrefabAssets();
        if (outError) {
            outError->clear();
        }
        return true;
    }

    void selectLayoutObject(
        const char* stableId) override {
        layoutSelection_.select(stableId);
    }

    bool layoutOverlayVisible() const noexcept override {
        return layoutOverlayVisible_;
    }

    void setLayoutOverlayVisible(
        bool visible) override {
        layoutOverlayVisible_ = visible;
    }

private:
    enum class ActiveAssetPreview {
        Model,
        VisualEffect,
        Environment,
    };

    struct SavedEnvironment {
        std::string name;
        std::optional<std::string> previous;
    };

    using PreviewUnitTransform =
        layout_transactions::PreviewUnitTransform;

    static bool samePreviewUnitTransform(
        const PreviewUnitTransform& left,
        const PreviewUnitTransform& right) {
        const auto same3 = [](
            const std::array<float, 3>& a,
            const std::array<float, 3>& b) {
            for (std::size_t index = 0u;
                 index < a.size();
                 ++index) {
                if (std::abs(a[index] - b[index]) > 0.0001f) {
                    return false;
                }
            }
            return true;
        };
        return same3(left.position, right.position) &&
            same3(left.rotationDegrees, right.rotationDegrees);
    }

    struct PreviewUnitLayoutObject {
        game::runtime::EditorPreviewUnit unit;
        PreviewUnitTransform source;
        std::string unitKey;
        std::string stableId;
        std::string displayName;
        std::string categoryPath;
        std::string prefabAssetId;
        std::array<float, 3> boundsMinimum{};
        std::array<float, 3> boundsMaximum{};
        std::array<float, 2> viewportPosition{};
        std::array<float, 6> viewportAxisDirections{};
        std::array<float, 3> viewportSourceUnitsPerPixel{
            1.0f, 1.0f, 1.0f};
        bool viewportVisible = false;
        bool hasOverride = false;
    };

    void refreshPreviewUnitLayoutObjects(
        bool captureSourceDefaults) {
        previewUnitLayoutObjects_.clear();
        if (!gameRuntime_) {
            return;
        }
        std::unordered_map<std::string, int> occurrences;
        const std::size_t unitCount =
            gameRuntime_->editorPreviewUnitCount();
        previewUnitLayoutObjects_.reserve(unitCount);
        for (std::size_t index = 0u;
             index < unitCount;
             ++index) {
            game::runtime::EditorPreviewUnit unit;
            if (!gameRuntime_->editorPreviewUnit(index, unit)) {
                continue;
            }
            const std::string placement =
                unit.benchUnit ? "bench" : "board";
            const std::string side =
                unit.playerSide ? "player" : "enemy";
            const std::string occurrenceKey =
                placement + "/" + side + "/" +
                unit.speciesName;
            const int occurrence =
                ++occurrences[occurrenceKey];
            const std::string unitKey =
                occurrenceKey + "/" +
                std::to_string(occurrence);
            const PreviewUnitTransform current{
                .position = unit.position,
                .rotationDegrees = unit.rotationDegrees};
            if (captureSourceDefaults ||
                !previewUnitSourceTransforms_.contains(unitKey)) {
                previewUnitSourceTransforms_[unitKey] = current;
            }
            std::string displayName = unit.speciesName;
            if (!displayName.empty()) {
                displayName.front() = static_cast<char>(
                    std::toupper(
                        static_cast<unsigned char>(
                            displayName.front())));
            }
            displayName += unit.playerSide
                ? " (Player)"
                : " (Enemy)";
            PreviewUnitLayoutObject object{
                .unit = unit,
                .source = previewUnitSourceTransforms_[unitKey],
                .unitKey = unitKey,
                .stableId =
                    "gameplay-preview/" +
                    activePreviewId_ + "/" + unitKey,
                .displayName = std::move(displayName),
                .categoryPath =
                    std::string("Gameplay Preview Units/") +
                    (placement == "bench"
                         ? "Benches"
                         : side == "player"
                         ? "Player Board"
                         : "Enemy Board"),
                .prefabAssetId =
                    "pokemon/" + unit.speciesName,
                .hasOverride =
                    persistence_.hasPreviewUnitOverride(
                        activePreviewId_,
                        unitKey)};
            const float radius = std::max(
                0.18f,
                object.unit.resolvedRenderScale * 0.45f);
            object.boundsMinimum = {
                object.unit.position[0] - radius,
                object.unit.position[1],
                object.unit.position[2] - radius};
            object.boundsMaximum = {
                object.unit.position[0] + radius,
                object.unit.position[1] + radius * 2.0f,
                object.unit.position[2] + radius};

            if (gameLayoutProjectionReady_) {
                auto markerPosition = object.unit.position;
                markerPosition[1] += radius;
                const auto projection =
                    viewport_projection::projectTransform(
                        viewport_projection::Context{
                            .viewProjectionMatrix4x4 =
                                glm::value_ptr(
                                    gameLayoutViewProjection_),
                            .surfaceWidth =
                                gameLayoutProjectionWidth_,
                            .surfaceHeight =
                                gameLayoutProjectionHeight_},
                        viewport_projection::TransformInput{
                            .sourcePosition = markerPosition,
                            .sourceAxisLength = 0.5f});
                if (projection.visible) {
                    object.viewportPosition =
                        projection.viewportPosition;
                    object.viewportAxisDirections =
                        projection.viewportAxisDirections;
                    object.viewportSourceUnitsPerPixel =
                        projection.viewportSourceUnitsPerPixel;
                    object.viewportVisible = true;
                }
            }
            previewUnitLayoutObjects_.push_back(
                std::move(object));
        }
    }

    void applySavedPreviewUnitOverrides() {
        if (!gameRuntime_) {
            return;
        }
        for (const auto& object :
             previewUnitLayoutObjects_) {
            PreviewUnitTransform saved{
                .position = object.unit.position,
                .rotationDegrees =
                    object.unit.rotationDegrees};
            if (!persistence_.applyPreviewUnitOverride(
                    activePreviewId_,
                    object.unitKey,
                    saved)) {
                continue;
            }
            gameRuntime_->setEditorPreviewUnitTransform(
                object.unit.unitId,
                saved.position,
                saved.rotationDegrees,
                true);
        }
    }

    PreviewUnitLayoutObject* findPreviewUnitLayoutObject(
        std::string_view stableId) {
        const auto found = std::find_if(
            previewUnitLayoutObjects_.begin(),
            previewUnitLayoutObjects_.end(),
            [&](const PreviewUnitLayoutObject& object) {
                return object.stableId == stableId;
            });
        return found == previewUnitLayoutObjects_.end()
            ? nullptr
            : &*found;
    }

    bool previewPreviewUnitTransform(
        const engine::editor::EditorProjectLayoutEdit& edit,
        std::string* outError) {
        auto* object = edit.stableId
            ? findPreviewUnitLayoutObject(edit.stableId)
            : nullptr;
        if (!object || !gameRuntime_) {
            if (outError) {
                *outError =
                    "The selected gameplay-preview unit is no longer available.";
            }
            return false;
        }
        editSession_.beginPreviewUnitLiveEdit(
            edit.stableId,
            PreviewUnitTransform{
                .position = object->unit.position,
                .rotationDegrees = object->unit.rotationDegrees});
        const int unitId = object->unit.unitId;
        if (!gameRuntime_->setEditorPreviewUnitTransform(
                unitId,
                edit.translation,
                edit.rotationDegrees,
                true)) {
            if (outError) {
                *outError =
                    "The runtime rejected the gameplay-preview unit transform.";
            }
            return false;
        }
        layoutSelection_.select(edit.stableId);
        refreshPreviewUnitLayoutObjects(false);
        status_ =
            "Live grid-snapped starting-position preview: " +
            layoutSelection_.id() + ".";
        if (outError) {
            outError->clear();
        }
        return true;
    }

    bool commitPreviewUnitTransform(
        const char* stableId,
        std::string* outError) {
        auto* object = stableId
            ? findPreviewUnitLayoutObject(stableId)
            : nullptr;
        if (!object) {
            if (outError) {
                *outError =
                    "The gameplay-preview unit disappeared before its transform could be saved.";
            }
            return false;
        }
        const auto commitPlan =
            editSession_.preparePreviewUnitCommit(stableId);
        if (!commitPlan.baseline &&
            !commitPlan.targetConflict) {
            // ImGui can report a deactivation when hierarchy selection moves
            // between unrelated object types. A commit without a preceding
            // live preview is not an authored placement edit.
            if (outError) {
                outError->clear();
            }
            return true;
        }
        if (commitPlan.targetConflict) {
            if (outError) {
                *outError =
                    "The gameplay-preview unit changed before its live edit was committed.";
            }
            return false;
        }
        const PreviewUnitTransform current{
            .position = object->unit.position,
            .rotationDegrees = object->unit.rotationDegrees};
        if (samePreviewUnitTransform(
                *commitPlan.baseline,
                current)) {
            editSession_.finishPreviewUnitLiveEdit();
            if (outError) {
                outError->clear();
            }
            return true;
        }

        std::string error;
        if (!persistence_.savePreviewUnitOverride(
                activePreviewId_,
                object->unitKey,
                editor_persistence::PreviewUnitRecord{
                    .speciesName =
                        object->unit.speciesName,
                    .playerSide =
                        object->unit.playerSide,
                    .benchUnit = object->unit.benchUnit,
                    .boardColumn =
                        object->unit.boardColumn,
                    .boardRow = object->unit.boardRow,
                    .benchSlot = object->unit.benchSlot,
                    .transform = current},
                &error)) {
            if (commitPlan.baseline && gameRuntime_) {
                gameRuntime_->setEditorPreviewUnitTransform(
                    object->unit.unitId,
                    commitPlan.baseline->position,
                    commitPlan.baseline->rotationDegrees,
                    true);
            }
            editSession_.finishPreviewUnitLiveEdit();
            refreshPreviewUnitLayoutObjects(false);
            if (outError) {
                *outError =
                    "Could not save the gameplay-preview starting position: " +
                    error;
            }
            return false;
        }
        editSession_.finishPreviewUnitLiveEdit();
        layoutSelection_.select(stableId);
        const std::string speciesName =
            object->unit.speciesName;
        refreshPreviewUnitLayoutObjects(false);
        status_ =
            "Gameplay-preview starting position autosaved for " +
            speciesName + ".";
        if (outError) {
            outError->clear();
        }
        return true;
    }

    void cancelPreviewUnitTransform(const char* stableId) {
        if (!gameRuntime_) {
            return;
        }
        const auto rollback =
            editSession_.cancelPreviewUnitLiveEdit(stableId);
        if (!rollback) {
            return;
        }
        if (auto* object =
                findPreviewUnitLayoutObject(rollback->targetId)) {
            gameRuntime_->setEditorPreviewUnitTransform(
                object->unit.unitId,
                rollback->baseline.position,
                rollback->baseline.rotationDegrees,
                true);
        }
        refreshPreviewUnitLayoutObjects(false);
        status_ =
            "Gameplay-preview starting-position edit cancelled.";
    }

    bool resetPreviewUnitTransform(
        const char* stableId,
        std::string* outError) {
        auto* object = stableId
            ? findPreviewUnitLayoutObject(stableId)
            : nullptr;
        if (!object || !gameRuntime_) {
            if (outError) {
                *outError =
                    "The selected gameplay-preview unit is no longer available.";
            }
            return false;
        }
        const auto source =
            previewUnitSourceTransforms_.find(object->unitKey);
        if (source == previewUnitSourceTransforms_.end()) {
            if (outError) {
                *outError =
                    "The preview's original unit transform is unavailable.";
            }
            return false;
        }
        const PreviewUnitTransform previous{
            .position = object->unit.position,
            .rotationDegrees = object->unit.rotationDegrees};
        const int unitId = object->unit.unitId;
        const std::string unitKey = object->unitKey;
        gameRuntime_->setEditorPreviewUnitTransform(
            unitId,
            source->second.position,
            source->second.rotationDegrees,
            true);
        std::string error;
        if (!persistence_.resetPreviewUnitOverride(
                activePreviewId_,
                unitKey,
                &error)) {
            gameRuntime_->setEditorPreviewUnitTransform(
                unitId,
                previous.position,
                previous.rotationDegrees,
                true);
            refreshPreviewUnitLayoutObjects(false);
            if (outError) {
                *outError =
                    "Could not reset the gameplay-preview starting position: " +
                    error;
            }
            return false;
        }
        editSession_.clearPreviewUnitLiveEdit();
        refreshPreviewUnitLayoutObjects(false);
        status_ =
            "Gameplay-preview unit restored to its original starting slot.";
        if (outError) {
            outError->clear();
        }
        return true;
    }

    void refreshEnvironmentPrefabAssets() {
        environmentPrefabCatalog_.clear();
        if (!sceneViewReady_ || projectRoot_.empty()) {
            return;
        }
        const auto& objects = environment_.layoutObjects();
        environmentPrefabCatalog_.rebuild(
            projectRoot_,
            activeSceneId_,
            objects);
        std::cerr
            << "[PokemonAutochessEditor][PrefabCatalog] objects="
            << objects.size()
            << " assets=" << environmentPrefabCatalog_.size()
            << '\n';
    }

    void recordSceneEdit(
        game::runtime::route1_environment::
            BoardLayoutTransform previous) {
        editSession_.recordSceneEdit(std::move(previous));
        refreshEnvironmentPrefabAssets();
    }

    scene_mutation_session::Session sceneMutationSession() {
        return scene_mutation_session::Session(
            environment_,
            persistence_,
            sceneViewReady_,
            activeBoardLayoutPath_,
            activeAuthoredScenePath_);
    }

    scene_mutations::BoardClearanceConfig
    boardClearanceConfig() const noexcept {
        return {
            .boardCellSizeWorld = boardCellSize_,
            .terrainTileSizeCm = kTerrainTileSizeCm,
            .terrainElevationStepCm =
                kTerrainElevationStepCm,
            .groundPrototypeStableId =
                kBoardGroundPrototypeStableId,
            .groundPrefabAssetId =
                kBoardGroundPrefabAssetId,
            .groundInstanceStableId =
                kBoardGroundInstanceStableId,
            .terrainTileSetAssetId =
                kTerrainTileSetAssetId};
    }

    void synchronizeBoardCellSize(float cellSizeWorld) {
        boardCellSize_ = std::clamp(
            cellSizeWorld,
            0.25f,
            4.0f);
        if (gameRuntime_) {
            gameRuntime_->setEditorBoardCellSize(
                boardCellSize_);
        }
    }

    void renderLayoutOverlay(
        const engine::editor::EditorProjectRenderContext&
            context) const {
        if (!layoutOverlayVisible_ ||
            !context.renderer ||
            !context.viewProjectionMatrix4x4 ||
            !sceneViewReady_) {
            return;
        }
        const auto& layout =
            environment_.layout();
        const int columns = std::max(
            1,
            static_cast<int>(layout.boardCells[0]));
        const int rows = std::max(
            1,
            static_cast<int>(layout.boardCells[1]));
        const float cellSize =
            std::max(0.05f, boardCellSize_);
        const float halfWidth =
            static_cast<float>(columns) *
            cellSize * 0.5f;
        const float halfDepth =
            static_cast<float>(rows) *
            cellSize * 0.5f;
        constexpr float gridY = 0.08f;
        std::vector<IRenderBackend::DebugLine> lines;
        lines.reserve(
            static_cast<std::size_t>(
                columns * rows * 4 +
                static_cast<int>(layout.benchSlots) * 8 +
                static_cast<int>(environment_.terrainTiles().size()) * 4 +
                30));
        // Draw the board and benches through the recovered terrain cells
        // themselves. This deliberately avoids a second floating-point grid
        // that could merely look aligned while remaining logically
        // independent.
        const glm::mat4 worldFromSource = glm::make_mat4(
            game::runtime::route1_environment::
                worldFromSourceMatrix(layout).data());
        constexpr std::array<std::array<float, 2>, 4>
            tileCorners{{
                {0.0f, 0.0f},
                {1.0f, 0.0f},
                {1.0f, 1.0f},
                {0.0f, 1.0f},
            }};
        const auto terrainTileAt =
            [&](std::int32_t gridX,
                std::int32_t gridZ)
                -> const game::runtime::route1_environment::
                    TerrainTileState* {
                const auto found = std::find_if(
                    environment_.terrainTiles().begin(),
                    environment_.terrainTiles().end(),
                    [&](const auto& tile) {
                        return tile.gridX == gridX &&
                            tile.gridZ == gridZ;
                    });
                return found == environment_.terrainTiles().end()
                    ? nullptr
                    : &*found;
            };
        const auto terrainCellWorldCorners =
            [&](std::int32_t gridX,
                std::int32_t gridZ) {
                const auto* tile = terrainTileAt(gridX, gridZ);
                std::array<glm::vec3, 4> worldCorners{};
                for (std::size_t corner = 0u;
                     corner < tileCorners.size();
                     ++corner) {
                    const float localX = tileCorners[corner][0];
                    const float localZ = tileCorners[corner][1];
                    std::int32_t cornerLevel = tile
                        ? tile->elevationLevel
                        : layout.terrainElevationLevel;
                    if (tile &&
                        ((tile->shape == "ramp_east" && localX > 0.5f) ||
                         (tile->shape == "ramp_west" && localX < 0.5f) ||
                         (tile->shape == "ramp_north" && localZ > 0.5f) ||
                         (tile->shape == "ramp_south" && localZ < 0.5f))) {
                        ++cornerLevel;
                    }
                    const glm::vec3 sourcePoint{
                        (static_cast<float>(gridX) + localX) *
                            kTerrainTileSizeCm,
                        static_cast<float>(cornerLevel) *
                                kTerrainElevationStepCm +
                            1.0f,
                        (static_cast<float>(gridZ) + localZ) *
                            kTerrainTileSizeCm};
                    worldCorners[corner] = glm::vec3(
                        worldFromSource *
                        glm::vec4(sourcePoint, 1.0f));
                }
                return worldCorners;
            };
        for (int row = 0; row < rows; ++row) {
            for (int column = 0; column < columns; ++column) {
                const std::int32_t gridX =
                    layout.terrainGridOrigin[0] + column;
                const std::int32_t gridZ =
                    layout.terrainGridOrigin[1] + row;
                const auto worldCorners =
                    terrainCellWorldCorners(gridX, gridZ);
                for (std::size_t edge = 0u;
                     edge < worldCorners.size();
                     ++edge) {
                    const bool perimeter =
                        (edge == 0u && row == 0) ||
                        (edge == 1u && column == columns - 1) ||
                        (edge == 2u && row == rows - 1) ||
                        (edge == 3u && column == 0);
                    appendProjectedEditorLine(
                        context,
                        worldCorners[edge],
                        worldCorners[(edge + 1u) % worldCorners.size()],
                        perimeter ? 1.0f : 0.20f,
                        perimeter ? 0.64f : 0.92f,
                        perimeter ? 0.18f : 0.68f,
                        perimeter ? 0.95f : 0.72f,
                        perimeter ? 2.4f : 1.15f,
                        lines);
                }
            }
        }

        if (terrainSeamDiagnosticsVisible_) {
            // Cyan outlines show the connected authored regions whose
            // material fields were rebuilt as one continuous field. Magenta
            // edges identify compatible neighbor pairs with inconsistent
            // projected-shadow receipt; that remains an explicit authoring
            // choice rather than an automatic destructive correction.
            constexpr std::array<std::array<std::size_t, 2>, 4>
                seamCornerIndices{{
                    {3u, 2u},
                    {1u, 2u},
                    {0u, 1u},
                    {0u, 3u},
                }};
            constexpr std::array<std::array<std::int32_t, 2>, 4>
                seamNeighborDirections{{
                    {0, 1},
                    {1, 0},
                    {0, -1},
                    {-1, 0},
                }};
            for (const auto& tile : environment_.terrainTiles()) {
                if (!tile.rebuildContinuousMaterialFields &&
                    tile.projectedShadowMismatchEdgeMask == 0u) {
                    continue;
                }
                const auto worldCorners = terrainCellWorldCorners(
                    tile.gridX, tile.gridZ);
                for (std::size_t edge = 0u;
                     edge < seamNeighborDirections.size();
                     ++edge) {
                    const auto direction = seamNeighborDirections[edge];
                    const auto* neighbor = terrainTileAt(
                        tile.gridX + direction[0],
                        tile.gridZ + direction[1]);
                    const bool resolvedBoundary =
                        tile.rebuildContinuousMaterialFields &&
                        (!neighbor ||
                         !neighbor->rebuildContinuousMaterialFields);
                    if (resolvedBoundary) {
                        const auto corners = seamCornerIndices[edge];
                        appendProjectedEditorLine(
                            context,
                            worldCorners[corners[0]],
                            worldCorners[corners[1]],
                            0.12f,
                            0.84f,
                            1.0f,
                            0.92f,
                            2.25f,
                            lines);
                    }
                    const bool shadowMismatch =
                        (tile.projectedShadowMismatchEdgeMask &
                         static_cast<std::uint8_t>(1u << edge)) != 0u;
                    // The opposite tile owns edge 0/1, so drawing only these
                    // directions keeps one line per shared boundary.
                    if (shadowMismatch && edge < 2u) {
                        const auto corners = seamCornerIndices[edge];
                        appendProjectedEditorLine(
                            context,
                            worldCorners[corners[0]],
                            worldCorners[corners[1]],
                            1.0f,
                            0.12f,
                            0.72f,
                            1.0f,
                            4.0f,
                            lines);
                    }
                }
            }
        }

        const int benchSlots = std::max(
            1,
            static_cast<int>(layout.benchSlots));
        const float benchHalfWidth =
            static_cast<float>(benchSlots) *
            cellSize * 0.5f;
        const float benchGap =
            static_cast<float>(layout.benchGapCells) *
            cellSize;
        const auto appendBenchGrid =
            [&](const std::array<std::int32_t, 2>& gridOrigin,
                bool north) {
                const float red = north ? 0.24f : 0.78f;
                const float green = north ? 0.82f : 0.48f;
                const float blue = north ? 1.0f : 1.0f;
                for (int slot = 0; slot < benchSlots; ++slot) {
                    const auto worldCorners =
                        terrainCellWorldCorners(
                            gridOrigin[0] + slot,
                            gridOrigin[1]);
                    for (std::size_t edge = 0u;
                         edge < worldCorners.size();
                         ++edge) {
                        const bool perimeter =
                            edge == 0u ||
                            edge == 2u ||
                            (edge == 3u && slot == 0) ||
                            (edge == 1u && slot == benchSlots - 1);
                        appendProjectedEditorLine(
                            context,
                            worldCorners[edge],
                            worldCorners[
                                (edge + 1u) % worldCorners.size()],
                            red,
                            green,
                            blue,
                            perimeter ? 0.96f : 0.78f,
                            perimeter ? 2.4f : 1.15f,
                            lines);
                    }
                }
            };
        if (layout.northBench) {
            appendBenchGrid(
                game::runtime::route1_environment::
                    northBenchTerrainGridOrigin(layout),
                true);
        }
        if (layout.southBench) {
            appendBenchGrid(
                game::runtime::route1_environment::
                    southBenchTerrainGridOrigin(layout),
                false);
        }

        const float clearance =
            cellSize * 0.35f;
        const auto appendClearance =
            [&](float minX, float maxX,
                float minZ, float maxZ) {
                minX -= clearance;
                maxX += clearance;
                minZ -= clearance;
                maxZ += clearance;
                appendProjectedEditorLine(
                    context, {minX, gridY, minZ},
                    {maxX, gridY, minZ},
                    1.0f, 0.34f, 0.12f, 0.86f, 1.8f, lines);
                appendProjectedEditorLine(
                    context, {maxX, gridY, minZ},
                    {maxX, gridY, maxZ},
                    1.0f, 0.34f, 0.12f, 0.86f, 1.8f, lines);
                appendProjectedEditorLine(
                    context, {maxX, gridY, maxZ},
                    {minX, gridY, maxZ},
                    1.0f, 0.34f, 0.12f, 0.86f, 1.8f, lines);
                appendProjectedEditorLine(
                    context, {minX, gridY, maxZ},
                    {minX, gridY, minZ},
                    1.0f, 0.34f, 0.12f, 0.86f, 1.8f, lines);
            };
        appendClearance(
            -halfWidth, halfWidth,
            -halfDepth, halfDepth);
        if (layout.northBench) {
            appendClearance(
                -benchHalfWidth, benchHalfWidth,
                halfDepth + benchGap,
                halfDepth + benchGap + cellSize);
        }
        if (layout.southBench) {
            appendClearance(
                -benchHalfWidth, benchHalfWidth,
                -halfDepth - benchGap - cellSize,
                -halfDepth - benchGap);
        }

        const auto selected = std::find_if(
            environment_.layoutObjects().begin(),
            environment_.layoutObjects().end(),
            [&](const game::runtime::route1_environment::
                    LayoutObject& object) {
                return object.stableId ==
                    layoutSelection_.id();
            });
        if (selected !=
            environment_.layoutObjects().end()) {
            const auto selectedWorldFromSource =
                game::runtime::route1_environment::
                    worldFromSourceMatrix(layout);
            const glm::vec4 world =
                glm::make_mat4(
                    selectedWorldFromSource.data()) *
                glm::vec4(
                    selected->translationCm[0],
                    selected->translationCm[1],
                    selected->translationCm[2],
                    1.0f);
            const glm::vec3 center(world);
            const float radius =
                cellSize * 0.32f;
            constexpr int segments = 20;
            for (int segment = 0;
                 segment < segments;
                 ++segment) {
                const float angle0 =
                    static_cast<float>(segment) /
                    static_cast<float>(segments) *
                    6.283185307f;
                const float angle1 =
                    static_cast<float>(segment + 1) /
                    static_cast<float>(segments) *
                    6.283185307f;
                appendProjectedEditorLine(
                    context,
                    center +
                        glm::vec3(
                            std::cos(angle0) * radius,
                            0.12f,
                            std::sin(angle0) * radius),
                    center +
                        glm::vec3(
                            std::cos(angle1) * radius,
                            0.12f,
                            std::sin(angle1) * radius),
                    selected->suppressed
                        ? 1.0f
                        : 0.98f,
                    selected->suppressed
                        ? 0.20f
                        : 0.94f,
                    0.16f,
                    1.0f,
                    3.0f,
                    lines);
            }
        }
        if (!lines.empty()) {
            context.renderer->drawDebugLines(
                lines.data(),
                lines.size(),
                context.surfaceWidth,
                context.surfaceHeight);
        }
    }

    bool activateScene(
        std::string sceneId,
        std::string displayName,
        std::string environmentAssetId,
        std::string environmentKind,
        const std::filesystem::path& environmentPath,
        const std::filesystem::path& authoredScenePath,
        std::string runtimePath,
        std::string sceneStatus,
        std::string* outError) {
        if (sceneId.empty() ||
            environmentAssetId.empty() ||
            environmentKind.empty()) {
            if (outError) {
                *outError =
                    "A game scene requires scene and environment identities.";
            }
            return false;
        }
        if (environmentKind != "cooked") {
            if (environmentKind != "runtime_generated" &&
                environmentKind != "placeholder") {
                if (outError) {
                    *outError =
                        "Unsupported environment backdrop kind: " +
                        environmentKind;
                }
                return false;
            }
            sceneStore_ = {};
            environment_ = {};
            batches_.clear();
            sceneViewReady_ = false;
            layoutProjectionReady_ = false;
            editSession_.clearSceneState();
            activeSceneId_ = std::move(sceneId);
            activeEnvironmentAssetId_ =
                std::move(environmentAssetId);
            activeEnvironmentPath_.clear();
            activeBoardLayoutPath_.clear();
            activeAuthoredScenePath_.clear();
            environmentPrefabCatalog_.clear();
            simulationSeconds_ = 0.0f;
            status_ =
                "Game scene active: " +
                (displayName.empty()
                     ? activeSceneId_
                     : displayName) +
                ". Its " + environmentKind +
                " backdrop is available in Game view; a cooked Scene-view adapter is not available yet.";
            if (!runtimePath.empty()) {
                status_ += " Runtime: " + runtimePath + ".";
            }
            if (!sceneStatus.empty()) {
                status_ += " Status: " + sceneStatus + ".";
            }
            if (outError) {
                outError->clear();
            }
            return true;
        }
        if (environmentPath.empty()) {
            if (outError) {
                *outError =
                    "A cooked environment backdrop requires a resolved path.";
            }
            return false;
        }
        if (sceneViewReady_ &&
            activeEnvironmentAssetId_ ==
                environmentAssetId &&
            activeEnvironmentPath_ ==
                environmentPath &&
            activeAuthoredScenePath_ ==
                authoredScenePath) {
            activeSceneId_ = std::move(sceneId);
            refreshEnvironmentPrefabAssets();
            simulationSeconds_ = 0.0f;
            batches_.clear();
            status_ =
                "Game scene active: " +
                (displayName.empty()
                     ? activeSceneId_
                     : displayName) +
                ". Reusing cooked environment backdrop " +
                activeEnvironmentAssetId_ + ".";
            if (!runtimePath.empty()) {
                status_ += " Runtime: " + runtimePath + ".";
            }
            if (outError) {
                outError->clear();
            }
            return true;
        }
        using Clock = std::chrono::steady_clock;
        const auto activationStart = Clock::now();
        auto phaseStart = activationStart;
        const auto logPhase = [&](const char* phase) {
            const auto now = Clock::now();
            std::cerr
                << "[PokemonAutochessEditor][SceneLoad] "
                << phase << '='
                << std::chrono::duration<double, std::milli>(
                       now - phaseStart)
                       .count()
                << "ms\n";
            phaseStart = now;
        };
        std::error_code relativeError;
        const std::filesystem::path virtualPath =
            std::filesystem::relative(
                environmentPath,
                projectRoot_,
                relativeError);
        if (relativeError ||
            virtualPath.empty() ||
            virtualPath.is_absolute() ||
            *virtualPath.begin() == "..") {
            if (outError) {
                *outError =
                    "The cooked environment backdrop must resolve inside the Pokemon Autochess project.";
            }
            return false;
        }

        game::assets::DevAssetStore projectStore(
            projectRoot_.string());
        const auto* routeVariant =
            route1_scene_variants::find(sceneId);
        const std::string_view boardLayoutVirtualPath =
            routeVariant
            ? routeVariant->boardLayoutManifestPath
            : route1_scene_variants::
                  kRoute1.boardLayoutManifestPath;
        const std::filesystem::path boardLayoutPath =
            projectRoot_ /
            std::filesystem::path(boardLayoutVirtualPath);
        engine::assets::phlosion::SceneArchiveStore
            nextSceneStore;
        game::runtime::route1_environment::
            RuntimeEnvironment nextEnvironment;
        std::string error;
        if (!nextSceneStore.load(
                projectStore,
                virtualPath.generic_string(),
                &error)) {
            if (outError) {
                *outError =
                    "Could not mount cooked scene '" +
                    environmentAssetId + "': " + error;
            }
            return false;
        }
        logPhase("archive");
        if (!nextEnvironment.load(
                nextSceneStore,
                game::runtime::route1_environment::
                    cookedCanonicalRoot(nextSceneStore),
                game::runtime::route1_environment::
                    cookedCompositionManifestPath(nextSceneStore),
                game::runtime::route1_environment::
                    cookedBoardLayoutManifestPath(nextSceneStore),
                &error)) {
            if (outError) {
                *outError =
                    "Cooked environment '" +
                    environmentAssetId +
                    "' was rejected by the environment adapter: " +
                    error;
            }
            return false;
        }
        logPhase("environment");
        game::runtime::route1_environment::
            BoardLayoutTransform projectLayout;
        if (!game::runtime::route1_environment::
                loadBoardLayoutTransform(
                    projectStore,
                    std::string(boardLayoutVirtualPath),
                    projectLayout,
                    &error) ||
            !nextEnvironment.previewBoardLayout(
                projectLayout,
                &error)) {
            if (outError) {
                *outError =
                    "The project-owned Route 1 layout manifest was "
                    "rejected: " +
                    error;
            }
            return false;
        }
        logPhase("board_registration");
        if (authoredScenePath.empty()) {
            if (outError) {
                *outError =
                    "A cooked editable scene requires a project-owned authored_scene_path.";
            }
            return false;
        }
        std::filesystem::path authoredVirtualPath =
            std::filesystem::relative(
                authoredScenePath,
                projectRoot_,
                relativeError);
        if (relativeError ||
            authoredVirtualPath.empty() ||
            authoredVirtualPath.is_absolute() ||
            *authoredVirtualPath.begin() == "..") {
            if (outError) {
                *outError =
                    "The authored scene document must resolve inside the Pokemon Autochess project.";
            }
            return false;
        }
        engine::assets::phlosion::AuthoredSceneDocument
            authoredScene;
        if (!engine::assets::phlosion::
                loadAuthoredSceneDocument(
                    projectStore,
                    authoredVirtualPath.generic_string(),
                    authoredScene,
                    &error) ||
            !nextEnvironment.applyAuthoredScene(
                authoredScene,
                &error)) {
            if (outError) {
                *outError =
                    "The project-owned authored scene document was rejected: " +
                    error;
            }
            return false;
        }
        if (terrainPatchV2PreviewEnabled_ &&
            !nextEnvironment.setTerrainPatchV2PreviewEnabled(
                true,
                &error)) {
            if (outError) {
                *outError =
                    "The non-destructive Terrain Patch V2 preview was rejected: " +
                    error;
            }
            return false;
        }
        if (terrainPatchV2PreviewEnabled_) {
            const auto& patchStats = nextEnvironment.stats();
            std::cerr
                << "[PokemonAutochessEditor][TerrainPatchV2] regions="
                << patchStats.terrainPatchV2RegionCount
                << " core_cells="
                << patchStats.terrainPatchV2CoreCellCount
                << " transition_cells="
                << patchStats.terrainPatchV2TransitionCellCount
                << " boundary_loops="
                << patchStats.terrainPatchV2BoundaryLoopCount
                << " invalid_boundaries="
                << patchStats.terrainPatchV2InvalidBoundaryCount
                << '\n';
        }
        logPhase("authored_scene");

        sceneStore_ = std::move(nextSceneStore);
        environment_ = std::move(nextEnvironment);
        sceneViewReady_ = true;
        layoutProjectionReady_ = false;
        editSession_.clearSceneState();
        activeSceneId_ = std::move(sceneId);
        activeEnvironmentAssetId_ =
            std::move(environmentAssetId);
        activeEnvironmentPath_ = environmentPath;
        activeBoardLayoutPath_ = boardLayoutPath;
        activeAuthoredScenePath_ = authoredScenePath;
        simulationSeconds_ = 0.0f;
        batches_.clear();
        refreshEnvironmentPrefabAssets();
        logPhase("prefab_catalog");
        std::cerr
            << "[PokemonAutochessEditor][SceneLoad] total="
            << std::chrono::duration<double, std::milli>(
                   Clock::now() - activationStart)
                   .count()
            << "ms\n";
        status_ =
            "Game scene active: " +
            (displayName.empty()
                 ? activeSceneId_
                 : displayName) +
            ". Cooked environment backdrop mounted: " +
            activeEnvironmentAssetId_ + ".";
        if (!runtimePath.empty()) {
            status_ += " Runtime: " + runtimePath + ".";
        }
        if (outError) {
            outError->clear();
        }
        return true;
    }

    void rememberAndSetEnvironment(
        const std::string& name,
        std::optional<std::string> value) {
        const auto alreadySaved = std::find_if(
            savedEnvironment_.begin(),
            savedEnvironment_.end(),
            [&](const SavedEnvironment& saved) {
                return saved.name == name;
            });
        if (alreadySaved == savedEnvironment_.end()) {
            savedEnvironment_.push_back(
                SavedEnvironment{
                    .name = name,
                    .previous =
                        engine::env::get(name.c_str())});
        }
        setProcessEnvironment(name, value);
    }

    void restoreEnvironment() {
        for (auto it = savedEnvironment_.rbegin();
             it != savedEnvironment_.rend();
             ++it) {
            setProcessEnvironment(it->name, it->previous);
        }
        savedEnvironment_.clear();
    }

    bool adoptProjectWorkingDirectory(
        std::string* outError) {
        if (!previousWorkingDirectory_.empty()) {
            return true;
        }
        std::error_code error;
        previousWorkingDirectory_ =
            std::filesystem::current_path(error);
        if (error) {
            if (outError) {
                *outError =
                    "Could not read the editor working directory: " +
                    error.message();
            }
            previousWorkingDirectory_.clear();
            return false;
        }
        std::filesystem::current_path(projectRoot_, error);
        if (error) {
            if (outError) {
                *outError =
                    "Could not enter the Pokemon Autochess project directory: " +
                    error.message();
            }
            previousWorkingDirectory_.clear();
            return false;
        }
        return true;
    }

    void restoreWorkingDirectory() {
        if (previousWorkingDirectory_.empty()) {
            return;
        }
        std::error_code ignored;
        std::filesystem::current_path(
            previousWorkingDirectory_,
            ignored);
        previousWorkingDirectory_.clear();
    }

    engine::assets::phlosion::SceneArchiveStore sceneStore_;
    game::runtime::route1_environment::RuntimeEnvironment
        environment_;
    std::vector<
        game::runtime::shared_world_batches::WorldIndexedBatch>
        batches_;
    std::filesystem::path projectRoot_;
    editor_persistence::Store persistence_;
    std::filesystem::path previousWorkingDirectory_;
    ResourceManager resources_;
    ShaderCache shaders_;
    EventBus events_;
    EngineServices services_;
    std::unique_ptr<GameRuntime> gameRuntime_;
    game::editor::PokemonPrefabPreview prefabPreview_;
    game::editor::PokemonVfxPrefabPreview vfxPreview_;
    game::editor::Route1EnvironmentPrefabPreview
        environmentPrefabPreview_;
    asset_catalog::EnvironmentPrefabCatalog
        environmentPrefabCatalog_;
    std::vector<PreviewUnitLayoutObject>
        previewUnitLayoutObjects_;
    std::unordered_map<std::string, PreviewUnitTransform>
        previewUnitSourceTransforms_;
    layout_transactions::EditSession editSession_;
    ActiveAssetPreview activeAssetPreview_ =
        ActiveAssetPreview::Model;
    IRenderBackend* renderer_ = nullptr;
    Camera3D* gameCamera_ = nullptr;
    std::vector<SavedEnvironment> savedEnvironment_;
    std::filesystem::path activeEnvironmentPath_;
    std::filesystem::path activeBoardLayoutPath_;
    std::filesystem::path activeAuthoredScenePath_;
    std::string activeSceneId_;
    std::string activeEnvironmentAssetId_;
    std::string activePreviewId_ = "main-menu";
    editor_hierarchy::Selection layoutSelection_;
    glm::mat4 layoutViewProjection_{1.0f};
    glm::mat4 gameLayoutViewProjection_{1.0f};
    std::string runtimeTitle_;
    std::string commandStatus_;
    std::string status_ =
        "Mounted strict cooked Route 1 scene through PHSC; "
        "no source-cache fallback is active.";
    float simulationSeconds_ = 0.0f;
    float latestBootProgress_ = 0.0f;
    float bootReplaySeconds_ = 0.0f;
    float boardCellSize_ = 1.2f;
    int previewWidth_ = 1280;
    int previewHeight_ = 720;
    int layoutProjectionWidth_ = 0;
    int layoutProjectionHeight_ = 0;
    int gameLayoutProjectionWidth_ = 0;
    int gameLayoutProjectionHeight_ = 0;
    bool previewFullscreen_ = false;
    bool runtimeRequestedQuit_ = false;
    bool bootReplayActive_ = false;
    bool sceneViewReady_ = false;
    bool layoutProjectionReady_ = false;
    bool gameLayoutProjectionReady_ = false;
    bool layoutOverlayVisible_ = true;
    bool terrainSeamDiagnosticsVisible_ = false;
    bool terrainPatchV2PreviewEnabled_ = true;
    bool terrainBatchDiagnosticsWritten_ = false;
    bool ownsTtf_ = false;
    static constexpr float kBootReplayDurationSeconds =
        2.5f;
};

} // namespace

PHLOSION_EDITOR_PROJECT_EXPORT std::uint32_t
phlosionEditorProjectPluginAbiVersion() {
    return engine::editor::kEditorProjectPluginAbiVersion;
}

PHLOSION_EDITOR_PROJECT_EXPORT
engine::editor::IEditorProjectRuntime*
phlosionCreateEditorProjectRuntime() {
    return new PokemonAutochessEditorProject();
}

PHLOSION_EDITOR_PROJECT_EXPORT void
phlosionDestroyEditorProjectRuntime(
    engine::editor::IEditorProjectRuntime* runtime) {
    delete runtime;
}

PHLOSION_EDITOR_PROJECT_EXPORT const
engine::editor::EditorProjectPluginContract*
phlosionEditorProjectPluginContract() {
    static constexpr engine::editor::EditorProjectPluginContract contract{
        engine::editor::kEditorProjectPluginAbiVersion,
        sizeof(engine::editor::EditorProjectPluginContract),
        engine::editor::kEditorProjectPluginLayoutFingerprint,
        PHLOSION_EDITOR_PLUGIN_BUILD_CONFIGURATION,
        engine::editor::kEditorProjectPluginCompilerAbi,
        &phlosionCreateEditorProjectRuntime,
        &phlosionDestroyEditorProjectRuntime,
    };
    return &contract;
}
