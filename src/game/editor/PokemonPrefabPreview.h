#pragma once

#include "engine/editor/EditorProjectPlugin.h"

#include <memory>
#include <string>

namespace game::editor {

class PokemonPrefabPreview {
public:
    PokemonPrefabPreview();
    ~PokemonPrefabPreview();

    PokemonPrefabPreview(
        const PokemonPrefabPreview&) = delete;
    PokemonPrefabPreview& operator=(
        const PokemonPrefabPreview&) = delete;

    bool select(
        const char* assetId,
        const char* phloPath,
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
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace game::editor
