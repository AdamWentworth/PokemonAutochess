#include "engine/assets/lgpe/LgpeCanonicalScene.h"
#include "engine/core/Paths.h"
#include "game/assets/DevAssetStore.h"
#include "game/runtime/shared/scene/LgpeWorldSceneAdapter.h"
#include "game/runtime/shared/scene/LgpeRoute1TerrainAssemblies.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace {

struct DisjointSet {
    explicit DisjointSet(std::size_t size)
        : parent(size), rank(size, 0u) {
        std::iota(parent.begin(), parent.end(), 0u);
    }

    std::size_t find(std::size_t value) {
        if (parent[value] != value) {
            parent[value] = find(parent[value]);
        }
        return parent[value];
    }

    void unite(std::size_t left, std::size_t right) {
        left = find(left);
        right = find(right);
        if (left == right) {
            return;
        }
        if (rank[left] < rank[right]) {
            std::swap(left, right);
        }
        parent[right] = left;
        if (rank[left] == rank[right]) {
            ++rank[left];
        }
    }

    std::vector<std::size_t> parent;
    std::vector<std::uint8_t> rank;
};

using PositionKey = std::tuple<std::int32_t, std::int32_t, std::int32_t>;

PositionKey positionKey(
    const engine::assets::lgpe::CanonicalVertex& vertex) {
    constexpr float kQuantization = 1000.0f;
    return {
        static_cast<std::int32_t>(
            std::lround(vertex.position[0] * kQuantization)),
        static_cast<std::int32_t>(
            std::lround(vertex.position[1] * kQuantization)),
        static_cast<std::int32_t>(
            std::lround(vertex.position[2] * kQuantization))};
}

void printTerrainComponents(
    const engine::assets::lgpe::CanonicalScene& scene) {
    for (const auto& mesh : scene.meshes) {
        if (mesh.sourceIndex < 29u || mesh.sourceIndex > 35u) {
            continue;
        }
        DisjointSet sets(mesh.vertices.size());
        std::map<PositionKey, std::size_t> firstAtPosition;
        for (std::size_t index = 0u;
             index < mesh.vertices.size();
             ++index) {
            const auto [found, inserted] =
                firstAtPosition.emplace(
                    positionKey(mesh.vertices[index]),
                    index);
            if (!inserted) {
                sets.unite(index, found->second);
            }
        }
        for (const auto& group : mesh.polygonGroups) {
            for (std::size_t index = 0u;
                 index + 2u < group.indices.size();
                 index += 3u) {
                sets.unite(group.indices[index], group.indices[index + 1u]);
                sets.unite(group.indices[index], group.indices[index + 2u]);
            }
        }
        struct Component {
            std::array<float, 3> minimum{
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max()};
            std::array<float, 3> maximum{
                std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest()};
            std::set<std::uint32_t> materials;
            std::size_t triangles = 0u;
        };
        std::map<std::size_t, Component> components;
        for (const auto& group : mesh.polygonGroups) {
            for (std::size_t index = 0u;
                 index + 2u < group.indices.size();
                 index += 3u) {
                auto& component =
                    components[sets.find(group.indices[index])];
                ++component.triangles;
                component.materials.insert(group.materialIndex);
                for (std::size_t corner = 0u; corner < 3u; ++corner) {
                    const auto& position =
                        mesh.vertices[group.indices[index + corner]].position;
                    for (std::size_t axis = 0u; axis < 3u; ++axis) {
                        component.minimum[axis] = std::min(
                            component.minimum[axis], position[axis]);
                        component.maximum[axis] = std::max(
                            component.maximum[axis], position[axis]);
                    }
                }
            }
        }
        std::cout << "[PAC_LgpeInspect][Terrain] mesh="
                  << mesh.sourceIndex << " name=" << mesh.name
                  << " components=" << components.size() << '\n';
        std::size_t componentIndex = 0u;
        for (const auto& [root, component] : components) {
            (void)root;
            std::cout << "  component=" << componentIndex++
                      << " triangles=" << component.triangles
                      << " min=" << component.minimum[0] << ','
                      << component.minimum[1] << ','
                      << component.minimum[2]
                      << " max=" << component.maximum[0] << ','
                      << component.maximum[1] << ','
                      << component.maximum[2]
                      << " materials=";
            bool first = true;
            for (const auto material : component.materials) {
                std::cout << (first ? "" : ",") << material;
                first = false;
            }
            std::cout << '\n';
        }
        game::runtime::lgpe_route1_terrain_assemblies::
            MeshPartition partition;
        std::string partitionError;
        if (!game::runtime::
                lgpe_route1_terrain_assemblies::derivePartition(
                    mesh,
                    partition,
                    &partitionError)) {
            std::cout << "  partition_error="
                      << partitionError << '\n';
            continue;
        }
        for (const auto& assembly : partition.assemblies) {
            std::cout << "  assembly="
                      << assembly.assemblyIndex
                      << " role=" << assembly.profileRole
                      << " pivot=" << assembly.sourcePivotCm[0]
                      << ',' << assembly.sourcePivotCm[1]
                      << ',' << assembly.sourcePivotCm[2]
                      << " min=" << assembly.boundsMinimum[0]
                      << ',' << assembly.boundsMinimum[1]
                      << ',' << assembly.boundsMinimum[2]
                      << " max=" << assembly.boundsMaximum[0]
                      << ',' << assembly.boundsMaximum[1]
                      << ',' << assembly.boundsMaximum[2]
                      << '\n';
        }
    }
}

} // namespace

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

    if (argc >= 3 && argv[2] &&
        std::string(argv[2]) == "--terrain-components") {
        printTerrainComponents(scene);
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
        << " world_field_grass01_surfaces="
        << worldScene.stats.fieldGrass01SurfaceMaterialCount
        << " world_field_grass02_surfaces="
        << worldScene.stats.fieldGrass02SurfaceMaterialCount
        << " world_field_grass04_surfaces="
        << worldScene.stats.fieldGrass04SurfaceMaterialCount
        << " world_field_grass05_surfaces="
        << worldScene.stats.fieldGrass05SurfaceMaterialCount
        << " world_field_roadstone_surfaces="
        << worldScene.stats.fieldRoadstoneSurfaceMaterialCount
        << " world_field_rockmask_surfaces="
        << worldScene.stats.fieldRockMaskSurfaceMaterialCount
        << " world_field_flower_surfaces="
        << worldScene.stats.fieldFlowerSurfaceMaterialCount
        << " world_field_rock_surfaces="
        << worldScene.stats.fieldRockSurfaceMaterialCount
        << " world_field_sign_surfaces="
        << worldScene.stats.fieldSignSurfaceMaterialCount
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
