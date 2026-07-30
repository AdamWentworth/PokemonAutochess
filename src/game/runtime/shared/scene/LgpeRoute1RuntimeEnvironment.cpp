#include "game/runtime/shared/scene/LgpeRoute1RuntimeEnvironment.h"

#include "engine/assets/lgpe/LgpeCanonicalScene.h"
#include "engine/core/Environment.h"
#include "engine/core/IAssetStore.h"
#include "engine/render/LgpeFieldEncounterGrassMaterial.h"
#include "engine/render/LgpeFieldSmallGrassMaterial.h"
#include "game/runtime/shared/scene/LgpeRoute1ProjectedShadow.h"
#include "game/runtime/shared/scene/LgpeWorldSceneAdapter.h"
#include "game/runtime/shared/scene/SharedWorldScene.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace game::runtime::lgpe_route1_runtime {
namespace {

using CanonicalScene = engine::assets::lgpe::CanonicalScene;
using PreparedScene = lgpe_world_scene::PreparedScene;
using WorldBatch = shared_world_batches::WorldIndexedBatch;
using GridCell = std::pair<int, int>;

constexpr float kInitialWindPhaseCycles = 36.0f / 120.0f;
constexpr float kWindPeriodSeconds = 4.0f;

bool fail(std::string* outError, std::string message) {
    if (outError) {
        *outError = std::move(message);
    }
    return false;
}

bool materialFilterMatches(
    std::string_view filter,
    std::string_view materialName) {
    if (filter.empty()) {
        return true;
    }
    std::size_t begin = 0u;
    while (begin <= filter.size()) {
        const std::size_t end = filter.find('|', begin);
        const std::string_view candidate = filter.substr(
            begin,
            end == std::string_view::npos
                ? std::string_view::npos
                : end - begin);
        if (candidate == materialName) {
            return true;
        }
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1u;
    }
    return false;
}

std::array<float, 16> toArray(const glm::mat4& matrix) {
    std::array<float, 16> out{};
    std::copy(
        glm::value_ptr(matrix),
        glm::value_ptr(matrix) + out.size(),
        out.begin());
    return out;
}

std::array<float, 4> toArray(const glm::vec4& vector) {
    return {vector.x, vector.y, vector.z, vector.w};
}

glm::mat4 boardMatrix(const BoardLayoutTransform& transform) {
    glm::mat4 matrix(1.0f);
    matrix = glm::translate(
        matrix,
        glm::vec3(
            transform.worldAnchor[0],
            transform.worldAnchor[1],
            transform.worldAnchor[2]));
    matrix = glm::rotate(
        matrix,
        glm::radians(transform.yawDegrees),
        glm::vec3(0.0f, 1.0f, 0.0f));
    matrix = glm::scale(
        matrix,
        glm::vec3(transform.sourceUnitsToWorld));
    matrix = glm::translate(
        matrix,
        -glm::vec3(
            transform.sourceAnchorCm[0],
            transform.sourceAnchorCm[1],
            transform.sourceAnchorCm[2]));
    return matrix;
}

template <std::size_t N>
std::array<float, N> jsonFloatArray(
    const nlohmann::json& value,
    const char* label) {
    if (!value.is_array() || value.size() != N) {
        throw std::runtime_error(
            std::string(label) + " must contain " + std::to_string(N) +
            " numeric values.");
    }
    std::array<float, N> out{};
    for (std::size_t index = 0u; index < N; ++index) {
        if (!value[index].is_number()) {
            throw std::runtime_error(
                std::string(label) + " must contain only numeric values.");
        }
        out[index] = value[index].get<float>();
    }
    return out;
}

struct EncounterGrassPlacement {
    std::array<float, 3> center{};
    float phaseCycles = 0.0f;
};

struct EncounterGrassLayer {
    std::string logicalName;
    CanonicalScene source;
    PreparedScene scene;
    std::vector<EncounterGrassPlacement> placements;
    std::vector<std::vector<float>> skinPalettes;
    std::size_t instanceCount = 0u;
};

struct PlacedVegetationSourceDraw {
    IRenderBackend::WorldSceneRenderObjectHandle objectHandle{};
    std::array<float, 16> modelMatrix{};
};

struct PlacedVegetationLayer {
    std::string logicalName;
    CanonicalScene source;
    PreparedScene scene;
    std::vector<std::array<float, 16>> modelMatrices;
    std::vector<PlacedVegetationSourceDraw> sourceDraws;
    std::vector<PlacedVegetationSourceDraw> shadowSourceDraws;
    std::vector<float> skinPalette;
    std::size_t instanceCount = 0u;
};

struct SceneMaterialTemplates {
    PreparedScene* scene = nullptr;
    std::vector<WorldBatch> materials;
};

const IRenderBackend::WorldSceneRenderObject* renderObject(
    const shared_world_scene::WorldSceneRegistry& registry,
    IRenderBackend::WorldSceneRenderObjectHandle handle) {
    if (handle.id == 0u || handle.id > registry.renderObjects.size()) {
        return nullptr;
    }
    return &registry.renderObjects[handle.id - 1u];
}

const IRenderBackend::WorldSceneGeometry* geometry(
    const shared_world_scene::WorldSceneRegistry& registry,
    IRenderBackend::WorldSceneGeometryHandle handle) {
    if (handle.id == 0u || handle.id > registry.geometries.size()) {
        return nullptr;
    }
    return &registry.geometries[handle.id - 1u];
}

std::vector<EncounterGrassPlacement> expandedEncounterGrassPlacements(
    const nlohmann::json& record) {
    std::set<GridCell> core;
    for (const auto& cell : record.at("core_cells_source_xz")) {
        core.emplace(cell.at(0).get<int>(), cell.at(1).get<int>());
    }
    if (core.empty()) {
        throw std::runtime_error(
            "Encounter-grass collision footprint is empty.");
    }

    std::set<GridCell> expanded;
    for (const auto& cell : core) {
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                expanded.emplace(cell.first + dx, cell.second + dz);
            }
        }
    }

    const auto translation =
        record.at("translation_cm").get<std::array<float, 3>>();
    const int recordIndex = record.at("record_index").get<int>();
    std::vector<EncounterGrassPlacement> placements;
    placements.reserve(expanded.size());
    for (const auto& cell : expanded) {
        float gridX = static_cast<float>(cell.first);
        float gridZ = static_cast<float>(cell.second);
        if (core.find(cell) == core.end()) {
            const auto nearest = std::min_element(
                core.begin(),
                core.end(),
                [&](const GridCell& lhs, const GridCell& rhs) {
                    const int lhsDx = lhs.first - cell.first;
                    const int lhsDz = lhs.second - cell.second;
                    const int rhsDx = rhs.first - cell.first;
                    const int rhsDz = rhs.second - cell.second;
                    return std::array<int, 3>{
                               lhsDx * lhsDx + lhsDz * lhsDz,
                               lhs.first,
                               lhs.second} <
                           std::array<int, 3>{
                               rhsDx * rhsDx + rhsDz * rhsDz,
                               rhs.first,
                               rhs.second};
                });
            gridX =
                0.5f * (gridX + static_cast<float>(nearest->first));
            gridZ =
                0.5f * (gridZ + static_cast<float>(nearest->second));
        }
        EncounterGrassPlacement placement;
        placement.center = {
            translation[0] + (gridX + 0.5f) * 100.0f,
            translation[1],
            translation[2] + (gridZ + 0.5f) * 100.0f};
        placement.phaseCycles = std::fmod(
            static_cast<float>(recordIndex) * 0.173f +
                gridX * 0.127f + gridZ * 0.193f,
            1.0f);
        if (placement.phaseCycles < 0.0f) {
            placement.phaseCycles += 1.0f;
        }
        placements.push_back(placement);
    }
    return placements;
}

