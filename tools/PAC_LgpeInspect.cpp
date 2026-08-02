#include "game/assets/lgpe/LgpeCanonicalScene.h"
#include "engine/core/Paths.h"
#include "game/assets/DevAssetStore.h"
#include "game/runtime/shared/scene/LgpeWorldSceneAdapter.h"
#include "game/runtime/shared/scene/LgpeRoute1TerrainAssemblies.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
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

float repeatUnit(float value) {
    return value - std::floor(value);
}

std::array<float, 4> sampleTextureRepeatLinear(
    const engine::assets::lgpe::TextureSubresource& texture,
    float u,
    float v) {
    if (texture.width == 0u || texture.height == 0u ||
        texture.rgba8.size() <
            static_cast<std::size_t>(texture.width) *
                static_cast<std::size_t>(texture.height) * 4u) {
        return {};
    }
    const float texelX =
        repeatUnit(u) * static_cast<float>(texture.width) - 0.5f;
    const float texelY =
        repeatUnit(v) * static_cast<float>(texture.height) - 0.5f;
    const auto firstX = static_cast<std::int32_t>(std::floor(texelX));
    const auto firstY = static_cast<std::int32_t>(std::floor(texelY));
    const float blendX = texelX - static_cast<float>(firstX);
    const float blendY = texelY - static_cast<float>(firstY);
    const auto wrap = [](std::int32_t value, std::uint32_t size) {
        const auto signedSize = static_cast<std::int32_t>(size);
        const std::int32_t remainder = value % signedSize;
        return static_cast<std::uint32_t>(
            remainder < 0 ? remainder + signedSize : remainder);
    };
    const auto channel = [&](std::int32_t x,
                             std::int32_t y,
                             std::size_t component) {
        const std::size_t pixel =
            static_cast<std::size_t>(wrap(y, texture.height)) *
                static_cast<std::size_t>(texture.width) +
            static_cast<std::size_t>(wrap(x, texture.width));
        return static_cast<float>(texture.rgba8[pixel * 4u + component]) /
            255.0f;
    };
    std::array<float, 4> sampled{};
    for (std::size_t component = 0u;
         component < sampled.size();
         ++component) {
        const float top =
            channel(firstX, firstY, component) * (1.0f - blendX) +
            channel(firstX + 1, firstY, component) * blendX;
        const float bottom =
            channel(firstX, firstY + 1, component) * (1.0f - blendX) +
            channel(firstX + 1, firstY + 1, component) * blendX;
        sampled[component] = top * (1.0f - blendY) +
            bottom * blendY;
    }
    return sampled;
}

