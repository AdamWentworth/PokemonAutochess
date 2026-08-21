#include "game/editor/PokemonAutochessEditorAssetCatalog.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

bool test_editor_asset_catalog_contract(std::string& outFail) {
    namespace catalog = game::editor::asset_catalog;
    namespace route1 = game::runtime::route1_environment;

    std::vector<route1::LayoutObject> objects;
    objects.push_back(
        route1::LayoutObject{
            .stableId = "terrain/primary",
            .displayName = "Primary Terrain",
            .targetKind = "canonical_terrain_assembly",
            .categoryPath = "Terrain",
            .prefabAssetId = "route1/terrain_primary",
            .authored = false});
    objects.push_back(
        route1::LayoutObject{
            .stableId = "vegetation/tree-01",
            .displayName = "Source Tree",
            .targetKind = "placed_vegetation",
            .categoryPath = "Vegetation/Trees",
            .prefabAssetId = "route1/tree_oak",
            .authored = false});
    objects.push_back(
        route1::LayoutObject{
            .stableId = "authored/bench-01",
            .displayName = "Authored Bench",
            .targetKind = "authored_prefab",
            .categoryPath = "Authored",
            .prefabAssetId = "route1/bench",
            .authored = true});
    objects.push_back(
        route1::LayoutObject{
            .stableId = "invalid/empty",
            .displayName = "No Prefab",
            .prefabAssetId = ""});
    objects.push_back(
        route1::LayoutObject{
            .stableId = "invalid/no-separator",
            .displayName = "Malformed Prefab",
            .prefabAssetId = "malformed"});
    objects.push_back(
        route1::LayoutObject{
            .stableId = "invalid/no-stem",
            .displayName = "Missing Stem",
            .prefabAssetId = "route1/"});

    const std::filesystem::path projectRoot =
        std::filesystem::path("editor-catalog-root");
    catalog::EnvironmentPrefabCatalog environmentCatalog;
    environmentCatalog.rebuild(
        projectRoot,
        "routes/route1",
        objects);

    if (environmentCatalog.size() != 4u) {
        outFail =
            "The environment asset catalog should publish three valid scene prefabs plus the terrain tile set.";
        return false;
    }

    struct Expected {
        std::string_view assetId;
        std::string_view typeName;
        std::string_view category;
        std::string_view pathSuffix;
        std::string_view layoutStableId;
        bool sceneInstantiable;
    };
    const std::vector<Expected> expected{
        {
            "scene-prefab/routes/route1/terrain/primary",
            "Source Terrain Prefab",
            "Scene Prefabs/Terrain",
            "terrain_primary/terrain_primary.phlo",
            "terrain/primary",
            true,
        },
        {
            "scene-prefab/routes/route1/vegetation/tree-01",
            "Source-bound Prefab",
            "Scene Prefabs/Vegetation/Trees",
            "tree_oak/tree_oak.phlo",
            "vegetation/tree-01",
            true,
        },
        {
            "scene-prefab/routes/route1/authored/bench-01",
            "Authored Prefab",
            "Scene Prefabs/Authored",
            "bench/bench.phlo",
            "authored/bench-01",
            true,
        },
        {
            "scene-prefab/routes/route1/terrain-tileset",
            "Terrain Tile Set",
            "Environment Prefabs/Terrain",
            "terrain_tileset/terrain_tileset.phlo",
            "",
            false,
        },
    };

    for (std::size_t index = 0u; index < expected.size(); ++index) {
        const auto& wanted = expected[index];
        const auto* definition = environmentCatalog.find(wanted.assetId);
        if (!definition || definition->typeName != wanted.typeName ||
            definition->category != wanted.category ||
            definition->layoutStableId != wanted.layoutStableId ||
            definition->sceneInstantiable != wanted.sceneInstantiable ||
            !definition->previewable ||
            !std::string_view(definition->path).ends_with(wanted.pathSuffix)) {
            outFail =
                "The environment asset catalog did not preserve the expected record for " +
                std::string(wanted.assetId) + ".";
            return false;
        }

        const auto editorAsset = environmentCatalog.asset(index);
        if (!editorAsset.id || wanted.assetId != editorAsset.id ||
            !editorAsset.displayName || !editorAsset.typeName ||
            !editorAsset.category || !editorAsset.path ||
            !editorAsset.description || !editorAsset.previewable ||
            editorAsset.sceneInstantiable != wanted.sceneInstantiable) {
            outFail =
                "The editor ABI view should mirror each owning environment catalog record.";
            return false;
        }
    }

    if (environmentCatalog.find("not-an-asset") ||
        environmentCatalog.asset(environmentCatalog.size()).id) {
        outFail = "The environment asset catalog should reject unknown IDs and out-of-range indexes.";
        return false;
    }

    environmentCatalog.clear();
    if (environmentCatalog.size() != 0u ||
        environmentCatalog.find(expected.front().assetId)) {
        outFail = "Clearing the environment asset catalog should discard every published record.";
        return false;
    }

    std::ifstream pluginSource("tools/PokemonAutochessEditorProject.cpp");
    const std::string pluginText{
        std::istreambuf_iterator<char>(pluginSource),
        std::istreambuf_iterator<char>()};
    if (pluginText.find("struct EnvironmentPrefabAsset") !=
            std::string::npos ||
        pluginText.find("One-to-one prefab view for scene object") !=
            std::string::npos ||
        pluginText.find("PokemonAutochessEditorAssetCatalog.h") ==
            std::string::npos) {
        outFail =
            "The editor plugin should consume the dedicated asset catalog instead of rebuilding environment records itself.";
        return false;
    }

    return true;
}
