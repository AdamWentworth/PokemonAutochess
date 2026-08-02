#include "game/editor/Route1EnvironmentPrefabPreview.h"

#include "game/assets/lgpe/LgpeCanonicalScene.h"
#include "engine/assets/phlosion/PhlosionSceneArchive.h"
#include "game/render/lgpe/LgpeFieldEncounterGrassMaterial.h"
#include "game/assets/DevAssetStore.h"
#include "game/runtime/shared/scene/LgpeRoute1ProjectedShadow.h"
#include "game/runtime/shared/scene/LgpeRoute1RuntimeEnvironment.h"
#include "game/runtime/shared/scene/LgpeRoute1TerrainAssemblies.h"
#include "game/runtime/shared/scene/LgpeWorldSceneAdapter.h"
#include "game/runtime/shared/scene/SharedWorldScene.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <limits>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

namespace game::editor {
namespace {

namespace fs = std::filesystem;
using CanonicalScene = engine::assets::lgpe::CanonicalScene;
using PreparedScene =
    game::runtime::lgpe_world_scene::PreparedScene;
using WorldBatch =
    game::runtime::shared_world_batches::WorldIndexedBatch;

constexpr float kInitialWindPhaseCycles = 36.0f / 120.0f;
constexpr float kWindPeriodSeconds = 4.0f;

std::array<float, 16> toArray(const glm::mat4& matrix) {
    std::array<float, 16> out{};
    std::copy(
        glm::value_ptr(matrix),
        glm::value_ptr(matrix) + out.size(),
        out.begin());
    return out;
}

const IRenderBackend::WorldSceneRenderObject* renderObject(
    const game::runtime::shared_world_scene::WorldSceneRegistry&
        registry,
    IRenderBackend::WorldSceneRenderObjectHandle handle) {
    if (handle.id == 0u ||
        handle.id > registry.renderObjects.size()) {
        return nullptr;
    }
    return &registry.renderObjects[handle.id - 1u];
}

const IRenderBackend::WorldSceneGeometry* geometry(
    const game::runtime::shared_world_scene::WorldSceneRegistry&
        registry,
    IRenderBackend::WorldSceneGeometryHandle handle) {
    if (handle.id == 0u ||
        handle.id > registry.geometries.size()) {
        return nullptr;
    }
    return &registry.geometries[handle.id - 1u];
}

std::vector<float> encounterGrassSkinPalette(
    engine::render::lgpe_field_encounter_grass::SourceVariant
        variant,
    std::size_t jointCount,
    float windPhaseCycles) {
    std::vector<float> palette(jointCount * 16u, 0.0f);
    for (std::size_t joint = 0u; joint < jointCount; ++joint) {
        const auto rotation =
            engine::render::lgpe_field_encounter_grass::
                evaluateWindJointRotation(
                    static_cast<std::uint32_t>(joint),
                    0.0f,
                    windPhaseCycles);
        const auto pivotValues =
            engine::render::lgpe_field_encounter_grass::
                sourceJointPivot(
                    variant,
                    static_cast<std::uint32_t>(joint));
        const glm::vec3 pivot{
            pivotValues[0],
            pivotValues[1],
            pivotValues[2]};
        const glm::mat4 jointMatrix =
            glm::translate(glm::mat4(1.0f), pivot) *
            glm::rotate(
                glm::mat4(1.0f),
                -rotation.bendRadians,
                glm::vec3(0.0f, 0.0f, 1.0f)) *
            glm::rotate(
                glm::mat4(1.0f),
                rotation.crossRadians,
                glm::vec3(1.0f, 0.0f, 0.0f)) *
            glm::translate(glm::mat4(1.0f), -pivot);
        std::copy(
            glm::value_ptr(jointMatrix),
            glm::value_ptr(jointMatrix) + 16u,
            palette.data() + joint * 16u);
    }
    return palette;
}

std::vector<float> vegetationSkinPalette(
    const CanonicalScene& source,
    float windPhaseCycles) {
    std::vector<float> palette(
        source.bones.size() * 16u,
        0.0f);
    std::vector<glm::mat4> restWorld(
        source.bones.size(),
        glm::mat4(1.0f));
    for (std::size_t index = 0u;
         index < source.bones.size();
         ++index) {
        const auto& bone = source.bones[index];
        const glm::quat restRotation(
            bone.rotation[3],
            bone.rotation[0],
            bone.rotation[1],
            bone.rotation[2]);
        const glm::mat4 local =
            glm::translate(
                glm::mat4(1.0f),
                glm::vec3(
                    bone.position[0],
                    bone.position[1],
                    bone.position[2])) *
            glm::mat4_cast(restRotation) *
            glm::scale(
                glm::mat4(1.0f),
                glm::vec3(
                    bone.scale[0],
                    bone.scale[1],
                    bone.scale[2]));
        restWorld[index] =
            bone.parentIndex >= 0 &&
                    static_cast<std::size_t>(bone.parentIndex) <
                        index
                ? restWorld[
                      static_cast<std::size_t>(
                          bone.parentIndex)] *
                      local
                : local;

        glm::mat4 jointMatrix(1.0f);
        constexpr std::string_view kGrassJointPrefix =
            "grass_joint";
        if (bone.name.rfind(kGrassJointPrefix, 0u) == 0u) {
            std::uint32_t componentIndex = 1u;
            const std::string suffix =
                bone.name.substr(kGrassJointPrefix.size());
            if (!suffix.empty() &&
                std::all_of(
                    suffix.begin(),
                    suffix.end(),
                    [](unsigned char value) {
                        return std::isdigit(value) != 0;
                    })) {
                componentIndex =
                    static_cast<std::uint32_t>(
                        std::stoul(suffix)) +
                    1u;
            }
            const auto rotation =
                engine::render::lgpe_field_encounter_grass::
                    evaluateWindJointRotation(
                        componentIndex,
                        0.0f,
                        windPhaseCycles);
            const glm::vec3 pivot(restWorld[index][3]);
            jointMatrix =
                glm::translate(glm::mat4(1.0f), pivot) *
                glm::rotate(
                    glm::mat4(1.0f),
                    -rotation.bendRadians,
                    glm::vec3(0.0f, 0.0f, 1.0f)) *
                glm::rotate(
                    glm::mat4(1.0f),
                    rotation.crossRadians,
                    glm::vec3(1.0f, 0.0f, 0.0f)) *
                glm::translate(glm::mat4(1.0f), -pivot);
        }
        std::copy(
            glm::value_ptr(jointMatrix),
            glm::value_ptr(jointMatrix) + 16u,
            palette.data() + index * 16u);
    }
    return palette;
}

bool pathLooksLikeRoute1EnvironmentPrefab(
    const char* assetPath) {
    if (!assetPath) {
        return false;
    }
    std::string path(assetPath);
    std::replace(path.begin(), path.end(), '\\', '/');
    return path.find(
               "/objects/environment/route1/") !=
               std::string::npos &&
        fs::path(path).extension() == ".phlo";
}

fs::path projectRootForAsset(const fs::path& assetPath) {
    std::error_code error;
    fs::path current =
        fs::absolute(assetPath, error).parent_path();
    if (error) {
        return fs::current_path();
    }
    while (!current.empty()) {
        if (fs::is_regular_file(
                current / "phlosion.project.json",
                error) &&
            !error) {
            return current;
        }
        error.clear();
        const fs::path parent = current.parent_path();
        if (parent == current) break;
        current = parent;
    }
    return fs::current_path();
}

bool applyDerivedTreePartition(
    CanonicalScene& source,
    const nlohmann::json& derivation,
    std::string& outError) {
    const std::string kind =
        derivation.value("kind", std::string{});
    const bool sourceVertexBlocks =
        kind == "source_repeated_instance_vertex_blocks";
    const bool legacyNearestCenter =
        kind ==
        "connected_trunk_components_nearest_center_partition";
    if (!sourceVertexBlocks && !legacyNearestCenter) {
        return true;
    }
    if (source.meshes.size() != 1u) {
        outError =
            "Derived tree selector has invalid instance metadata.";
        return false;
    }

    auto& mesh = source.meshes.front();
    if (sourceVertexBlocks) {
        const std::size_t selectedInstance =
            derivation.at("selected_source_instance")
                .get<std::size_t>();
        const auto& groupRecords =
            derivation.at("polygon_groups");
        if (!groupRecords.is_array() ||
            groupRecords.size() !=
                mesh.polygonGroups.size()) {
            outError =
                "Derived tree source block metadata does not match its polygon groups.";
            return false;
        }
        for (std::size_t groupIndex = 0u;
             groupIndex < mesh.polygonGroups.size();
             ++groupIndex) {
            auto& group = mesh.polygonGroups[groupIndex];
            const auto recordIt = std::find_if(
                groupRecords.begin(),
                groupRecords.end(),
                [&](const nlohmann::json& record) {
                    return record.at("polygon_group_index")
                                   .get<std::size_t>() ==
                               groupIndex &&
                        record.at("material_index")
                                .get<std::uint32_t>() ==
                            group.materialIndex;
                });
            if (recordIt == groupRecords.end()) {
                outError =
                    "Derived tree source block is missing a polygon group.";
                return false;
            }
            const auto& instances = recordIt->at("instances");
            if (!instances.is_array() ||
                selectedInstance >= instances.size()) {
                outError =
                    "Derived tree selected source instance is out of range.";
                return false;
            }
            const std::uint32_t first =
                instances[selectedInstance]
                    .at("first_vertex")
                    .get<std::uint32_t>();
            const std::uint32_t count =
                instances[selectedInstance]
                    .at("vertex_count")
                    .get<std::uint32_t>();
            if (count == 0u ||
                first >= mesh.vertices.size() ||
                count > mesh.vertices.size() - first) {
                outError =
                    "Derived tree source vertex block is out of range.";
                return false;
            }
            const std::uint32_t end = first + count;
            std::vector<std::uint32_t> selected;
            selected.reserve(
                group.indices.size() / instances.size() + 3u);
            for (std::size_t index = 0u;
                 index + 2u < group.indices.size();
                 index += 3u) {
                const std::uint32_t a = group.indices[index];
                const std::uint32_t b = group.indices[index + 1u];
                const std::uint32_t c = group.indices[index + 2u];
                if (a >= mesh.vertices.size() ||
                    b >= mesh.vertices.size() ||
                    c >= mesh.vertices.size()) {
                    outError =
                        "Derived tree triangle index is out of range.";
                    return false;
                }
                const bool insideA = a >= first && a < end;
                const bool insideB = b >= first && b < end;
                const bool insideC = c >= first && c < end;
                if ((insideA || insideB || insideC) &&
                    !(insideA && insideB && insideC)) {
                    outError =
                        "Derived tree triangle crosses source instance blocks.";
                    return false;
                }
                if (insideA) {
                    selected.push_back(a);
                    selected.push_back(b);
                    selected.push_back(c);
                }
            }
            group.indices = std::move(selected);
        }
    } else {
        const auto& centersJson =
            derivation.at("instance_centers_cm");
        const std::size_t selectedInstance =
            derivation.at("selected_instance")
                .get<std::size_t>();
        if (!centersJson.is_array() ||
            centersJson.empty() ||
            selectedInstance >= centersJson.size()) {
            outError =
                "Derived tree selector contains invalid source centres.";
            return false;
        }
        std::vector<glm::vec3> centers;
        centers.reserve(centersJson.size());
        for (const auto& value : centersJson) {
            if (!value.is_array() || value.size() != 3u) {
                outError =
                    "Derived tree selector contains an invalid center.";
                return false;
            }
            centers.emplace_back(
                value[0].get<float>(),
                value[1].get<float>(),
                value[2].get<float>());
        }

        for (auto& group : mesh.polygonGroups) {
            std::vector<std::uint32_t> selected;
            selected.reserve(
                group.indices.size() / centers.size() + 3u);
            for (std::size_t index = 0u;
                 index + 2u < group.indices.size();
                 index += 3u) {
                const std::uint32_t a = group.indices[index];
                const std::uint32_t b = group.indices[index + 1u];
                const std::uint32_t c = group.indices[index + 2u];
                if (a >= mesh.vertices.size() ||
                    b >= mesh.vertices.size() ||
                    c >= mesh.vertices.size()) {
                    outError =
                        "Derived tree triangle index is out of range.";
                    return false;
                }
                const auto& pa = mesh.vertices[a].position;
                const auto& pb = mesh.vertices[b].position;
                const auto& pc = mesh.vertices[c].position;
                const float x =
                    (pa[0] + pb[0] + pc[0]) / 3.0f;
                const float z =
                    (pa[2] + pb[2] + pc[2]) / 3.0f;
                std::size_t nearest = 0u;
                float nearestDistance =
                    std::numeric_limits<float>::max();
                for (std::size_t candidate = 0u;
                     candidate < centers.size();
                     ++candidate) {
                    const float dx =
                        x - centers[candidate].x;
                    const float dz =
                        z - centers[candidate].z;
                    const float distance =
                        dx * dx + dz * dz;
                    if (distance < nearestDistance) {
                        nearest = candidate;
                        nearestDistance = distance;
                    }
                }
                if (nearest == selectedInstance) {
                    selected.push_back(a);
                    selected.push_back(b);
                    selected.push_back(c);
                }
            }
            group.indices = std::move(selected);
        }
    }
    std::erase_if(
        mesh.polygonGroups,
        [](const auto& group) {
            return group.indices.empty();
        });

    constexpr std::uint32_t kUnused =
        std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> remap(
        mesh.vertices.size(),
        kUnused);
    std::vector<engine::assets::lgpe::CanonicalVertex>
        compactVertices;
    for (auto& group : mesh.polygonGroups) {
        for (std::uint32_t& index : group.indices) {
            if (remap[index] == kUnused) {
                remap[index] =
                    static_cast<std::uint32_t>(
                        compactVertices.size());
                compactVertices.push_back(mesh.vertices[index]);
            }
            index = remap[index];
        }
    }
    if (compactVertices.empty()) {
        outError =
            "Derived tree selector produced no geometry.";
        return false;
    }
    if (sourceVertexBlocks) {
        const std::size_t expectedVertexCount =
            derivation.at("selected_vertex_count")
                .get<std::size_t>();
        if (compactVertices.size() != expectedVertexCount) {
            outError =
                "Derived tree source block vertex count changed: expected " +
                std::to_string(expectedVertexCount) +
                ", received " +
                std::to_string(compactVertices.size()) + ".";
            return false;
        }
    }
    mesh.vertices = std::move(compactVertices);
    mesh.sourceRawVertexData.clear();
    glm::vec3 minimum(std::numeric_limits<float>::max());
    glm::vec3 maximum(std::numeric_limits<float>::lowest());
    for (const auto& vertex : mesh.vertices) {
        const glm::vec3 position(
            vertex.position[0],
            vertex.position[1],
            vertex.position[2]);
        minimum = glm::min(minimum, position);
        maximum = glm::max(maximum, position);
    }
    mesh.boundsMinimum = {minimum.x, minimum.y, minimum.z};
    mesh.boundsMaximum = {maximum.x, maximum.y, maximum.z};
    return true;
}

bool applyDerivedTerrainPartition(
    CanonicalScene& source,
    const nlohmann::json& derivation,
    std::string& outError) {
    namespace terrain =
        game::runtime::lgpe_route1_terrain_assemblies;
    if (derivation.value("kind", std::string{}) !=
            "source_connected_terrain_body_cap_pair" ||
        source.meshes.size() != 1u) {
        outError =
            "Derived terrain selector has an invalid source boundary.";
        return false;
    }
    auto& mesh = source.meshes.front();
    const std::uint32_t meshIndex =
        derivation.at("mesh_index")
            .get<std::uint32_t>();
    const std::size_t assemblyIndex =
        derivation.at("assembly_index")
            .get<std::size_t>();
    const std::size_t expectedCount =
        derivation.at("expected_assembly_count")
            .get<std::size_t>();
    if (mesh.sourceIndex != meshIndex) {
        outError =
            "Derived terrain selector references a different source mesh.";
        return false;
    }
    terrain::MeshPartition partition;
    if (!terrain::derivePartition(
            mesh,
            partition,
            &outError) ||
        partition.assemblies.size() != expectedCount ||
        assemblyIndex >= partition.assemblies.size()) {
        if (outError.empty()) {
            outError =
                "Derived terrain selector no longer matches the source topology.";
        }
        return false;
    }
    const auto& assembly =
        partition.assemblies[assemblyIndex];
    for (std::size_t groupIndex = 0u;
         groupIndex < mesh.polygonGroups.size();
         ++groupIndex) {
        const auto selected = std::find_if(
            assembly.polygonGroups.begin(),
            assembly.polygonGroups.end(),
            [&](const auto& group) {
                return group.polygonGroupIndex ==
                    groupIndex;
            });
        mesh.polygonGroups[groupIndex].indices =
            selected == assembly.polygonGroups.end()
            ? std::vector<std::uint32_t>{}
            : selected->indices;
    }
    std::erase_if(
        mesh.polygonGroups,
        [](const auto& group) {
            return group.indices.empty();
        });

    constexpr std::uint32_t kUnused =
        std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> remap(
        mesh.vertices.size(),
        kUnused);
    std::vector<engine::assets::lgpe::CanonicalVertex>
        compactVertices;
    for (auto& group : mesh.polygonGroups) {
        for (std::uint32_t& index : group.indices) {
            if (index >= remap.size()) {
                outError =
                    "Derived terrain selector contains an invalid vertex index.";
                return false;
            }
            if (remap[index] == kUnused) {
                remap[index] =
                    static_cast<std::uint32_t>(
                        compactVertices.size());
                compactVertices.push_back(
                    mesh.vertices[index]);
            }
            index = remap[index];
        }
    }
    if (compactVertices.empty()) {
        outError =
            "Derived terrain selector produced no geometry.";
        return false;
    }
    mesh.vertices = std::move(compactVertices);
    mesh.sourceRawVertexData.clear();
    mesh.boundsMinimum = assembly.boundsMinimum;
    mesh.boundsMaximum = assembly.boundsMaximum;
    return true;
}

} // namespace

struct Route1EnvironmentPrefabPreview::Impl {
    struct SourceDraw {
        IRenderBackend::WorldSceneRenderObjectHandle object{};
        std::array<float, 16> modelMatrix{};
    };