std::vector<float> encounterGrassSkinPalette(
    engine::render::lgpe_field_encounter_grass::SourceVariant variant,
    std::size_t jointCount,
    float placementPhaseCycles,
    float windPhaseCycles) {
    std::vector<float> palette(jointCount * 16u, 0.0f);
    for (std::size_t joint = 0u; joint < jointCount; ++joint) {
        const auto rotation =
            engine::render::lgpe_field_encounter_grass::
                evaluateWindJointRotation(
                    static_cast<std::uint32_t>(joint),
                    placementPhaseCycles,
                    windPhaseCycles);
        const auto pivotValues =
            engine::render::lgpe_field_encounter_grass::sourceJointPivot(
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

void placeEncounterGrassLayer(
    EncounterGrassLayer& layer,
    float windPhaseCycles) {
    std::vector<IRenderBackend::WorldSceneRenderObjectHandle> objects;
    objects.reserve(layer.scene.frame.drawClasses.size());
    for (const auto& drawClass : layer.scene.frame.drawClasses) {
        objects.push_back(drawClass.objectHandle);
    }
    std::vector<IRenderBackend::WorldSceneRenderObjectHandle> shadowObjects;
    shadowObjects.reserve(layer.scene.shadowFrame.drawClasses.size());
    for (const auto& drawClass : layer.scene.shadowFrame.drawClasses) {
        shadowObjects.push_back(drawClass.objectHandle);
    }

    shared_world_scene::beginWorldSceneFrame(layer.scene.frame);
    shared_world_scene::beginWorldSceneFrame(layer.scene.shadowFrame);
    layer.skinPalettes.resize(layer.placements.size());
    const auto variant =
        layer.logicalName == "enc_grass02"
        ? engine::render::lgpe_field_encounter_grass::SourceVariant::Grass02
        : engine::render::lgpe_field_encounter_grass::SourceVariant::Grass01;
    std::uint32_t instanceId = 1u;
    for (std::size_t placementIndex = 0u;
         placementIndex < layer.placements.size();
         ++placementIndex) {
        const auto& placement = layer.placements[placementIndex];
        const std::array<float, 16> modelMatrix{
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            placement.center[0],
            placement.center[1],
            placement.center[2],
            1.0f};
        const auto nextPalette = encounterGrassSkinPalette(
            variant,
            layer.source.bones.size(),
            placement.phaseCycles,
            windPhaseCycles);
        auto& palette = layer.skinPalettes[placementIndex];
        palette.resize(nextPalette.size());
        std::copy(
            nextPalette.begin(),
            nextPalette.end(),
            palette.begin());
        for (const auto objectHandle : objects) {
            IRenderBackend::WorldSceneRenderInstanceHandle handle{};
            handle.id = instanceId++;
            shared_world_scene::appendSkinnedInstance(
                layer.scene.frame,
                objectHandle,
                handle,
                modelMatrix,
                1.0f,
                1.0f,
                1.0f,
                1.0f,
                0.0f,
                0u,
                static_cast<std::uint32_t>(layer.source.bones.size()),
                palette.data());
        }
        for (const auto objectHandle : shadowObjects) {
            IRenderBackend::WorldSceneRenderInstanceHandle handle{};
            handle.id = instanceId++;
            shared_world_scene::appendSkinnedInstance(
                layer.scene.shadowFrame,
                objectHandle,
                handle,
                modelMatrix,
                1.0f,
                1.0f,
                1.0f,
                1.0f,
                0.0f,
                0u,
                static_cast<std::uint32_t>(layer.source.bones.size()),
                palette.data());
        }
    }
    layer.instanceCount = layer.placements.size();
}

std::array<float, 16> sourcePlacementMatrix(
    const nlohmann::json& placement) {
    const auto translation =
        placement.at("translation_cm").get<std::array<float, 3>>();
    const auto rotation =
        placement.at("rotation_degrees").get<std::array<float, 3>>();
    const auto scale =
        placement.at("scale").get<std::array<float, 3>>();
    return toArray(
        glm::translate(
            glm::mat4(1.0f),
            glm::vec3(translation[0], translation[1], translation[2])) *
        glm::rotate(
            glm::mat4(1.0f),
            glm::radians(rotation[0]),
            glm::vec3(1.0f, 0.0f, 0.0f)) *
        glm::rotate(
            glm::mat4(1.0f),
            glm::radians(rotation[1]),
            glm::vec3(0.0f, 1.0f, 0.0f)) *
        glm::rotate(
            glm::mat4(1.0f),
            glm::radians(rotation[2]),
            glm::vec3(0.0f, 0.0f, 1.0f)) *
        glm::scale(
            glm::mat4(1.0f),
            glm::vec3(scale[0], scale[1], scale[2])));
}

std::vector<float> vegetationSkinPalette(
    const CanonicalScene& source,
    float windPhaseCycles) {
    std::vector<float> palette(source.bones.size() * 16u, 0.0f);
    std::vector<glm::mat4> restWorld(
        source.bones.size(),
        glm::mat4(1.0f));
    for (std::size_t index = 0u; index < source.bones.size(); ++index) {
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
                    static_cast<std::size_t>(bone.parentIndex) < index
            ? restWorld[static_cast<std::size_t>(bone.parentIndex)] * local
            : local;

        glm::mat4 jointMatrix(1.0f);
        constexpr std::string_view kGrassJointPrefix = "grass_joint";
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
                    static_cast<std::uint32_t>(std::stoul(suffix)) + 1u;
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

void placeVegetationLayer(
    PlacedVegetationLayer& layer,
    float windPhaseCycles) {
    if (layer.sourceDraws.empty()) {
        layer.sourceDraws.reserve(layer.scene.frame.drawClasses.size());
        for (const auto& drawClass : layer.scene.frame.drawClasses) {
            if (drawClass.instances.size() != 1u) {
                throw std::runtime_error(
                    layer.logicalName +
                    " source draw does not contain one authored transform.");
            }
            layer.sourceDraws.push_back(
                {drawClass.objectHandle,
                 drawClass.instances.front().modelMatrix});
            if (drawClass.objectHandle.id > 0u &&
                drawClass.objectHandle.id <=
                    layer.scene.registry.renderObjects.size()) {
                layer.scene.registry.renderObjects[
                    drawClass.objectHandle.id - 1u]
                    .skinned = true;
            }
        }
        layer.shadowSourceDraws.reserve(
            layer.scene.shadowFrame.drawClasses.size());
        for (const auto& drawClass : layer.scene.shadowFrame.drawClasses) {
            if (drawClass.instances.size() != 1u) {
                throw std::runtime_error(
                    layer.logicalName +
                    " shadow draw does not contain one authored transform.");
            }
            layer.shadowSourceDraws.push_back(
                {drawClass.objectHandle,
                 drawClass.instances.front().modelMatrix});
            if (drawClass.objectHandle.id > 0u &&
                drawClass.objectHandle.id <=
                    layer.scene.registry.renderObjects.size()) {
                layer.scene.registry.renderObjects[
                    drawClass.objectHandle.id - 1u]
                    .skinned = true;
            }
        }
    }

    shared_world_scene::beginWorldSceneFrame(layer.scene.frame);
    shared_world_scene::beginWorldSceneFrame(layer.scene.shadowFrame);
    const auto nextPalette =
        vegetationSkinPalette(layer.source, windPhaseCycles);
    layer.skinPalette.resize(nextPalette.size());
    std::copy(
        nextPalette.begin(),
        nextPalette.end(),
        layer.skinPalette.begin());
    std::uint32_t instanceId = 1u;
    for (const auto& placementMatrix : layer.modelMatrices) {
        for (const auto& sourceDraw : layer.sourceDraws) {
            const auto composed = toArray(
                glm::make_mat4(placementMatrix.data()) *
                glm::make_mat4(sourceDraw.modelMatrix.data()));
            IRenderBackend::WorldSceneRenderInstanceHandle handle{};
            handle.id = instanceId++;
            shared_world_scene::appendSkinnedInstance(
                layer.scene.frame,
                sourceDraw.objectHandle,
                handle,
                composed,
                1.0f,
                1.0f,
                1.0f,
                1.0f,
                0.0f,
                0u,
                static_cast<std::uint32_t>(layer.source.bones.size()),
                layer.skinPalette.data());
        }
        for (const auto& sourceDraw : layer.shadowSourceDraws) {
            const auto composed = toArray(
                glm::make_mat4(placementMatrix.data()) *
                glm::make_mat4(sourceDraw.modelMatrix.data()));
            IRenderBackend::WorldSceneRenderInstanceHandle handle{};
            handle.id = instanceId++;
            shared_world_scene::appendSkinnedInstance(
                layer.scene.shadowFrame,
                sourceDraw.objectHandle,
                handle,
                composed,
                1.0f,
                1.0f,
                1.0f,
                1.0f,
                0.0f,
                0u,
                static_cast<std::uint32_t>(layer.source.bones.size()),
                layer.skinPalette.data());
        }
    }
    layer.instanceCount = layer.modelMatrices.size();
}

bool loadJson(
    const engine::IAssetStore& store,
    const std::string& virtualPath,
    nlohmann::json& out,
    std::string* outError) {
    std::string text;
    if (!store.readText(virtualPath, text, outError)) {
        return false;
    }
    try {
        out = nlohmann::json::parse(text);
        return true;
    } catch (const std::exception& ex) {
        return fail(
            outError,
            "Could not parse " + virtualPath + ": " + ex.what());
    }
}

} // namespace

struct RuntimeEnvironment::Impl {
    BoardLayoutTransform layout;
    RuntimeStats stats;
    bool isLoaded = false;
    CanonicalScene source;
    PreparedScene scene;
    std::vector<EncounterGrassLayer> encounterGrass;
    std::vector<PlacedVegetationLayer> placedVegetation;
    lgpe_route1_projected_shadow::Atlas projectedShadowAtlas;
    std::vector<PreparedScene*> scenes;
    std::vector<SceneMaterialTemplates> materialTemplates;
    glm::mat4 worldFromSource{1.0f};
    glm::mat4 sourceFromWorld{1.0f};
    LightProjectionRows cloudProjectionRows;
    std::string materialFilter;

    void updateWind(float simulationSeconds) {
        const float phase = kInitialWindPhaseCycles +
            simulationSeconds / kWindPeriodSeconds;
        for (auto& layer : encounterGrass) {
            placeEncounterGrassLayer(layer, phase);
        }
        for (auto& layer : placedVegetation) {
            placeVegetationLayer(layer, phase);
        }
    }

    void rebuildMaterialTemplates() {
        materialTemplates.clear();
        materialTemplates.reserve(scenes.size());
        for (PreparedScene* prepared : scenes) {
            materialTemplates.emplace_back();
            auto& set = materialTemplates.back();
            set.scene = prepared;
            set.materials.reserve(prepared->registry.materials.size());
            for (const auto& material : prepared->registry.materials) {
                auto batch =
                    shared_world_scene::makeWorldIndexedMaterialTemplate(
                        material);
                batch.lightProjectionUvRowU = cloudProjectionRows.u;
                batch.lightProjectionUvRowV = cloudProjectionRows.v;
                if (batch.projectedShadowEnabled != 0u) {
                    batch.projectedShadowMatrix = toArray(
                        glm::make_mat4(
                            batch.projectedShadowMatrix.data()) *
                        sourceFromWorld);
                }
                set.materials.push_back(std::move(batch));
            }
        }
    }

    void appendScene(
        SceneMaterialTemplates& set,
        float,
        std::vector<WorldBatch>& out) {
        if (!set.scene) {
            return;
        }
        const auto& registry = set.scene->registry;
        for (const auto& drawClass : set.scene->frame.drawClasses) {
            const auto* object =
                renderObject(registry, drawClass.objectHandle);
            if (!object || object->materialHandle.id == 0u ||
                object->materialHandle.id > set.materials.size()) {
                continue;
            }
            const auto& sourceMaterial =
                registry.materials[object->materialHandle.id - 1u];
            if (!materialFilterMatches(
                    materialFilter,
                    sourceMaterial.sourceMaterialName)) {
                continue;
            }
            const auto* mesh = geometry(registry, object->geometryHandle);
            if (!mesh || !mesh->vertices || !mesh->indices ||
                mesh->vertexCount == 0u || mesh->indexCount < 3u) {
                continue;
            }

            WorldBatch batch{};
            batch.sharedTemplate =
                &set.materials[object->materialHandle.id - 1u];
            batch.sharedVertices = mesh->vertices;
            batch.sharedVertexCount = mesh->vertexCount;
            batch.sharedIndices = mesh->indices;
            batch.sharedIndexCount = mesh->indexCount;
            batch.geometryCacheKey = mesh->geometryCacheKey;
            batch.preserveSubmissionOrder = true;
            batch.instances.reserve(drawClass.instances.size());
            for (const auto& instance : drawClass.instances) {
                IRenderBackend::WorldMeshInstance worldInstance{};
                worldInstance.modelMatrix = toArray(
                    worldFromSource *
                    glm::make_mat4(instance.modelMatrix.data()));
                worldInstance.vertexColorMulR =
                    instance.vertexColorMulR;
                worldInstance.vertexColorMulG =
                    instance.vertexColorMulG;
                worldInstance.vertexColorMulB =
                    instance.vertexColorMulB;
                worldInstance.vertexColorMulA =
                    instance.vertexColorMulA;
                worldInstance.gpuSkinning = instance.gpuSkinning;
                worldInstance.gpuSkinningMode =
                    instance.gpuSkinningMode;
                worldInstance.skinMatrixCount =
                    instance.skinMatrixCount;
                worldInstance.skinMatrices = instance.skinMatrices;
                batch.instances.push_back(worldInstance);
            }
            if (!batch.instances.empty()) {
                out.push_back(std::move(batch));
            }
        }
    }
};

bool loadBoardLayoutTransform(
    const engine::IAssetStore& store,
    const std::string& virtualPath,
    BoardLayoutTransform& out,
    std::string* outError) {
    nlohmann::json root;
    if (!loadJson(store, virtualPath, root, outError)) {
        return false;
    }
    try {
        if (root.at("schema_version").get<int>() != 1 ||
            root.at("kind").get<std::string>() !=
                "lgpe_route1_board_layout_delta") {
            return fail(
                outError,
                "Unsupported Route 1 board-layout manifest contract.");
        }
        BoardLayoutTransform decoded;
        decoded.coordinateSystem =
            root.at("coordinate_system").get<std::string>();
        decoded.sourceProfileId =
            root.at("source_profile_id").get<std::string>();
        const auto& transform = root.at("source_to_world");
        decoded.sourceUnitsToWorld =
            transform.at("source_units_to_world").get<float>();
        decoded.sourceAnchorCm = jsonFloatArray<3>(
            transform.at("source_anchor_cm"),
            "source_anchor_cm");
        decoded.worldAnchor = jsonFloatArray<3>(
            transform.at("world_anchor"),
            "world_anchor");
        decoded.yawDegrees =
            transform.at("yaw_degrees").get<float>();
        decoded.declaredLocalDeltaCount = static_cast<std::uint32_t>(
            root.at("local_layout_deltas").size());
        if (decoded.coordinateSystem !=
                "source_centimetres_xyz_y_up" ||
            decoded.sourceProfileId != "lgpe_route1_road001_00" ||
            !std::isfinite(decoded.sourceUnitsToWorld) ||
            decoded.sourceUnitsToWorld <= 0.0f ||
            !std::isfinite(decoded.yawDegrees)) {
            return fail(
                outError,
                "Route 1 board-layout manifest has invalid source metadata "
                "or transform values.");
        }
        out = std::move(decoded);
        return true;
    } catch (const std::exception& ex) {
        return fail(
            outError,
            "Invalid Route 1 board-layout manifest: " +
                std::string(ex.what()));
    }
}

std::array<float, 16> worldFromSourceMatrix(
    const BoardLayoutTransform& transform) {
    return toArray(boardMatrix(transform));
}

std::array<float, 16> sourceFromWorldMatrix(
    const BoardLayoutTransform& transform) {
    return toArray(glm::inverse(boardMatrix(transform)));
}

LightProjectionRows route1CloudProjectionRows(
    const BoardLayoutTransform& transform) {
    namespace small_grass =
        engine::render::lgpe_field_small_grass;
    const glm::mat4 transposeSourceFromWorld =
        glm::transpose(glm::inverse(boardMatrix(transform)));
    const glm::vec4 sourceU{
        small_grass::kRoute1CloudProjectionU[0],
        small_grass::kRoute1CloudProjectionU[1],
        small_grass::kRoute1CloudProjectionU[2],
        small_grass::kRoute1CloudProjectionOffset[0]};
    const glm::vec4 sourceV{
        small_grass::kRoute1CloudProjectionV[0],
        small_grass::kRoute1CloudProjectionV[1],
        small_grass::kRoute1CloudProjectionV[2],
        small_grass::kRoute1CloudProjectionOffset[1]};
    return {
        toArray(transposeSourceFromWorld * sourceU),
        toArray(transposeSourceFromWorld * sourceV)};
}

RuntimeEnvironment::RuntimeEnvironment()
    : impl_(std::make_unique<Impl>()) {}

RuntimeEnvironment::~RuntimeEnvironment() = default;
RuntimeEnvironment::RuntimeEnvironment(RuntimeEnvironment&&) noexcept =
    default;
RuntimeEnvironment& RuntimeEnvironment::operator=(
    RuntimeEnvironment&&) noexcept = default;

bool RuntimeEnvironment::load(
    const engine::IAssetStore& store,
    const std::string& canonicalRoot,
    const std::string& compositionManifestPath,
    const std::string& boardLayoutManifestPath,
    std::string* outError) {
    auto loaded = std::make_unique<Impl>();
    if (const auto filter =
            engine::env::get("PAC_LGPE_ROUTE1_MATERIAL_FILTER")) {
        loaded->materialFilter = *filter;
    }
    if (!loadBoardLayoutTransform(
            store,
            boardLayoutManifestPath,
            loaded->layout,
            outError)) {
        return false;
    }
    loaded->worldFromSource = boardMatrix(loaded->layout);
    loaded->sourceFromWorld = glm::inverse(loaded->worldFromSource);
    loaded->cloudProjectionRows =
        route1CloudProjectionRows(loaded->layout);

    std::string error;
    if (!engine::assets::lgpe::loadCanonicalScene(
            store,
            canonicalRoot,
            loaded->source,
            &error)) {
        return fail(
            outError,
            "Could not load canonical Route 1: " + error);
    }
    if (loaded->source.profileId != loaded->layout.sourceProfileId) {
        return fail(
            outError,
            "Route 1 board-layout source profile does not match the "
            "canonical scene.");
    }
    if (!lgpe_world_scene::prepareCanonicalScene(
            loaded->source,
            loaded->scene,
            &error)) {
        return fail(
            outError,
            "Could not prepare canonical Route 1: " + error);
    }

    nlohmann::json composition;
    if (!loadJson(
            store,
            compositionManifestPath,
            composition,
            outError)) {
        return false;
    }
    try {
        if (composition.at("coordinate_system") !=
            "source_centimetres_xyz_y_up") {
            return fail(
                outError,
                "Route 1 composition coordinate system changed.");
        }
        const auto& encounter = composition.at("encounter_grass");
        const auto& encounterModels = encounter.at("models");
        std::map<
            std::string,
            std::vector<EncounterGrassPlacement>>
            placementsByModel;
        for (const auto& record : encounter.at("records")) {
            auto expanded = expandedEncounterGrassPlacements(record);
            auto& placements =
                placementsByModel[record.at("model").get<std::string>()];
            placements.insert(
                placements.end(),
                expanded.begin(),
                expanded.end());
        }
        loaded->encounterGrass.reserve(placementsByModel.size());
        for (auto& [logicalName, placements] : placementsByModel) {
            loaded->encounterGrass.emplace_back();
            auto& layer = loaded->encounterGrass.back();
            layer.logicalName = logicalName;
            layer.placements = std::move(placements);
            const std::string modelRoot =
                encounterModels.at(logicalName).get<std::string>();
            if (!engine::assets::lgpe::loadCanonicalScene(
                    store,
                    modelRoot,
                    layer.source,
                    &error) ||
                !lgpe_world_scene::prepareCanonicalScene(
                    layer.source,
                    layer.scene,
                    &error)) {
                return fail(
                    outError,
                    "Could not prepare " + logicalName + ": " + error);
            }
            if (layer.scene.stats
                    .fieldEncounterGrassSurfaceMaterialCount != 1u) {
                return fail(
                    outError,
                    logicalName +
                        " is not one FieldEncGrassShader01 surface.");
            }
            placeEncounterGrassLayer(
                layer,
                kInitialWindPhaseCycles);
        }

        const auto& vegetation =
            composition.at("buildmodel_vegetation");
        const std::string placementManifestPath =
            vegetation.at("placement_manifest").get<std::string>();
        const std::size_t expectedVegetationInstances =
            vegetation.at("expected_instance_count").get<std::size_t>();
        nlohmann::json placementRoot;
        if (!loadJson(
                store,
                placementManifestPath,
                placementRoot,
                outError)) {
            return false;
        }
        if (placementRoot.at("coordinate_system") !=
                "source_centimetres_xyz_y_up" ||
            placementRoot.at("instance_count").get<std::size_t>() !=
                expectedVegetationInstances) {
            return fail(
                outError,
                "Route 1 build-model placement contract changed.");
        }
        const auto& models = placementRoot.at("models");
        const std::array<std::string, 3> logicalNames{
            "grass02",
            "flowers02",
            "flowers04"};
        loaded->placedVegetation.reserve(logicalNames.size());
        std::size_t loadedVegetationInstances = 0u;
        for (const auto& logicalName : logicalNames) {
            const auto& model = models.at(logicalName);
            loaded->placedVegetation.emplace_back();
            auto& layer = loaded->placedVegetation.back();
            layer.logicalName = logicalName;
            if (!engine::assets::lgpe::loadCanonicalScene(
                    store,
                    model.at("cache_root").get<std::string>(),
                    layer.source,
                    &error) ||
                !lgpe_world_scene::prepareCanonicalScene(
                    layer.source,
                    layer.scene,
                    &error)) {
                return fail(
                    outError,
                    "Could not prepare " + logicalName + ": " + error);
            }
            for (const auto& placement : model.at("placements")) {
                layer.modelMatrices.push_back(
                    sourcePlacementMatrix(placement));
            }
            const std::size_t expectedModelInstances =
                model.at("instance_count").get<std::size_t>();
            if (layer.modelMatrices.size() !=
                expectedModelInstances) {
                return fail(
                    outError,
                    logicalName + " placement count changed.");
            }
            placeVegetationLayer(
                layer,
                kInitialWindPhaseCycles);
            loadedVegetationInstances += layer.instanceCount;
        }
        if (loadedVegetationInstances != expectedVegetationInstances) {
            return fail(
                outError,
                "Route 1 placed-vegetation total changed.");
        }
    } catch (const std::exception& ex) {
        return fail(
            outError,
            "Could not compose Route 1 runtime layers: " +
                std::string(ex.what()));
    }

    loaded->scenes.reserve(
        1u + loaded->encounterGrass.size() +
        loaded->placedVegetation.size());
    loaded->scenes.push_back(&loaded->scene);
    for (auto& layer : loaded->encounterGrass) {
        loaded->scenes.push_back(&layer.scene);
        loaded->stats.encounterGrassInstanceCount +=
            static_cast<std::uint32_t>(layer.instanceCount);
    }
    for (auto& layer : loaded->placedVegetation) {
        loaded->scenes.push_back(&layer.scene);
        loaded->stats.placedVegetationInstanceCount +=
            static_cast<std::uint32_t>(layer.instanceCount);
    }

    const std::array<float, 3> shadowCenter{
        loaded->layout.sourceAnchorCm[0],
        loaded->layout.sourceAnchorCm[1],
        loaded->layout.sourceAnchorCm[2]};
    if (!loaded->projectedShadowAtlas.build(
            loaded->scenes,
            shadowCenter,
            &error)) {
        return fail(
            outError,
            "Could not build Route 1 projected shadow: " + error);
    }
    loaded->projectedShadowAtlas.attach(loaded->scenes);
    loaded->stats.sceneCount =
        static_cast<std::uint32_t>(loaded->scenes.size());
    for (const PreparedScene* prepared : loaded->scenes) {
        loaded->stats.materialCount +=
            static_cast<std::uint32_t>(
                prepared->registry.materials.size());
        loaded->stats.drawClassCount +=
            static_cast<std::uint32_t>(
                prepared->frame.drawClasses.size());
        for (const auto& drawClass : prepared->frame.drawClasses) {
            const auto* object =
                renderObject(prepared->registry, drawClass.objectHandle);
            const auto* mesh =
                object
                ? geometry(
                      prepared->registry,
                      object->geometryHandle)
                : nullptr;
            if (mesh) {
                loaded->stats.visibleTriangleCount +=
                    (mesh->indexCount / 3u) *
                    drawClass.instances.size();
            }
        }
    }
    loaded->stats.shadowTriangleCount =
        loaded->projectedShadowAtlas.stats().submittedTriangleCount;
    loaded->rebuildMaterialTemplates();
    loaded->isLoaded = true;
    impl_ = std::move(loaded);
    return true;
}

bool RuntimeEnvironment::loaded() const noexcept {
    return impl_ && impl_->isLoaded;
}

const BoardLayoutTransform& RuntimeEnvironment::layout() const noexcept {
    return impl_->layout;
}

const RuntimeStats& RuntimeEnvironment::stats() const noexcept {
    return impl_->stats;
}

void RuntimeEnvironment::updateAnimation(float simulationSeconds) {
    if (loaded()) {
        impl_->updateWind(simulationSeconds);
    }
}

void RuntimeEnvironment::appendIndexedBatches(
    float simulationSeconds,
    std::vector<shared_world_batches::WorldIndexedBatch>& out) {
    if (!loaded()) {
        return;
    }
    for (auto& set : impl_->materialTemplates) {
        impl_->appendScene(set, simulationSeconds, out);
    }
}

} // namespace game::runtime::lgpe_route1_runtime
