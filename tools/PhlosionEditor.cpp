#define SDL_MAIN_HANDLED

#include "engine/assets/phlosion/PhlosionSceneArchive.h"
#include "engine/editor/EditorShell.h"
#include "engine/editor/ProjectDescriptor.h"
#include "engine/platform/Window.h"
#include "engine/render/Camera3D.h"
#include "engine/render/OpenGLRenderBackend.h"
#include "game/assets/DevAssetStore.h"
#include "game/runtime/shared/scene/LgpeRoute1RuntimeEnvironment.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"

#include <SDL2/SDL.h>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Arguments {
    std::filesystem::path project = "phlosion.project.json";
    int frameLimit = 0;
};

Arguments parseArguments(int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        if (!argv[index]) {
            continue;
        }
        const std::string argument(argv[index]);
        constexpr std::string_view kProject = "--project=";
        constexpr std::string_view kFrames = "--frames=";
        if (argument.rfind(kProject, 0u) == 0u) {
            result.project = argument.substr(kProject.size());
        } else if (argument.rfind(kFrames, 0u) == 0u) {
            result.frameLimit = std::max(
                1,
                std::stoi(argument.substr(kFrames.size())));
        }
    }
    return result;
}

glm::vec3 horizontalDirection(glm::vec3 direction) {
    direction.y = 0.0f;
    const float length = glm::length(direction);
    if (length <= 0.0001f) {
        return glm::vec3(0.0f, 0.0f, -1.0f);
    }
    return direction / length;
}

} // namespace

