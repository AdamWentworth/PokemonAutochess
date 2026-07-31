#pragma once

#include "engine/editor/EditorProjectPlugin.h"

#include <memory>
#include <string>
#include <vector>

namespace game::preview {
class PokemonAutochessVfxPreviewProject;
}

namespace game::editor {

class PokemonVfxPrefabPreview {
public:
    PokemonVfxPrefabPreview();
    ~PokemonVfxPrefabPreview();

    PokemonVfxPrefabPreview(
        const PokemonVfxPrefabPreview&) = delete;
    PokemonVfxPrefabPreview& operator=(
        const PokemonVfxPrefabPreview&) = delete;

    std::size_t assetCount() const noexcept;
    engine::editor::EditorProjectAsset asset(
        std::size_t index) const noexcept;
    bool owns(
        const char* assetId,
        const char* assetPath) const noexcept;
    bool select(
        const char* assetId,
        const char* assetPath,
        std::string* outError = nullptr);

    engine::editor::EditorProjectAssetPreviewInfo
    info() const noexcept;
    engine::editor::EditorProjectAssetAnimation
    animation(std::size_t index) const noexcept;

    void setOptions(
        const engine::editor::
            EditorProjectAssetPreviewOptions& options);
    void update(float deltaSeconds);
    void render(
        const engine::editor::
            EditorProjectRenderContext& context);

private:
    struct AssetDefinition {
        std::string id;
        std::string displayName;
        std::string path;
        std::string description;
    };

    void rebuildProjectAndReplay(float seekSeconds);
    void replay();
    int findAsset(
        const char* assetId,
        const char* assetPath) const noexcept;

    std::vector<AssetDefinition> assets_;
    std::unique_ptr<
        game::preview::
            PokemonAutochessVfxPreviewProject>
        project_;
    engine::editor::EditorProjectAssetPreviewOptions
        options_;
    std::string status_;
    int activeIndex_ = -1;
    float playheadSeconds_ = 0.0f;
    float emptyCooldownSeconds_ = 0.0f;
};

} // namespace game::editor
