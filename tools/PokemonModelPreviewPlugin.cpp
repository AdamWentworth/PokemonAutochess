#include "engine/editor/EditorProjectPlugin.h"
#include "game/editor/PokemonPrefabPreview.h"

#include <filesystem>

namespace {

class ModelPreviewRuntime final : public engine::editor::IEditorProjectRuntime {
public:
    bool open(
        const engine::editor::EditorProjectOpenContext& context,
        std::string* outError) override {
        if (context.projectRoot && *context.projectRoot) {
            const std::filesystem::path projectRoot(context.projectRoot);
            std::filesystem::path resourceRoot = projectRoot;
            if (!std::filesystem::is_regular_file(
                    resourceRoot / "assets/textures/environment/model_preview/source_stage_diffuse.png")) {
                const auto mountedContent =
                    std::filesystem::weakly_canonical(projectRoot / "content/autochess");
                resourceRoot = mountedContent.parent_path().parent_path();
            }
            if (!std::filesystem::is_regular_file(
                    resourceRoot / "assets/textures/environment/model_preview/source_stage_diffuse.png") ||
                !std::filesystem::is_regular_file(
                    resourceRoot / "assets/textures/environment/model_preview/source_stage_specular.png")) {
                if (outError) {
                    *outError = "Model-preview source-stage lighting probes are missing under " +
                        resourceRoot.string();
                }
                return false;
            }
            preview_.setSourceStageProbeRoot(resourceRoot);
        }
        if (outError) outError->clear();
        return true;
    }

    void prewarm(
        IRenderBackend&,
        const engine::editor::EditorProjectCameraContext&) override {}
    void update(float) override {}
    void render(const engine::editor::EditorProjectRenderContext&) override {}
    engine::editor::EditorProjectStats stats() const override { return {}; }
    const char* status() const noexcept override { return "Model preview provider ready."; }

    bool selectAssetPreview(
        const char* assetId,
        const char* assetPath,
        std::string* outError) override {
        return preview_.select(assetId, assetPath, outError);
    }

    engine::editor::EditorProjectAssetPreviewInfo
    assetPreviewInfo() const noexcept override {
        return preview_.info();
    }

    engine::editor::EditorProjectAssetAnimation
    assetPreviewAnimation(std::size_t index) const noexcept override {
        return preview_.animation(index);
    }

    void setAssetPreviewOptions(
        const engine::editor::EditorProjectAssetPreviewOptions& options) override {
        preview_.setOptions(options);
    }

    void updateAssetPreview(float deltaSeconds) override {
        preview_.update(deltaSeconds);
    }

    void renderAssetPreview(
        const engine::editor::EditorProjectRenderContext& context) override {
        preview_.render(context);
    }

private:
    game::editor::PokemonPrefabPreview preview_;
};

engine::editor::IEditorProjectRuntime* createRuntime() {
    return new ModelPreviewRuntime();
}

void destroyRuntime(engine::editor::IEditorProjectRuntime* runtime) {
    delete runtime;
}

const engine::editor::EditorProjectPluginContract contract{
    engine::editor::kEditorProjectPluginAbiVersion,
    sizeof(engine::editor::EditorProjectPluginContract),
    engine::editor::kEditorProjectPluginLayoutFingerprint,
    PHLOSION_EDITOR_BUILD_CONFIGURATION,
    engine::editor::kEditorProjectPluginCompilerAbi,
    &createRuntime,
    &destroyRuntime};

} // namespace

PHLOSION_EDITOR_PROJECT_EXPORT
const engine::editor::EditorProjectPluginContract*
phlosionEditorProjectPluginContract() {
    return &contract;
}