int main(int argc, char** argv) {
    const Arguments arguments = parseArguments(argc, argv);
    engine::editor::ProjectDescriptor project;
    std::string error;
    if (!engine::editor::loadProjectDescriptor(
            arguments.project,
            project,
            &error)) {
        std::cerr << "[Phlosion Editor] " << error << '\n';
        return 1;
    }

    std::filesystem::path startupScenePath;
    if (!engine::editor::resolveStartupScenePath(
            arguments.project,
            project,
            startupScenePath,
            &error)) {
        std::cerr << "[Phlosion Editor] " << error << '\n';
        return 1;
    }
    const std::filesystem::path projectRoot =
        std::filesystem::absolute(arguments.project).parent_path();
    std::error_code relativeError;
    const std::filesystem::path startupSceneVirtualPath =
        std::filesystem::relative(
            startupScenePath,
            projectRoot,
            relativeError);
    if (relativeError) {
        std::cerr
            << "[Phlosion Editor] Could not resolve startup scene mount: "
            << relativeError.message() << '\n';
        return 1;
    }

    game::assets::DevAssetStore projectStore(projectRoot.string());
    engine::assets::phlosion::SceneArchiveStore sceneStore;
    if (!sceneStore.load(
            projectStore,
            startupSceneVirtualPath.generic_string(),
            &error)) {
        std::cerr
            << "[Phlosion Editor] Could not mount cooked startup scene: "
            << error
            << "\nRun PhlosionForge cook-route1 before opening the project.\n";
        return 1;
    }

    game::runtime::lgpe_route1_runtime::RuntimeEnvironment environment;
    if (!environment.load(
            sceneStore,
            game::runtime::lgpe_route1_runtime::kCanonicalRoot,
            game::runtime::lgpe_route1_runtime::
                kCompositionManifestPath,
            game::runtime::lgpe_route1_runtime::
                kBoardLayoutManifestPath,
            &error)) {
        std::cerr
            << "[Phlosion Editor] Cooked startup scene rejected: "
            << error << '\n';
        return 1;
    }

    int result = 0;
    try {
        Window window(
            project.displayName + " - Phlosion Editor",
            1600,
            900,
            Window::GraphicsApi::OpenGL,
            true);
        if (!gladLoadGLLoader(
                reinterpret_cast<GLADloadproc>(
                    SDL_GL_GetProcAddress))) {
            throw std::runtime_error("Failed to initialize GLAD.");
        }

        int width = 0;
        int height = 0;
        window.getDrawableSize(width, height);
        width = std::max(width, 1);
        height = std::max(height, 1);
        glViewport(0, 0, width, height);

        OpenGLRenderBackend renderer;
        renderer.onResize(width, height);
        renderer.prewarmWorldRenderAssets();

        Camera3D camera(
            35.0f,
            static_cast<float>(width) /
                static_cast<float>(height),
            0.1f,
            200.0f);

        engine::editor::EditorShell editor;
        if (!editor.initialize(window.getSDLWindow(), &error)) {
            throw std::runtime_error(error);
        }

        std::vector<
            game::runtime::shared_world_batches::WorldIndexedBatch>
            batches;
        environment.appendIndexedBatches(0.0f, batches);
        const glm::vec3 initialPosition = camera.getPosition();
        const glm::vec3 initialForward = camera.getDirection();
        const glm::vec3 initialTarget = camera.getTarget();
        game::runtime::shared_world_batches::
            prewarmWorldIndexedBatches(
                renderer,
                batches,
                glm::value_ptr(initialPosition),
                glm::value_ptr(initialForward),
                glm::value_ptr(initialTarget));

        const auto& stats = environment.stats();
        const std::string projectRootText =
            projectRoot.generic_string();
        const std::string scenePathText =
            startupScenePath.generic_string();
        const std::string status =
            "Mounted strict cooked Route 1 scene through PHSC; "
            "no source-cache fallback is active.";
        const engine::editor::WorkspaceView workspace{
            .projectName = project.displayName,
            .projectId = project.projectId,
            .projectRoot = projectRootText,
            .sceneAssetId = project.startupScene.assetId,
            .scenePath = scenePathText,
            .backendName = "OpenGL 3.3 / shared world renderer",
            .status = status,
            .sceneCount = stats.sceneCount,
            .materialCount = stats.materialCount,
            .drawClassCount = stats.drawClassCount,
            .encounterGrassInstanceCount =
                stats.encounterGrassInstanceCount,
            .vegetationInstanceCount =
                stats.placedVegetationInstanceCount,
            .visibleTriangleCount = stats.visibleTriangleCount,
            .shadowTriangleCount = stats.shadowTriangleCount,
            .archiveFileCount = sceneStore.fileCount()};

        bool running = true;
        int frameCount = 0;
        float simulationSeconds = 0.0f;
        using Clock = std::chrono::steady_clock;
        auto previous = Clock::now();

        while (running) {
            const auto now = Clock::now();
            const float deltaSeconds = std::min(
                0.05f,
                std::chrono::duration<float>(now - previous).count());
            previous = now;
            simulationSeconds += deltaSeconds;

            float wheelDelta = 0.0f;
            glm::vec2 panPixels(0.0f);
            glm::vec2 orbitRadians(0.0f);
            SDL_Event event{};
            while (SDL_PollEvent(&event)) {
                editor.processEvent(event);
                if (event.type == SDL_QUIT) {
                    running = false;
                } else if (
                    event.type == SDL_KEYDOWN &&
                    event.key.keysym.sym == SDLK_ESCAPE &&
                    !editor.wantsKeyboardCapture()) {
                    running = false;
                } else if (
                    event.type == SDL_WINDOWEVENT &&
                    (event.window.event ==
                         SDL_WINDOWEVENT_SIZE_CHANGED ||
                     event.window.event ==
                         SDL_WINDOWEVENT_RESIZED)) {
                    window.getDrawableSize(width, height);
                    width = std::max(width, 1);
                    height = std::max(height, 1);
                    glViewport(0, 0, width, height);
                    renderer.onResize(width, height);
                    camera.setAspectRatio(
                        static_cast<float>(width) /
                        static_cast<float>(height));
                } else if (
                    event.type == SDL_MOUSEWHEEL &&
                    !editor.wantsMouseCapture()) {
                    wheelDelta +=
                        static_cast<float>(event.wheel.y);
                } else if (
                    event.type == SDL_MOUSEMOTION &&
                    !editor.wantsMouseCapture()) {
                    const bool shift =
                        (SDL_GetModState() & KMOD_SHIFT) != 0;
                    const bool left =
                        (event.motion.state &
                         SDL_BUTTON_LMASK) != 0u;
                    const bool middle =
                        (event.motion.state &
                         SDL_BUTTON_MMASK) != 0u;
                    const bool right =
                        (event.motion.state &
                         SDL_BUTTON_RMASK) != 0u;
                    if (left || (middle && shift)) {
                        panPixels += glm::vec2(
                            static_cast<float>(event.motion.xrel),
                            static_cast<float>(event.motion.yrel));
                    } else if (right || middle) {
                        orbitRadians +=
                            glm::vec2(
                                -static_cast<float>(
                                    event.motion.xrel),
                                -static_cast<float>(
                                    event.motion.yrel)) *
                            0.006f;
                    }
                }
            }
            if (!running) {
                break;
            }

            editor.beginFrame(deltaSeconds);
            if (!editor.wantsKeyboardCapture()) {
                const Uint8* keys =
                    SDL_GetKeyboardState(nullptr);
                const glm::vec3 forward =
                    horizontalDirection(camera.getDirection());
                const glm::vec3 right =
                    glm::normalize(glm::cross(
                        forward,
                        glm::vec3(0.0f, 1.0f, 0.0f)));
                glm::vec3 translation(0.0f);
                if (keys[SDL_SCANCODE_W]) translation += forward;
                if (keys[SDL_SCANCODE_S]) translation -= forward;
                if (keys[SDL_SCANCODE_D]) translation += right;
                if (keys[SDL_SCANCODE_A]) translation -= right;
                if (keys[SDL_SCANCODE_E]) translation.y += 1.0f;
                if (keys[SDL_SCANCODE_Q]) translation.y -= 1.0f;
                if (glm::length(translation) > 0.001f) {
                    const float speed =
                        (keys[SDL_SCANCODE_LSHIFT] ||
                         keys[SDL_SCANCODE_RSHIFT])
                            ? 12.0f
                            : 5.0f;
                    camera.move(
                        glm::normalize(translation) *
                        speed * deltaSeconds);
                }
            }
            if (glm::length(panPixels) > 0.001f) {
                camera.panPlanar(
                    panPixels.x,
                    panPixels.y,
                    0.012f);
            }
            if (glm::length(orbitRadians) > 0.0001f) {
                camera.orbit(
                    orbitRadians.x,
                    orbitRadians.y);
            }
            if (wheelDelta != 0.0f) {
                camera.zoom(wheelDelta * 1.25f);
            }

            environment.updateAnimation(simulationSeconds);
            batches.clear();
            environment.appendIndexedBatches(
                simulationSeconds,
                batches);

            const glm::mat4 viewProjection =
                camera.getProjectionMatrix() *
                camera.getViewMatrix();
            const glm::vec3 cameraPosition =
                camera.getPosition();
            const glm::vec3 cameraForward =
                camera.getDirection();
            const glm::vec3 cameraTarget =
                camera.getTarget();

            renderer.beginFrame(0.035f, 0.045f, 0.040f, 1.0f);
            renderer.beginWorldSceneColorPass(width, height);
            game::runtime::shared_world_batches::
                submitWorldIndexedBatches(
                    renderer,
                    batches,
                    glm::value_ptr(viewProjection),
                    width,
                    height,
                    glm::value_ptr(cameraPosition),
                    glm::value_ptr(cameraForward),
                    glm::value_ptr(cameraTarget));
            renderer.endWorldSceneColorPass();

            editor.drawWorkspace(workspace);
            editor.render();
            renderer.endFrame();
            window.swapBuffers();

            ++frameCount;
            if (arguments.frameLimit > 0 &&
                frameCount >= arguments.frameLimit) {
                running = false;
            }
        }

        editor.shutdown();
        renderer.shutdown();
    } catch (const std::exception& exception) {
        std::cerr
            << "[Phlosion Editor] "
            << exception.what() << '\n';
        result = 1;
    }
    if (SDL_WasInit(SDL_INIT_EVERYTHING) != 0) {
        SDL_Quit();
    }
    return result;
}
