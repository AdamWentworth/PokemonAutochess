#include "engine/editor/EditorProjectPlugin.h"
#include "engine/render/ModelRenderPlugin.h"
#include "game/editor/PokemonPrefabPreview.h"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

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

struct BattlePokemonRuntime {
    std::unordered_map<std::string,
        std::unique_ptr<game::editor::PokemonPrefabPreview>> previews;
    std::string error;
};

bool renderBattlePokemon(void* context,
    const engine::render::ModelRenderFrame* frame) noexcept {
    auto& runtime = *static_cast<BattlePokemonRuntime*>(context);
    try {
        if (!frame || !frame->assetId || !frame->phloPath ||
            !frame->animation) {
            runtime.error = "Incomplete battle Pokemon frame";
            return false;
        }
        const std::string id(frame->assetId);
        auto& preview = runtime.previews[id];
        if (!preview) {
            preview = std::make_unique<game::editor::PokemonPrefabPreview>();
            if (!preview->select(frame->assetId, frame->phloPath,
                                 &runtime.error)) {
                preview.reset();
                return false;
            }
        }
        return preview->renderBattle(*frame, &runtime.error);
    } catch (const std::exception& exception) {
        runtime.error = exception.what();
        return false;
    } catch (...) {
        runtime.error = "Unknown battle Pokemon rendering error";
        return false;
    }
}

const char* battlePokemonLastError(void* context) noexcept {
    return static_cast<BattlePokemonRuntime*>(context)->error.c_str();
}

bool sampleBattlePokemonNode(void* context,
    const engine::render::ModelNodePoseRequest* request,
    engine::render::ModelNodePoseResult* result) noexcept {
    if (!context) return false;
    auto& runtime = *static_cast<BattlePokemonRuntime*>(context);
    try {
        if (!request || !result || !request->assetId || !*request->assetId ||
            !request->phloPath || !*request->phloPath) {
            runtime.error = "Incomplete model node pose request";
            return false;
        }
        auto& preview = runtime.previews[request->assetId];
        if (!preview) {
            preview = std::make_unique<game::editor::PokemonPrefabPreview>();
            if (!preview->select(request->assetId, request->phloPath, &runtime.error)) {
                preview.reset();
                return false;
            }
        }
        return preview->sampleNodePose(*request, *result, &runtime.error);
    } catch (const std::exception& exception) {
        runtime.error = exception.what();
        return false;
    } catch (...) {
        runtime.error = "Unknown model node pose sampling error";
        return false;
    }
}

void destroyBattlePokemon(void* context) noexcept {
    delete static_cast<BattlePokemonRuntime*>(context);
}

} // namespace

PHLOSION_EDITOR_PROJECT_EXPORT
const engine::editor::EditorProjectPluginContract*
phlosionEditorProjectPluginContract() {
    return &contract;
}

PHLOSION_EDITOR_PROJECT_EXPORT bool phlosionModelRenderPluginCreate(
    std::uint32_t abi,
    engine::render::ModelRenderPluginApi* out) noexcept {
    if (abi != engine::render::modelRenderPluginAbi || !out) return false;
    try {
        *out = {engine::render::modelRenderPluginAbi,
                sizeof(engine::render::ModelRenderPluginApi),
                new BattlePokemonRuntime(),
                &renderBattlePokemon,
                &battlePokemonLastError,
                &destroyBattlePokemon,
                &sampleBattlePokemonNode};
        return true;
    } catch (...) {
        return false;
    }
}
