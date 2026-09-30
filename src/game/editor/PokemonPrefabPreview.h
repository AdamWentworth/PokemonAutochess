#pragma once

#include "engine/editor/EditorProjectPlugin.h"

#include <memory>
#include <filesystem>
#include <string>

namespace engine::render {
struct ModelRenderFrame;
struct ModelNodePoseRequest;
struct ModelNodePoseResult;
}

namespace game::editor {

class PokemonPrefabPreview {
public:
    PokemonPrefabPreview();
    ~PokemonPrefabPreview();

    PokemonPrefabPreview(
        const PokemonPrefabPreview&) = delete;
    PokemonPrefabPreview& operator=(
        const PokemonPrefabPreview&) = delete;

    void setSourceStageProbeRoot(const std::filesystem::path& root);

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
    bool renderBattle(const engine::render::ModelRenderFrame& frame,
                      std::string* outError = nullptr);
    bool sampleNodePose(const engine::render::ModelNodePoseRequest& request,
                        engine::render::ModelNodePoseResult& result,
                        std::string* outError = nullptr) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace game::editor
