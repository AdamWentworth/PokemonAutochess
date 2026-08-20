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
#include "game/editor/PokemonAutochessEditorHierarchy.h"
#include "game/editor/PokemonAutochessEditorPreviewCatalog.h"
#include "game/editor/PokemonPrefabPreview.h"
#include "game/editor/PokemonVfxPrefabPreview.h"
#include "game/editor/Route1EnvironmentPrefabPreview.h"
#include "game/runtime/GameRuntime.h"
#include "game/runtime/RuntimeBootLoading.h"
#include "game/runtime/video/VideoPreferences.h"
#include "game/runtime/shared/scene/LgpeRoute1RuntimeEnvironment.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"

#include <SDL2/SDL_ttf.h>
#include <glad/glad.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <nlohmann/json.hpp>

#ifndef PHLOSION_EDITOR_PLUGIN_BUILD_CONFIGURATION
#define PHLOSION_EDITOR_PLUGIN_BUILD_CONFIGURATION "Unknown"
#endif

namespace {

namespace asset_catalog = game::editor::asset_catalog;
namespace editor_hierarchy = game::editor::hierarchy;
namespace preview_catalog = game::editor::preview_catalog;

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
constexpr std::array<float, 3> kDefaultBoardSourceAnchorCm{
    2100.0f, 0.0f, -1500.0f};
constexpr float kDefaultBoardCellSizeWorld = 1.0f;
constexpr std::array<std::int32_t, 2>
    kDefaultBoardTerrainGridOrigin{17, -19};

struct BoardClearanceRequest {
    float paddingCells = 0.35f;
    bool clearTerrain = true;
    bool clearVegetation = true;
    bool clearObjects = true;
    bool retainRamps = true;
    bool addGroundInfill = true;
};

struct BoardClearanceResult {
    std::uint32_t suppressedTerrainCount = 0u;
    std::uint32_t suppressedVegetationCount = 0u;
    std::uint32_t suppressedObjectCount = 0u;
    std::uint32_t retainedRampCount = 0u;
    std::uint32_t skippedUnsafeAggregateCount = 0u;
    bool groundInfillCreated = false;
};

constexpr std::array<engine::editor::EditorProjectCommandField, 6>
    kBoardClearanceCommandFields{{
        {.id = "padding_cells",
         .displayName = "Clearance padding (cells)",
         .description =
             "Additional Route 1 terrain-cell margin around the board and benches.",
         .kind = engine::editor::EditorProjectCommandFieldKind::Float,
         .defaultFloat = 0.35f,
         .minimumFloat = 0.0f,
         .maximumFloat = 3.0f,
         .stepFloat = 0.05f},
        {.id = "clear_terrain",
         .displayName = "Clear ledges and raised terrain",
         .defaultBoolean = true},
        {.id = "clear_vegetation",
         .displayName = "Clear vegetation",
         .defaultBoolean = true},
        {.id = "clear_objects",
         .displayName = "Clear props and other obstructions",
         .defaultBoolean = true},
        {.id = "retain_ramps",
         .displayName = "Retain ramps as entrances",
         .defaultBoolean = true},
        {.id = "add_ground_infill",
         .displayName = "Create light-lawn ground infill",
         .defaultBoolean = true},
    }};

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

void setBoardTerrainGridOriginFromCenter(
    game::runtime::lgpe_route1_runtime::
        BoardLayoutTransform& layout,
    const std::array<float, 3>& requestedCenterCm) {
    layout.terrainGridOrigin = {
        static_cast<std::int32_t>(std::llround(
            requestedCenterCm[0] / kTerrainTileSizeCm -
            static_cast<float>(layout.boardCells[0]) * 0.5f)),
        static_cast<std::int32_t>(std::llround(
            requestedCenterCm[2] / kTerrainTileSizeCm -
            static_cast<float>(layout.boardCells[1]) * 0.5f))};
    layout.terrainElevationLevel =
        static_cast<std::int32_t>(std::llround(
            requestedCenterCm[1] /
            kTerrainElevationStepCm));
    game::runtime::lgpe_route1_runtime::
        bindBoardLayoutToTerrainGrid(layout);
}

std::string terrainTileStableId(
    std::int32_t gridX,
    std::int32_t gridZ) {
    return game::runtime::lgpe_route1_runtime::
        route1TerrainTileStableId(gridX, gridZ);
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

bool projectEditorPoint(
    const float* viewProjectionMatrix4x4,
    const glm::vec3& world,
    int surfaceWidth,
    int surfaceHeight,
    float& outX,
    float& outY) {
    if (!viewProjectionMatrix4x4 ||
        surfaceWidth <= 0 ||
        surfaceHeight <= 0) {
        return false;
    }
    const glm::vec4 clip =
        glm::make_mat4(viewProjectionMatrix4x4) *
        glm::vec4(world, 1.0f);
    if (!std::isfinite(clip.x) ||
        !std::isfinite(clip.y) ||
        !std::isfinite(clip.z) ||
        !std::isfinite(clip.w) ||
        std::abs(clip.w) <= 1.0e-6f) {
        return false;
    }
    const glm::vec3 ndc =
        glm::vec3(clip) / clip.w;
    if (ndc.z < -1.0f || ndc.z > 1.0f) {
        return false;
    }
    outX =
        (ndc.x * 0.5f + 0.5f) *
        static_cast<float>(surfaceWidth);
    outY =
        (0.5f - ndc.y * 0.5f) *
        static_cast<float>(surfaceHeight);
    return std::isfinite(outX) &&
        std::isfinite(outY);
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
    if (!projectEditorPoint(
            context.viewProjectionMatrix4x4,
            start,
            context.surfaceWidth,
            context.surfaceHeight,
            line.x1,
            line.y1) ||
        !projectEditorPoint(
            context.viewProjectionMatrix4x4,
            end,
            context.surfaceWidth,
            context.surfaceHeight,
            line.x2,
            line.y2)) {
        return;
    }
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

        if (!loadPreviewUnitOverrides(outError)) {
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
        if (!selectGamePreview("main-menu", outError)) {
            gameRuntime_->shutdown();
            gameRuntime_.reset();
            return false;
        }
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
        previewUnitEditBaseline_.reset();
        previewUnitEditStableId_.clear();
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
            auto view = editor_hierarchy::gameplayBoardView(
                layout,
                editor_hierarchy::GameplayBoardViewConfig{
                    .stableId = kGameplayBoardStableId.data(),
                    .defaultSourceAnchorCm =
                        kDefaultBoardSourceAnchorCm,
                    .defaultTerrainGridOrigin =
                        kDefaultBoardTerrainGridOrigin,
                    .defaultBoardCellSizeWorld =
                        kDefaultBoardCellSizeWorld,
                    .terrainTileSizeCm = kTerrainTileSizeCm,
                    .terrainElevationStepCm =
                        kTerrainElevationStepCm});
            if (!layoutProjectionReady_) {
                return view;
            }
            const glm::mat4 worldFromSource =
                glm::make_mat4(
                    game::runtime::lgpe_route1_runtime::
                        worldFromSourceMatrix(layout).data());
            const auto worldPoint =
                [&](const std::array<float, 3>& source) {
                    return glm::vec3(
                        worldFromSource * glm::vec4(
                            source[0], source[1], source[2], 1.0f));
                };
            float centerX = 0.0f;
            float centerY = 0.0f;
            if (!projectEditorPoint(
                    glm::value_ptr(layoutViewProjection_),
                    worldPoint(layout.sourceAnchorCm),
                    layoutProjectionWidth_,
                    layoutProjectionHeight_,
                    centerX,
                    centerY)) {
                return view;
            }
            view.viewportPosition = {centerX, centerY};
            view.viewportVisible = true;
            view.viewportAxisDirections = {
                1.0f, 0.0f,
                0.0f, -1.0f,
                0.70710678f, 0.70710678f};
            view.viewportSourceUnitsPerPixel = {
                1.0f, 1.0f, 1.0f};
            constexpr float kSourceAxisLength = 100.0f;
            for (std::size_t axis = 0u; axis < 3u; ++axis) {
                auto endpoint = layout.sourceAnchorCm;
                endpoint[axis] += kSourceAxisLength;
                float endpointX = 0.0f;
                float endpointY = 0.0f;
                if (!projectEditorPoint(
                        glm::value_ptr(layoutViewProjection_),
                        worldPoint(endpoint),
                        layoutProjectionWidth_,
                        layoutProjectionHeight_,
                        endpointX,
                        endpointY)) {
                    continue;
                }
                const float dx = endpointX - centerX;
                const float dy = endpointY - centerY;
                const float length = std::sqrt(dx * dx + dy * dy);
                if (length <= 0.001f) {
                    continue;
                }
                view.viewportAxisDirections[axis * 2u] = dx / length;
                view.viewportAxisDirections[axis * 2u + 1u] = dy / length;
                view.viewportSourceUnitsPerPixel[axis] =
                    kSourceAxisLength / length;
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
            game::runtime::lgpe_route1_runtime::
                worldFromSourceMatrix(
                    environment_.layout());
        const glm::mat4 worldFromSource =
            glm::make_mat4(
                worldFromSourceArray.data());
        const auto worldPoint =
            [&](const std::array<float, 3>& source) {
                return glm::vec3(
                    worldFromSource *
                    glm::vec4(
                        source[0],
                        source[1],
                        source[2],
                        1.0f));
            };
        float centerX = 0.0f;
        float centerY = 0.0f;
        if (!projectEditorPoint(
                glm::value_ptr(layoutViewProjection_),
                worldPoint(object.translationCm),
                layoutProjectionWidth_,
                layoutProjectionHeight_,
                centerX,
                centerY)) {
            return view;
        }
        view.viewportPosition = {
            centerX,
            centerY};
        view.viewportVisible = true;
        constexpr float kSourceAxisLength = 100.0f;
        constexpr std::array<float, 6>
            kFallbackDirections{{
                1.0f, 0.0f,
                0.0f, -1.0f,
                0.70710678f, 0.70710678f,
            }};
        view.viewportAxisDirections =
            kFallbackDirections;
        view.viewportSourceUnitsPerPixel = {
            1.0f, 1.0f, 1.0f};
        for (std::size_t axis = 0u;
             axis < 3u;
             ++axis) {
            auto endpointSource =
                object.translationCm;
            endpointSource[axis] +=
                kSourceAxisLength;
            float endpointX = 0.0f;
            float endpointY = 0.0f;
            if (!projectEditorPoint(
                    glm::value_ptr(
                        layoutViewProjection_),
                    worldPoint(endpointSource),
                    layoutProjectionWidth_,
                    layoutProjectionHeight_,
                    endpointX,
                    endpointY)) {
                continue;
            }
            const float directionX =
                endpointX - centerX;
            const float directionY =
                endpointY - centerY;
            const float pixelLength =
                std::sqrt(
                    directionX * directionX +
                    directionY * directionY);
            if (pixelLength <= 0.001f) {
                continue;
            }
            view.viewportAxisDirections[
                axis * 2u] =
                directionX / pixelLength;
            view.viewportAxisDirections[
                axis * 2u + 1u] =
                directionY / pixelLength;
            view.viewportSourceUnitsPerPixel[axis] =
                kSourceAxisLength /
                pixelLength;
        }
        return view;
    }

    bool supportsTerrainTileEditing() const noexcept override {
        return sceneViewReady_ &&
            activeSceneId_ == "routes/route1";
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
                tile.sourceReference.has_value()};
        if (!layoutProjectionReady_ ||
            (!tile.sourceOccupied && !tile.authored)) {
            return view;
        }
        const glm::mat4 worldFromSource = glm::make_mat4(
            game::runtime::lgpe_route1_runtime::
                worldFromSourceMatrix(environment_.layout()).data());
        constexpr std::array<std::array<float, 2>, 4> corners{{
            {0.0f, 0.0f},
            {1.0f, 0.0f},
            {1.0f, 1.0f},
            {0.0f, 1.0f},
        }};
        for (std::size_t corner = 0u;
             corner < corners.size();
             ++corner) {
            const float localX = corners[corner][0];
            const float localZ = corners[corner][1];
            std::int32_t cornerLevel = tile.elevationLevel;
            if ((tile.shape == "ramp_east" && localX > 0.5f) ||
                (tile.shape == "ramp_west" && localX < 0.5f) ||
                (tile.shape == "ramp_north" && localZ > 0.5f) ||
                (tile.shape == "ramp_south" && localZ < 0.5f)) {
                ++cornerLevel;
            }
            const glm::vec3 sourcePoint{
                (static_cast<float>(tile.gridX) + localX) *
                    kTerrainTileSizeCm,
                static_cast<float>(cornerLevel) *
                        kTerrainElevationStepCm +
                    1.0f,
                (static_cast<float>(tile.gridZ) + localZ) *
                    kTerrainTileSizeCm};
            const glm::vec3 worldPoint = glm::vec3(
                worldFromSource * glm::vec4(sourcePoint, 1.0f));
            if (!projectEditorPoint(
                    glm::value_ptr(layoutViewProjection_),
                    worldPoint,
                    layoutProjectionWidth_,
                    layoutProjectionHeight_,
                    view.viewportCorners[corner * 2u],
                    view.viewportCorners[corner * 2u + 1u])) {
                return view;
            }
            const glm::vec3 flatSourcePoint{
                sourcePoint.x,
                static_cast<float>(tile.elevationLevel) *
                        kTerrainElevationStepCm +
                    1.0f,
                sourcePoint.z};
            const glm::vec3 flatWorldPoint = glm::vec3(
                worldFromSource * glm::vec4(flatSourcePoint, 1.0f));
            float nextLevelX = 0.0f;
            float nextLevelY = 0.0f;
            if (!projectEditorPoint(
                    glm::value_ptr(layoutViewProjection_),
                    flatWorldPoint,
                    layoutProjectionWidth_,
                    layoutProjectionHeight_,
                    view.viewportFlatCorners[corner * 2u],
                    view.viewportFlatCorners[corner * 2u + 1u]) ||
                !projectEditorPoint(
                    glm::value_ptr(layoutViewProjection_),
                    flatWorldPoint + glm::vec3(
                        worldFromSource *
                        glm::vec4(
                            0.0f,
                            kTerrainElevationStepCm,
                            0.0f,
                            0.0f)),
                    layoutProjectionWidth_,
                    layoutProjectionHeight_,
                    nextLevelX,
                    nextLevelY)) {
                return view;
            }
            view.viewportLevelStep[corner * 2u] =
                nextLevelX -
                view.viewportFlatCorners[corner * 2u];
            view.viewportLevelStep[corner * 2u + 1u] =
                nextLevelY -
                view.viewportFlatCorners[corner * 2u + 1u];
        }
        view.viewportVisible = true;
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
        if (!supportsTerrainTileEditing() ||
            !request.coordinates ||
            request.coordinateCount == 0u ||
            !request.operation) {
            if (outError) {
                *outError =
                    "Terrain editing requires Route 1, an operation, and at least one selected tile.";
            }
            return false;
        }
        const std::string_view operation(request.operation);
        const std::string_view requestedSurface =
            request.surface ? request.surface : "";
        const std::string_view requestedShape =
            request.shape ? request.shape : "";
        const std::string_view requestedVisualVariant =
            request.visualVariant ? request.visualVariant : "";
        const bool validOperation =
            operation == "create" || operation == "raise" ||
            operation == "lower" || operation == "terrace_raise" ||
            operation == "terrace_lower" ||
            operation == "flatten_tidy" ||
            operation == "tidy_surface" ||
            operation == "platform_set" ||
            operation == "swap_prefab" ||
            operation == "paste_tiles_relative" ||
            operation == "paste_tiles_exact" ||
            operation == "paint_surface" ||
            operation == "set_shape" || operation == "restore_source";
        const auto validSurfaceId =
            [](std::string_view surface) {
                return surface == "light_lawn" ||
                    surface == "dark_lawn" ||
                    surface == "dirt_path" ||
                    surface == "empty";
            };
        const auto validShapeId =
            [](std::string_view shape) {
                return shape == "flat" ||
                    shape == "ramp_north" ||
                    shape == "ramp_east" ||
                    shape == "ramp_south" ||
                    shape == "ramp_west";
            };
        const bool validSurface = validSurfaceId(requestedSurface);
        const bool validShape = validShapeId(requestedShape);
        const bool validPlatformProfile = validShape ||
            requestedShape == "preserve" ||
            requestedShape == "source";
        const bool validRelativeElevationDelta =
            request.relativeElevationDelta >= -128 &&
            request.relativeElevationDelta <= 128;
        const auto validVariantForSurface =
            [](std::string_view surface,
               std::string_view variant) {
                if (variant == "auto") {
                    return true;
                }
                if (surface != "dirt_path" ||
                    !variant.starts_with("path_")) {
                    return false;
                }
                const std::string_view digits = variant.substr(5u);
                std::uint32_t mask = 0u;
                const auto result = std::from_chars(
                    digits.data(),
                    digits.data() + digits.size(),
                    mask);
                return result.ec == std::errc{} &&
                    result.ptr == digits.data() + digits.size() &&
                    mask <= 15u;
            };
        if (!validOperation ||
            !validRelativeElevationDelta ||
            ((operation == "flatten_tidy" ||
              operation == "platform_set") &&
             (request.targetElevationLevel < -128 ||
              request.targetElevationLevel > 128)) ||
            ((operation == "paint_surface" ||
              operation == "swap_prefab" ||
              operation == "platform_set") && !validSurface) ||
            (operation == "platform_set" &&
             requestedSurface == "empty") ||
            (operation == "platform_set" &&
             !validPlatformProfile) ||
            ((operation == "set_shape" ||
              operation == "swap_prefab") && !validShape) ||
            (operation == "swap_prefab" &&
             !validVariantForSurface(
                 requestedSurface,
                 requestedVisualVariant)) ||
            (operation == "swap_prefab" &&
             requestedVisualVariant != "auto" &&
             requestedShape != "flat") ||
            (request.relativeElevationDelta != 0 &&
             (operation != "swap_prefab" ||
              requestedShape != "flat" ||
              requestedSurface == "empty")) ||
            (requestedSurface == "empty" &&
             (requestedShape != "flat" ||
              requestedVisualVariant != "auto"))) {
            if (outError) {
                *outError = "The requested terrain-tile operation is invalid.";
            }
            return false;
        }
        const bool pasteTilesRelative =
            operation == "paste_tiles_relative";
        const bool pasteTilesExact =
            operation == "paste_tiles_exact";
        const bool pasteTiles = pasteTilesRelative || pasteTilesExact;
        if (pasteTiles) {
            if (request.coordinateCount != 1u ||
                !request.stampTiles ||
                request.stampTileCount == 0u ||
                request.stampTileCount > 4096u) {
                if (outError) {
                    *outError =
                        "Tile paste requires one destination anchor and a non-empty bounded clipboard.";
                }
                return false;
            }
            std::set<std::pair<std::int32_t, std::int32_t>>
                stampOffsets;
            for (std::size_t index = 0u;
                 index < request.stampTileCount;
                 ++index) {
                const auto& stamp = request.stampTiles[index];
                const std::string_view surface =
                    stamp.surface ? stamp.surface : "";
                const std::string_view shape =
                    stamp.shape ? stamp.shape : "";
                const std::string_view variant =
                    stamp.visualVariant
                    ? stamp.visualVariant
                    : "";
                const bool sourceReferenceValid =
                    !stamp.hasSourceReference || std::any_of(
                        environment_.terrainTiles().begin(),
                        environment_.terrainTiles().end(),
                        [&](const auto& tile) {
                            return tile.sourceOccupied &&
                                tile.gridX ==
                                    stamp.sourceReference.gridX &&
                                tile.gridZ ==
                                    stamp.sourceReference.gridZ;
                        });
                if (stamp.offsetGridX < -512 ||
                    stamp.offsetGridZ < -512 ||
                    stamp.offsetGridX > 512 ||
                    stamp.offsetGridZ > 512 ||
                    stamp.relativeElevationLevel < -256 ||
                    stamp.relativeElevationLevel > 256 ||
                    stamp.absoluteElevationLevel < -128 ||
                    stamp.absoluteElevationLevel > 128 ||
                    !stampOffsets.emplace(
                        stamp.offsetGridX,
                        stamp.offsetGridZ).second ||
                    !validSurfaceId(surface) ||
                    !validShapeId(shape) ||
                    !validVariantForSurface(surface, variant) ||
                    !sourceReferenceValid ||
                    (variant != "auto" && shape != "flat") ||
                    (surface == "empty" &&
                     (shape != "flat" || variant != "auto"))) {
                    if (outError) {
                        *outError =
                            "The copied terrain footprint contains an invalid tile state.";
                    }
                    return false;
                }
            }
        }

        const auto previous = environment_.layout();
        auto next = previous;
        std::set<std::pair<std::int32_t, std::int32_t>> visited;
        if (pasteTiles) {
            const auto anchor = request.coordinates[0];
            const auto anchorTile = std::find_if(
                environment_.terrainTiles().begin(),
                environment_.terrainTiles().end(),
                [&](const auto& tile) {
                    return tile.gridX == anchor.gridX &&
                        tile.gridZ == anchor.gridZ;
                });
            if (anchorTile == environment_.terrainTiles().end()) {
                if (outError) {
                    *outError =
                        "The paste anchor is outside the Route 1 authoring bounds.";
                }
                return false;
            }
            const std::int32_t destinationBaseLevel =
                anchorTile->elevationLevel;
            for (std::size_t index = 0u;
                 index < request.stampTileCount;
                 ++index) {
                const auto& stamp = request.stampTiles[index];
                const engine::editor::EditorProjectTerrainTileCoordinate
                    coordinate{
                        .gridX = anchor.gridX + stamp.offsetGridX,
                        .gridZ = anchor.gridZ + stamp.offsetGridZ};
                if (!visited.emplace(
                        coordinate.gridX,
                        coordinate.gridZ).second) {
                    continue;
                }
                const auto source = std::find_if(
                    environment_.terrainTiles().begin(),
                    environment_.terrainTiles().end(),
                    [&](const auto& tile) {
                        return tile.gridX == coordinate.gridX &&
                            tile.gridZ == coordinate.gridZ;
                    });
                if (source == environment_.terrainTiles().end()) {
                    if (outError) {
                        *outError =
                            "The copied terrain footprint extends outside the Route 1 authoring bounds.";
                    }
                    return false;
                }
                auto authored = std::find_if(
                    next.authoredTerrainTiles.begin(),
                    next.authoredTerrainTiles.end(),
                    [&](const auto& tile) {
                        return tile.gridX == coordinate.gridX &&
                            tile.gridZ == coordinate.gridZ;
                    });
                if (authored == next.authoredTerrainTiles.end()) {
                    const std::string stableId = terrainTileStableId(
                        coordinate.gridX,
                        coordinate.gridZ);
                    next.authoredTerrainTiles.push_back(
                        game::runtime::lgpe_route1_runtime::AuthoredTerrainTile{
                            .stableId = stableId,
                            .displayName =
                                "Terrain Tile (" +
                                std::to_string(coordinate.gridX) + ", " +
                                std::to_string(coordinate.gridZ) + ")",
                            .categoryPath =
                                "Environment/Terrain/Tiles",
                            .tileSetAssetId =
                                std::string(kTerrainTileSetAssetId),
                            .gridX = coordinate.gridX,
                            .gridZ = coordinate.gridZ,
                            .elevationLevel = source->elevationLevel,
                            .surface = source->surface,
                            .shape = source->shape,
                            .visualVariant = "auto",
                            .reason = "terrain_tile_paste"});
                    authored = std::prev(
                        next.authoredTerrainTiles.end());
                }
                authored->elevationLevel = pasteTilesExact
                    ? stamp.absoluteElevationLevel
                    : std::clamp(
                          destinationBaseLevel +
                              stamp.relativeElevationLevel,
                          -128,
                          128);
                authored->surface = stamp.surface;
                authored->shape = stamp.shape;
                authored->visualVariant = stamp.visualVariant;
                authored->sourceReference =
                    stamp.hasSourceReference
                    ? std::optional<std::array<std::int32_t, 2>>{
                          std::array<std::int32_t, 2>{
                              stamp.sourceReference.gridX,
                              stamp.sourceReference.gridZ}}
                    : std::nullopt;
                authored->reason = "terrain_tile_paste";
            }
        }
        for (std::size_t index = 0u;
             !pasteTiles &&
             index < request.coordinateCount;
             ++index) {
            const auto coordinate = request.coordinates[index];
            if (!visited.emplace(
                    coordinate.gridX,
                    coordinate.gridZ).second) {
                continue;
            }
            const auto source = std::find_if(
                environment_.terrainTiles().begin(),
                environment_.terrainTiles().end(),
                [&](const auto& tile) {
                    return tile.gridX == coordinate.gridX &&
                        tile.gridZ == coordinate.gridZ;
                });
            if (source == environment_.terrainTiles().end()) {
                if (outError) {
                    *outError = "The selected terrain tile is outside the Route 1 authoring bounds.";
                }
                return false;
            }
            auto authored = std::find_if(
                next.authoredTerrainTiles.begin(),
                next.authoredTerrainTiles.end(),
                [&](const auto& tile) {
                    return tile.gridX == coordinate.gridX &&
                        tile.gridZ == coordinate.gridZ;
                });
            if (operation == "restore_source") {
                if (authored != next.authoredTerrainTiles.end()) {
                    next.authoredTerrainTiles.erase(authored);
                }
                continue;
            }
            if (authored == next.authoredTerrainTiles.end()) {
                const std::string stableId = terrainTileStableId(
                    coordinate.gridX,
                    coordinate.gridZ);
                next.authoredTerrainTiles.push_back(
                    game::runtime::lgpe_route1_runtime::AuthoredTerrainTile{
                        .stableId = stableId,
                        .displayName =
                            "Terrain Tile (" +
                            std::to_string(coordinate.gridX) + ", " +
                            std::to_string(coordinate.gridZ) + ")",
                        .categoryPath = "Environment/Terrain/Tiles",
                        .tileSetAssetId = std::string(kTerrainTileSetAssetId),
                        .gridX = coordinate.gridX,
                        .gridZ = coordinate.gridZ,
                        .elevationLevel = source->elevationLevel,
                        .surface = source->surface,
                        .shape = source->shape,
                        .visualVariant = "auto",
                        .reason = "terrain_tile_authoring"});
                authored = std::prev(next.authoredTerrainTiles.end());
            }
            // Any ordinary authoring operation intentionally leaves exact
            // source-reference mode. Otherwise painting or leveling this
            // cell would appear to do nothing because the canonical donor
            // geometry would continue to win visually.
            authored->sourceReference.reset();
            if (operation == "raise") {
                authored->elevationLevel = std::min(
                    128, authored->elevationLevel + 1);
            } else if (operation == "lower") {
                authored->elevationLevel = std::max(
                    -128, authored->elevationLevel - 1);
            } else if (operation == "terrace_raise") {
                authored->elevationLevel = std::min(
                    128, authored->elevationLevel + 1);
                authored->shape = "flat";
                authored->visualVariant = "auto";
                authored->reason = "terrain_platform_authoring";
            } else if (operation == "terrace_lower") {
                authored->elevationLevel = std::max(
                    -128, authored->elevationLevel - 1);
                authored->shape = "flat";
                authored->visualVariant = "auto";
                authored->reason = "terrain_platform_authoring";
            } else if (operation == "flatten_tidy") {
                authored->elevationLevel =
                    request.targetElevationLevel;
                authored->shape = "flat";
                authored->visualVariant = "auto";
                authored->reason = "terrain_flatten_cleanup";
            } else if (operation == "platform_set") {
                authored->elevationLevel =
                    request.targetElevationLevel;
                authored->surface = requestedSurface;
                if (requestedShape == "source") {
                    authored->shape = source->sourceShape;
                } else if (requestedShape != "preserve") {
                    authored->shape = requestedShape;
                }
                authored->visualVariant = "auto";
                authored->reason = "terrain_platform_profiled";
            } else if (operation == "tidy_surface") {
                authored->visualVariant = "auto";
                authored->reason = "terrain_surface_authoring";
            } else if (operation == "paint_surface") {
                authored->surface = requestedSurface;
                authored->visualVariant = "auto";
                if (authored->surface == "empty") {
                    authored->shape = "flat";
                }
            } else if (operation == "set_shape") {
                if (authored->surface == "empty" &&
                    requestedShape != "flat") {
                    if (outError) {
                        *outError =
                            "An empty terrain cell cannot have a ramp shape.";
                    }
                    return false;
                }
                authored->shape = requestedShape;
                if (requestedShape != "flat") {
                    authored->visualVariant = "auto";
                }
            } else if (operation == "swap_prefab") {
                authored->surface = requestedSurface;
                authored->shape = requestedShape;
                authored->visualVariant = requestedVisualVariant;
                if (request.relativeElevationDelta != 0) {
                    authored->elevationLevel = std::clamp(
                        authored->elevationLevel +
                            request.relativeElevationDelta,
                        -128,
                        128);
                    authored->reason = "terrain_platform_authoring";
                }
            }
        }

        std::string error;
        if (!environment_.applyBoardLayout(next, &error) ||
            !saveLayoutManifest(&error)) {
            std::string ignored;
            environment_.applyBoardLayout(previous, &ignored);
            if (outError) {
                *outError =
                    "Could not apply and persist the terrain-tile edit: " +
                    error;
            }
            return false;
        }
        recordSceneEdit(previous);
        status_ = "Saved " + std::to_string(visited.size()) +
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
            auto next = previous;
            setBoardTerrainGridOriginFromCenter(
                next,
                edit.translation);
            std::string error;
            if (!environment_.applyBoardLayout(next, &error) ||
                !saveBoardRegistrationManifest(&error)) {
                std::string ignored;
                environment_.applyBoardLayout(previous, &ignored);
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
            layoutEditBaseline_.reset();
            layoutEditStableId_.clear();
            status_ =
                "Gameplay board placement and cells are bound to the Route 1 grid.";
            if (outError) {
                outError->clear();
            }
            return true;
        }
        const auto previous =
            environment_.layout();
        std::string error;
        if (!environment_.setLayoutObjectOverride(
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
        if (!saveLayoutManifest(&error)) {
            std::string ignored;
            environment_.applyBoardLayout(
                previous,
                &ignored);
            if (outError) {
                *outError =
                    "Could not persist the layout override; the "
                    "in-memory edit was rolled back: " +
                    error;
            }
            return false;
        }
        layoutSelection_.select(edit.stableId);
        recordSceneEdit(previous);
        layoutEditBaseline_.reset();
        layoutEditStableId_.clear();
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
            if (!layoutEditBaseline_ ||
                layoutEditStableId_ != edit.stableId) {
                layoutEditBaseline_ = environment_.layout();
                layoutEditStableId_ = edit.stableId;
            }
            auto next = environment_.layout();
            setBoardTerrainGridOriginFromCenter(
                next,
                edit.translation);
            const bool sameGridRegistration =
                next.terrainGridOrigin ==
                    environment_.layout().terrainGridOrigin &&
                next.terrainElevationLevel ==
                    environment_.layout().terrainElevationLevel;
            if (sameGridRegistration) {
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
        if (!layoutEditBaseline_ ||
            layoutEditStableId_ != edit.stableId) {
            layoutEditBaseline_ =
                environment_.layout();
            layoutEditStableId_ =
                edit.stableId;
        }
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
            if (layoutEditBaseline_ &&
                layoutEditStableId_ != stableId) {
                if (outError) {
                    *outError =
                        "The live board-layout target changed before commit.";
                }
                return false;
            }
            const auto historyBaseline = layoutEditBaseline_;
            const auto liveLayout = environment_.layout();
            if (historyBaseline &&
                liveLayout.terrainGridOrigin ==
                    historyBaseline->terrainGridOrigin &&
                liveLayout.terrainElevationLevel ==
                    historyBaseline->terrainElevationLevel) {
                layoutEditBaseline_.reset();
                layoutEditStableId_.clear();
                layoutSelection_.select(stableId);
                status_ =
                    "Gameplay board remained on its current Route 1 grid cell.";
                if (outError) {
                    outError->clear();
                }
                return true;
            }
            std::string error;
            if (!environment_.applyBoardLayout(
                    liveLayout,
                    &error) ||
                !saveBoardRegistrationManifest(&error)) {
                if (layoutEditBaseline_) {
                    std::string ignored;
                    environment_.applyBoardLayout(
                        *layoutEditBaseline_,
                        &ignored);
                    synchronizeBoardCellSize(
                        layoutEditBaseline_->boardCellSizeWorld);
                }
                layoutEditBaseline_.reset();
                layoutEditStableId_.clear();
                if (outError) {
                    *outError =
                        "Could not autosave the gameplay board layout: " +
                        error;
                }
                return false;
            }
            if (historyBaseline) {
                recordSceneEdit(*historyBaseline);
            }
            layoutEditBaseline_.reset();
            layoutEditStableId_.clear();
            layoutSelection_.select(stableId);
            status_ =
                "Snapped gameplay board layout rebuilt and autosaved.";
            if (outError) {
                outError->clear();
            }
            return true;
        }
        if (layoutEditBaseline_ &&
            layoutEditStableId_ != stableId) {
            if (outError) {
                *outError =
                    "The live layout target changed before commit.";
            }
            return false;
        }
        std::string error;
        const auto liveLayout =
            environment_.layout();
        const auto historyBaseline =
            layoutEditBaseline_;
        if (!environment_.applyBoardLayout(
                liveLayout,
                &error)) {
            if (layoutEditBaseline_) {
                std::string ignored;
                environment_.applyBoardLayout(
                    *layoutEditBaseline_,
                    &ignored);
            }
            layoutEditBaseline_.reset();
            layoutEditStableId_.clear();
            if (outError) {
                *outError =
                    "Could not finalize the live layout edit; it "
                    "was rolled back: " +
                    error;
            }
            return false;
        }
        if (!saveLayoutManifest(&error)) {
            if (layoutEditBaseline_) {
                std::string ignored;
                environment_.applyBoardLayout(
                    *layoutEditBaseline_,
                    &ignored);
            }
            layoutEditBaseline_.reset();
            layoutEditStableId_.clear();
            if (outError) {
                *outError =
                    "Could not autosave the layout override; the "
                    "live edit was rolled back: " +
                    error;
            }
            return false;
        }
        layoutSelection_.select(stableId);
        if (historyBaseline) {
            recordSceneEdit(*historyBaseline);
        }
        layoutEditBaseline_.reset();
        layoutEditStableId_.clear();
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
            (!stableId && previewUnitEditBaseline_)) {
            cancelPreviewUnitTransform(stableId);
            return;
        }
        if (layoutEditBaseline_ &&
            (!stableId ||
             layoutEditStableId_ == stableId)) {
            std::string ignored;
            environment_.applyBoardLayout(
                *layoutEditBaseline_,
                &ignored);
            synchronizeBoardCellSize(
                layoutEditBaseline_->boardCellSizeWorld);
        }
        layoutEditBaseline_.reset();
        layoutEditStableId_.clear();
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
            auto next = previous;
            next.terrainGridOrigin =
                kDefaultBoardTerrainGridOrigin;
            next.terrainElevationLevel = 0;
            game::runtime::lgpe_route1_runtime::
                bindBoardLayoutToTerrainGrid(next);
            std::string error;
            if (!environment_.applyBoardLayout(next, &error) ||
                !saveBoardRegistrationManifest(&error)) {
                std::string ignored;
                environment_.applyBoardLayout(previous, &ignored);
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
            layoutEditBaseline_.reset();
            layoutEditStableId_.clear();
            layoutSelection_.select(stableId);
            status_ = "Gameplay board layout restored to its default registration.";
            if (outError) {
                outError->clear();
            }
            return true;
        }
        const auto previous =
            environment_.layout();
        std::string error;
        if (!environment_.resetLayoutObjectOverride(
                stableId,
                &error)) {
            if (outError) {
                *outError = std::move(error);
            }
            return false;
        }
        if (!saveLayoutManifest(&error)) {
            std::string ignored;
            environment_.applyBoardLayout(
                previous,
                &ignored);
            if (outError) {
                *outError =
                    "Could not persist the layout reset; the "
                    "in-memory edit was rolled back: " +
                    error;
            }
            return false;
        }
        layoutSelection_.select(stableId);
        recordSceneEdit(previous);
        layoutEditBaseline_.reset();
        layoutEditStableId_.clear();
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
        const auto previous = environment_.layout();
        std::string createdStableId;
        std::string error;
        if (!environment_.duplicateLayoutObject(
                stableId,
                createdStableId,
                &error) ||
            !saveLayoutManifest(&error)) {
            std::string ignored;
            environment_.applyBoardLayout(
                previous,
                &ignored);
            if (outError) {
                *outError =
                    "Could not duplicate and persist the prefab instance: " +
                    error;
            }
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
        const auto previous = environment_.layout();
        std::string error;
        if (!environment_.deleteLayoutObject(
                stableId,
                &error) ||
            !saveLayoutManifest(&error)) {
            std::string ignored;
            environment_.applyBoardLayout(
                previous,
                &ignored);
            if (outError) {
                *outError =
                    "Could not delete and persist the scene object: " +
                    error;
            }
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
        const auto previous = environment_.layout();
        std::string error;
        for (std::size_t index = 0u;
             index < stableIdCount;
             ++index) {
            if (!stableIds[index] ||
                !environment_.deleteLayoutObject(
                    stableIds[index],
                    &error)) {
                std::string ignored;
                environment_.applyBoardLayout(
                    previous,
                    &ignored);
                if (outError) {
                    *outError = error.empty()
                        ? "A selected scene object had no stable ID."
                        : error;
                }
                return false;
            }
        }
        if (!saveLayoutManifest(&error)) {
            std::string ignored;
            environment_.applyBoardLayout(
                previous,
                &ignored);
            if (outError) {
                *outError =
                    "Could not persist the batch scene edit: " +
                    error;
            }
            return false;
        }
        recordSceneEdit(previous);
        layoutSelection_.clear();
        layoutEditBaseline_.reset();
        layoutEditStableId_.clear();
        refreshEnvironmentPrefabAssets();
        if (outError) {
            outError->clear();
        }
        return true;
    }

    bool boardClearanceAvailable() const noexcept {
        return sceneViewReady_ &&
            activeSceneId_ == "routes/route1";
    }

    std::size_t projectCommandCount() const noexcept override {
        return boardClearanceAvailable() ? 2u : 0u;
    }

    engine::editor::EditorProjectCommand projectCommand(
        std::size_t index) const noexcept override {
        if (!boardClearanceAvailable()) {
            return {};
        }
        if (index == 0u) {
            return {
                .id = "pokemonautochess.route1.clear_board_footprint",
                .displayName = "Autochess Board Clearing",
                .category = "PokemonAutochess / Route 1",
                .description =
                    "Suppress Route 1 obstructions, flatten covered cells to the registered board level, and rebuild clean lawn below the board and benches.",
                .buttonLabel = "Clear + Flatten Board Footprint",
                .confirmationText =
                    "Apply the selected clearing operations to the Route 1 board footprint? This autosaves as one undoable PokemonAutochess scene edit.",
                .fields = kBoardClearanceCommandFields.data(),
                .fieldCount = kBoardClearanceCommandFields.size(),
                .confirmationRequired = true};
        }
        if (index == 1u) {
            return {
                .id = "pokemonautochess.route1.reset_imported_scene",
                .displayName = "Restore Imported Route 1",
                .category = "PokemonAutochess / Route 1",
                .description =
                    "Remove every PokemonAutochess-authored Route 1 node and restore the promoted imported source baseline.",
                .buttonLabel = "Reset Entire Scene To Imported Source",
                .confirmationText =
                    "Restore the imported Route 1 baseline and remove all authored layout work? This is undoable, but it intentionally replaces the current authored scene state.",
                .confirmationRequired = true};
        }
        return {};
    }

    bool executeProjectCommand(
        const char* commandId,
        const engine::editor::EditorProjectCommandValue* values,
        std::size_t valueCount,
        engine::editor::EditorProjectCommandResult& outResult,
        std::string* outError) override {
        outResult = {};
        const std::string_view id = commandId ? commandId : "";
        if (id ==
            "pokemonautochess.route1.clear_board_footprint") {
            BoardClearanceRequest request;
            for (std::size_t index = 0u;
                 values && index < valueCount;
                 ++index) {
                const std::string_view field =
                    values[index].id ? values[index].id : "";
                if (field == "padding_cells") {
                    request.paddingCells = values[index].floatValue;
                } else if (field == "clear_terrain") {
                    request.clearTerrain = values[index].booleanValue;
                } else if (field == "clear_vegetation") {
                    request.clearVegetation = values[index].booleanValue;
                } else if (field == "clear_objects") {
                    request.clearObjects = values[index].booleanValue;
                } else if (field == "retain_ramps") {
                    request.retainRamps = values[index].booleanValue;
                } else if (field == "add_ground_infill") {
                    request.addGroundInfill =
                        values[index].booleanValue;
                }
            }
            if (!request.clearTerrain &&
                !request.clearVegetation &&
                !request.clearObjects &&
                !request.addGroundInfill) {
                if (outError) {
                    *outError =
                        "Enable at least one Route 1 clearing or infill option.";
                }
                return false;
            }
            BoardClearanceResult result;
            if (!applyBoardClearance(
                    request,
                    result,
                    outError)) {
                return false;
            }
            commandStatus_ =
                "Board clearing autosaved: " +
                std::to_string(result.suppressedTerrainCount) +
                " terrain, " +
                std::to_string(result.suppressedVegetationCount) +
                " vegetation, " +
                std::to_string(result.suppressedObjectCount) +
                " object obstructions suppressed" +
                (result.groundInfillCreated
                     ? "; ground infill created."
                     : ".");
            if (result.skippedUnsafeAggregateCount > 0u) {
                commandStatus_ +=
                    " " + std::to_string(
                        result.skippedUnsafeAggregateCount) +
                    " broad source foliage layers were preserved because their editable boundaries are not safe.";
            }
            outResult = {
                .status = commandStatus_.c_str(),
                .sceneChanged = true,
                .assetsChanged = true};
            return true;
        }
        if (id ==
            "pokemonautochess.route1.reset_imported_scene") {
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
        if (outError) {
            *outError =
                "Unknown PokemonAutochess editor command: " +
                std::string(id);
        }
        return false;
    }

    bool applyBoardClearance(
        const BoardClearanceRequest& request,
        BoardClearanceResult& outResult,
        std::string* outError) {
        outResult = {};
        if (!boardClearanceAvailable()) {
            if (outError) {
                *outError =
                    "Board clearing requires the mounted Route 1 scene.";
            }
            return false;
        }
        const auto previous = environment_.layout();
        const auto worldFromSource = glm::make_mat4(
            game::runtime::lgpe_route1_runtime::
                worldFromSourceMatrix(previous)
                .data());
        const float paddingWorld =
            std::max(0.0f, request.paddingCells) *
            boardCellSize_;
        const float boardHalfWidth =
            static_cast<float>(previous.boardCells[0]) *
                boardCellSize_ * 0.5f;
        const float boardHalfDepth =
            static_cast<float>(previous.boardCells[1]) *
                boardCellSize_ * 0.5f;
        struct Footprint {
            float minX;
            float maxX;
            float minZ;
            float maxZ;
        };
        std::vector<Footprint> footprints{{
            -boardHalfWidth - paddingWorld,
            boardHalfWidth + paddingWorld,
            -boardHalfDepth - paddingWorld,
            boardHalfDepth + paddingWorld}};
        const float benchGapWorld =
            static_cast<float>(previous.benchGapCells) *
            boardCellSize_;
        const float benchHalfWidth =
            static_cast<float>(previous.benchSlots) *
            boardCellSize_ * 0.5f;
        if (previous.northBench) {
            footprints.push_back({
                -benchHalfWidth - paddingWorld,
                benchHalfWidth + paddingWorld,
                boardHalfDepth + benchGapWorld - paddingWorld,
                boardHalfDepth + benchGapWorld +
                    boardCellSize_ + paddingWorld});
        }
        if (previous.southBench) {
            footprints.push_back({
                -benchHalfWidth - paddingWorld,
                benchHalfWidth + paddingWorld,
                -boardHalfDepth - benchGapWorld -
                    boardCellSize_ - paddingWorld,
                -boardHalfDepth - benchGapWorld + paddingWorld});
        }
        const auto overlapsFootprint =
            [&](float minX, float maxX,
                float minZ, float maxZ) {
                return std::any_of(
                    footprints.begin(),
                    footprints.end(),
                    [&](const Footprint& footprint) {
                        return maxX >= footprint.minX &&
                            minX <= footprint.maxX &&
                            maxZ >= footprint.minZ &&
                            minZ <= footprint.maxZ;
                    });
            };
        const auto intersectsBoard =
            [&](const game::runtime::lgpe_route1_runtime::
                    LayoutObject& object) {
                glm::vec3 minimum(
                    std::numeric_limits<float>::max());
                glm::vec3 maximum(
                    std::numeric_limits<float>::lowest());
                for (std::uint32_t corner = 0u;
                     corner < 8u;
                     ++corner) {
                    const glm::vec4 world =
                        worldFromSource * glm::vec4(
                            (corner & 1u) != 0u
                                ? object.boundsMaximumCm[0]
                                : object.boundsMinimumCm[0],
                            (corner & 2u) != 0u
                                ? object.boundsMaximumCm[1]
                                : object.boundsMinimumCm[1],
                            (corner & 4u) != 0u
                                ? object.boundsMaximumCm[2]
                                : object.boundsMinimumCm[2],
                            1.0f);
                    minimum = glm::min(
                        minimum,
                        glm::vec3(world));
                    maximum = glm::max(
                        maximum,
                        glm::vec3(world));
                }
                return overlapsFootprint(
                    minimum.x,
                    maximum.x,
                    minimum.z,
                    maximum.z);
            };

        std::vector<std::string> suppressIds;
        for (const auto& object :
             environment_.layoutObjects()) {
            if (object.suppressed ||
                object.stableId ==
                    kBoardGroundPrototypeStableId ||
                object.prefabAssetId ==
                    kBoardGroundPrefabAssetId ||
                !intersectsBoard(object)) {
                continue;
            }
            const bool terrain =
                object.targetKind ==
                    "canonical_terrain_assembly";
            const bool ramp = terrain &&
                object.categoryPath.find("/Ramps") !=
                    std::string::npos;
            const bool exactVegetation =
                object.targetKind ==
                    "canonical_tree_instance" ||
                object.targetKind ==
                    "encounter_grass_record" ||
                object.targetKind ==
                    "buildmodel_vegetation_placement" ||
                (object.authored &&
                 object.categoryPath.rfind(
                     "Environment/Vegetation",
                     0u) == 0u);
            const bool aggregateVegetation =
                object.targetKind ==
                    "canonical_mesh_group" &&
                object.categoryPath.rfind(
                    "Environment/Vegetation",
                    0u) == 0u;
            const bool objectObstruction =
                object.categoryPath.rfind(
                    "Environment/Props",
                    0u) == 0u ||
                (object.authored && !terrain &&
                 !exactVegetation);
            if (ramp && request.retainRamps) {
                ++outResult.retainedRampCount;
                continue;
            }
            if (aggregateVegetation) {
                ++outResult.skippedUnsafeAggregateCount;
                continue;
            }
            if (terrain && request.clearTerrain &&
                request.addGroundInfill) {
                // The cell infill below performs a local source-triangle
                // replacement. Suppressing this whole connected source
                // assembly would erase valid terrain beyond the board.
                continue;
            }
            if (terrain && request.clearTerrain) {
                suppressIds.push_back(object.stableId);
                ++outResult.suppressedTerrainCount;
            } else if (
                exactVegetation &&
                request.clearVegetation) {
                suppressIds.push_back(object.stableId);
                ++outResult.suppressedVegetationCount;
            } else if (
                objectObstruction &&
                request.clearObjects) {
                suppressIds.push_back(object.stableId);
                ++outResult.suppressedObjectCount;
            }
        }

        std::string error;
        for (const auto& stableId : suppressIds) {
            if (!environment_.deleteLayoutObject(
                    stableId,
                    &error)) {
                std::string ignored;
                environment_.applyBoardLayout(
                    previous,
                    &ignored);
                if (outError) {
                    *outError =
                        "Could not suppress board obstruction " +
                        stableId + ": " + error;
                }
                return false;
            }
        }

        if (request.addGroundInfill) {
            auto next = environment_.layout();
            std::erase_if(
                next.authoredPrefabInstances,
                [](const auto& candidate) {
                    return candidate.stableId ==
                        kBoardGroundInstanceStableId;
                });
            std::size_t createdTileCount = 0u;
            for (const auto& sourceTile :
                 environment_.terrainTiles()) {
                glm::vec2 minimum(
                    std::numeric_limits<float>::max());
                glm::vec2 maximum(
                    std::numeric_limits<float>::lowest());
                for (std::uint32_t corner = 0u;
                     corner < 4u;
                     ++corner) {
                    const glm::vec4 world =
                        worldFromSource * glm::vec4(
                            (static_cast<float>(sourceTile.gridX) +
                             ((corner & 1u) != 0u ? 1.0f : 0.0f)) *
                                kTerrainTileSizeCm,
                            static_cast<float>(sourceTile.elevationLevel) *
                                kTerrainElevationStepCm,
                            (static_cast<float>(sourceTile.gridZ) +
                             ((corner & 2u) != 0u ? 1.0f : 0.0f)) *
                                kTerrainTileSizeCm,
                            1.0f);
                    minimum = glm::min(
                        minimum,
                        glm::vec2(world.x, world.z));
                    maximum = glm::max(
                        maximum,
                        glm::vec2(world.x, world.z));
                }
                if (!overlapsFootprint(
                        minimum.x,
                        maximum.x,
                        minimum.y,
                        maximum.y)) {
                    continue;
                }
                const std::string stableId = terrainTileStableId(
                    sourceTile.gridX,
                    sourceTile.gridZ);
                auto tile = std::find_if(
                    next.authoredTerrainTiles.begin(),
                    next.authoredTerrainTiles.end(),
                    [&](const auto& candidate) {
                        return candidate.gridX == sourceTile.gridX &&
                            candidate.gridZ == sourceTile.gridZ;
                    });
                const game::runtime::lgpe_route1_runtime::
                    AuthoredTerrainTile groundTile{
                        .stableId = stableId,
                        .displayName =
                            "Board Ground Tile (" +
                            std::to_string(sourceTile.gridX) + ", " +
                            std::to_string(sourceTile.gridZ) + ")",
                        .categoryPath =
                            "Environment/Terrain/Gameplay Board",
                        .tileSetAssetId =
                            std::string(kTerrainTileSetAssetId),
                        .gridX = sourceTile.gridX,
                        .gridZ = sourceTile.gridZ,
                        .elevationLevel =
                            previous.terrainElevationLevel,
                        .surface = "light_lawn",
                        .shape = "flat",
                        .reason =
                            "autochess_board_ground_infill"};
                if (tile == next.authoredTerrainTiles.end()) {
                    next.authoredTerrainTiles.push_back(groundTile);
                } else {
                    *tile = groundTile;
                }
                ++createdTileCount;
            }
            if (createdTileCount == 0u) {
                std::string ignored;
                environment_.applyBoardLayout(previous, &ignored);
                if (outError) {
                    *outError =
                        "The autochess board footprint did not overlap the Route 1 terrain grid.";
                }
                return false;
            }
            if (!environment_.applyBoardLayout(
                    next,
                    &error)) {
                std::string ignored;
                environment_.applyBoardLayout(
                    previous,
                    &ignored);
                if (outError) {
                    *outError =
                        "Could not create the board ground infill: " +
                        error;
                }
                return false;
            }
            outResult.groundInfillCreated = true;
        }

        if (!saveLayoutManifest(&error)) {
            std::string ignored;
            environment_.applyBoardLayout(
                previous,
                &ignored);
            if (outError) {
                *outError =
                    "Could not persist the board clearing: " +
                    error;
            }
            return false;
        }
        recordSceneEdit(previous);
        layoutSelection_.clear();
        layoutEditBaseline_.reset();
        layoutEditStableId_.clear();
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
        auto baseline = previous;
        baseline.localLayoutDeltas.clear();
        baseline.objectMetadataOverrides.clear();
        baseline.authoredPrefabInstances.clear();
        baseline.authoredTerrainTiles.clear();
        baseline.declaredLocalDeltaCount = 0u;
        std::string error;
        if (!environment_.applyBoardLayout(
                baseline,
                &error) ||
            !saveLayoutManifest(&error)) {
            std::string ignored;
            environment_.applyBoardLayout(
                previous,
                &ignored);
            if (outError) {
                *outError =
                    "Could not restore and persist the imported scene baseline: " +
                    error;
            }
            return false;
        }
        recordSceneEdit(previous);
        layoutSelection_.clear();
        layoutEditBaseline_.reset();
        layoutEditStableId_.clear();
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
        return persistSceneObjectMetadataCommand(
            environment_.layout(),
            [&]() {
                return environment_.renameLayoutObject(
                    command.stableId,
                    command.value,
                    outError);
            },
            command.stableId,
            "rename",
            outError);
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
        return persistSceneObjectMetadataCommand(
            environment_.layout(),
            [&]() {
                return environment_.reparentLayoutObject(
                    command.stableId,
                    command.value,
                    outError);
            },
            command.stableId,
            "reparent",
            outError);
    }

    bool canUndoSceneEdit() const noexcept override {
        return !sceneUndoStack_.empty();
    }

    bool canRedoSceneEdit() const noexcept override {
        return !sceneRedoStack_.empty();
    }

    bool undoSceneEdit(
        std::string* outError) override {
        if (sceneUndoStack_.empty()) {
            if (outError) {
                *outError = "There is no scene edit to undo.";
            }
            return false;
        }
        const auto current = environment_.layout();
        const auto target = sceneUndoStack_.back();
        std::string error;
        if (!environment_.applyBoardLayout(
                target,
                &error) ||
            !saveLayoutManifest(&error) ||
            !saveBoardRegistrationManifest(&error)) {
            std::string ignored;
            environment_.applyBoardLayout(
                current,
                &ignored);
            if (outError) {
                *outError =
                    "Could not undo and persist the scene edit: " +
                    error;
            }
            return false;
        }
        synchronizeBoardCellSize(
            target.boardCellSizeWorld);
        sceneUndoStack_.pop_back();
        sceneRedoStack_.push_back(current);
        refreshEnvironmentPrefabAssets();
        layoutEditBaseline_.reset();
        layoutEditStableId_.clear();
        if (outError) {
            outError->clear();
        }
        return true;
    }

    bool redoSceneEdit(
        std::string* outError) override {
        if (sceneRedoStack_.empty()) {
            if (outError) {
                *outError = "There is no scene edit to redo.";
            }
            return false;
        }
        const auto current = environment_.layout();
        const auto target = sceneRedoStack_.back();
        std::string error;
        if (!environment_.applyBoardLayout(
                target,
                &error) ||
            !saveLayoutManifest(&error) ||
            !saveBoardRegistrationManifest(&error)) {
            std::string ignored;
            environment_.applyBoardLayout(
                current,
                &ignored);
            if (outError) {
                *outError =
                    "Could not redo and persist the scene edit: " +
                    error;
            }
            return false;
        }
        synchronizeBoardCellSize(
            target.boardCellSizeWorld);
        sceneRedoStack_.pop_back();
        sceneUndoStack_.push_back(current);
        refreshEnvironmentPrefabAssets();
        layoutEditBaseline_.reset();
        layoutEditStableId_.clear();
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

    struct PreviewUnitTransform {
        std::array<float, 3> position{};
        std::array<float, 3> rotationDegrees{};
    };

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

    static bool readJsonVec3(
        const nlohmann::json& value,
        std::array<float, 3>& out) {
        if (!value.is_array() || value.size() != 3u) {
            return false;
        }
        for (std::size_t axis = 0u; axis < 3u; ++axis) {
            if (!value[axis].is_number()) {
                return false;
            }
            out[axis] = value[axis].get<float>();
            if (!std::isfinite(out[axis])) {
                return false;
            }
        }
        return true;
    }

    const nlohmann::json* savedPreviewUnitOverride(
        std::string_view unitKey) const {
        if (!previewUnitOverrides_.is_object()) {
            return nullptr;
        }
        const auto previews =
            previewUnitOverrides_.find("previews");
        if (previews == previewUnitOverrides_.end() ||
            !previews->is_object()) {
            return nullptr;
        }
        const auto preview = previews->find(activePreviewId_);
        if (preview == previews->end() || !preview->is_object()) {
            return nullptr;
        }
        const auto units = preview->find("units");
        if (units == preview->end() || !units->is_object()) {
            return nullptr;
        }
        const auto unit = units->find(std::string(unitKey));
        return unit == units->end() || !unit->is_object()
            ? nullptr
            : &*unit;
    }

    bool loadPreviewUnitOverrides(std::string* outError) {
        previewUnitOverrides_ = {
            {"schema_version", 1},
            {"previews", nlohmann::json::object()}};
        const std::filesystem::path path =
            projectRoot_ /
            "config/editor/game_preview_layouts.json";
        if (!std::filesystem::is_regular_file(path)) {
            if (outError) {
                outError->clear();
            }
            return true;
        }
        try {
            std::ifstream input(path, std::ios::binary);
            if (!input) {
                throw std::runtime_error(
                    "could not open the file");
            }
            nlohmann::json parsed;
            input >> parsed;
            if (!parsed.is_object()) {
                throw std::runtime_error(
                    "the document root is not an object");
            }
            if (!parsed.contains("previews") ||
                !parsed.at("previews").is_object()) {
                parsed["previews"] =
                    nlohmann::json::object();
            }
            parsed["schema_version"] = 1;
            previewUnitOverrides_ = std::move(parsed);
        } catch (const std::exception& error) {
            if (outError) {
                *outError =
                    "Could not load game-preview unit placements: " +
                    std::string(error.what());
            }
            return false;
        }
        if (outError) {
            outError->clear();
        }
        return true;
    }

    bool savePreviewUnitOverrides(std::string* outError) {
        const std::filesystem::path destination =
            projectRoot_ /
            "config/editor/game_preview_layouts.json";
        const std::filesystem::path temporary =
            destination.string() + ".editor-tmp";
        std::error_code error;
        std::filesystem::create_directories(
            destination.parent_path(), error);
        if (error) {
            if (outError) {
                *outError =
                    "Could not create the game-preview layout directory: " +
                    error.message();
            }
            return false;
        }
        {
            std::ofstream output(
                temporary,
                std::ios::binary | std::ios::trunc);
            if (!output) {
                if (outError) {
                    *outError =
                        "Could not open the temporary game-preview layout file.";
                }
                return false;
            }
            output << previewUnitOverrides_.dump(2) << '\n';
            output.flush();
            if (!output) {
                if (outError) {
                    *outError =
                        "Could not write the game-preview layout file.";
                }
                return false;
            }
        }
        std::filesystem::copy_file(
            temporary,
            destination,
            std::filesystem::copy_options::overwrite_existing,
            error);
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        if (error) {
            if (outError) {
                *outError =
                    "Could not replace the game-preview layout file: " +
                    error.message();
            }
            return false;
        }
        if (outError) {
            outError->clear();
        }
        return true;
    }

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
                    savedPreviewUnitOverride(unitKey) != nullptr};
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
                const glm::vec3 markerPosition(
                    object.unit.position[0],
                    object.unit.position[1] + radius,
                    object.unit.position[2]);
                float centerX = 0.0f;
                float centerY = 0.0f;
                if (projectEditorPoint(
                        glm::value_ptr(
                            gameLayoutViewProjection_),
                        markerPosition,
                        gameLayoutProjectionWidth_,
                        gameLayoutProjectionHeight_,
                        centerX,
                        centerY)) {
                    object.viewportPosition = {
                        centerX, centerY};
                    object.viewportVisible = true;
                    constexpr float axisLength = 0.5f;
                    constexpr std::array<glm::vec3, 3>
                        axes{{
                            {axisLength, 0.0f, 0.0f},
                            {0.0f, axisLength, 0.0f},
                            {0.0f, 0.0f, axisLength},
                        }};
                    for (std::size_t axis = 0u;
                         axis < axes.size();
                         ++axis) {
                        float endpointX = 0.0f;
                        float endpointY = 0.0f;
                        if (!projectEditorPoint(
                                glm::value_ptr(
                                    gameLayoutViewProjection_),
                                markerPosition + axes[axis],
                                gameLayoutProjectionWidth_,
                                gameLayoutProjectionHeight_,
                                endpointX,
                                endpointY)) {
                            continue;
                        }
                        const float dx = endpointX - centerX;
                        const float dy = endpointY - centerY;
                        const float length =
                            std::sqrt(dx * dx + dy * dy);
                        if (length <= 0.001f) {
                            continue;
                        }
                        object.viewportAxisDirections[axis * 2u] =
                            dx / length;
                        object.viewportAxisDirections[
                            axis * 2u + 1u] = dy / length;
                        object.viewportSourceUnitsPerPixel[axis] =
                            axisLength / length;
                    }
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
            const auto* saved =
                savedPreviewUnitOverride(object.unitKey);
            if (!saved) {
                continue;
            }
            auto position = object.unit.position;
            auto rotation = object.unit.rotationDegrees;
            const auto positionIt = saved->find("position_world");
            const auto rotationIt =
                saved->find("rotation_degrees");
            if (positionIt == saved->end() ||
                !readJsonVec3(*positionIt, position)) {
                continue;
            }
            if (rotationIt != saved->end()) {
                readJsonVec3(*rotationIt, rotation);
            }
            gameRuntime_->setEditorPreviewUnitTransform(
                object.unit.unitId,
                position,
                rotation,
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
        if (!previewUnitEditBaseline_ ||
            previewUnitEditStableId_ != edit.stableId) {
            previewUnitEditBaseline_ = PreviewUnitTransform{
                .position = object->unit.position,
                .rotationDegrees = object->unit.rotationDegrees};
            previewUnitEditStableId_ = edit.stableId;
        }
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
        if (!previewUnitEditBaseline_) {
            // ImGui can report a deactivation when hierarchy selection moves
            // between unrelated object types. A commit without a preceding
            // live preview is not an authored placement edit.
            if (outError) {
                outError->clear();
            }
            return true;
        }
        if (previewUnitEditBaseline_ &&
            previewUnitEditStableId_ != stableId) {
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
                *previewUnitEditBaseline_,
                current)) {
            previewUnitEditBaseline_.reset();
            previewUnitEditStableId_.clear();
            if (outError) {
                outError->clear();
            }
            return true;
        }

        const nlohmann::json previousDocument =
            previewUnitOverrides_;
        auto& saved =
            previewUnitOverrides_["previews"]
                [activePreviewId_]["units"]
                [object->unitKey];
        saved = {
            {"species", object->unit.speciesName},
            {"side", object->unit.playerSide
                ? "player"
                : "enemy"},
            {"placement", object->unit.benchUnit
                ? "bench"
                : "board"},
            {"board_cell", {
                object->unit.boardColumn,
                object->unit.boardRow}},
            {"bench_slot", object->unit.benchSlot},
            {"position_world", object->unit.position},
            {"rotation_degrees",
                object->unit.rotationDegrees}};
        std::string error;
        if (!savePreviewUnitOverrides(&error)) {
            previewUnitOverrides_ = previousDocument;
            if (previewUnitEditBaseline_ && gameRuntime_) {
                gameRuntime_->setEditorPreviewUnitTransform(
                    object->unit.unitId,
                    previewUnitEditBaseline_->position,
                    previewUnitEditBaseline_->rotationDegrees,
                    true);
            }
            previewUnitEditBaseline_.reset();
            previewUnitEditStableId_.clear();
            refreshPreviewUnitLayoutObjects(false);
            if (outError) {
                *outError =
                    "Could not save the gameplay-preview starting position: " +
                    error;
            }
            return false;
        }
        previewUnitEditBaseline_.reset();
        previewUnitEditStableId_.clear();
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
        if (!previewUnitEditBaseline_ || !gameRuntime_ ||
            (stableId &&
             previewUnitEditStableId_ != stableId)) {
            return;
        }
        if (auto* object = findPreviewUnitLayoutObject(
                previewUnitEditStableId_)) {
            gameRuntime_->setEditorPreviewUnitTransform(
                object->unit.unitId,
                previewUnitEditBaseline_->position,
                previewUnitEditBaseline_->rotationDegrees,
                true);
        }
        previewUnitEditBaseline_.reset();
        previewUnitEditStableId_.clear();
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
        const nlohmann::json previousDocument =
            previewUnitOverrides_;
        gameRuntime_->setEditorPreviewUnitTransform(
            unitId,
            source->second.position,
            source->second.rotationDegrees,
            true);
        auto& units =
            previewUnitOverrides_["previews"]
                [activePreviewId_]["units"];
        if (units.is_object()) {
            units.erase(unitKey);
        }
        std::string error;
        if (!savePreviewUnitOverrides(&error)) {
            previewUnitOverrides_ = previousDocument;
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
        previewUnitEditBaseline_.reset();
        previewUnitEditStableId_.clear();
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
        game::runtime::lgpe_route1_runtime::
            BoardLayoutTransform previous) {
        constexpr std::size_t kHistoryLimit = 128u;
        sceneUndoStack_.push_back(
            std::move(previous));
        if (sceneUndoStack_.size() >
            kHistoryLimit) {
            sceneUndoStack_.erase(
                sceneUndoStack_.begin());
        }
        sceneRedoStack_.clear();
        refreshEnvironmentPrefabAssets();
    }

    template <typename Command>
    bool persistSceneObjectMetadataCommand(
        game::runtime::lgpe_route1_runtime::
            BoardLayoutTransform previous,
        Command&& command,
        const char* stableId,
        const char* operation,
        std::string* outError) {
        if (!command()) {
            return false;
        }
        std::string error;
        if (!saveLayoutManifest(&error)) {
            std::string ignored;
            environment_.applyBoardLayout(
                previous,
                &ignored);
            if (outError) {
                *outError =
                    "Could not persist the scene object " +
                    std::string(operation) + ": " +
                    error;
            }
            return false;
        }
        recordSceneEdit(std::move(previous));
        layoutSelection_.select(stableId);
        if (outError) {
            outError->clear();
        }
        return true;
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

    bool saveBoardRegistrationManifest(
        std::string* outError) {
        if (projectRoot_.empty() || !sceneViewReady_) {
            if (outError) {
                *outError =
                    "The gameplay board cannot be saved before Route 1 is mounted.";
            }
            return false;
        }
        const std::filesystem::path destination =
            projectRoot_ /
            game::runtime::lgpe_route1_runtime::
                kBoardLayoutManifestPath;
        const std::filesystem::path temporary =
            destination.string() + ".editor-tmp";
        std::error_code error;
        std::filesystem::create_directories(
            destination.parent_path(),
            error);
        if (error) {
            if (outError) {
                *outError =
                    "Could not create the board-layout directory: " +
                    error.message();
            }
            return false;
        }
        {
            std::ofstream output(
                temporary,
                std::ios::binary | std::ios::trunc);
            if (!output) {
                if (outError) {
                    *outError =
                        "Could not open the temporary board-layout manifest.";
                }
                return false;
            }
            output << game::runtime::lgpe_route1_runtime::
                serializeBoardLayoutTransform(
                    environment_.layout());
            output.flush();
            if (!output) {
                if (outError) {
                    *outError =
                        "Could not write the temporary board-layout manifest.";
                }
                return false;
            }
        }
        std::filesystem::copy_file(
            temporary,
            destination,
            std::filesystem::copy_options::overwrite_existing,
            error);
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        if (error) {
            if (outError) {
                *outError =
                    "Could not replace the board-layout manifest: " +
                    error.message();
            }
            return false;
        }
        if (outError) {
            outError->clear();
        }
        return true;
    }

    bool saveLayoutManifest(
        std::string* outError) {
        if (projectRoot_.empty() ||
            !sceneViewReady_) {
            if (outError) {
                *outError =
                    "Route 1 layout cannot be saved before the "
                    "project scene is mounted.";
            }
            return false;
        }
        const std::filesystem::path destination =
            activeAuthoredScenePath_;
        if (destination.empty()) {
            if (outError) {
                *outError =
                    "The active scene has no authored document path.";
            }
            return false;
        }
        const std::filesystem::path temporary =
            destination.string() + ".editor-tmp";
        std::error_code error;
        std::filesystem::create_directories(
            destination.parent_path(),
            error);
        if (error) {
            if (outError) {
                *outError =
                    "Could not create the layout manifest directory: " +
                    error.message();
            }
            return false;
        }
        {
            std::ofstream output(
                temporary,
                std::ios::binary |
                    std::ios::trunc);
            if (!output) {
                if (outError) {
                    *outError =
                        "Could not open the temporary layout manifest.";
                }
                return false;
            }
            const auto& authoredScene =
                environment_.authoredScene();
            if (authoredScene.nodes.empty()) {
                // Preserve the promoted, source-identical empty checkpoint
                // byte for byte after the last authored edit is restored.
                output <<
                    "{\n"
                    "  \"schema_version\": 1,\n"
                    "  \"kind\": \"phlosion_authored_scene\",\n"
                    "  \"scene_id\": \"routes/route1\",\n"
                    "  \"base_environment_asset_id\": \"environments/route1\",\n"
                    "  \"coordinate_system\": \"source_centimetres_xyz_y_up\",\n"
                    "  \"nodes\": []\n"
                    "}\n";
            } else {
                output <<
                    engine::assets::phlosion::
                        serializeAuthoredSceneDocument(authoredScene);
            }
            output.flush();
            if (!output) {
                if (outError) {
                    *outError =
                        "Could not write the temporary layout manifest.";
                }
                return false;
            }
        }
        std::filesystem::copy_file(
            temporary,
            destination,
            std::filesystem::copy_options::
                overwrite_existing,
            error);
        std::error_code cleanupError;
        std::filesystem::remove(
            temporary,
            cleanupError);
        if (error) {
            if (outError) {
                *outError =
                    "Could not replace the project layout manifest: " +
                    error.message();
            }
            return false;
        }
        if (outError) {
            outError->clear();
        }
        return true;
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
                30));
        // Draw the board and benches through the recovered terrain cells
        // themselves. This deliberately avoids a second floating-point grid
        // that could merely look aligned while remaining logically
        // independent.
        const glm::mat4 worldFromSource = glm::make_mat4(
            game::runtime::lgpe_route1_runtime::
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
                -> const game::runtime::lgpe_route1_runtime::
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
                game::runtime::lgpe_route1_runtime::
                    northBenchTerrainGridOrigin(layout),
                true);
        }
        if (layout.southBench) {
            appendBenchGrid(
                game::runtime::lgpe_route1_runtime::
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
            [&](const game::runtime::lgpe_route1_runtime::
                    LayoutObject& object) {
                return object.stableId ==
                    layoutSelection_.id();
            });
        if (selected !=
            environment_.layoutObjects().end()) {
            const auto selectedWorldFromSource =
                game::runtime::lgpe_route1_runtime::
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
            layoutEditBaseline_.reset();
            layoutEditStableId_.clear();
            sceneUndoStack_.clear();
            sceneRedoStack_.clear();
            activeSceneId_ = std::move(sceneId);
            activeEnvironmentAssetId_ =
                std::move(environmentAssetId);
            activeEnvironmentPath_.clear();
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
        engine::assets::phlosion::SceneArchiveStore
            nextSceneStore;
        game::runtime::lgpe_route1_runtime::
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
                game::runtime::lgpe_route1_runtime::
                    kCanonicalRoot,
                game::runtime::lgpe_route1_runtime::
                    kCompositionManifestPath,
                game::runtime::lgpe_route1_runtime::
                    kBoardLayoutManifestPath,
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
        game::runtime::lgpe_route1_runtime::
            BoardLayoutTransform projectLayout;
        if (!game::runtime::lgpe_route1_runtime::
                loadBoardLayoutTransform(
                    projectStore,
                    game::runtime::lgpe_route1_runtime::
                        kBoardLayoutManifestPath,
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
        logPhase("authored_scene");

        sceneStore_ = std::move(nextSceneStore);
        environment_ = std::move(nextEnvironment);
        sceneViewReady_ = true;
        layoutProjectionReady_ = false;
        layoutEditBaseline_.reset();
        layoutEditStableId_.clear();
        sceneUndoStack_.clear();
        sceneRedoStack_.clear();
        activeSceneId_ = std::move(sceneId);
        activeEnvironmentAssetId_ =
            std::move(environmentAssetId);
        activeEnvironmentPath_ = environmentPath;
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
    game::runtime::lgpe_route1_runtime::RuntimeEnvironment
        environment_;
    std::vector<
        game::runtime::shared_world_batches::WorldIndexedBatch>
        batches_;
    std::filesystem::path projectRoot_;
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
    nlohmann::json previewUnitOverrides_ = {
        {"schema_version", 1},
        {"previews", nlohmann::json::object()}};
    std::optional<PreviewUnitTransform>
        previewUnitEditBaseline_;
    std::string previewUnitEditStableId_;
    ActiveAssetPreview activeAssetPreview_ =
        ActiveAssetPreview::Model;
    IRenderBackend* renderer_ = nullptr;
    Camera3D* gameCamera_ = nullptr;
    std::vector<SavedEnvironment> savedEnvironment_;
    std::filesystem::path activeEnvironmentPath_;
    std::filesystem::path activeAuthoredScenePath_;
    std::string activeSceneId_;
    std::string activeEnvironmentAssetId_;
    std::string activePreviewId_ = "main-menu";
    editor_hierarchy::Selection layoutSelection_;
    std::string layoutEditStableId_;
    std::optional<
        game::runtime::lgpe_route1_runtime::
            BoardLayoutTransform>
        layoutEditBaseline_;
    std::vector<
        game::runtime::lgpe_route1_runtime::
            BoardLayoutTransform>
        sceneUndoStack_;
    std::vector<
        game::runtime::lgpe_route1_runtime::
            BoardLayoutTransform>
        sceneRedoStack_;
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
