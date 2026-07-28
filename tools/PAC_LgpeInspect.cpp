#include "engine/assets/lgpe/LgpeCanonicalScene.h"
#include "engine/core/Paths.h"
#include "game/assets/DevAssetStore.h"

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
        << '\n';
    return 0;
}