bool printGroundTransitionCsv(
    const engine::assets::lgpe::CanonicalScene& scene,
    std::string& outError) {
    const auto texture = std::find_if(
        scene.textures.begin(),
        scene.textures.end(),
        [](const engine::assets::lgpe::Texture& candidate) {
            return candidate.name == "glassmask01_com";
        });
    if (texture == scene.textures.end()) {
        outError = "glassmask01_com is absent.";
        return false;
    }
    const auto mip = std::find_if(
        texture->subresources.begin(),
        texture->subresources.end(),
        [](const engine::assets::lgpe::TextureSubresource& candidate) {
            return candidate.arrayLevel == 0u &&
                candidate.mipLevel == 0u &&
                candidate.depthLevel == 0u;
        });
    if (mip == texture->subresources.end()) {
        outError = "glassmask01_com mip 0 is absent.";
        return false;
    }

    std::cout
        << "mesh,triangle,corner,vertex,x,y,z,uv0_u,uv0_v,uv2_u,uv2_v,"
           "color_r,color_g,color_b,color_a,mask_r,mask_g,mask_b,mask_a\n";
    std::cout << std::fixed << std::setprecision(9);
    for (const auto& mesh : scene.meshes) {
        for (const auto& group : mesh.polygonGroups) {
            if (group.materialIndex != 19u) {
                continue;
            }
            for (std::size_t offset = 0u;
                 offset + 2u < group.indices.size();
                 offset += 3u) {
                for (std::size_t corner = 0u; corner < 3u; ++corner) {
                    const std::uint32_t vertexIndex =
                        group.indices[offset + corner];
                    if (vertexIndex >= mesh.vertices.size()) {
                        outError = "Ground polygon index is out of range.";
                        return false;
                    }
                    const auto& vertex = mesh.vertices[vertexIndex];
                    const auto mask = sampleTextureRepeatLinear(
                        *mip,
                        vertex.texcoords[2][0],
                        1.0f - vertex.texcoords[2][1]);
                    std::cout
                        << mesh.sourceIndex << ',' << offset / 3u << ','
                        << corner << ',' << vertexIndex << ','
                        << vertex.position[0] << ','
                        << vertex.position[1] << ','
                        << vertex.position[2] << ','
                        << vertex.texcoords[0][0] << ','
                        << vertex.texcoords[0][1] << ','
                        << vertex.texcoords[2][0] << ','
                        << vertex.texcoords[2][1] << ','
                        << vertex.colors[0][0] << ','
                        << vertex.colors[0][1] << ','
                        << vertex.colors[0][2] << ','
                        << vertex.colors[0][3] << ','
                        << mask[0] << ',' << mask[1] << ','
                        << mask[2] << ',' << mask[3] << '\n';
                }
            }
        }
    }
    return true;
}

bool printTerrainRampCsv(
    const engine::assets::lgpe::CanonicalScene& scene,
    std::string& outError) {
    std::cout
        << "mesh,assembly,group,material,triangle,corner,vertex,"
           "x,y,z,nx,ny,nz,uv0_u,uv0_v,uv1_u,uv1_v,uv2_u,uv2_v,"
           "color_r,color_g,color_b,color_a\n";
    std::cout << std::fixed << std::setprecision(9);
    for (const auto& mesh : scene.meshes) {
        if (mesh.sourceIndex < 29u || mesh.sourceIndex > 35u) {
            continue;
        }
        game::runtime::lgpe_route1_terrain_assemblies::MeshPartition
            partition;
        if (!game::runtime::lgpe_route1_terrain_assemblies::
                derivePartition(mesh, partition, &outError)) {
            return false;
        }
        for (const auto& assembly : partition.assemblies) {
            if (assembly.profileRole != "source_ramp") {
                continue;
            }
            for (const auto& selection : assembly.polygonGroups) {
                for (std::size_t offset = 0u;
                     offset + 2u < selection.indices.size();
                     offset += 3u) {
                    for (std::size_t corner = 0u;
                         corner < 3u;
                         ++corner) {
                        const std::uint32_t vertexIndex =
                            selection.indices[offset + corner];
                        if (vertexIndex >= mesh.vertices.size()) {
                            outError =
                                "Terrain ramp polygon index is out of range.";
                            return false;
                        }
                        const auto& vertex = mesh.vertices[vertexIndex];
                        std::cout
                            << mesh.sourceIndex << ','
                            << assembly.assemblyIndex << ','
                            << selection.polygonGroupIndex << ','
                            << selection.materialIndex << ','
                            << offset / 3u << ',' << corner << ','
                            << vertexIndex << ','
                            << vertex.position[0] << ','
                            << vertex.position[1] << ','
                            << vertex.position[2] << ','
                            << vertex.normal[0] << ','
                            << vertex.normal[1] << ','
                            << vertex.normal[2] << ','
                            << vertex.texcoords[0][0] << ','
                            << vertex.texcoords[0][1] << ','
                            << vertex.texcoords[1][0] << ','
                            << vertex.texcoords[1][1] << ','
                            << vertex.texcoords[2][0] << ','
                            << vertex.texcoords[2][1] << ','
                            << vertex.colors[0][0] << ','
                            << vertex.colors[0][1] << ','
                            << vertex.colors[0][2] << ','
                            << vertex.colors[0][3] << '\n';
                    }
                }
            }
        }
    }
    return true;
}