    std::string assetId;
    std::string assetPath;
    std::string status;
    std::string displayName;
    std::string motionDriver;
    std::string sourceBoundary;
    CanonicalScene source;
    PreparedScene prepared;
    game::runtime::lgpe_route1_projected_shadow::Atlas
        projectedShadowAtlas;
    std::vector<SourceDraw> sourceDraws;
    std::vector<SourceDraw> shadowSourceDraws;
    std::vector<float> skinPalette;
    std::vector<WorldBatch> materialTemplates;
    std::vector<WorldBatch> batches;
    engine::editor::EditorProjectAssetPreviewOptions options;
    glm::mat4 worldFromSource{1.0f};
    glm::mat4 sourceFromWorld{1.0f};
    game::runtime::lgpe_route1_runtime::LightProjectionRows
        cloudProjectionRows;
    float animationTime = 0.0f;
    float boundsRadius = 1.0f;
    float boundsCenterY = 0.5f;
    std::uint32_t vertexCount = 0u;
    std::uint32_t triangleCount = 0u;
    std::uint32_t materialCount = 0u;
    std::uint32_t textureCount = 0u;
    bool dynamicWind = false;
    bool encounterGrass02 = false;
    bool ready = false;

    void updateWind() {
        if (!dynamicWind) {
            return;
        }
        const float phase =
            kInitialWindPhaseCycles +
            animationTime / kWindPeriodSeconds;
        if (motionDriver.find("encounter_grass") !=
            std::string::npos) {
            skinPalette = encounterGrassSkinPalette(
                encounterGrass02
                    ? engine::render::
                          lgpe_field_encounter_grass::
                              SourceVariant::Grass02
                    : engine::render::
                          lgpe_field_encounter_grass::
                              SourceVariant::Grass01,
                source.bones.size(),
                phase);
        } else {
            skinPalette =
                vegetationSkinPalette(source, phase);
        }

        game::runtime::shared_world_scene::
            beginWorldSceneFrame(prepared.frame);
        game::runtime::shared_world_scene::
            beginWorldSceneFrame(prepared.shadowFrame);
        std::uint32_t instanceId = 1u;
        for (const auto& draw : sourceDraws) {
            IRenderBackend::WorldSceneRenderInstanceHandle handle{};
            handle.id = instanceId++;
            game::runtime::shared_world_scene::
                appendSkinnedInstance(
                    prepared.frame,
                    draw.object,
                    handle,
                    draw.modelMatrix,
                    1.0f,
                    1.0f,
                    1.0f,
                    1.0f,
                    0.0f,
                    0u,
                    static_cast<std::uint32_t>(
                        source.bones.size()),
                    skinPalette.data());
        }
        for (const auto& draw : shadowSourceDraws) {
            IRenderBackend::WorldSceneRenderInstanceHandle handle{};
            handle.id = instanceId++;
            game::runtime::shared_world_scene::
                appendSkinnedInstance(
                    prepared.shadowFrame,
                    draw.object,
                    handle,
                    draw.modelMatrix,
                    1.0f,
                    1.0f,
                    1.0f,
                    1.0f,
                    0.0f,
                    0u,
                    static_cast<std::uint32_t>(
                        source.bones.size()),
                    skinPalette.data());
        }
    }

