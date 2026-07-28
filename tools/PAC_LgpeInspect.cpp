#include "engine/assets/lgpe/LgpeCanonicalScene.h"
#include "engine/core/Paths.h"
#include "game/assets/DevAssetStore.h"
#include "game/runtime/shared/scene/LgpeWorldSceneAdapter.h"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    const std::string virtualRoot =
        (argc >= 2 && argv[1] && argv[1][0] != '\0')
            ? argv[1]
            : "cache/lgpe/route1";

    game::assets::DevAssetStore store(engine::paths::dataRoot());
    engine::assets::lgpe::CanonicalScene scene;
    std::string error;
    if (!engine::assets::lgpe::loadCanonicalScene(
            store,
            virtualRoot,
            scene,
            &error)) {
        std::cerr << "[PAC_LgpeInspect] FAIL root=" << virtualRoot
                  << " error=" << error << '\n';
        return 1;
    }

    std::size_t polygonGroupCount = 0u;
    std::size_t textureSubresourceCount = 0u;
    std::size_t skipMainRenderingMaterialCount = 0u;
    for (const auto& mesh : scene.meshes) {
        polygonGroupCount += mesh.polygonGroups.size();
    }
    for (const auto& texture : scene.textures) {
        textureSubresourceCount += texture.subresources.size();
    }
    for (const auto& material : scene.materials) {
        if (material.skipMainRendering) {
            ++skipMainRenderingMaterialCount;
        }
    }

    game::runtime::lgpe_world_scene::PreparedScene worldScene;
    if (!game::runtime::lgpe_world_scene::prepareCanonicalScene(
            scene,
            worldScene,
            &error)) {
        std::cerr << "[PAC_LgpeInspect] FAIL root=" << virtualRoot
                  << " world_scene_error=" << error << '\n';
        return 1;
    }
    std::size_t preparedAuthoredMipLevelCount = 0u;
    std::size_t maxPreparedAuthoredMipLevelCount = 0u;
    for (const auto& texture : worldScene.textureStorage) {
        preparedAuthoredMipLevelCount += texture.mipLevels.size();
        maxPreparedAuthoredMipLevelCount =
            std::max(maxPreparedAuthoredMipLevelCount, texture.mipLevels.size());
    }

    std::cout
        << "[PAC_LgpeInspect] PASS"
        << " profile=" << scene.profileId
        << " meshes=" << scene.meshes.size()
        << " polygon_groups=" << polygonGroupCount
        << " materials=" << scene.materials.size()
        << " skip_main_materials=" << skipMainRenderingMaterialCount
        << " bones=" << scene.bones.size()
        << " triangle_records=" << scene.triangleRecordCount
        << " unique_triangles="
        << scene.uniqueMaterialIndexedTriangleCount
        << " duplicate_records="
        << scene.duplicateMaterialIndexedTriangleRecordCount
        << " textures=" << scene.textures.size()
        << " texture_subresources=" << textureSubresourceCount
        << " world_authored_mip_levels=" << preparedAuthoredMipLevelCount
        << " world_max_texture_mips=" << maxPreparedAuthoredMipLevelCount
        << " world_geometries=" << worldScene.registry.geometries.size()
        << " world_materials=" << worldScene.registry.materials.size()
        << " world_main_groups="
        << worldScene.stats.mainPassPolygonGroupCount
        << " world_skipped_main_groups="
        << worldScene.stats.skippedMainPassPolygonGroupCount
        << " world_main_triangles="
        << worldScene.stats.mainPassTriangleCount
        << " world_skipped_main_triangles="
        << worldScene.stats.skippedMainPassTriangleCount
        << " world_preview_textures="
        << worldScene.stats.materialWithPreviewTextureCount
        << " world_field_ground_surfaces="
        << worldScene.stats.fieldGroundSurfaceMaterialCount
        << " world_field_cliff_surfaces="
        << worldScene.stats.fieldCliffSurfaceMaterialCount
        << " world_field_tree02_surfaces="
        << worldScene.stats.fieldTree02SurfaceMaterialCount
        << " world_field_tree04_surfaces="
        << worldScene.stats.fieldTree04SurfaceMaterialCount
        << " world_field_tree05_surfaces="
        << worldScene.stats.fieldTree05SurfaceMaterialCount
        << " world_field_object_tree_miki_surfaces="
        << worldScene.stats.fieldObjectTreeMikiSurfaceMaterialCount
        << " world_source_texture_bindings="
        << worldScene.stats.sourceTextureBindingCount
        << " world_uv1_meshes=" << worldScene.stats.texCoord1MeshCount
        << " world_uv2_meshes=" << worldScene.stats.texCoord2MeshCount
        << " world_uv3_meshes=" << worldScene.stats.texCoord3MeshCount
        << " world_color1_meshes=" << worldScene.stats.color1MeshCount
        << " world_color2_meshes=" << worldScene.stats.color2MeshCount
        << " world_color3_meshes=" << worldScene.stats.color3MeshCount
        << '\n';
    return 0;
}
