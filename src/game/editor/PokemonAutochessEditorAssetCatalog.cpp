#include "game/editor/PokemonAutochessEditorAssetCatalog.h"

#include <algorithm>

namespace game::editor::asset_catalog {

void EnvironmentPrefabCatalog::clear() noexcept {
    definitions_.clear();
}

void EnvironmentPrefabCatalog::rebuild(
    const std::filesystem::path& projectRoot,
    std::string_view sceneId,
    const std::vector<
        game::runtime::lgpe_route1_runtime::LayoutObject>& objects) {
    definitions_.clear();
    definitions_.reserve(objects.size() + 1u);

    for (const auto& object : objects) {
        if (object.prefabAssetId.empty()) {
            continue;
        }
        const std::size_t separator = object.prefabAssetId.find('/');
        if (separator == std::string::npos ||
            separator + 1u >= object.prefabAssetId.size()) {
            continue;
        }

        const std::string stem =
            object.prefabAssetId.substr(separator + 1u);
        const std::filesystem::path prefabPath =
            projectRoot /
            "content/phlosion/objects/environment/route1" /
            stem /
            (stem + ".phlo");
        const bool terrain =
            object.targetKind == "canonical_terrain_assembly";
        const bool imported = !object.authored;

        definitions_.push_back(
            EnvironmentPrefabDefinition{
                .id =
                    "scene-prefab/" + std::string(sceneId) + "/" +
                    object.stableId,
                .displayName = object.displayName,
                .typeName =
                    terrain
                    ? "Source Terrain Prefab"
                    : imported
                    ? "Source-bound Prefab"
                    : "Authored Prefab",
                .category = "Scene Prefabs/" + object.categoryPath,
                .path = prefabPath.generic_string(),
                .description =
                    "One-to-one prefab view for scene object " +
                    object.stableId +
                    "; immutable geometry is shared through " +
                    object.prefabAssetId + ".",
                .layoutStableId = object.stableId,
                .previewable = true});
    }

    const std::filesystem::path tileSetPath =
        projectRoot /
        "content/phlosion/objects/environment/route1/terrain_tileset/terrain_tileset.phlo";
    definitions_.push_back(
        EnvironmentPrefabDefinition{
            .id = "scene-prefab/routes/route1/terrain-tileset",
            .displayName = "Route 1 Terrain Tile Set",
            .typeName = "Terrain Tile Set",
            .category = "Environment Prefabs/Terrain",
            .path = tileSetPath.generic_string(),
            .description =
                "One-metre Route 1 lawn cells with half-metre elevation steps; ramps and ledge seams are derived from neighboring cells.",
            .layoutStableId = {},
            .previewable = true,
            .sceneInstantiable = false});
}

std::size_t EnvironmentPrefabCatalog::size() const noexcept {
    return definitions_.size();
}

engine::editor::EditorProjectAsset
EnvironmentPrefabCatalog::asset(
    std::size_t index) const noexcept {
    if (index >= definitions_.size()) {
        return {};
    }
    const auto& definition = definitions_[index];
    return {
        .id = definition.id.c_str(),
        .displayName = definition.displayName.c_str(),
        .typeName = definition.typeName.c_str(),
        .category = definition.category.c_str(),
        .path = definition.path.c_str(),
        .description = definition.description.c_str(),
        .previewable = definition.previewable,
        .sceneInstantiable = definition.sceneInstantiable};
}

const EnvironmentPrefabDefinition*
EnvironmentPrefabCatalog::find(
    std::string_view assetId) const noexcept {
    const auto found = std::find_if(
        definitions_.begin(),
        definitions_.end(),
        [assetId](const EnvironmentPrefabDefinition& definition) {
            return assetId == definition.id;
        });
    return found == definitions_.end() ? nullptr : &*found;
}

} // namespace game::editor::asset_catalog