    void buildMaterialTemplates() {
        materialTemplates.clear();
        materialTemplates.reserve(
            prepared.registry.materials.size());
        for (const auto& material :
             prepared.registry.materials) {
            auto batch =
                game::runtime::shared_world_scene::
                    makeWorldIndexedMaterialTemplate(material);
            batch.lightProjectionUvRowU =
                cloudProjectionRows.u;
            batch.lightProjectionUvRowV =
                cloudProjectionRows.v;
            if (batch.projectedShadowEnabled != 0u) {
                batch.projectedShadowMatrix = toArray(
                    glm::make_mat4(
                        batch.projectedShadowMatrix.data()) *
                    sourceFromWorld);
            }
            materialTemplates.push_back(std::move(batch));
        }
    }

    void appendBatches() {
        batches.clear();
        if (!options.showMesh) {
            return;
        }
        const auto& registry = prepared.registry;
        for (const auto& drawClass :
             prepared.frame.drawClasses) {
            const auto* object =
                renderObject(
                    registry,
                    drawClass.objectHandle);
            if (!object ||
                object->materialHandle.id == 0u ||
                object->materialHandle.id >
                    materialTemplates.size()) {
                continue;
            }
            const auto* mesh =
                geometry(
                    registry,
                    object->geometryHandle);
            if (!mesh ||
                !mesh->vertices ||
                !mesh->indices ||
                mesh->vertexCount == 0u ||
                mesh->indexCount < 3u) {
                continue;
            }
            WorldBatch batch{};
            batch.sharedTemplate =
                &materialTemplates[
                    object->materialHandle.id - 1u];
            batch.sharedVertices = mesh->vertices;
            batch.sharedVertexCount = mesh->vertexCount;
            batch.sharedIndices = mesh->indices;
            batch.sharedIndexCount = mesh->indexCount;
            batch.geometryCacheKey =
                mesh->geometryCacheKey +
                ":editor_environment_prefab:" +
                assetId;
            batch.preserveSubmissionOrder = true;
            batch.instances.reserve(
                drawClass.instances.size());
            for (const auto& instance :
                 drawClass.instances) {
                IRenderBackend::WorldMeshInstance
                    worldInstance{};
                worldInstance.modelMatrix = toArray(
                    worldFromSource *
                    glm::make_mat4(
                        instance.modelMatrix.data()));
                worldInstance.vertexColorMulR =
                    instance.vertexColorMulR;
                worldInstance.vertexColorMulG =
                    instance.vertexColorMulG;
                worldInstance.vertexColorMulB =
                    instance.vertexColorMulB;
                worldInstance.vertexColorMulA =
                    instance.vertexColorMulA;
                worldInstance.gpuSkinning =
                    instance.gpuSkinning;
                worldInstance.gpuSkinningMode =
                    instance.gpuSkinningMode;
                worldInstance.skinMatrixCount =
                    instance.skinMatrixCount;
                worldInstance.skinMatrices =
                    instance.skinMatrices;
                batch.instances.push_back(worldInstance);
            }
            if (!batch.instances.empty()) {
                batches.push_back(std::move(batch));
            }
        }
    }
};

Route1EnvironmentPrefabPreview::
    Route1EnvironmentPrefabPreview()
    : impl_(std::make_unique<Impl>()) {}

Route1EnvironmentPrefabPreview::
    ~Route1EnvironmentPrefabPreview() = default;

bool Route1EnvironmentPrefabPreview::owns(
    const char*,
    const char* assetPath) const {
    return pathLooksLikeRoute1EnvironmentPrefab(assetPath);
}

bool Route1EnvironmentPrefabPreview::select(
    const char* assetId,
    const char* assetPath,
    std::string* outError) {
    impl_ = std::make_unique<Impl>();
    impl_->assetId = assetId ? assetId : "";
    impl_->assetPath = assetPath ? assetPath : "";
    if (!owns(assetId, assetPath)) {
        if (outError) {
            *outError =
                "The selected PHLO is not a Route 1 environment prefab.";
        }
        return false;
    }

    const fs::path prefabPath =
        fs::path(impl_->assetPath);
    const fs::path hostRoot =
        prefabPath.has_parent_path()
            ? prefabPath.parent_path()
            : fs::path(".");
    game::assets::DevAssetStore host(hostRoot.string());
    engine::assets::phlosion::PrefabArchiveStore archive;
    std::string error;
    if (!archive.load(
            host,
            prefabPath.filename().generic_string(),
            &error) ||
        archive.prefabKind() != "LgpeEnvironment") {
        if (outError) {
            *outError =
                "Could not decode the environment PHLO: " +
                error;
        }
        return false;
    }

    try {
        const nlohmann::json metadata =
            nlohmann::json::parse(archive.metadataJson());
        impl_->displayName =
            metadata.at("display_name").get<std::string>();
        impl_->motionDriver =
            metadata.at("motion")
                .at("driver")
                .get<std::string>();
        impl_->sourceBoundary =
            metadata.at("source_boundary")
                .get<std::string>();
        impl_->dynamicWind =
            impl_->motionDriver.rfind("lgpe_", 0u) == 0u;
        impl_->encounterGrass02 =
            archive.prefabId().find(
                "encounter_grass_02") !=
            std::string::npos;
        const std::string canonicalRoot =
            metadata.at("canonical_root")
                .get<std::string>();
        const std::string canonicalStorage =
            metadata.value(
                "canonical_storage",
                std::string("embedded"));
        bool sourceLoaded = false;
        if (canonicalStorage == "route_scene_dependency") {
            const fs::path projectRoot =
                projectRootForAsset(prefabPath);
            game::assets::DevAssetStore projectStore(
                projectRoot.string());
            engine::assets::phlosion::SceneArchiveStore
                sceneArchive;
            const std::string scenePath =
                metadata.at("scene_archive")
                    .get<std::string>();
            sourceLoaded =
                sceneArchive.load(
                    projectStore,
                    scenePath,
                    &error) &&
                engine::assets::lgpe::loadCanonicalScene(
                    sceneArchive,
                    canonicalRoot,
                    impl_->source,
                    &error);
        } else {
            sourceLoaded =
                engine::assets::lgpe::loadCanonicalScene(
                    archive,
                    canonicalRoot,
                    impl_->source,
                    &error);
        }
        if (!sourceLoaded) {
            if (outError) {
                *outError =
                    "Could not load the prefab's canonical scene: " +
                    error;
            }
            return false;
        }

        const auto& selected =
            metadata.at("selector").at("mesh_indices");
        if (!selected.empty()) {
            std::vector<engine::assets::lgpe::Mesh>
                selectedMeshes;
            selectedMeshes.reserve(selected.size());
            for (const auto& value : selected) {
                const std::uint32_t sourceIndex =
                    value.get<std::uint32_t>();
                const auto found = std::find_if(
                    impl_->source.meshes.begin(),
                    impl_->source.meshes.end(),
                    [&](const auto& mesh) {
                        return mesh.sourceIndex ==
                            sourceIndex;
                    });
                if (found == impl_->source.meshes.end()) {
                    if (outError) {
                        *outError =
                            "Environment PHLO mesh selector is out of range.";
                    }
                    return false;
                }
                selectedMeshes.push_back(*found);
            }
            impl_->source.meshes =
                std::move(selectedMeshes);
        }
        const auto& selector = metadata.at("selector");
        if (selector.contains("derivation")) {
            const auto& derivation =
                selector.at("derivation");
            const std::string kind =
                derivation.value("kind", std::string{});
            const bool isolated =
                kind ==
                    "source_connected_terrain_body_cap_pair"
                ? applyDerivedTerrainPartition(
                      impl_->source,
                      derivation,
                      error)
                : applyDerivedTreePartition(
                      impl_->source,
                      derivation,
                      error);
            if (!isolated) {
                if (outError) {
                    *outError =
                        "Could not isolate the derived environment prefab: " +
                        error;
                }
                return false;
            }
        }
    } catch (const std::exception& exception) {
        if (outError) {
            *outError =
                "Environment PHLO metadata is invalid: " +
                std::string(exception.what());
        }
        return false;
    }

    glm::vec3 boundsMin(
        std::numeric_limits<float>::max());
    glm::vec3 boundsMax(
        std::numeric_limits<float>::lowest());
    std::set<std::uint32_t> usedMaterials;
    for (const auto& mesh : impl_->source.meshes) {
        boundsMin = glm::min(
            boundsMin,
            glm::vec3(
                mesh.boundsMinimum[0],
                mesh.boundsMinimum[1],
                mesh.boundsMinimum[2]));
        boundsMax = glm::max(
            boundsMax,
            glm::vec3(
                mesh.boundsMaximum[0],
                mesh.boundsMaximum[1],
                mesh.boundsMaximum[2]));
        impl_->vertexCount +=
            static_cast<std::uint32_t>(
                mesh.vertices.size());
        for (const auto& group : mesh.polygonGroups) {
            impl_->triangleCount +=
                static_cast<std::uint32_t>(
                    group.indices.size() / 3u);
            usedMaterials.insert(group.materialIndex);
        }
    }
    if (impl_->source.meshes.empty() ||
        !std::isfinite(boundsMin.x) ||
        !std::isfinite(boundsMax.x)) {
        if (outError) {
            *outError =
                "Environment PHLO contains no selected geometry.";
        }
        return false;
    }
    impl_->materialCount =
        static_cast<std::uint32_t>(
            usedMaterials.size());
    impl_->textureCount =
        static_cast<std::uint32_t>(
            impl_->source.textures.size());

    if (!game::runtime::lgpe_world_scene::
            prepareCanonicalScene(
                impl_->source,
                impl_->prepared,
                &error)) {
        if (outError) {
            *outError =
                "Could not prepare the environment prefab: " +
                error;
        }
        return false;
    }
    if (impl_->dynamicWind) {
        for (const auto& drawClass :
             impl_->prepared.frame.drawClasses) {
            if (drawClass.instances.size() != 1u) {
                if (outError) {
                    *outError =
                        "Environment prefab source draw is not singular.";
                }
                return false;
            }
            impl_->sourceDraws.push_back({
                drawClass.objectHandle,
                drawClass.instances.front().modelMatrix});
            if (drawClass.objectHandle.id > 0u &&
                drawClass.objectHandle.id <=
                    impl_->prepared.registry
                        .renderObjects.size()) {
                impl_->prepared.registry.renderObjects[
                    drawClass.objectHandle.id - 1u]
                    .skinned = true;
            }
        }
        for (const auto& drawClass :
             impl_->prepared.shadowFrame.drawClasses) {
            if (drawClass.instances.size() != 1u) {
                if (outError) {
                    *outError =
                        "Environment prefab shadow draw is not singular.";
                }
                return false;
            }
            impl_->shadowSourceDraws.push_back({
                drawClass.objectHandle,
                drawClass.instances.front().modelMatrix});
            if (drawClass.objectHandle.id > 0u &&
                drawClass.objectHandle.id <=
                    impl_->prepared.registry
                        .renderObjects.size()) {
                impl_->prepared.registry.renderObjects[
                    drawClass.objectHandle.id - 1u]
                    .skinned = true;
            }
        }
    }

    const glm::vec3 center(
        (boundsMin.x + boundsMax.x) * 0.5f,
        boundsMin.y,
        (boundsMin.z + boundsMax.z) * 0.5f);
    game::runtime::lgpe_route1_runtime::
        BoardLayoutTransform previewLayout;
    previewLayout.sourceUnitsToWorld = 0.01f;
    previewLayout.sourceAnchorCm = {
        center.x,
        center.y,
        center.z};
    previewLayout.worldAnchor = {0.0f, 0.0f, 0.0f};
    impl_->worldFromSource = glm::make_mat4(
        game::runtime::lgpe_route1_runtime::
            worldFromSourceMatrix(previewLayout)
            .data());
    impl_->sourceFromWorld =
        glm::inverse(impl_->worldFromSource);
    impl_->cloudProjectionRows =
        game::runtime::lgpe_route1_runtime::
            route1CloudProjectionRows(previewLayout);
    const glm::vec3 extent =
        (boundsMax - boundsMin) * 0.01f;
    impl_->boundsRadius =
        std::max(0.45f, glm::length(extent) * 0.5f);
    impl_->boundsCenterY =
        std::max(0.1f, extent.y * 0.5f);

    impl_->options = {};
    impl_->options.animationIndex =
        impl_->dynamicWind ? 0 : -1;
    impl_->options.animationPlaying =
        impl_->dynamicWind;
    impl_->updateWind();
    std::vector<PreparedScene*> scenes{
        &impl_->prepared};
    const std::array<float, 3> shadowCenter{
        center.x,
        center.y,
        center.z};
    if (!impl_->projectedShadowAtlas.build(
            scenes,
            shadowCenter,
            512,
            512,
            &error)) {
        if (outError) {
            *outError =
                "Could not build the prefab shadow atlas: " +
                error;
        }
        return false;
    }
    impl_->projectedShadowAtlas.attach(scenes);
    impl_->buildMaterialTemplates();
    impl_->status =
        impl_->dynamicWind
            ? "Exact cooked LGPE prefab; scene lighting inputs and the recovered four-second joint-wind driver are active."
            : impl_->sourceBoundary ==
                      "derived_tree_archetype_from_exact_route_mesh"
                ? "Tree archetype isolated from exact Route 1 mesh topology; source materials are active, all source placement centres are retained, and the source vertex program declares no local wind."
                : "Exact static LGPE environment prefab; source materials are active and the source vertex program declares no local wind.";
    impl_->ready = true;
    if (outError) {
        outError->clear();
    }
    return true;
}

engine::editor::EditorProjectAssetPreviewInfo
Route1EnvironmentPrefabPreview::info() const noexcept {
    return {
        .assetId = impl_->assetId.c_str(),
        .status = impl_->status.c_str(),
        .vertexCount = impl_->vertexCount,
        .triangleCount = impl_->triangleCount,
        .materialCount = impl_->materialCount,
        .textureCount = impl_->textureCount,
        .boneCount = static_cast<std::uint32_t>(
            impl_->source.bones.size()),
        .animationCount =
            impl_->dynamicWind ? 1u : 0u,
        .animationIndex =
            impl_->dynamicWind ? 0 : -1,
        .animationTimeSeconds =
            impl_->animationTime,
        .animationDurationSeconds =
            impl_->dynamicWind
                ? kWindPeriodSeconds
                : 0.0f,
        .boundsRadius = impl_->boundsRadius,
        .boundsCenterY = impl_->boundsCenterY,
        .ready = impl_->ready,
    };
}

engine::editor::EditorProjectAssetAnimation
Route1EnvironmentPrefabPreview::animation(
    std::size_t index) const noexcept {
    if (!impl_->dynamicWind || index != 0u) {
        return {};
    }
    return {
        .name = "LGPE Wind",
        .durationSeconds = kWindPeriodSeconds,
    };
}

void Route1EnvironmentPrefabPreview::setOptions(
    const engine::editor::
        EditorProjectAssetPreviewOptions& options) {
    impl_->options = options;
    impl_->options.animationIndex =
        impl_->dynamicWind ? 0 : -1;
    impl_->options.playbackSpeed =
        std::clamp(
            impl_->options.playbackSpeed,
            0.0f,
            4.0f);
    if (impl_->dynamicWind &&
        impl_->options.seekRequested) {
        impl_->animationTime = std::clamp(
            impl_->options.seekTimeSeconds,
            0.0f,
            std::nextafter(
                kWindPeriodSeconds,
                0.0f));
        impl_->updateWind();
    }
    impl_->options.seekRequested = false;
}

void Route1EnvironmentPrefabPreview::update(
    float deltaSeconds) {
    if (!impl_->ready ||
        !impl_->dynamicWind ||
        !impl_->options.animationPlaying) {
        return;
    }
    impl_->animationTime = std::fmod(
        impl_->animationTime +
            std::max(0.0f, deltaSeconds) *
                impl_->options.playbackSpeed,
        kWindPeriodSeconds);
    impl_->updateWind();
}

void Route1EnvironmentPrefabPreview::render(
    const engine::editor::
        EditorProjectRenderContext& context) {
    if (!impl_->ready ||
        !context.renderer ||
        !context.viewProjectionMatrix4x4) {
        return;
    }
    const int width = std::max(1, context.surfaceWidth);
    const int height = std::max(1, context.surfaceHeight);
    IRenderBackend& renderer = *context.renderer;
    renderer.beginWorldSceneColorPass(width, height);
    impl_->appendBatches();
    game::runtime::shared_world_batches::
        submitWorldIndexedBatches(
            renderer,
            impl_->batches,
            context.viewProjectionMatrix4x4,
            width,
            height,
            context.cameraWorldPosition3,
            context.cameraForward3,
            context.cameraTarget3);
    renderer.endWorldSceneColorPass();
}

} // namespace game::editor
