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
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace {

constexpr std::string_view kBoardGroundPrototypeStableId =
    "gameplay-board/ground-patch-prototype";
constexpr std::string_view kBoardGroundPrefabAssetId =
    "route1/autochess_board_ground_patch";
constexpr std::string_view kBoardGroundInstanceStableId =
    "authored-prefab/autochess-board-ground-patch/board-clearance";

struct PreviewDefinition {
    const char* id;
    const char* displayName;
    const char* group;
    const char* description;
    const char* state;
    const char* gameMode;
    const char* snapshot;
    const char* sceneId = "";
};

constexpr std::array<PreviewDefinition, 28>
    kPreviewDefinitions = {{
        {
            "boot",
            "Boot Sequence",
            "Frontend",
            "Replay the loading presentation, then enter the main menu.",
            "main_menu",
            "classic",
            "",
        },
        {
            "main-menu",
            "Main Menu",
            "Frontend",
            "Open the Classic / Adventure frontend without restarting the runtime.",
            "main_menu",
            "classic",
            "",
        },
        {
            "starter-classic",
            "Starter Selection - Classic",
            "Starter Selection",
            "Open the real starter-selection state in Classic mode.",
            "starter",
            "classic",
            "",
        },
        {
            "starter-adventure",
            "Starter Selection - Adventure",
            "Starter Selection",
            "Open the real starter-selection state in Adventure mode.",
            "starter",
            "adventure",
            "",
        },
        {
            "route1-planning-classic",
            "Route 1 Planning - Classic",
            "Route 1",
            "Restore Route 1 in its Classic planning phase.",
            "snapshot",
            "classic",
            "config/debug/editor_route1_planning.json",
            "routes/route1",
        },
        {
            "route1-planning-adventure",
            "Route 1 Planning - Adventure",
            "Route 1",
            "Restore Route 1 in its Adventure planning phase.",
            "snapshot",
            "adventure",
            "config/debug/editor_route1_planning.json",
            "routes/route1",
        },
        {
            "route1-battle-classic",
            "Route 1 Battle - Classic",
            "Route 1",
            "Restore the deterministic Route 1 battle in Classic mode.",
            "snapshot",
            "classic",
            "config/debug/debug_state_snapshot_bulbasaur_route1_combat.json",
            "routes/route1",
        },
        {
            "route1-battle-adventure",
            "Route 1 Battle - Adventure",
            "Route 1",
            "Restore the deterministic Route 1 battle in Adventure mode.",
            "snapshot",
            "adventure",
            "config/debug/debug_state_snapshot_bulbasaur_route1_combat.json",
            "routes/route1",
        },
        {
            "route1-5-planning-classic",
            "Route 1.5 Planning - Classic",
            "Route 1.5",
            "Open the Route 1.5 planning phase in Classic mode.",
            "route_planning",
            "classic",
            "scripts/states/route1_5.lua",
            "routes/route1-5",
        },
        {
            "route1-5-planning-adventure",
            "Route 1.5 Planning - Adventure",
            "Route 1.5",
            "Open the Route 1.5 planning phase in Adventure mode.",
            "route_planning",
            "adventure",
            "scripts/states/route1_5.lua",
            "routes/route1-5",
        },
        {
            "route1-5-battle-classic",
            "Route 1.5 Battle - Classic",
            "Route 1.5",
            "Open the Route 1.5 battle phase in Classic mode.",
            "route_battle",
            "classic",
            "scripts/states/route1_5.lua",
            "routes/route1-5",
        },
        {
            "route1-5-battle-adventure",
            "Route 1.5 Battle - Adventure",
            "Route 1.5",
            "Open the Route 1.5 battle phase in Adventure mode.",
            "route_battle",
            "adventure",
            "scripts/states/route1_5.lua",
            "routes/route1-5",
        },
        {
            "route22-planning-classic",
            "Route 22 Planning - Classic",
            "Route 22",
            "Open the Route 22 planning phase in Classic mode.",
            "route_planning",
            "classic",
            "scripts/states/route22.lua",
            "routes/route22",
        },
        {
            "route22-planning-adventure",
            "Route 22 Planning - Adventure",
            "Route 22",
            "Open the Route 22 planning phase in Adventure mode.",
            "route_planning",
            "adventure",
            "scripts/states/route22.lua",
            "routes/route22",
        },
        {
            "route22-battle-classic",
            "Route 22 Battle - Classic",
            "Route 22",
            "Open the Route 22 battle phase in Classic mode.",
            "route_battle",
            "classic",
            "scripts/states/route22.lua",
            "routes/route22",
        },
        {
            "route22-battle-adventure",
            "Route 22 Battle - Adventure",
            "Route 22",
            "Open the Route 22 battle phase in Adventure mode.",
            "route_battle",
            "adventure",
            "scripts/states/route22.lua",
            "routes/route22",
        },
        {
            "route2-planning-classic",
            "Route 2 Planning - Classic",
            "Route 2",
            "Open the Route 2 planning phase in Classic mode.",
            "route_planning",
            "classic",
            "scripts/states/route2.lua",
            "routes/route2",
        },
        {
            "route2-planning-adventure",
            "Route 2 Planning - Adventure",
            "Route 2",
            "Open the Route 2 planning phase in Adventure mode.",
            "route_planning",
            "adventure",
            "scripts/states/route2.lua",
            "routes/route2",
        },
        {
            "route2-battle-classic",
            "Route 2 Battle - Classic",
            "Route 2",
            "Open the Route 2 battle phase in Classic mode.",
            "route_battle",
            "classic",
            "scripts/states/route2.lua",
            "routes/route2",
        },
        {
            "route2-battle-adventure",
            "Route 2 Battle - Adventure",
            "Route 2",
            "Open the Route 2 battle phase in Adventure mode.",
            "route_battle",
            "adventure",
            "scripts/states/route2.lua",
            "routes/route2",
        },
        {
            "viridian-forest-planning-classic",
            "Viridian Forest Planning - Classic",
            "Viridian Forest",
            "Open the Viridian Forest planning phase in Classic mode.",
            "route_planning",
            "classic",
            "scripts/states/viridian_forest.lua",
            "routes/viridian-forest",
        },
        {
            "viridian-forest-planning-adventure",
            "Viridian Forest Planning - Adventure",
            "Viridian Forest",
            "Open the Viridian Forest planning phase in Adventure mode.",
            "route_planning",
            "adventure",
            "scripts/states/viridian_forest.lua",
            "routes/viridian-forest",
        },
        {
            "viridian-forest-battle-classic",
            "Viridian Forest Battle - Classic",
            "Viridian Forest",
            "Open the Viridian Forest battle phase in Classic mode.",
            "route_battle",
            "classic",
            "scripts/states/viridian_forest.lua",
            "routes/viridian-forest",
        },
        {
            "viridian-forest-battle-adventure",
            "Viridian Forest Battle - Adventure",
            "Viridian Forest",
            "Open the Viridian Forest battle phase in Adventure mode.",
            "route_battle",
            "adventure",
            "scripts/states/viridian_forest.lua",
            "routes/viridian-forest",
        },
        {
            "route3-planning-classic",
            "Route 3 Planning - Classic",
            "Route 3",
            "Open the Route 3 planning phase in Classic mode.",
            "route_planning",
            "classic",
            "scripts/states/route3.lua",
            "routes/route3",
        },
        {
            "route3-planning-adventure",
            "Route 3 Planning - Adventure",
            "Route 3",
            "Open the Route 3 planning phase in Adventure mode.",
            "route_planning",
            "adventure",
            "scripts/states/route3.lua",
            "routes/route3",
        },
        {
            "route3-battle-classic",
            "Route 3 Battle - Classic",
            "Route 3",
            "Open the Route 3 battle phase in Classic mode.",
            "route_battle",
            "classic",
            "scripts/states/route3.lua",
            "routes/route3",
        },
        {
            "route3-battle-adventure",
            "Route 3 Battle - Adventure",
            "Route 3",
            "Open the Route 3 battle phase in Adventure mode.",
            "route_battle",
            "adventure",
            "scripts/states/route3.lua",
            "routes/route3",
        },
    }};

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
            .encounterGrassInstanceCount =
                source.encounterGrassInstanceCount,
            .vegetationInstanceCount =
                source.placedVegetationInstanceCount,
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
        return kPreviewDefinitions.size();
    }

    engine::editor::EditorProjectGamePreview gamePreview(
        std::size_t index) const noexcept override {
        if (index >= kPreviewDefinitions.size()) {
            return {};
        }
        const auto& preview = kPreviewDefinitions[index];
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
            "PAC_DATA_ROOT",
            projectRoot_.string());
        rememberAndSetEnvironment(
            "PAC_ASSET_ROOT",
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
        const auto found = std::find_if(
            kPreviewDefinitions.begin(),
            kPreviewDefinitions.end(),
            [&](const PreviewDefinition& preview) {
                return requested == preview.id;
            });
        if (found == kPreviewDefinitions.end()) {
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
            environmentPrefabAssets_.size();
    }

    engine::editor::EditorProjectAsset asset(
        std::size_t index) const noexcept override {
        const std::size_t vfxCount =
            vfxPreview_.assetCount();
        if (index < vfxCount) {
            return vfxPreview_.asset(index);
        }
        index -= vfxCount;
        if (index >= environmentPrefabAssets_.size()) {
            return {};
        }
        const auto& asset =
            environmentPrefabAssets_[index];
        return {
            .id = asset.id.c_str(),
            .displayName = asset.displayName.c_str(),
            .typeName = asset.typeName.c_str(),
            .category = asset.category.c_str(),
            .path = asset.path.c_str(),
            .description = asset.description.c_str(),
            .previewable = asset.previewable,
            .sceneInstantiable = true};
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
        const auto found = std::find_if(
            environmentPrefabAssets_.begin(),
            environmentPrefabAssets_.end(),
            [&](const EnvironmentPrefabAsset& asset) {
                return asset.id == assetId;
            });
        if (found == environmentPrefabAssets_.end()) {
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
        return sceneViewReady_
            ? environment_.layoutObjects().size()
            : 0u;
    }

    engine::editor::EditorProjectLayoutObject
    layoutObject(std::size_t index) const noexcept override {
        const auto& objects =
            environment_.layoutObjects();
        if (!sceneViewReady_ ||
            index >= objects.size()) {
            return {};
        }
        const auto& object = objects[index];
        engine::editor::EditorProjectLayoutObject view{
            .stableId = object.stableId.c_str(),
            .displayName = object.displayName.c_str(),
            .typeName =
                object.authored
                ? "Authored Prefab Instance"
                : object.targetKind ==
                        "canonical_terrain_assembly"
                ? "Source Terrain Assembly"
                : object.targetKind ==
                        "canonical_mesh_group"
                ? "Source Mesh Group"
                : object.targetKind ==
                          "gameplay_board_ground_prototype"
                ? "Gameplay Ground Prefab"
                : object.targetKind ==
                          "canonical_tree_instance"
                ? "Tree Prefab Placement"
                : object.targetKind ==
                          "encounter_grass_record"
                ? "Encounter Grass Prefab Placement"
                : "Environment Prefab Placement",
            .coordinateSystem =
                "Source centimetres (XYZ, Y-up)",
            .reason = object.reason.c_str(),
            .targetKind =
                object.targetKind.c_str(),
            .categoryPath =
                object.categoryPath.c_str(),
            .prefabAssetId =
                object.prefabAssetId.c_str(),
            .sourceTranslation =
                object.sourceTranslationCm,
            .sourceRotationDegrees =
                object.sourceRotationDegrees,
            .sourceScale = object.sourceScale,
            .translation = object.translationCm,
            .rotationDegrees =
                object.rotationDegrees,
            .scale = object.scale,
            .boundsMinimum =
                object.boundsMinimumCm,
            .boundsMaximum =
                object.boundsMaximumCm,
            .suppressed = object.suppressed,
            .hasOverride = object.hasOverride};
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

    bool setLayoutObjectOverride(
        const engine::editor::EditorProjectLayoutEdit& edit,
        std::string* outError) override {
        if (!sceneViewReady_ ||
            !edit.stableId) {
            if (outError) {
                *outError =
                    "A mounted Route 1 scene and stable target are required.";
            }
            return false;
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
        selectedLayoutObjectId_ =
            edit.stableId;
        recordSceneEdit(previous);
        layoutEditBaseline_.reset();
        layoutEditStableId_.clear();
        status_ =
            "Route 1 layout override saved and hot-reloaded: " +
            selectedLayoutObjectId_ + ".";
        if (outError) {
            outError->clear();
        }
        return true;
    }

    bool previewLayoutObjectOverride(
        const engine::editor::EditorProjectLayoutEdit& edit,
        std::string* outError) override {
        if (!sceneViewReady_ ||
            !edit.stableId) {
            if (outError) {
                *outError =
                    "A mounted Route 1 scene and stable target are required.";
            }
            return false;
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
        selectedLayoutObjectId_ =
            edit.stableId;
        status_ =
            "Live Route 1 layout edit: " +
            selectedLayoutObjectId_ +
            " (release to autosave).";
        if (outError) {
            outError->clear();
        }
        return true;
    }

    bool commitLayoutObjectOverride(
        const char* stableId,
        std::string* outError) override {
        if (!sceneViewReady_ ||
            !stableId) {
            if (outError) {
                *outError =
                    "A mounted Route 1 scene and stable target are required.";
            }
            return false;
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
        selectedLayoutObjectId_ = stableId;
        if (historyBaseline) {
            recordSceneEdit(*historyBaseline);
        }
        layoutEditBaseline_.reset();
        layoutEditStableId_.clear();
        status_ =
            "Route 1 layout override autosaved: " +
            selectedLayoutObjectId_ + ".";
        if (outError) {
            outError->clear();
        }
        return true;
    }

    void cancelLayoutObjectOverride(
        const char* stableId) override {
        if (layoutEditBaseline_ &&
            (!stableId ||
             layoutEditStableId_ == stableId)) {
            std::string ignored;
            environment_.applyBoardLayout(
                *layoutEditBaseline_,
                &ignored);
        }
        layoutEditBaseline_.reset();
        layoutEditStableId_.clear();
        status_ =
            "Live Route 1 layout edit cancelled.";
    }

    bool resetLayoutObjectOverride(
        const char* stableId,
        std::string* outError) override {
        if (!sceneViewReady_ ||
            !stableId) {
            if (outError) {
                *outError =
                    "A mounted Route 1 scene and stable target are required.";
            }
            return false;
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
        selectedLayoutObjectId_ = stableId;
        recordSceneEdit(previous);
        layoutEditBaseline_.reset();
        layoutEditStableId_.clear();
        status_ =
            "Route 1 layout target restored to canonical source: " +
            selectedLayoutObjectId_ + ".";
        if (outError) {
            outError->clear();
        }
        return true;
    }

    bool duplicateLayoutObject(
        const char* stableId,
        std::string* outCreatedStableId,
        std::string* outError) override {
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
        selectedLayoutObjectId_ = createdStableId;
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
        selectedLayoutObjectId_.clear();
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
        selectedLayoutObjectId_.clear();
        layoutEditBaseline_.reset();
        layoutEditStableId_.clear();
        refreshEnvironmentPrefabAssets();
        if (outError) {
            outError->clear();
        }
        return true;
    }

    bool supportsBoardClearance() const noexcept override {
        return sceneViewReady_ &&
            activeSceneId_ == "routes/route1";
    }

    bool applyBoardClearance(
        const engine::editor::
            EditorProjectBoardClearanceRequest& request,
        engine::editor::
            EditorProjectBoardClearanceResult& outResult,
        std::string* outError) override {
        outResult = {};
        if (!supportsBoardClearance()) {
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
        const auto sourceFromWorld = glm::inverse(
            worldFromSource);
        const float paddingWorld =
            std::max(0.0f, request.paddingCells) *
            boardCellSize_;
        const float halfWidth =
            static_cast<float>(previous.boardCells[0]) *
                boardCellSize_ * 0.5f +
            paddingWorld;
        const float halfDepth =
            static_cast<float>(previous.boardCells[1]) *
                boardCellSize_ * 0.5f +
            paddingWorld;
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
                return maximum.x >= -halfWidth &&
                    minimum.x <= halfWidth &&
                    maximum.z >= -halfDepth &&
                    minimum.z <= halfDepth;
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
            const auto prototype = std::find_if(
                environment_.layoutObjects().begin(),
                environment_.layoutObjects().end(),
                [](const auto& object) {
                    return object.stableId ==
                        kBoardGroundPrototypeStableId;
                });
            if (prototype ==
                environment_.layoutObjects().end()) {
                std::string ignored;
                environment_.applyBoardLayout(
                    previous,
                    &ignored);
                if (outError) {
                    *outError =
                        "The Route 1 board-ground prefab prototype is missing.";
                }
                return false;
            }
            const glm::vec4 sourceCenter =
                sourceFromWorld *
                glm::vec4(
                    0.0f,
                    previous.worldAnchor[1] + 0.005f,
                    0.0f,
                    1.0f);
            const float sourceWidthCm =
                (halfWidth * 2.0f) /
                previous.sourceUnitsToWorld;
            const float sourceDepthCm =
                (halfDepth * 2.0f) /
                previous.sourceUnitsToWorld;
            auto instance = std::find_if(
                next.authoredPrefabInstances.begin(),
                next.authoredPrefabInstances.end(),
                [](const auto& candidate) {
                    return candidate.stableId ==
                        kBoardGroundInstanceStableId;
                });
            const game::runtime::lgpe_route1_runtime::
                AuthoredPrefabInstance ground{
                    .stableId =
                        std::string(
                            kBoardGroundInstanceStableId),
                    .prototypeStableId =
                        std::string(
                            kBoardGroundPrototypeStableId),
                    .displayName =
                        "Autochess Board Ground Infill",
                    .categoryPath =
                        "Environment/Terrain/Gameplay Board",
                    .sourceTranslationCm =
                        prototype->sourceTranslationCm,
                    .sourceRotationDegrees = {},
                    .sourceScale =
                        {1.0f, 1.0f, 1.0f},
                    .translationCm = {
                        sourceCenter.x,
                        sourceCenter.y,
                        sourceCenter.z},
                    .rotationDegrees = {
                        0.0f,
                        -previous.yawDegrees,
                        0.0f},
                    .scale = {
                        sourceWidthCm / 100.0f,
                        1.0f,
                        sourceDepthCm / 100.0f},
                    .suppressed = false,
                    .reason =
                        "autochess_board_ground_infill"};
            if (instance ==
                next.authoredPrefabInstances.end()) {
                next.authoredPrefabInstances.push_back(
                    ground);
            } else {
                *instance = ground;
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
        selectedLayoutObjectId_.clear();
        layoutEditBaseline_.reset();
        layoutEditStableId_.clear();
        refreshEnvironmentPrefabAssets();
        if (outError) {
            outError->clear();
        }
        return true;
    }

    bool resetSceneToSource(
        std::string* outError) override {
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
        selectedLayoutObjectId_.clear();
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
            !saveLayoutManifest(&error)) {
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
            !saveLayoutManifest(&error)) {
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
        selectedLayoutObjectId_ =
            stableId ? stableId : "";
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

    struct EnvironmentPrefabAsset {
        std::string id;
        std::string displayName;
        std::string typeName;
        std::string category;
        std::string path;
        std::string description;
        std::string layoutStableId;
        bool previewable = false;
    };

    void refreshEnvironmentPrefabAssets() {
        environmentPrefabAssets_.clear();
        if (!sceneViewReady_ || projectRoot_.empty()) {
            return;
        }
        const auto& objects = environment_.layoutObjects();
        environmentPrefabAssets_.reserve(objects.size());
        for (const auto& object : objects) {
            if (object.prefabAssetId.empty()) {
                continue;
            }
            const std::size_t separator =
                object.prefabAssetId.find('/');
            if (separator == std::string::npos ||
                separator + 1u >=
                    object.prefabAssetId.size()) {
                continue;
            }
            const std::string stem =
                object.prefabAssetId.substr(separator + 1u);
            const std::filesystem::path prefabPath =
                projectRoot_ /
                "content/phlosion/objects/environment/route1" /
                stem /
                (stem + ".phlo");
            const bool terrain =
                object.targetKind ==
                    "canonical_terrain_assembly";
            const bool imported = !object.authored;
            environmentPrefabAssets_.push_back(
                EnvironmentPrefabAsset{
                    .id =
                        "scene-prefab/" +
                        activeSceneId_ + "/" +
                        object.stableId,
                    .displayName = object.displayName,
                    .typeName =
                        terrain
                        ? "Source Terrain Prefab"
                        : imported
                        ? "Source-bound Prefab"
                        : "Authored Prefab",
                    .category =
                        "Scene Prefabs/" +
                        object.categoryPath,
                    .path = prefabPath.generic_string(),
                    .description =
                        "One-to-one prefab view for scene object " +
                        object.stableId +
                        "; immutable geometry is shared through " +
                        object.prefabAssetId + ".",
                    .layoutStableId = object.stableId,
                    .previewable =
                        std::filesystem::is_regular_file(
                            prefabPath)});
        }
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
        selectedLayoutObjectId_ =
            stableId ? stableId : "";
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
            output <<
                engine::assets::phlosion::
                    serializeAuthoredSceneDocument(
                        environment_.authoredScene());
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
                columns + rows + 30));
        for (int column = 0;
             column <= columns;
             ++column) {
            const float x =
                -halfWidth +
                static_cast<float>(column) *
                    cellSize;
            const bool perimeter =
                column == 0 ||
                column == columns;
            appendProjectedEditorLine(
                context,
                {x, gridY, -halfDepth},
                {x, gridY, halfDepth},
                perimeter ? 1.0f : 0.20f,
                perimeter ? 0.64f : 0.92f,
                perimeter ? 0.18f : 0.68f,
                perimeter ? 0.95f : 0.72f,
                perimeter ? 2.4f : 1.15f,
                lines);
        }
        for (int row = 0;
             row <= rows;
             ++row) {
            const float z =
                -halfDepth +
                static_cast<float>(row) *
                    cellSize;
            const bool perimeter =
                row == 0 ||
                row == rows;
            appendProjectedEditorLine(
                context,
                {-halfWidth, gridY, z},
                {halfWidth, gridY, z},
                perimeter ? 1.0f : 0.20f,
                perimeter ? 0.64f : 0.92f,
                perimeter ? 0.18f : 0.68f,
                perimeter ? 0.95f : 0.72f,
                perimeter ? 2.4f : 1.15f,
                lines);
        }

        const float clearance =
            cellSize * 0.35f;
        const float clearMinX =
            -halfWidth - clearance;
        const float clearMaxX =
            halfWidth + clearance;
        const float clearMinZ =
            -halfDepth - clearance;
        const float clearMaxZ =
            halfDepth + clearance;
        appendProjectedEditorLine(
            context,
            {clearMinX, gridY, clearMinZ},
            {clearMaxX, gridY, clearMinZ},
            1.0f, 0.34f, 0.12f, 0.86f, 1.8f, lines);
        appendProjectedEditorLine(
            context,
            {clearMaxX, gridY, clearMinZ},
            {clearMaxX, gridY, clearMaxZ},
            1.0f, 0.34f, 0.12f, 0.86f, 1.8f, lines);
        appendProjectedEditorLine(
            context,
            {clearMaxX, gridY, clearMaxZ},
            {clearMinX, gridY, clearMaxZ},
            1.0f, 0.34f, 0.12f, 0.86f, 1.8f, lines);
        appendProjectedEditorLine(
            context,
            {clearMinX, gridY, clearMaxZ},
            {clearMinX, gridY, clearMinZ},
            1.0f, 0.34f, 0.12f, 0.86f, 1.8f, lines);

        const auto selected = std::find_if(
            environment_.layoutObjects().begin(),
            environment_.layoutObjects().end(),
            [&](const game::runtime::lgpe_route1_runtime::
                    LayoutObject& object) {
                return object.stableId ==
                    selectedLayoutObjectId_;
            });
        if (selected !=
            environment_.layoutObjects().end()) {
            const auto worldFromSource =
                game::runtime::lgpe_route1_runtime::
                    worldFromSourceMatrix(layout);
            const glm::vec4 world =
                glm::make_mat4(
                    worldFromSource.data()) *
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
            environmentPrefabAssets_.clear();
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
        game::runtime::lgpe_route1_runtime::
            BoardLayoutTransform projectLayout;
        if (!game::runtime::lgpe_route1_runtime::
                loadBoardLayoutTransform(
                    projectStore,
                    game::runtime::lgpe_route1_runtime::
                        kBoardLayoutManifestPath,
                    projectLayout,
                    &error) ||
            !nextEnvironment.applyBoardLayout(
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
    std::vector<EnvironmentPrefabAsset>
        environmentPrefabAssets_;
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
    std::string selectedLayoutObjectId_;
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
    std::string runtimeTitle_;
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
    bool previewFullscreen_ = false;
    bool runtimeRequestedQuit_ = false;
    bool bootReplayActive_ = false;
    bool sceneViewReady_ = false;
    bool layoutProjectionReady_ = false;
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