bool printMeshGroupCsv(
    const engine::assets::lgpe::CanonicalScene& scene,
    std::uint32_t sourceMeshIndex,
    std::uint32_t polygonGroupIndex,
    std::string& outError) {
    const auto mesh = std::find_if(
        scene.meshes.begin(),
        scene.meshes.end(),
        [&](const auto& candidate) {
            return candidate.sourceIndex == sourceMeshIndex;
        });
    if (mesh == scene.meshes.end() ||
        polygonGroupIndex >= mesh->polygonGroups.size()) {
        outError = "Requested source mesh or polygon group is absent.";
        return false;
    }
    const auto& group = mesh->polygonGroups[polygonGroupIndex];
    std::cout
        << "mesh,group,material,triangle,corner,vertex,"
           "x,y,z,nx,ny,nz,uv0_u,uv0_v,uv1_u,uv1_v,uv2_u,uv2_v,"
           "color_r,color_g,color_b,color_a\n";
    std::cout << std::fixed << std::setprecision(9);
    for (std::size_t offset = 0u;
         offset + 2u < group.indices.size();
         offset += 3u) {
        for (std::size_t corner = 0u; corner < 3u; ++corner) {
            const std::uint32_t vertexIndex =
                group.indices[offset + corner];
            if (vertexIndex >= mesh->vertices.size()) {
                outError = "Requested polygon index is out of range.";
                return false;
            }
            const auto& vertex = mesh->vertices[vertexIndex];
            std::cout
                << mesh->sourceIndex << ',' << polygonGroupIndex << ','
                << group.materialIndex << ',' << offset / 3u << ','
                << corner << ',' << vertexIndex << ','
                << vertex.position[0] << ',' << vertex.position[1] << ','
                << vertex.position[2] << ',' << vertex.normal[0] << ','
                << vertex.normal[1] << ',' << vertex.normal[2] << ','
                << vertex.texcoords[0][0] << ','
                << vertex.texcoords[0][1] << ','
                << vertex.texcoords[1][0] << ','
                << vertex.texcoords[1][1] << ','
                << vertex.texcoords[2][0] << ','
                << vertex.texcoords[2][1] << ','
                << vertex.colors[0][0] << ','
                << vertex.colors[0][1] << ','
                << vertex.colors[0][2] << ','
                << vertex.colors[0][3] << '\n';
        }
    }
    return true;
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
        std::string(argv[2]) == "--ground-transition-csv") {
        if (!printGroundTransitionCsv(scene, error)) {
            std::cerr << "[PAC_LgpeInspect] FAIL root=" << virtualRoot
                      << " ground_transition_error=" << error << '\n';
            return 1;
        }
        return 0;
    }

    if (argc >= 3 && argv[2] &&
        std::string(argv[2]) == "--terrain-ramp-csv") {
        if (!printTerrainRampCsv(scene, error)) {
            std::cerr << "[PAC_LgpeInspect] FAIL root=" << virtualRoot
                      << " terrain_ramp_error=" << error << '\n';
            return 1;
        }
        return 0;
    }

    if (argc >= 5 && argv[2] &&
        std::string(argv[2]) == "--mesh-group-csv") {
        try {
            const auto meshIndex = static_cast<std::uint32_t>(
                std::stoul(argv[3]));
            const auto groupIndex = static_cast<std::uint32_t>(
                std::stoul(argv[4]));
            if (!printMeshGroupCsv(
                    scene,
                    meshIndex,
                    groupIndex,
                    error)) {
                std::cerr << "[PAC_LgpeInspect] FAIL root=" << virtualRoot
                          << " mesh_group_error=" << error << '\n';
                return 1;
            }
            return 0;
        } catch (const std::exception& exception) {
            std::cerr << "[PAC_LgpeInspect] FAIL root=" << virtualRoot
                      << " mesh_group_error=" << exception.what() << '\n';
            return 1;
        }
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
