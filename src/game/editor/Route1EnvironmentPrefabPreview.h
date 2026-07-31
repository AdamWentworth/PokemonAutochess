#pragma once

#include "engine/editor/EditorProjectPlugin.h"

#include <memory>
#include <string>

namespace game::editor {

class Route1EnvironmentPrefabPreview {
public:
    Route1EnvironmentPrefabPreview();
    ~Route1EnvironmentPrefabPreview();

    Route1EnvironmentPrefabPreview(
        const Route1EnvironmentPrefabPreview&) = delete;
    Route1EnvironmentPrefabPreview& operator=(
        const Route1EnvironmentPrefabPreview&) = delete;

    bool owns(
        const char* assetId,
        const char* assetPath) const;

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
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace game::editor
