#include "PokemonPrefabPreview.h"
#include "engine/editor/ModelPreview.h"
#include "engine/core/Paths.h"
#include "pokemon/models/ModelProfile.h"
#include "pokemon/models/SourceLighting.h"
namespace game::editor {
struct PokemonPrefabPreview::Impl {
    std::unique_ptr<engine::editor::ModelPreview> preview;
    void setRoot(const std::filesystem::path& root) {
        preview=std::make_unique<engine::editor::ModelPreview>(
            std::make_shared<pokemon::models::ModelProfile>(),
            pokemon::models::sourceLighting(root/"assets/textures/environment/model_preview"));
    }
};
PokemonPrefabPreview::PokemonPrefabPreview() : impl_(std::make_unique<Impl>()) {
    impl_->setRoot(engine::paths::data(""));
}
PokemonPrefabPreview::~PokemonPrefabPreview() = default;
void PokemonPrefabPreview::setSourceStageProbeRoot(const std::filesystem::path& root) { impl_->setRoot(root); }
bool PokemonPrefabPreview::select(const char* id,const char* path,std::string* error) { return impl_->preview->select(id,path,error); }
engine::editor::EditorProjectAssetPreviewInfo PokemonPrefabPreview::info() const noexcept { return impl_->preview->info(); }
engine::editor::EditorProjectAssetAnimation PokemonPrefabPreview::animation(std::size_t index) const noexcept { return impl_->preview->animation(index); }
void PokemonPrefabPreview::setOptions(const engine::editor::EditorProjectAssetPreviewOptions& options) { impl_->preview->setOptions(options); }
void PokemonPrefabPreview::update(float seconds) { impl_->preview->update(seconds); }
void PokemonPrefabPreview::render(const engine::editor::EditorProjectRenderContext& context) { impl_->preview->render(context); }
}
