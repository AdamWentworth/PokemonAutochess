#include "engine/assets/phlosion/PhlosionSceneArchive.h"
#include "engine/core/EngineServices.h"
#include "engine/core/Environment.h"
#include "engine/core/GameContext.h"
#include "engine/editor/EditorProjectPlugin.h"
#include "engine/events/EventBus.h"
#include "engine/render/Camera3D.h"
#include "engine/utils/ResourceManager.h"
#include "engine/utils/ShaderCache.h"
#include "game/assets/DevAssetStore.h"
#include "game/runtime/GameRuntime.h"
#include "game/runtime/RuntimeBootLoading.h"
#include "game/runtime/video/VideoPreferences.h"
#include "game/runtime/shared/scene/LgpeRoute1RuntimeEnvironment.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"

#include <SDL2/SDL_ttf.h>
#include <glad/glad.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct PreviewDefinition {
    const char* id;
    const char* displayName;
    const char* group;
    const char* description;
    const char* state;
    const char* gameMode;
    const char* snapshot;
};

constexpr std::array<PreviewDefinition, 18>
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
        },
        {
            "route1-planning-adventure",
            "Route 1 Planning - Adventure",
            "Route 1",
            "Restore Route 1 in its Adventure planning phase.",
            "snapshot",
            "adventure",
            "config/debug/editor_route1_planning.json",
        },
        {
            "route1-battle-classic",
            "Route 1 Battle - Classic",
            "Route 1",
            "Restore the deterministic Route 1 battle in Classic mode.",
            "snapshot",
            "classic",
            "config/debug/debug_state_snapshot_bulbasaur_route1_combat.json",
        },
        {
            "route1-battle-adventure",
            "Route 1 Battle - Adventure",
            "Route 1",
            "Restore the deterministic Route 1 battle in Adventure mode.",
            "snapshot",
            "adventure",
            "config/debug/debug_state_snapshot_bulbasaur_route1_combat.json",
        },
        {
            "route1-5-classic",
            "Route 1.5 - Classic",
            "Route 1.5",
            "Open the scripted Route 1.5 runtime stage in Classic mode.",
            "route",
            "classic",
            "scripts/states/route1_5.lua",
        },
        {
            "route1-5-adventure",
            "Route 1.5 - Adventure",
            "Route 1.5",
            "Open the scripted Route 1.5 runtime stage in Adventure mode.",
            "route",
            "adventure",
            "scripts/states/route1_5.lua",
        },
        {
            "route22-classic",
            "Route 22 - Classic",
            "Route 22",
            "Open the procedural Route 22 foothills stage in Classic mode.",
            "route",
            "classic",
            "scripts/states/route22.lua",
        },
        {
            "route22-adventure",
            "Route 22 - Adventure",
            "Route 22",
            "Open the procedural Route 22 foothills stage in Adventure mode.",
            "route",
            "adventure",
            "scripts/states/route22.lua",
        },
        {
            "route2-classic",
            "Route 2 - Classic",
            "Route 2",
            "Open the procedural Route 2 forest-edge stage in Classic mode.",
            "route",
            "classic",
            "scripts/states/route2.lua",
        },
        {
            "route2-adventure",
            "Route 2 - Adventure",
            "Route 2",
            "Open the procedural Route 2 forest-edge stage in Adventure mode.",
            "route",
            "adventure",
            "scripts/states/route2.lua",
        },
        {
            "viridian-forest-classic",
            "Viridian Forest - Classic",
            "Viridian Forest",
            "Open the procedural Viridian Forest shrine stage in Classic mode.",
            "route",
            "classic",
            "scripts/states/viridian_forest.lua",
        },
        {
            "viridian-forest-adventure",
            "Viridian Forest - Adventure",
            "Viridian Forest",
            "Open the procedural Viridian Forest shrine stage in Adventure mode.",
            "route",
            "adventure",
            "scripts/states/viridian_forest.lua",
        },
        {
            "route3-classic",
            "Route 3 - Classic",
            "Route 3",
            "Open the procedural Route 3 mountain-pass stage in Classic mode.",
            "route",
            "classic",
            "scripts/states/route3.lua",
        },
        {
            "route3-adventure",
            "Route 3 - Adventure",
            "Route 3",
            "Open the procedural Route 3 mountain-pass stage in Adventure mode.",
            "route",
            "adventure",
            "scripts/states/route3.lua",
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
        const std::filesystem::path startupScenePath(
            context.startupScenePath);
        std::error_code relativeError;
        const std::filesystem::path startupSceneVirtualPath =
            std::filesystem::relative(
                startupScenePath,
                projectRoot,
                relativeError);
        if (relativeError) {
            if (outError) {
                *outError =
                    "Could not resolve the startup scene inside the project: " +
                    relativeError.message();
            }
            return false;
        }

        game::assets::DevAssetStore projectStore(
            projectRoot.string());
        std::string error;
        if (!sceneStore_.load(
                projectStore,
                startupSceneVirtualPath.generic_string(),
                &error)) {
            if (outError) {
                *outError =
                    "Could not mount the cooked startup scene: " +
                    error +
                    "\nRun PhlosionForge cook-route1 in PokemonAutochess before opening the project.";
            }
            return false;
        }
        if (!environment_.load(
                sceneStore_,
                game::runtime::lgpe_route1_runtime::
                    kCanonicalRoot,
                game::runtime::lgpe_route1_runtime::
                    kCompositionManifestPath,
                game::runtime::lgpe_route1_runtime::
                    kBoardLayoutManifestPath,
                &error)) {
            if (outError) {
                *outError =
                    "Cooked Route 1 startup scene was rejected: " +
                    error;
            }
            return false;
        }
        if (outError) {
            outError->clear();
        }
        return true;
    }

    void prewarm(
        IRenderBackend& renderer,
        const engine::editor::EditorProjectCameraContext&
            camera) override {
        batches_.clear();
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
        environment_.updateAnimation(simulationSeconds);
    }

    void render(
        const engine::editor::EditorProjectRenderContext&
            context) override {
        if (!context.renderer ||
            !context.viewProjectionMatrix4x4) {
            return;
        }
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
    }

    engine::editor::EditorProjectStats stats() const override {
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
        if (gladLoadGLLoader(
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

private:
    struct SavedEnvironment {
        std::string name;
        std::optional<std::string> previous;
    };

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
    IRenderBackend* renderer_ = nullptr;
    Camera3D* gameCamera_ = nullptr;
    std::vector<SavedEnvironment> savedEnvironment_;
    std::string activePreviewId_ = "main-menu";
    std::string runtimeTitle_;
    std::string status_ =
        "Mounted strict cooked Route 1 scene through PHSC; "
        "no source-cache fallback is active.";
    float simulationSeconds_ = 0.0f;
    float latestBootProgress_ = 0.0f;
    float bootReplaySeconds_ = 0.0f;
    int previewWidth_ = 1280;
    int previewHeight_ = 720;
    bool previewFullscreen_ = false;
    bool runtimeRequestedQuit_ = false;
    bool bootReplayActive_ = false;
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
