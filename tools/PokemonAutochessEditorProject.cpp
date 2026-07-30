#include "engine/assets/phlosion/PhlosionSceneArchive.h"
#include "engine/editor/EditorProjectPlugin.h"
#include "game/assets/DevAssetStore.h"
#include "game/runtime/shared/scene/LgpeRoute1RuntimeEnvironment.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"

#include <filesystem>
#include <string>
#include <vector>

namespace {

class PokemonAutochessEditorProject final
    : public engine::editor::IEditorProjectRuntime {
public:
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
        return
            "Mounted strict cooked Route 1 scene through PHSC; "
            "no source-cache fallback is active.";
    }

private:
    engine::assets::phlosion::SceneArchiveStore sceneStore_;
    game::runtime::lgpe_route1_runtime::RuntimeEnvironment
        environment_;
    std::vector<
        game::runtime::shared_world_batches::WorldIndexedBatch>
        batches_;
    float simulationSeconds_ = 0.0f;
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
