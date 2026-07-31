#include "game/runtime/shared/scene/LgpeRoute1RuntimeEnvironment.h"

#include "engine/assets/lgpe/LgpeCanonicalScene.h"
#include "engine/core/Environment.h"
#include "engine/core/IAssetStore.h"
#include "engine/render/LgpeFieldEncounterGrassMaterial.h"
#include "engine/render/LgpeFieldSmallGrassMaterial.h"
#include "game/runtime/shared/scene/LgpeRoute1ProjectedShadow.h"
#include "game/runtime/shared/scene/LgpeRoute1TreeInstances.h"
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
#include <unordered_map>
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

struct PlacedVegetationSourceDraw {
    IRenderBackend::WorldSceneRenderObjectHandle objectHandle{};
    std::array<float, 16> modelMatrix{};
};

struct EncounterGrassPlacement {
    std::uint32_t recordIndex = 0u;
    std::array<float, 3> sourceCenter{};
    std::array<float, 3> center{};
    std::array<float, 16> modelMatrix{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
    float phaseCycles = 0.0f;
    bool suppressed = false;
};

struct EncounterGrassRecord {
    std::string stableId;
    std::string logicalName;
    std::uint32_t recordIndex = 0u;
    std::array<float, 3> sourceTranslationCm{};
    std::array<float, 3> translationCm{};
    std::array<float, 3> rotationDegrees{};
    std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
    bool suppressed = false;
    bool hasOverride = false;
    bool authored = false;
    std::string reason;
};

struct EncounterGrassLayer {
    std::string logicalName;
    CanonicalScene source;
    PreparedScene scene;
    std::vector<EncounterGrassPlacement> placements;
    std::vector<PlacedVegetationSourceDraw> sourceDraws;
    std::vector<PlacedVegetationSourceDraw> shadowSourceDraws;
    std::vector<std::vector<float>> skinPalettes;
    std::size_t canonicalPlacementCount = 0u;
    std::size_t instanceCount = 0u;
};

struct PlacedVegetationPlacement {
    std::string stableId;
    std::uint32_t recordIndex = 0u;
    std::array<float, 3> sourceTranslationCm{};
    std::array<float, 3> sourceRotationDegrees{};
    std::array<float, 3> sourceScale{1.0f, 1.0f, 1.0f};
    std::array<float, 3> translationCm{};
    std::array<float, 3> rotationDegrees{};
    std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
    std::array<float, 16> modelMatrix{};
    bool suppressed = false;
    bool hasOverride = false;
    bool authored = false;
    std::string reason;
};

struct PlacedVegetationLayer {
    std::string logicalName;
    CanonicalScene source;
    PreparedScene scene;
    std::vector<PlacedVegetationPlacement> placements;
    std::vector<PlacedVegetationSourceDraw> sourceDraws;
    std::vector<PlacedVegetationSourceDraw> shadowSourceDraws;
    std::vector<float> skinPalette;
    std::size_t canonicalPlacementCount = 0u;
    std::size_t instanceCount = 0u;
};

struct CanonicalMeshGroup {
    std::string stableId;
    std::string displayName;
    std::string categoryPath;
    std::string prefabAssetId;
    std::string logicalName;
    std::uint32_t sourceMeshIndex = 0u;
    std::array<float, 16> sourceModelMatrix{};
    std::array<float, 3> sourcePivotCm{};
    std::array<float, 3> translationCm{};
    std::array<float, 3> rotationDegrees{};
    std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
    bool suppressed = false;
    bool hasOverride = false;
    std::string reason;
};

struct CanonicalTreeInstance {
    std::string stableId;
    std::string logicalName;
    std::string prefabAssetId;
    std::uint32_t sourceMeshIndex = 0u;
    std::uint32_t recordIndex = 0u;
    std::array<float, 16> sourceModelMatrix{};
    std::array<float, 3> sourcePivotCm{};
    std::array<float, 3> groupBaselinePivotCm{};
    std::array<float, 3> translationCm{};
    std::array<float, 3> rotationDegrees{};
    std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
    std::vector<
        IRenderBackend::WorldSceneRenderObjectHandle>
        objectHandles;
    bool suppressed = false;
    bool hasOverride = false;
    std::string reason;
};

struct SceneMaterialTemplates {
    PreparedScene* scene = nullptr;
    std::vector<WorldBatch> materials;
};

std::array<float, 16> sourcePlacementMatrix(
    const std::array<float, 3>& translation,
    const std::array<float, 3>& rotation,
    const std::array<float, 3>& scale);

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
        placement.recordIndex =
            static_cast<std::uint32_t>(recordIndex);
        placement.sourceCenter = {
            translation[0] + (gridX + 0.5f) * 100.0f,
            translation[1],
            translation[2] + (gridZ + 0.5f) * 100.0f};
        placement.center = placement.sourceCenter;
        placement.modelMatrix =
            sourcePlacementMatrix(
                placement.center,
                {0.0f, 0.0f, 0.0f},
                {1.0f, 1.0f, 1.0f});
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
    if (layer.sourceDraws.empty()) {
        layer.sourceDraws.reserve(
            layer.scene.frame.drawClasses.size());
        for (const auto& drawClass :
             layer.scene.frame.drawClasses) {
            if (drawClass.instances.size() != 1u) {
                throw std::runtime_error(
                    layer.logicalName +
                    " encounter-grass source draw does not contain "
                    "one authored transform.");
            }
            layer.sourceDraws.push_back(
                {drawClass.objectHandle,
                 drawClass.instances.front().modelMatrix});
        }
        layer.shadowSourceDraws.reserve(
            layer.scene.shadowFrame.drawClasses.size());
        for (const auto& drawClass :
             layer.scene.shadowFrame.drawClasses) {
            if (drawClass.instances.size() != 1u) {
                throw std::runtime_error(
                    layer.logicalName +
                    " encounter-grass shadow draw does not contain "
                    "one authored transform.");
            }
            layer.shadowSourceDraws.push_back(
                {drawClass.objectHandle,
                 drawClass.instances.front().modelMatrix});
        }
    }

    shared_world_scene::beginWorldSceneFrame(layer.scene.frame);
    shared_world_scene::beginWorldSceneFrame(layer.scene.shadowFrame);
    layer.skinPalettes.resize(layer.placements.size());
    const auto variant =
        layer.logicalName == "enc_grass02"
        ? engine::render::lgpe_field_encounter_grass::SourceVariant::Grass02
        : engine::render::lgpe_field_encounter_grass::SourceVariant::Grass01;
    std::uint32_t instanceId = 1u;
    std::size_t visiblePlacementCount = 0u;
    for (std::size_t placementIndex = 0u;
         placementIndex < layer.placements.size();
         ++placementIndex) {
        const auto& placement = layer.placements[placementIndex];
        if (placement.suppressed) {
            continue;
        }
        ++visiblePlacementCount;
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
        for (const auto& sourceDraw : layer.sourceDraws) {
            const auto composed = toArray(
                glm::make_mat4(
                    placement.modelMatrix.data()) *
                glm::make_mat4(
                    sourceDraw.modelMatrix.data()));
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
                palette.data());
        }
        for (const auto& sourceDraw :
             layer.shadowSourceDraws) {
            const auto composed = toArray(
                glm::make_mat4(
                    placement.modelMatrix.data()) *
                glm::make_mat4(
                    sourceDraw.modelMatrix.data()));
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
                palette.data());
        }
    }
    layer.instanceCount = visiblePlacementCount;
}

std::array<float, 16> sourcePlacementMatrix(
    const std::array<float, 3>& translation,
    const std::array<float, 3>& rotation,
    const std::array<float, 3>& scale) {
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

std::string placementStableId(
    std::string_view logicalName,
    std::uint32_t recordIndex) {
    return "buildmodel-vegetation/" +
        std::string(logicalName) +
        "/record-" +
        std::to_string(recordIndex);
}

std::string encounterGrassStableId(
    std::string_view logicalName,
    std::uint32_t recordIndex) {
    return "encounter-grass/" +
        std::string(logicalName) +
        "/record-" +
        std::to_string(recordIndex);
}

std::string canonicalMeshStableId(
    std::uint32_t sourceMeshIndex) {
    return "canonical-mesh/mesh-" +
        std::to_string(sourceMeshIndex);
}

std::string treeLogicalName(
    std::uint32_t sourceMeshIndex) {
    return "tree_00" +
        std::to_string(sourceMeshIndex - 9u);
}

std::string treeInstanceStableId(
    std::string_view logicalName,
    std::uint32_t recordIndex) {
    return "canonical-tree/" +
        std::string(logicalName) +
        "/instance-" +
        std::to_string(recordIndex);
}

std::string canonicalMeshDisplayName(
    std::uint32_t index,
    std::string_view sourceName) {
    if (index == 0u) return "Ground Blend Overlay";
    if (index >= 1u && index <= 9u) {
        return "Road Stone Patch " +
            std::to_string(index);
    }
    if (index >= 10u && index <= 15u) {
        return "Tree 00" +
            std::to_string(index - 9u) +
            " Source Group";
    }
    if (index == 16u) return "Small Ground Vegetation Layer";
    if (index == 17u) return "Source Flower Layer";
    if (index >= 18u && index <= 25u) {
        return "Ground Foliage Layer " +
            std::to_string(index - 17u);
    }
    if (index == 26u) return "Rock and Foliage Layer";
    if (index == 27u) return "Cliff Foliage Layer 1";
    if (index == 28u) return "Cliff Foliage Layer 2";
    if (index >= 29u && index <= 35u) {
        return "Ledge and Raised Platform " +
            std::to_string(index - 28u);
    }
    if (index == 36u) return "Route Ground Plane";
    if (index == 37u) return "Route Sign";
    return std::string(sourceName);
}

std::string canonicalMeshCategory(
    std::uint32_t index) {
    if (index == 0u ||
        (index >= 1u && index <= 9u) ||
        index == 36u) {
        return "Environment/Terrain/Ground and Paths";
    }
    if (index >= 10u && index <= 15u) {
        return "Environment/Vegetation/Trees/Source Groups";
    }
    if (index >= 16u && index <= 25u) {
        return "Environment/Vegetation/Baked Foliage Layers";
    }
    if (index >= 26u && index <= 28u) {
        return "Environment/Vegetation/Rocks and Cliff Foliage";
    }
    if (index >= 29u && index <= 35u) {
        return "Environment/Terrain/Ledges and Raised Platforms";
    }
    if (index == 37u) {
        return "Environment/Props/Signs";
    }
    return "Environment/Source Mesh Groups";
}

std::string canonicalMeshPrefabAssetId(
    std::uint32_t index) {
    if (index >= 10u && index <= 15u) {
        return "route1/tree_00" +
            std::to_string(index - 9u);
    }
    return {};
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
    std::size_t visiblePlacementCount = 0u;
    for (const auto& placement : layer.placements) {
        if (placement.suppressed) {
            continue;
        }
        ++visiblePlacementCount;
        for (const auto& sourceDraw : layer.sourceDraws) {
            const auto composed = toArray(
                glm::make_mat4(placement.modelMatrix.data()) *
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
                glm::make_mat4(placement.modelMatrix.data()) *
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
    layer.instanceCount = visiblePlacementCount;
}

using AuthoredSceneDocument =
    engine::assets::phlosion::AuthoredSceneDocument;
using AuthoredSceneNode =
    engine::assets::phlosion::AuthoredSceneNode;
using AuthoredSceneTransform =
    engine::assets::phlosion::AuthoredSceneTransform;
using ImportedSourceBinding =
    engine::assets::phlosion::ImportedSourceBinding;
using PrefabInstanceBinding =
    engine::assets::phlosion::PrefabInstanceBinding;

AuthoredSceneTransform authoredTransform(
    const std::array<float, 3>& translation,
    const std::array<float, 3>& rotation,
    const std::array<float, 3>& scale) {
    return {
        .translation = translation,
        .rotationDegrees = rotation,
        .scale = scale};
}

std::string stableIdForImportedBinding(
    std::string_view targetKind,
    std::string_view logicalName,
    std::uint32_t recordIndex) {
    if (targetKind == "buildmodel_vegetation_placement") {
        return placementStableId(logicalName, recordIndex);
    }
    if (targetKind == "encounter_grass_record") {
        return encounterGrassStableId(logicalName, recordIndex);
    }
    if (targetKind == "canonical_tree_instance") {
        return treeInstanceStableId(logicalName, recordIndex);
    }
    if (targetKind == "canonical_mesh_group") {
        return canonicalMeshStableId(recordIndex);
    }
    return {};
}

std::string folderSegmentSlug(std::string_view segment) {
    std::string slug;
    bool separator = false;
    for (const unsigned char character : segment) {
        if (std::isalnum(character)) {
            slug.push_back(
                static_cast<char>(std::tolower(character)));
            separator = false;
        } else if (!separator && !slug.empty()) {
            slug.push_back('-');
            separator = true;
        }
    }
    while (!slug.empty() && slug.back() == '-') {
        slug.pop_back();
    }
    return slug;
}

std::uint32_t nextAuthoredSiblingOrder(
    const AuthoredSceneDocument& document,
    const std::string& parentId) {
    std::uint32_t next = 0u;
    for (const auto& node : document.nodes) {
        if (node.parentId == parentId) {
            next = std::max(next, node.siblingOrder + 1u);
        }
    }
    return next;
}

std::string ensureAuthoredFolders(
    AuthoredSceneDocument& document,
    std::string_view categoryPath) {
    std::string parentId;
    std::size_t begin = 0u;
    while (begin <= categoryPath.size()) {
        const std::size_t end = categoryPath.find('/', begin);
        const std::string segment(
            categoryPath.substr(
                begin,
                end == std::string_view::npos
                    ? std::string_view::npos
                    : end - begin));
        if (segment.empty()) {
            return {};
        }
        const std::string id =
            parentId.empty()
            ? "folder/" + folderSegmentSlug(segment)
            : parentId + "/" + folderSegmentSlug(segment);
        if (std::none_of(
                document.nodes.begin(),
                document.nodes.end(),
                [&](const AuthoredSceneNode& candidate) {
                    return candidate.id == id;
                })) {
            const std::uint32_t siblingOrder =
                nextAuthoredSiblingOrder(
                    document,
                    parentId);
            document.nodes.push_back(
                AuthoredSceneNode{
                    .id = id,
                    .displayName = segment,
                    .parentId = parentId,
                    .siblingOrder = siblingOrder});
        }
        parentId = id;
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1u;
    }
    return parentId;
}

std::string authoredFolderPath(
    const AuthoredSceneDocument& document,
    const std::string& parentId,
    std::string* outError) {
    if (parentId.empty()) {
        return {};
    }
    std::vector<std::string> segments;
    std::set<std::string> visited;
    std::string cursor = parentId;
    while (!cursor.empty()) {
        if (!visited.insert(cursor).second) {
            fail(outError, "Authored scene folder hierarchy contains a cycle.");
            return {};
        }
        const auto folder = std::find_if(
            document.nodes.begin(),
            document.nodes.end(),
            [&](const AuthoredSceneNode& candidate) {
                return candidate.id == cursor;
            });
        if (folder == document.nodes.end() || !folder->folder()) {
            fail(
                outError,
                "Authored scene object references a missing folder: " +
                    parentId);
            return {};
        }
        segments.push_back(folder->displayName);
        cursor = folder->parentId;
    }
    std::reverse(segments.begin(), segments.end());
    std::string path;
    for (const auto& segment : segments) {
        if (!path.empty()) {
            path += '/';
        }
        path += segment;
    }
    return path;
}

const LayoutObject* layoutObjectByStableId(
    const std::vector<LayoutObject>& objects,
    const std::string& stableId) {
    const auto found = std::find_if(
        objects.begin(),
        objects.end(),
        [&](const LayoutObject& candidate) {
            return candidate.stableId == stableId;
        });
    return found == objects.end() ? nullptr : &*found;
}

void appendImportedAuthoredNode(
    AuthoredSceneDocument& document,
    const LayoutObject& object,
    std::string reason) {
    if (std::any_of(
            document.nodes.begin(),
            document.nodes.end(),
            [&](const AuthoredSceneNode& candidate) {
                return candidate.id == object.stableId;
            })) {
        return;
    }
    const std::string parentId = ensureAuthoredFolders(
        document,
        object.categoryPath);
    const std::uint32_t siblingOrder =
        nextAuthoredSiblingOrder(document, parentId);
    document.nodes.push_back(
        AuthoredSceneNode{
            .id = object.stableId,
            .displayName = object.displayName,
            .parentId = parentId,
            .siblingOrder = siblingOrder,
            .enabled = !object.suppressed,
            .reason = std::move(reason),
            .transform = authoredTransform(
                object.translationCm,
                object.rotationDegrees,
                object.scale),
            .importedSource = ImportedSourceBinding{
                .targetKind = object.targetKind,
                .logicalName = object.logicalName,
                .recordIndex = object.recordIndex,
                .expectedSourceTransform = authoredTransform(
                    object.sourceTranslationCm,
                    object.sourceRotationDegrees,
                    object.sourceScale)}});
}

AuthoredSceneDocument authoredSceneFromLayout(
    const BoardLayoutTransform& layout,
    const std::vector<LayoutObject>& objects) {
    AuthoredSceneDocument document{
        .sceneId = "routes/route1",
        .baseEnvironmentAssetId = "environments/route1",
        .coordinateSystem = layout.coordinateSystem};
    for (const auto& delta : layout.localLayoutDeltas) {
        if (const auto* object = layoutObjectByStableId(
                objects,
                stableIdForImportedBinding(
                    delta.targetKind,
                    delta.logicalName,
                    delta.recordIndex))) {
            appendImportedAuthoredNode(
                document,
                *object,
                delta.reason);
        }
    }
    for (const auto& metadata : layout.objectMetadataOverrides) {
        if (const auto* object =
                layoutObjectByStableId(objects, metadata.stableId)) {
            appendImportedAuthoredNode(
                document,
                *object,
                "editor_hierarchy_override");
        }
    }
    for (const auto& authored : layout.authoredPrefabInstances) {
        const auto* prototype = layoutObjectByStableId(
            objects,
            authored.prototypeStableId);
        const auto* object = layoutObjectByStableId(
            objects,
            authored.stableId);
        if (!prototype || !object) {
            continue;
        }
        appendImportedAuthoredNode(
            document,
            *prototype,
            "prefab_prototype_reference");
        const std::string parentId = ensureAuthoredFolders(
            document,
            authored.categoryPath);
        const std::uint32_t siblingOrder =
            nextAuthoredSiblingOrder(document, parentId);
        document.nodes.push_back(
            AuthoredSceneNode{
                .id = authored.stableId,
                .displayName = authored.displayName,
                .parentId = parentId,
                .siblingOrder = siblingOrder,
                .enabled = !authored.suppressed,
                .reason = authored.reason,
                .transform = authoredTransform(
                    authored.translationCm,
                    authored.rotationDegrees,
                    authored.scale),
                .prefabInstance = PrefabInstanceBinding{
                    .prototypeNodeId =
                        authored.prototypeStableId,
                    .prefabAssetId = object->prefabAssetId,
                    .creationTransform = authoredTransform(
                        authored.sourceTranslationCm,
                        authored.sourceRotationDegrees,
                        authored.sourceScale)}});
    }
    std::stable_sort(
        document.nodes.begin(),
        document.nodes.end(),
        [](const AuthoredSceneNode& left,
           const AuthoredSceneNode& right) {
            if (left.folder() != right.folder()) {
                return left.folder();
            }
            return left.id < right.id;
        });
    return document;
}

void inheritAuthoredSceneOrdering(
    AuthoredSceneDocument& document,
    const AuthoredSceneDocument& previous) {
    std::set<std::pair<std::string, std::uint32_t>> used;
    for (auto& node : document.nodes) {
        const auto existing = std::find_if(
            previous.nodes.begin(),
            previous.nodes.end(),
            [&](const AuthoredSceneNode& candidate) {
                return candidate.id == node.id;
            });
        if (existing != previous.nodes.end() &&
            existing->parentId == node.parentId) {
            node.siblingOrder = existing->siblingOrder;
            used.emplace(node.parentId, node.siblingOrder);
        }
    }
    for (auto& node : document.nodes) {
        const auto existing = std::find_if(
            previous.nodes.begin(),
            previous.nodes.end(),
            [&](const AuthoredSceneNode& candidate) {
                return candidate.id == node.id &&
                    candidate.parentId == node.parentId;
            });
        if (existing != previous.nodes.end()) {
            continue;
        }
        while (!used.emplace(
                    node.parentId,
                    node.siblingOrder).second) {
            ++node.siblingOrder;
        }
    }
}

bool boardLayoutFromAuthoredScene(
    const AuthoredSceneDocument& document,
    const BoardLayoutTransform& registration,
    BoardLayoutTransform& out,
    std::string* outError) {
    if (!engine::assets::phlosion::
            validateAuthoredSceneDocument(
                document,
                outError)) {
        return false;
    }
    if (document.sceneId != "routes/route1" ||
        document.baseEnvironmentAssetId !=
            "environments/route1" ||
        document.coordinateSystem !=
            registration.coordinateSystem) {
        return fail(
            outError,
            "The authored scene does not target the mounted Route 1 environment contract.");
    }
    BoardLayoutTransform composed = registration;
    composed.localLayoutDeltas.clear();
    composed.objectMetadataOverrides.clear();
    composed.authoredPrefabInstances.clear();
    for (const auto& node : document.nodes) {
        if (node.folder()) {
            continue;
        }
        const std::string categoryPath =
            authoredFolderPath(
                document,
                node.parentId,
                outError);
        if (!node.parentId.empty() && categoryPath.empty()) {
            return false;
        }
        if (!categoryPath.empty() &&
            categoryPath != "Environment" &&
            categoryPath.rfind("Environment/", 0u) != 0u) {
            return fail(
                outError,
                "Route 1 authored objects must remain under the Environment hierarchy: " +
                    node.id);
        }
        if (node.importedSource) {
            const auto& binding = *node.importedSource;
            const std::string expectedStableId =
                stableIdForImportedBinding(
                    binding.targetKind,
                    binding.logicalName,
                    binding.recordIndex);
            if (expectedStableId.empty() ||
                node.id != expectedStableId) {
                return fail(
                    outError,
                    "Authored imported-source node ID does not match its immutable source binding: " +
                        node.id);
            }
            const auto& expected =
                binding.expectedSourceTransform;
            const auto& authored = *node.transform;
            constexpr float kTolerance = 0.0001f;
            const auto equal = [](const auto& left, const auto& right) {
                return std::equal(
                    left.begin(),
                    left.end(),
                    right.begin(),
                    [](float lhs, float rhs) {
                        return std::abs(lhs - rhs) <= kTolerance;
                    });
            };
            if (!node.enabled ||
                !equal(authored.translation, expected.translation) ||
                !equal(
                    authored.rotationDegrees,
                    expected.rotationDegrees) ||
                !equal(authored.scale, expected.scale)) {
                composed.localLayoutDeltas.push_back(
                    LocalLayoutDelta{
                        .id = "authored-scene--" + node.id,
                        .targetKind = binding.targetKind,
                        .logicalName = binding.logicalName,
                        .recordIndex = binding.recordIndex,
                        .expectedSourceTranslationCm =
                            expected.translation,
                        .expectedSourceRotationDegrees =
                            expected.rotationDegrees,
                        .expectedSourceScale = expected.scale,
                        .translationCm = authored.translation,
                        .rotationDegrees =
                            authored.rotationDegrees,
                        .scale = authored.scale,
                        .suppressed = !node.enabled,
                        .reason = node.reason});
            }
            composed.objectMetadataOverrides.push_back(
                LayoutObjectMetadataOverride{
                    .stableId = node.id,
                    .displayName = node.displayName,
                    .categoryPath = categoryPath});
            continue;
        }
        const auto& prefab = *node.prefabInstance;
        if (categoryPath.empty()) {
            return fail(
                outError,
                "Authored prefab instances require a hierarchy folder: " +
                    node.id);
        }
        composed.authoredPrefabInstances.push_back(
            AuthoredPrefabInstance{
                .stableId = node.id,
                .prototypeStableId = prefab.prototypeNodeId,
                .displayName = node.displayName,
                .categoryPath = categoryPath,
                .sourceTranslationCm =
                    prefab.creationTransform.translation,
                .sourceRotationDegrees =
                    prefab.creationTransform.rotationDegrees,
                .sourceScale = prefab.creationTransform.scale,
                .translationCm = node.transform->translation,
                .rotationDegrees =
                    node.transform->rotationDegrees,
                .scale = node.transform->scale,
                .suppressed = !node.enabled,
                .reason = node.reason});
    }
    composed.declaredLocalDeltaCount =
        static_cast<std::uint32_t>(
            composed.localLayoutDeltas.size());
    out = std::move(composed);
    if (outError) {
        outError->clear();
    }
    return true;
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
    engine::assets::phlosion::AuthoredSceneDocument
        authoredScene;
    RuntimeStats stats;
    bool isLoaded = false;
    CanonicalScene source;
    PreparedScene scene;
    IRenderBackend::WorldSceneFrame canonicalFrame;
    IRenderBackend::WorldSceneFrame canonicalShadowFrame;
    std::vector<CanonicalMeshGroup> canonicalMeshGroups;
    std::vector<CanonicalTreeInstance> canonicalTreeInstances;
    std::vector<lgpe_world_scene::PolygonGroupStorage>
        canonicalTreePolygonStorage;
    std::vector<EncounterGrassRecord> encounterGrassRecords;
    std::size_t canonicalEncounterGrassRecordCount = 0u;
    std::vector<EncounterGrassLayer> encounterGrass;
    std::vector<PlacedVegetationLayer> placedVegetation;
    lgpe_route1_projected_shadow::Atlas projectedShadowAtlas;
    std::vector<PreparedScene*> scenes;
    std::vector<SceneMaterialTemplates> materialTemplates;
    std::vector<LayoutObject> layoutObjects;
    glm::mat4 worldFromSource{1.0f};
    glm::mat4 sourceFromWorld{1.0f};
    LightProjectionRows cloudProjectionRows;
    std::string materialFilter;
    float windPhaseCycles = kInitialWindPhaseCycles;

    bool splitCanonicalTreeInstances(
        std::string* outError) {
        canonicalTreeInstances.clear();
        canonicalTreePolygonStorage.clear();

        using TreePartition =
            lgpe_route1_tree_instances::MeshPartition;
        std::map<std::uint32_t, TreePartition>
            partitions;
        std::size_t storageCount = 0u;
        for (const auto& mesh : source.meshes) {
            const std::uint32_t instanceCount =
                lgpe_route1_tree_instances::
                    expectedInstanceCount(
                        mesh.sourceIndex);
            if (instanceCount == 0u) {
                continue;
            }
            TreePartition partition;
            if (!lgpe_route1_tree_instances::
                    derivePartition(
                        mesh,
                        instanceCount,
                        partition,
                        outError)) {
                return false;
            }
            storageCount +=
                partition.polygonGroups.size() *
                instanceCount;
            partitions.emplace(
                mesh.sourceIndex,
                std::move(partition));
        }
        canonicalTreePolygonStorage.reserve(
            storageCount);
        canonicalTreeInstances.reserve(47u);

        using ObjectHandle =
            IRenderBackend::
                WorldSceneRenderObjectHandle;
        using GroupKey =
            std::pair<std::uint32_t, std::uint32_t>;
        std::map<GroupKey, std::vector<ObjectHandle>>
            objectsByGroup;
        std::unordered_map<std::uint32_t, std::uint32_t>
            instanceIdByObjectId;
        std::uint32_t nextInstanceId = 1u;
        const auto observeInstanceIds =
            [&](const IRenderBackend::WorldSceneFrame&
                    frame) {
                for (const auto& drawClass :
                     frame.drawClasses) {
                    for (const auto& instance :
                         drawClass.instances) {
                        nextInstanceId = std::max(
                            nextInstanceId,
                            instance.handle.id + 1u);
                    }
                }
            };
        observeInstanceIds(scene.frame);
        observeInstanceIds(scene.shadowFrame);

        for (const auto& mesh : source.meshes) {
            const auto partitionIt =
                partitions.find(mesh.sourceIndex);
            if (partitionIt == partitions.end()) {
                continue;
            }
            const auto& partition =
                partitionIt->second;
            const std::uint32_t instanceCount =
                static_cast<std::uint32_t>(
                    partition.sourcePivotsCm.size());
            const std::string logicalName =
                treeLogicalName(mesh.sourceIndex);
            const std::size_t firstTree =
                canonicalTreeInstances.size();
            for (std::uint32_t instance = 0u;
                 instance < instanceCount;
                 ++instance) {
                const glm::vec4 transformedPivot =
                    glm::make_mat4(
                        mesh.transform.data()) *
                    glm::vec4(
                        partition
                            .sourcePivotsCm[instance][0],
                        partition
                            .sourcePivotsCm[instance][1],
                        partition
                            .sourcePivotsCm[instance][2],
                        1.0f);
                const std::array<float, 3> pivot{
                    transformedPivot.x,
                    transformedPivot.y,
                    transformedPivot.z};
                canonicalTreeInstances.push_back(
                    CanonicalTreeInstance{
                        .stableId =
                            treeInstanceStableId(
                                logicalName,
                                instance),
                        .logicalName = logicalName,
                        .prefabAssetId =
                            "route1/" + logicalName,
                        .sourceMeshIndex =
                            mesh.sourceIndex,
                        .recordIndex = instance,
                        .sourceModelMatrix =
                            mesh.transform,
                        .sourcePivotCm = pivot,
                        .groupBaselinePivotCm =
                            pivot,
                        .translationCm = pivot});
            }

            const auto sourceMeshIt =
                std::find_if(
                    source.meshes.begin(),
                    source.meshes.end(),
                    [&](const auto& candidate) {
                        return candidate.sourceIndex ==
                            mesh.sourceIndex;
                    });
            const std::size_t sourceMeshStorageIndex =
                static_cast<std::size_t>(
                    std::distance(
                        source.meshes.begin(),
                        sourceMeshIt));
            if (sourceMeshStorageIndex >=
                scene.meshVertexStorage.size()) {
                return fail(
                    outError,
                    "Route 1 tree mesh storage no longer matches "
                    "the canonical source.");
            }
            const auto& vertexStorage =
                scene.meshVertexStorage[
                    sourceMeshStorageIndex];

            for (const auto& group :
                 partition.polygonGroups) {
                const auto originalGeometry =
                    std::find_if(
                        scene.registry.geometries.begin(),
                        scene.registry.geometries.end(),
                        [&](const auto& candidate) {
                            return candidate.sourceMeshIndex ==
                                    mesh.sourceIndex &&
                                candidate
                                        .sourcePolygonGroupIndex ==
                                    group.polygonGroupIndex;
                        });
                if (originalGeometry ==
                    scene.registry.geometries.end()) {
                    return fail(
                        outError,
                        "Route 1 tree polygon group is missing "
                        "from the prepared scene.");
                }
                const auto originalObject =
                    std::find_if(
                        scene.registry.renderObjects.begin(),
                        scene.registry.renderObjects.end(),
                        [&](const auto& candidate) {
                            return candidate.geometryHandle.id ==
                                originalGeometry->handle.id;
                        });
                if (originalObject ==
                    scene.registry.renderObjects.end()) {
                    return fail(
                        outError,
                        "Route 1 tree render object is missing.");
                }
                const std::string sourceGeometryCacheKey =
                    originalGeometry->geometryCacheKey;
                const std::uint32_t sourceSemanticMask =
                    originalGeometry
                        ->sourceVertexSemanticMask;
                const auto sourceMaterialHandle =
                    originalObject->materialHandle;
                const auto sourcePipelineVariant =
                    static_cast<
                        shared_world_scene::
                            PipelineVariant>(
                        originalObject
                            ->pipelineVariant);
                const std::uint32_t sourceCookedDrawSlot =
                    originalObject->cookedDrawSlot;
                const bool sourceSkinned =
                    originalObject->skinned;

                auto& groupObjects =
                    objectsByGroup[
                        GroupKey{
                            mesh.sourceIndex,
                            group.polygonGroupIndex}];
                groupObjects.reserve(instanceCount);
                std::size_t selectedIndexCount = 0u;
                for (std::uint32_t instance = 0u;
                     instance < instanceCount;
                     ++instance) {
                    canonicalTreePolygonStorage
                        .emplace_back();
                    auto& storage =
                        canonicalTreePolygonStorage.back();
                    storage.geometryCacheKey =
                        sourceGeometryCacheKey +
                        ":source-instance:" +
                        std::to_string(instance);
                    if (!lgpe_route1_tree_instances::
                            selectInstanceTriangles(
                                mesh,
                                group,
                                instance,
                                storage.indices,
                                outError)) {
                        return false;
                    }
                    selectedIndexCount +=
                        storage.indices.size();
                    const auto geometryHandle =
                        shared_world_scene::
                            ensureRigidGeometry(
                                scene.registry,
                                &storage,
                                storage.geometryCacheKey
                                    .c_str(),
                                vertexStorage.vertices
                                    .data(),
                                vertexStorage.vertices
                                    .size(),
                                storage.indices.data(),
                                storage.indices.size(),
                                vertexStorage
                                    .sourceVertices
                                    .data(),
                                vertexStorage
                                    .sourceVertices
                                    .size(),
                                sourceSemanticMask,
                                mesh.sourceIndex,
                                group
                                    .polygonGroupIndex);
                    const auto objectHandle =
                        shared_world_scene::
                            ensureRenderObject(
                                scene.registry,
                                geometryHandle,
                                sourceMaterialHandle,
                                sourcePipelineVariant,
                                sourceCookedDrawSlot,
                                sourceSkinned);
                    groupObjects.push_back(
                        objectHandle);
                    auto& tree =
                        canonicalTreeInstances[
                            firstTree + instance];
                    tree.objectHandles.push_back(
                        objectHandle);
                    instanceIdByObjectId.emplace(
                        objectHandle.id,
                        nextInstanceId++);
                }
                if (selectedIndexCount !=
                    mesh.polygonGroups[
                        group.polygonGroupIndex]
                        .indices.size()) {
                    return fail(
                        outError,
                        "Route 1 tree source partition did not preserve "
                        "the complete polygon group.");
                }
            }
        }
        if (canonicalTreeInstances.size() != 47u) {
            return fail(
                outError,
                "Route 1 tree source instance total changed: "
                "expected 47, found " +
                    std::to_string(
                        canonicalTreeInstances.size()) +
                    ".");
        }

        const auto rebuildFrame =
            [&](IRenderBackend::WorldSceneFrame& frame) {
                IRenderBackend::WorldSceneFrame rebuilt;
                rebuilt.visibleSkeletons =
                    frame.visibleSkeletons;
                rebuilt.paletteUploadBytes =
                    frame.paletteUploadBytes;
                rebuilt.indirectCommandCount =
                    frame.indirectCommandCount;
                for (const auto& drawClass :
                     frame.drawClasses) {
                    const auto* object = renderObject(
                        scene.registry,
                        drawClass.objectHandle);
                    const auto* sourceGeometry =
                        object
                        ? geometry(
                              scene.registry,
                              object->geometryHandle)
                        : nullptr;
                    const GroupKey key{
                        sourceGeometry
                            ? sourceGeometry
                                  ->sourceMeshIndex
                            : 0u,
                        sourceGeometry
                            ? sourceGeometry
                                  ->sourcePolygonGroupIndex
                            : 0u};
                    const auto split =
                        sourceGeometry
                        ? objectsByGroup.find(key)
                        : objectsByGroup.end();
                    if (split ==
                        objectsByGroup.end()) {
                        rebuilt.drawClasses.push_back(
                            drawClass);
                        continue;
                    }
                    for (const ObjectHandle objectHandle :
                         split->second) {
                        auto instanceDraw = drawClass;
                        instanceDraw.objectHandle =
                            objectHandle;
                        for (auto& instance :
                             instanceDraw.instances) {
                            instance.objectHandle =
                                objectHandle;
                            instance.handle.id =
                                instanceIdByObjectId.at(
                                    objectHandle.id);
                        }
                        rebuilt.drawClasses.push_back(
                            std::move(instanceDraw));
                    }
                }
                rebuilt.drawClassIndexByObjectId.assign(
                    scene.registry.renderObjects.size(),
                    0u);
                for (std::size_t index = 0u;
                     index < rebuilt.drawClasses.size();
                     ++index) {
                    const std::uint32_t objectId =
                        rebuilt.drawClasses[index]
                            .objectHandle.id;
                    if (objectId > 0u &&
                        objectId <=
                            rebuilt
                                .drawClassIndexByObjectId
                                .size()) {
                        rebuilt
                            .drawClassIndexByObjectId[
                                objectId - 1u] =
                            static_cast<std::uint32_t>(
                                index + 1u);
                    }
                }
                frame = std::move(rebuilt);
            };
        rebuildFrame(scene.frame);
        rebuildFrame(scene.shadowFrame);
        return true;
    }

    static bool sourceTransformMatches(
        const PlacedVegetationPlacement& placement,
        const LocalLayoutDelta& delta) {
        return sourceTransformMatches(
            placement.sourceTranslationCm,
            placement.sourceRotationDegrees,
            placement.sourceScale,
            delta);
    }

    static bool sourceTransformMatches(
        const std::array<float, 3>& translation,
        const std::array<float, 3>& rotation,
        const std::array<float, 3>& scale,
        const LocalLayoutDelta& delta) {
        constexpr float kTolerance = 0.001f;
        const auto matches =
            [&](const std::array<float, 3>& lhs,
                const std::array<float, 3>& rhs) {
                return std::equal(
                    lhs.begin(),
                    lhs.end(),
                    rhs.begin(),
                    [&](float left, float right) {
                        return std::abs(left - right) <=
                            kTolerance;
                    });
            };
        return matches(
                   translation,
                   delta.expectedSourceTranslationCm) &&
            matches(
                rotation,
                delta.expectedSourceRotationDegrees) &&
            matches(
                scale,
                delta.expectedSourceScale);
    }

    bool applyLocalDeltas(std::string* outError) {
        encounterGrassRecords.resize(
            canonicalEncounterGrassRecordCount);
        for (auto& layer : encounterGrass) {
            layer.placements.resize(
                layer.canonicalPlacementCount);
        }
        for (auto& layer : placedVegetation) {
            layer.placements.resize(
                layer.canonicalPlacementCount);
        }
        for (auto& group : canonicalMeshGroups) {
            group.translationCm = group.sourcePivotCm;
            group.rotationDegrees = {};
            group.scale = {1.0f, 1.0f, 1.0f};
            group.suppressed = false;
            group.hasOverride = false;
            group.reason.clear();
        }
        for (auto& tree : canonicalTreeInstances) {
            tree.groupBaselinePivotCm =
                tree.sourcePivotCm;
            tree.translationCm =
                tree.sourcePivotCm;
            tree.rotationDegrees = {};
            tree.scale = {1.0f, 1.0f, 1.0f};
            tree.suppressed = false;
            tree.hasOverride = false;
            tree.reason.clear();
        }
        for (auto& record : encounterGrassRecords) {
            record.translationCm =
                record.sourceTranslationCm;
            record.rotationDegrees = {};
            record.scale = {1.0f, 1.0f, 1.0f};
            record.suppressed = false;
            record.hasOverride = false;
            record.reason.clear();
        }
        for (auto& layer : placedVegetation) {
            for (auto& placement : layer.placements) {
                placement.translationCm =
                    placement.sourceTranslationCm;
                placement.rotationDegrees =
                    placement.sourceRotationDegrees;
                placement.scale = placement.sourceScale;
                placement.suppressed = false;
                placement.hasOverride = false;
                placement.reason.clear();
            }
        }

        std::set<std::string> resolvedTargets;
        for (const auto& delta : layout.localLayoutDeltas) {
            std::string stableId;
            if (delta.targetKind ==
                "buildmodel_vegetation_placement") {
                PlacedVegetationPlacement* target = nullptr;
                for (auto& layer : placedVegetation) {
                    if (layer.logicalName != delta.logicalName) {
                        continue;
                    }
                    const auto found = std::find_if(
                        layer.placements.begin(),
                        layer.placements.end(),
                        [&](const PlacedVegetationPlacement& placement) {
                            return placement.recordIndex ==
                                delta.recordIndex;
                        });
                    if (found != layer.placements.end()) {
                        target = &*found;
                    }
                    break;
                }
                stableId = placementStableId(
                    delta.logicalName,
                    delta.recordIndex);
                if (!target) {
                    return fail(
                        outError,
                        "Route 1 local-layout target no longer exists: " +
                            stableId);
                }
                if (!sourceTransformMatches(*target, delta)) {
                    return fail(
                        outError,
                        "Route 1 local-layout target source transform "
                        "changed; refusing to retarget silently: " +
                            stableId);
                }
                target->translationCm = delta.translationCm;
                target->rotationDegrees = delta.rotationDegrees;
                target->scale = delta.scale;
                target->suppressed = delta.suppressed;
                target->hasOverride = true;
                target->reason = delta.reason;
            } else if (
                delta.targetKind ==
                "encounter_grass_record") {
                auto target = std::find_if(
                    encounterGrassRecords.begin(),
                    encounterGrassRecords.end(),
                    [&](const EncounterGrassRecord& record) {
                        return record.logicalName ==
                                delta.logicalName &&
                            record.recordIndex ==
                                delta.recordIndex;
                    });
                stableId = encounterGrassStableId(
                    delta.logicalName,
                    delta.recordIndex);
                if (target ==
                    encounterGrassRecords.end()) {
                    return fail(
                        outError,
                        "Route 1 local-layout target no longer exists: " +
                            stableId);
                }
                if (!sourceTransformMatches(
                        target->sourceTranslationCm,
                        {0.0f, 0.0f, 0.0f},
                        {1.0f, 1.0f, 1.0f},
                        delta)) {
                    return fail(
                        outError,
                        "Route 1 encounter-grass source transform "
                        "changed; refusing to retarget silently: " +
                            stableId);
                }
                target->translationCm = delta.translationCm;
                target->rotationDegrees = delta.rotationDegrees;
                target->scale = delta.scale;
                target->suppressed = delta.suppressed;
                target->hasOverride = true;
                target->reason = delta.reason;
            } else if (
                delta.targetKind ==
                "canonical_tree_instance") {
                auto target = std::find_if(
                    canonicalTreeInstances.begin(),
                    canonicalTreeInstances.end(),
                    [&](const CanonicalTreeInstance& tree) {
                        return tree.logicalName ==
                                delta.logicalName &&
                            tree.recordIndex ==
                                delta.recordIndex;
                    });
                stableId = treeInstanceStableId(
                    delta.logicalName,
                    delta.recordIndex);
                if (target ==
                    canonicalTreeInstances.end()) {
                    return fail(
                        outError,
                        "Route 1 tree instance no longer exists: " +
                            stableId);
                }
                if (!sourceTransformMatches(
                        target->sourcePivotCm,
                        {0.0f, 0.0f, 0.0f},
                        {1.0f, 1.0f, 1.0f},
                        delta)) {
                    return fail(
                        outError,
                        "Route 1 tree-instance source pivot changed; "
                        "refusing to retarget silently: " +
                            stableId);
                }
                target->translationCm =
                    delta.translationCm;
                target->rotationDegrees =
                    delta.rotationDegrees;
                target->scale = delta.scale;
                target->suppressed =
                    delta.suppressed;
                target->hasOverride = true;
                target->reason = delta.reason;
            } else if (
                delta.targetKind ==
                "canonical_mesh_group") {
                auto target = std::find_if(
                    canonicalMeshGroups.begin(),
                    canonicalMeshGroups.end(),
                    [&](const CanonicalMeshGroup& group) {
                        return group.sourceMeshIndex ==
                                delta.recordIndex &&
                            group.logicalName ==
                                delta.logicalName;
                    });
                stableId =
                    canonicalMeshStableId(
                        delta.recordIndex);
                if (target ==
                    canonicalMeshGroups.end()) {
                    return fail(
                        outError,
                        "Route 1 local-layout target no longer exists: " +
                            stableId);
                }
                if (!sourceTransformMatches(
                        target->sourcePivotCm,
                        {0.0f, 0.0f, 0.0f},
                        {1.0f, 1.0f, 1.0f},
                        delta)) {
                    return fail(
                        outError,
                        "Route 1 canonical-mesh source pivot changed; "
                        "refusing to retarget silently: " +
                            stableId);
                }
                target->translationCm = delta.translationCm;
                target->rotationDegrees = delta.rotationDegrees;
                target->scale = delta.scale;
                target->suppressed = delta.suppressed;
                target->hasOverride = true;
                target->reason = delta.reason;
            } else {
                return fail(
                    outError,
                    "Unsupported Route 1 local-layout target kind: " +
                        delta.targetKind);
            }
            if (!resolvedTargets.insert(stableId).second) {
                return fail(
                    outError,
                    "Route 1 local-layout target is declared more than "
                    "once: " +
                        stableId);
            }
        }

        const auto groupDeltaMatrix =
            [](const CanonicalMeshGroup& group) {
                return glm::make_mat4(
                           sourcePlacementMatrix(
                               group.translationCm,
                               group.rotationDegrees,
                               group.scale)
                               .data()) *
                    glm::translate(
                           glm::mat4(1.0f),
                           -glm::vec3(
                               group.sourcePivotCm[0],
                               group.sourcePivotCm[1],
                               group.sourcePivotCm[2]));
            };
        for (auto& tree : canonicalTreeInstances) {
            const auto group = std::find_if(
                canonicalMeshGroups.begin(),
                canonicalMeshGroups.end(),
                [&](const CanonicalMeshGroup& candidate) {
                    return candidate.sourceMeshIndex ==
                        tree.sourceMeshIndex;
                });
            if (group == canonicalMeshGroups.end()) {
                return fail(
                    outError,
                    "Route 1 tree instance lost its source group.");
            }
            const glm::vec4 baseline =
                groupDeltaMatrix(*group) *
                glm::vec4(
                    tree.sourcePivotCm[0],
                    tree.sourcePivotCm[1],
                    tree.sourcePivotCm[2],
                    1.0f);
            tree.groupBaselinePivotCm = {
                baseline.x,
                baseline.y,
                baseline.z};
            if (!tree.hasOverride) {
                tree.translationCm =
                    tree.groupBaselinePivotCm;
            }
        }

        std::set<std::string> authoredIds;
        for (std::size_t authoredIndex = 0u;
             authoredIndex <
                 layout.authoredPrefabInstances.size();
             ++authoredIndex) {
            const auto& authored =
                layout.authoredPrefabInstances[authoredIndex];
            if (authored.stableId.empty() ||
                authored.prototypeStableId.empty() ||
                !authoredIds.insert(
                    authored.stableId).second) {
                return fail(
                    outError,
                    "Route 1 contains an invalid or duplicate authored prefab instance.");
            }
            const auto vegetationPrototype =
                [&]() -> const PlacedVegetationPlacement* {
                    for (const auto& layer : placedVegetation) {
                        const auto found = std::find_if(
                            layer.placements.begin(),
                            layer.placements.begin() +
                                static_cast<std::ptrdiff_t>(
                                    layer.canonicalPlacementCount),
                            [&](const PlacedVegetationPlacement& placement) {
                                return placement.stableId ==
                                    authored.prototypeStableId;
                            });
                        if (found !=
                            layer.placements.begin() +
                                static_cast<std::ptrdiff_t>(
                                    layer.canonicalPlacementCount)) {
                            return &*found;
                        }
                    }
                    return nullptr;
                }();
            if (vegetationPrototype) {
                auto layer = std::find_if(
                    placedVegetation.begin(),
                    placedVegetation.end(),
                    [&](const PlacedVegetationLayer& candidate) {
                        return std::find_if(
                                   candidate.placements.begin(),
                                   candidate.placements.begin() +
                                       static_cast<std::ptrdiff_t>(
                                           candidate.canonicalPlacementCount),
                                   [&](const PlacedVegetationPlacement& placement) {
                                       return placement.stableId ==
                                           authored.prototypeStableId;
                                   }) !=
                            candidate.placements.begin() +
                                static_cast<std::ptrdiff_t>(
                                    candidate.canonicalPlacementCount);
                    });
                auto instance = *vegetationPrototype;
                instance.stableId = authored.stableId;
                instance.sourceTranslationCm =
                    authored.sourceTranslationCm;
                instance.sourceRotationDegrees =
                    authored.sourceRotationDegrees;
                instance.sourceScale = authored.sourceScale;
                instance.translationCm =
                    authored.translationCm;
                instance.rotationDegrees =
                    authored.rotationDegrees;
                instance.scale = authored.scale;
                instance.modelMatrix = sourcePlacementMatrix(
                    instance.translationCm,
                    instance.rotationDegrees,
                    instance.scale);
                instance.suppressed = authored.suppressed;
                instance.hasOverride = true;
                instance.authored = true;
                instance.reason = authored.reason;
                layer->placements.push_back(
                    std::move(instance));
                continue;
            }

            const auto encounterPrototype = std::find_if(
                encounterGrassRecords.begin(),
                encounterGrassRecords.begin() +
                    static_cast<std::ptrdiff_t>(
                        canonicalEncounterGrassRecordCount),
                [&](const EncounterGrassRecord& record) {
                    return record.stableId ==
                        authored.prototypeStableId;
                });
            if (encounterPrototype !=
                encounterGrassRecords.begin() +
                    static_cast<std::ptrdiff_t>(
                        canonicalEncounterGrassRecordCount)) {
                const std::uint32_t syntheticRecordIndex =
                    0x80000000u +
                    static_cast<std::uint32_t>(authoredIndex);
                const auto sourceRecord = *encounterPrototype;
                encounterGrassRecords.push_back(
                    EncounterGrassRecord{
                        .stableId = authored.stableId,
                        .logicalName =
                            sourceRecord.logicalName,
                        .recordIndex =
                            syntheticRecordIndex,
                        .sourceTranslationCm =
                            authored.sourceTranslationCm,
                        .translationCm =
                            authored.translationCm,
                        .rotationDegrees =
                            authored.rotationDegrees,
                        .scale = authored.scale,
                        .suppressed = authored.suppressed,
                        .hasOverride = true,
                        .authored = true,
                        .reason = authored.reason});
                auto layer = std::find_if(
                    encounterGrass.begin(),
                    encounterGrass.end(),
                    [&](const EncounterGrassLayer& candidate) {
                        return candidate.logicalName ==
                            sourceRecord.logicalName;
                    });
                if (layer == encounterGrass.end()) {
                    return fail(
                        outError,
                        "Route 1 authored encounter-grass prototype lost its layer.");
                }
                std::vector<EncounterGrassPlacement> copies;
                for (const auto& placement : layer->placements) {
                    if (placement.recordIndex !=
                        sourceRecord.recordIndex) {
                        continue;
                    }
                    auto copy = placement;
                    const std::array<float, 3> offset{
                        placement.sourceCenter[0] -
                            sourceRecord.sourceTranslationCm[0],
                        placement.sourceCenter[1] -
                            sourceRecord.sourceTranslationCm[1],
                        placement.sourceCenter[2] -
                            sourceRecord.sourceTranslationCm[2]};
                    copy.recordIndex = syntheticRecordIndex;
                    copy.sourceCenter = {
                        authored.sourceTranslationCm[0] + offset[0],
                        authored.sourceTranslationCm[1] + offset[1],
                        authored.sourceTranslationCm[2] + offset[2]};
                    copies.push_back(std::move(copy));
                }
                layer->placements.insert(
                    layer->placements.end(),
                    copies.begin(),
                    copies.end());
                continue;
            }

            const bool rigidPrototypeExists =
                std::any_of(
                    canonicalTreeInstances.begin(),
                    canonicalTreeInstances.end(),
                    [&](const CanonicalTreeInstance& tree) {
                        return tree.stableId ==
                            authored.prototypeStableId;
                    }) ||
                std::any_of(
                    canonicalMeshGroups.begin(),
                    canonicalMeshGroups.end(),
                    [&](const CanonicalMeshGroup& group) {
                        return group.stableId ==
                            authored.prototypeStableId;
                    });
            if (!rigidPrototypeExists) {
                return fail(
                    outError,
                    "Route 1 authored prefab prototype no longer exists: " +
                        authored.prototypeStableId);
            }
        }

        scene.frame = canonicalFrame;
        scene.shadowFrame = canonicalShadowFrame;
        const auto placeCanonicalFrame =
            [&](IRenderBackend::WorldSceneFrame& frame) {
                for (auto& drawClass : frame.drawClasses) {
                    const auto* object = renderObject(
                        scene.registry,
                        drawClass.objectHandle);
                    const auto* mesh =
                        object
                        ? geometry(
                              scene.registry,
                              object->geometryHandle)
                        : nullptr;
                    if (!mesh) {
                        continue;
                    }
                    const auto group = std::find_if(
                        canonicalMeshGroups.begin(),
                        canonicalMeshGroups.end(),
                        [&](const CanonicalMeshGroup& candidate) {
                            return candidate.sourceMeshIndex ==
                                mesh->sourceMeshIndex;
                        });
                    if (group ==
                        canonicalMeshGroups.end()) {
                        continue;
                    }
                    if (group->suppressed) {
                        drawClass.instances.clear();
                        continue;
                    }
                    const glm::mat4 authored =
                        groupDeltaMatrix(*group) *
                        glm::make_mat4(
                            group->sourceModelMatrix.data());
                    for (auto& instance :
                         drawClass.instances) {
                        instance.modelMatrix =
                            toArray(authored);
                    }
                }
            };
        placeCanonicalFrame(scene.frame);
        placeCanonicalFrame(scene.shadowFrame);

        const auto placeTreeInstances =
            [&](IRenderBackend::WorldSceneFrame& frame) {
                for (const auto& tree :
                     canonicalTreeInstances) {
                    const auto group = std::find_if(
                        canonicalMeshGroups.begin(),
                        canonicalMeshGroups.end(),
                        [&](const CanonicalMeshGroup& candidate) {
                            return candidate.sourceMeshIndex ==
                                tree.sourceMeshIndex;
                        });
                    if (group ==
                        canonicalMeshGroups.end()) {
                        continue;
                    }
                    const bool suppressed =
                        group->suppressed ||
                        tree.suppressed;
                    const glm::mat4 extra =
                        glm::make_mat4(
                            sourcePlacementMatrix(
                                tree.translationCm,
                                tree.rotationDegrees,
                                tree.scale)
                                .data()) *
                        glm::translate(
                            glm::mat4(1.0f),
                            -glm::vec3(
                                tree.groupBaselinePivotCm[0],
                                tree.groupBaselinePivotCm[1],
                                tree.groupBaselinePivotCm[2]));
                    const glm::mat4 authored =
                        extra *
                        groupDeltaMatrix(*group) *
                        glm::make_mat4(
                            tree.sourceModelMatrix.data());
                    for (const auto objectHandle :
                         tree.objectHandles) {
                        if (objectHandle.id == 0u ||
                            objectHandle.id >
                                frame
                                    .drawClassIndexByObjectId
                                    .size()) {
                            continue;
                        }
                        const std::uint32_t encodedIndex =
                            frame
                                .drawClassIndexByObjectId[
                                    objectHandle.id - 1u];
                        if (encodedIndex == 0u ||
                            encodedIndex >
                                frame.drawClasses.size()) {
                            continue;
                        }
                        auto& drawClass =
                            frame.drawClasses[
                                encodedIndex - 1u];
                        if (suppressed) {
                            drawClass.instances.clear();
                            continue;
                        }
                        for (auto& instance :
                             drawClass.instances) {
                            instance.modelMatrix =
                                toArray(authored);
                        }
                    }
                }
            };
        placeTreeInstances(scene.frame);
        placeTreeInstances(scene.shadowFrame);

        const auto appendAuthoredRigidInstances =
            [&](IRenderBackend::WorldSceneFrame& frame) {
                std::uint32_t nextInstanceId = 1u;
                for (const auto& drawClass :
                     frame.drawClasses) {
                    for (const auto& instance :
                         drawClass.instances) {
                        nextInstanceId = std::max(
                            nextInstanceId,
                            instance.handle.id + 1u);
                    }
                }
                const auto append =
                    [&](IRenderBackend::
                            WorldSceneRenderObjectHandle objectHandle,
                        const std::array<float, 16>& modelMatrix) {
                        if (objectHandle.id == 0u ||
                            objectHandle.id >
                                frame
                                    .drawClassIndexByObjectId
                                    .size()) {
                            return;
                        }
                        const std::uint32_t encodedIndex =
                            frame.drawClassIndexByObjectId[
                                objectHandle.id - 1u];
                        if (encodedIndex == 0u ||
                            encodedIndex >
                                frame.drawClasses.size()) {
                            return;
                        }
                        auto& drawClass =
                            frame.drawClasses[
                                encodedIndex - 1u];
                        IRenderBackend::WorldSceneInstance instance;
                        instance.handle.id = nextInstanceId++;
                        instance.objectHandle = objectHandle;
                        instance.modelMatrix = modelMatrix;
                        drawClass.instances.push_back(
                            std::move(instance));
                    };
                for (const auto& authored :
                     layout.authoredPrefabInstances) {
                    if (authored.suppressed) {
                        continue;
                    }
                    const auto tree = std::find_if(
                        canonicalTreeInstances.begin(),
                        canonicalTreeInstances.end(),
                        [&](const CanonicalTreeInstance& candidate) {
                            return candidate.stableId ==
                                authored.prototypeStableId;
                        });
                    if (tree !=
                        canonicalTreeInstances.end()) {
                        const auto modelMatrix = toArray(
                            glm::make_mat4(
                                sourcePlacementMatrix(
                                    authored.translationCm,
                                    authored.rotationDegrees,
                                    authored.scale)
                                    .data()) *
                            glm::translate(
                                glm::mat4(1.0f),
                                -glm::vec3(
                                    tree->sourcePivotCm[0],
                                    tree->sourcePivotCm[1],
                                    tree->sourcePivotCm[2])) *
                            glm::make_mat4(
                                tree->sourceModelMatrix.data()));
                        for (const auto objectHandle :
                             tree->objectHandles) {
                            append(
                                objectHandle,
                                modelMatrix);
                        }
                        continue;
                    }
                    const auto group = std::find_if(
                        canonicalMeshGroups.begin(),
                        canonicalMeshGroups.end(),
                        [&](const CanonicalMeshGroup& candidate) {
                            return candidate.stableId ==
                                authored.prototypeStableId;
                        });
                    if (group ==
                        canonicalMeshGroups.end()) {
                        continue;
                    }
                    const auto modelMatrix = toArray(
                        glm::make_mat4(
                            sourcePlacementMatrix(
                                authored.translationCm,
                                authored.rotationDegrees,
                                authored.scale)
                                .data()) *
                        glm::translate(
                            glm::mat4(1.0f),
                            -glm::vec3(
                                group->sourcePivotCm[0],
                                group->sourcePivotCm[1],
                                group->sourcePivotCm[2])) *
                        glm::make_mat4(
                            group->sourceModelMatrix.data()));
                    std::vector<IRenderBackend::
                        WorldSceneRenderObjectHandle>
                        groupObjectHandles;
                    for (const auto& drawClass :
                         frame.drawClasses) {
                        const auto* object = renderObject(
                            scene.registry,
                            drawClass.objectHandle);
                        const auto* mesh = object
                            ? geometry(
                                  scene.registry,
                                  object->geometryHandle)
                            : nullptr;
                        if (mesh &&
                            mesh->sourceMeshIndex ==
                                group->sourceMeshIndex) {
                            groupObjectHandles.push_back(
                                drawClass.objectHandle);
                        }
                    }
                    for (const auto objectHandle :
                         groupObjectHandles) {
                        append(
                            objectHandle,
                            modelMatrix);
                    }
                }
            };
        appendAuthoredRigidInstances(scene.frame);
        appendAuthoredRigidInstances(scene.shadowFrame);

        for (auto& layer : encounterGrass) {
            for (auto& placement : layer.placements) {
                const auto record = std::find_if(
                    encounterGrassRecords.begin(),
                    encounterGrassRecords.end(),
                    [&](const EncounterGrassRecord& candidate) {
                        return candidate.logicalName ==
                                layer.logicalName &&
                            candidate.recordIndex ==
                                placement.recordIndex;
                    });
                if (record ==
                    encounterGrassRecords.end()) {
                    return fail(
                        outError,
                        "Encounter-grass placement lost its source "
                        "record.");
                }
                const glm::vec3 localOffset(
                    placement.sourceCenter[0] -
                        record->sourceTranslationCm[0],
                    placement.sourceCenter[1] -
                        record->sourceTranslationCm[1],
                    placement.sourceCenter[2] -
                        record->sourceTranslationCm[2]);
                const glm::mat4 recordTransform =
                    glm::make_mat4(
                        sourcePlacementMatrix(
                            record->translationCm,
                            record->rotationDegrees,
                            record->scale)
                            .data());
                const glm::vec4 center =
                    recordTransform *
                    glm::vec4(localOffset, 1.0f);
                placement.center = {
                    center.x,
                    center.y,
                    center.z};
                placement.modelMatrix = toArray(
                    recordTransform *
                    glm::translate(
                        glm::mat4(1.0f),
                        localOffset));
                placement.suppressed =
                    record->suppressed;
            }
            placeEncounterGrassLayer(
                layer,
                windPhaseCycles);
        }

        layoutObjects.clear();
        layoutObjects.reserve(
            canonicalMeshGroups.size() +
            canonicalTreeInstances.size() +
            encounterGrassRecords.size() +
            54u +
            layout.authoredPrefabInstances.size());
        for (const auto& group : canonicalMeshGroups) {
            const bool treeSourceGroup =
                lgpe_route1_tree_instances::
                    expectedInstanceCount(
                        group.sourceMeshIndex) > 0u;
            if (treeSourceGroup &&
                !group.hasOverride) {
                continue;
            }
            layoutObjects.push_back(
                LayoutObject{
                    .stableId = group.stableId,
                    .displayName =
                        treeSourceGroup
                        ? group.displayName +
                              " (all instances)"
                        : group.displayName,
                    .targetKind =
                        "canonical_mesh_group",
                    .categoryPath =
                        treeSourceGroup
                        ? "Environment/Vegetation/Trees/Legacy Source Group Overrides"
                        : group.categoryPath,
                    .prefabAssetId =
                        group.prefabAssetId,
                    .logicalName =
                        group.logicalName,
                    .recordIndex =
                        group.sourceMeshIndex,
                    .sourceTranslationCm =
                        group.sourcePivotCm,
                    .sourceRotationDegrees = {},
                    .sourceScale =
                        {1.0f, 1.0f, 1.0f},
                    .translationCm =
                        group.translationCm,
                    .rotationDegrees =
                        group.rotationDegrees,
                    .scale = group.scale,
                    .suppressed =
                        group.suppressed,
                    .hasOverride =
                        group.hasOverride,
                    .reason = group.reason});
        }
        for (const auto& tree :
             canonicalTreeInstances) {
            const std::string treeLabel =
                "Tree 00" +
                std::to_string(
                    tree.sourceMeshIndex - 9u);
            layoutObjects.push_back(
                LayoutObject{
                    .stableId = tree.stableId,
                    .displayName =
                        treeLabel +
                        " - source instance " +
                        std::to_string(
                            tree.recordIndex + 1u),
                    .targetKind =
                        "canonical_tree_instance",
                    .categoryPath =
                        "Environment/Vegetation/Trees/" +
                        treeLabel,
                    .prefabAssetId =
                        tree.prefabAssetId,
                    .logicalName =
                        tree.logicalName,
                    .recordIndex =
                        tree.recordIndex,
                    .sourceTranslationCm =
                        tree.sourcePivotCm,
                    .sourceRotationDegrees = {},
                    .sourceScale =
                        {1.0f, 1.0f, 1.0f},
                    .translationCm =
                        tree.translationCm,
                    .rotationDegrees =
                        tree.rotationDegrees,
                    .scale = tree.scale,
                    .suppressed =
                        tree.suppressed,
                    .hasOverride =
                        tree.hasOverride,
                    .reason = tree.reason});
        }
        for (const auto& record :
             encounterGrassRecords) {
            if (record.authored) {
                continue;
            }
            layoutObjects.push_back(
                LayoutObject{
                    .stableId = record.stableId,
                    .displayName =
                        (record.logicalName ==
                                 "enc_grass01"
                             ? "Encounter Grass 01"
                             : "Encounter Grass 02") +
                        std::string(" - source patch ") +
                        std::to_string(
                            record.recordIndex),
                    .targetKind =
                        "encounter_grass_record",
                    .categoryPath =
                        "Environment/Vegetation/Encounter Grass/" +
                        (record.logicalName ==
                                 "enc_grass01"
                             ? std::string(
                                   "Encounter Grass 01")
                             : std::string(
                                   "Encounter Grass 02")),
                    .prefabAssetId =
                        record.logicalName ==
                                "enc_grass01"
                        ? "route1/encounter_grass_01"
                        : "route1/encounter_grass_02",
                    .logicalName =
                        record.logicalName,
                    .recordIndex =
                        record.recordIndex,
                    .sourceTranslationCm =
                        record.sourceTranslationCm,
                    .sourceRotationDegrees = {},
                    .sourceScale =
                        {1.0f, 1.0f, 1.0f},
                    .translationCm =
                        record.translationCm,
                    .rotationDegrees =
                        record.rotationDegrees,
                    .scale = record.scale,
                    .suppressed =
                        record.suppressed,
                    .hasOverride =
                        record.hasOverride,
                    .reason = record.reason});
        }
        for (auto& layer : placedVegetation) {
            for (auto& placement : layer.placements) {
                placement.modelMatrix = sourcePlacementMatrix(
                    placement.translationCm,
                    placement.rotationDegrees,
                    placement.scale);
            }
            placeVegetationLayer(layer, windPhaseCycles);
            for (const auto& placement : layer.placements) {
                if (placement.authored) {
                    continue;
                }
                layoutObjects.push_back(
                    LayoutObject{
                        .stableId = placement.stableId,
                        .displayName =
                            layer.logicalName +
                            " - source record " +
                            std::to_string(
                                placement.recordIndex),
                        .targetKind =
                            "buildmodel_vegetation_placement",
                        .categoryPath =
                            layer.logicalName == "grass02"
                            ? "Environment/Vegetation/Ground Cover/Small Grass 02"
                            : layer.logicalName == "flowers02"
                            ? "Environment/Vegetation/Flowers/Flowers 02"
                            : "Environment/Vegetation/Flowers/Flowers 04",
                        .prefabAssetId =
                            layer.logicalName == "grass02"
                            ? "route1/small_grass_02"
                            : layer.logicalName == "flowers02"
                            ? "route1/flowers_02"
                            : "route1/flowers_04",
                        .logicalName = layer.logicalName,
                        .recordIndex = placement.recordIndex,
                        .sourceTranslationCm =
                            placement.sourceTranslationCm,
                        .sourceRotationDegrees =
                            placement.sourceRotationDegrees,
                        .sourceScale =
                            placement.sourceScale,
                        .translationCm =
                            placement.translationCm,
                        .rotationDegrees =
                            placement.rotationDegrees,
                        .scale = placement.scale,
                        .suppressed =
                            placement.suppressed,
                        .hasOverride =
                            placement.hasOverride,
                    .reason = placement.reason});
            }
        }
        for (const auto& metadata :
             layout.objectMetadataOverrides) {
            const auto object = std::find_if(
                layoutObjects.begin(),
                layoutObjects.end(),
                [&](const LayoutObject& candidate) {
                    return candidate.stableId ==
                        metadata.stableId;
                });
            if (object == layoutObjects.end()) {
                return fail(
                    outError,
                    "Route 1 hierarchy metadata target no longer exists: " +
                        metadata.stableId);
            }
            if (!metadata.displayName.empty()) {
                object->displayName = metadata.displayName;
            }
            if (!metadata.categoryPath.empty()) {
                object->categoryPath = metadata.categoryPath;
            }
        }
        for (const auto& authored :
             layout.authoredPrefabInstances) {
            const auto prototype = std::find_if(
                layoutObjects.begin(),
                layoutObjects.end(),
                [&](const LayoutObject& candidate) {
                    return candidate.stableId ==
                        authored.prototypeStableId;
                });
            if (prototype == layoutObjects.end()) {
                return fail(
                    outError,
                    "Route 1 authored prefab prototype is not exposed as a layout object: " +
                        authored.prototypeStableId);
            }
            layoutObjects.push_back(
                LayoutObject{
                    .stableId = authored.stableId,
                    .displayName = authored.displayName,
                    .targetKind =
                        "authored_prefab_instance",
                    .categoryPath = authored.categoryPath,
                    .prefabAssetId =
                        prototype->prefabAssetId,
                    .logicalName =
                        prototype->logicalName,
                    .recordIndex =
                        prototype->recordIndex,
                    .sourceTranslationCm =
                        authored.sourceTranslationCm,
                    .sourceRotationDegrees =
                        authored.sourceRotationDegrees,
                    .sourceScale =
                        authored.sourceScale,
                    .translationCm =
                        authored.translationCm,
                    .rotationDegrees =
                        authored.rotationDegrees,
                    .scale = authored.scale,
                    .suppressed = authored.suppressed,
                    .hasOverride = true,
                    .authored = true,
                    .reason = authored.reason});
        }
        layout.declaredLocalDeltaCount =
            static_cast<std::uint32_t>(
                layout.localLayoutDeltas.size());
        return true;
    }

    void refreshStats() {
        stats = {};
        stats.sceneCount =
            static_cast<std::uint32_t>(scenes.size());
        for (const auto& layer : encounterGrass) {
            stats.encounterGrassInstanceCount +=
                static_cast<std::uint32_t>(
                    layer.instanceCount);
        }
        for (const auto& layer : placedVegetation) {
            stats.placedVegetationInstanceCount +=
                static_cast<std::uint32_t>(
                    layer.instanceCount);
        }
        for (const PreparedScene* prepared : scenes) {
            stats.materialCount +=
                static_cast<std::uint32_t>(
                    prepared->registry.materials.size());
            stats.drawClassCount +=
                static_cast<std::uint32_t>(
                    prepared->frame.drawClasses.size());
            for (const auto& drawClass :
                 prepared->frame.drawClasses) {
                const auto* object = renderObject(
                    prepared->registry,
                    drawClass.objectHandle);
                const auto* mesh =
                    object
                    ? geometry(
                          prepared->registry,
                          object->geometryHandle)
                    : nullptr;
                if (mesh) {
                    stats.visibleTriangleCount +=
                        (mesh->indexCount / 3u) *
                        drawClass.instances.size();
                }
            }
        }
        stats.shadowTriangleCount =
            projectedShadowAtlas.stats()
                .submittedTriangleCount;
    }

    bool rebuildLayoutDependentState(
        std::string* outError) {
        if (!applyLocalDeltas(outError)) {
            return false;
        }
        const std::array<float, 3> shadowCenter{
            layout.sourceAnchorCm[0],
            layout.sourceAnchorCm[1],
            layout.sourceAnchorCm[2]};
        std::string error;
        if (!projectedShadowAtlas.build(
                scenes,
                shadowCenter,
                &error)) {
            return fail(
                outError,
                "Could not rebuild Route 1 projected shadow after "
                "a layout edit: " +
                    error);
        }
        projectedShadowAtlas.attach(scenes);
        rebuildMaterialTemplates();
        refreshStats();
        return true;
    }

    void updateWind(float simulationSeconds) {
        windPhaseCycles = kInitialWindPhaseCycles +
            simulationSeconds / kWindPeriodSeconds;
        for (auto& layer : encounterGrass) {
            placeEncounterGrassLayer(
                layer,
                windPhaseCycles);
        }
        for (auto& layer : placedVegetation) {
            placeVegetationLayer(
                layer,
                windPhaseCycles);
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
        const int schemaVersion =
            root.at("schema_version").get<int>();
        if ((schemaVersion != 1 &&
             schemaVersion != 2 &&
             schemaVersion != 3 &&
             schemaVersion != 4) ||
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
        if (const auto registration =
                root.find("board_registration");
            registration != root.end()) {
            const auto cells = registration->at("board_cells");
            if (!cells.is_array() ||
                cells.size() != 2u) {
                throw std::runtime_error(
                    "board_cells must contain two positive integers.");
            }
            decoded.boardCells = {
                cells.at(0).get<std::uint32_t>(),
                cells.at(1).get<std::uint32_t>()};
        }
        const auto finiteArray =
            [](const std::array<float, 3>& values) {
                return std::all_of(
                    values.begin(),
                    values.end(),
                    [](float value) {
                        return std::isfinite(value);
                    });
            };
        std::set<std::string> deltaIds;
        std::set<std::string> deltaTargets;
        const auto deltaRecords =
            root.find("local_layout_deltas");
        if (deltaRecords != root.end()) {
        for (const auto& record : *deltaRecords) {
            LocalLayoutDelta delta;
            delta.id = record.at("id").get<std::string>();
            const auto& target = record.at("target");
            delta.targetKind =
                target.at("kind").get<std::string>();
            delta.logicalName =
                target.at("logical_name").get<std::string>();
            delta.recordIndex =
                target.at("record_index")
                    .get<std::uint32_t>();
            const auto& expected =
                record.at("expected_source_transform");
            delta.expectedSourceTranslationCm =
                jsonFloatArray<3>(
                    expected.at("translation_cm"),
                    "expected translation_cm");
            delta.expectedSourceRotationDegrees =
                jsonFloatArray<3>(
                    expected.at("rotation_degrees"),
                    "expected rotation_degrees");
            delta.expectedSourceScale =
                jsonFloatArray<3>(
                    expected.at("scale"),
                    "expected scale");
            const auto& authored =
                record.at("authored_transform");
            delta.translationCm =
                jsonFloatArray<3>(
                    authored.at("translation_cm"),
                    "authored translation_cm");
            delta.rotationDegrees =
                jsonFloatArray<3>(
                    authored.at("rotation_degrees"),
                    "authored rotation_degrees");
            delta.scale =
                jsonFloatArray<3>(
                    authored.at("scale"),
                    "authored scale");
            delta.suppressed =
                record.value("suppressed", false);
            delta.reason =
                record.value("reason", std::string{});
            const std::string stableTarget =
                delta.targetKind + "/" +
                delta.logicalName + "/" +
                std::to_string(
                    delta.recordIndex);
            const bool supportedTargetKind =
                delta.targetKind ==
                    "buildmodel_vegetation_placement" ||
                delta.targetKind ==
                    "encounter_grass_record" ||
                delta.targetKind ==
                    "canonical_tree_instance" ||
                delta.targetKind ==
                    "canonical_mesh_group";
            if (delta.id.empty() ||
                !supportedTargetKind ||
                delta.logicalName.empty() ||
                !finiteArray(
                    delta.expectedSourceTranslationCm) ||
                !finiteArray(
                    delta.expectedSourceRotationDegrees) ||
                !finiteArray(delta.expectedSourceScale) ||
                !finiteArray(delta.translationCm) ||
                !finiteArray(delta.rotationDegrees) ||
                !finiteArray(delta.scale) ||
                std::any_of(
                    delta.expectedSourceScale.begin(),
                    delta.expectedSourceScale.end(),
                    [](float value) {
                        return value <= 0.0f;
                    }) ||
                std::any_of(
                    delta.scale.begin(),
                    delta.scale.end(),
                    [](float value) {
                        return value <= 0.0f;
                    }) ||
                !deltaIds.insert(delta.id).second ||
                !deltaTargets.insert(stableTarget).second) {
                return fail(
                    outError,
                    "Route 1 board-layout manifest contains an "
                    "invalid or duplicate local delta.");
            }
            decoded.localLayoutDeltas.push_back(
                std::move(delta));
        }
        }
        if (const auto metadataRecords =
                root.find(
                    "hierarchy_metadata_overrides");
            metadataRecords != root.end()) {
            std::set<std::string> metadataTargets;
            for (const auto& record :
                 *metadataRecords) {
                LayoutObjectMetadataOverride metadata{
                    .stableId =
                        record.at("stable_id")
                            .get<std::string>(),
                    .displayName =
                        record.at("display_name")
                            .get<std::string>(),
                    .categoryPath =
                        record.at("category_path")
                            .get<std::string>()};
                if (metadata.stableId.empty() ||
                    metadata.displayName.empty() ||
                    metadata.categoryPath.empty() ||
                    !metadataTargets.insert(
                        metadata.stableId).second) {
                    return fail(
                        outError,
                        "Route 1 board-layout manifest contains invalid hierarchy metadata.");
                }
                decoded.objectMetadataOverrides.push_back(
                    std::move(metadata));
            }
        }
        if (const auto authoredRecords =
                root.find("authored_prefab_instances");
            authoredRecords != root.end()) {
            std::set<std::string> authoredIds;
            for (const auto& record :
                 *authoredRecords) {
                const auto& source =
                    record.at("creation_transform");
                const auto& authoredTransform =
                    record.at("authored_transform");
                AuthoredPrefabInstance instance{
                    .stableId =
                        record.at("stable_id")
                            .get<std::string>(),
                    .prototypeStableId =
                        record.at("prototype_stable_id")
                            .get<std::string>(),
                    .displayName =
                        record.at("display_name")
                            .get<std::string>(),
                    .categoryPath =
                        record.at("category_path")
                            .get<std::string>(),
                    .sourceTranslationCm =
                        jsonFloatArray<3>(
                            source.at("translation_cm"),
                            "authored prefab creation translation_cm"),
                    .sourceRotationDegrees =
                        jsonFloatArray<3>(
                            source.at("rotation_degrees"),
                            "authored prefab creation rotation_degrees"),
                    .sourceScale =
                        jsonFloatArray<3>(
                            source.at("scale"),
                            "authored prefab creation scale"),
                    .translationCm =
                        jsonFloatArray<3>(
                            authoredTransform.at("translation_cm"),
                            "authored prefab translation_cm"),
                    .rotationDegrees =
                        jsonFloatArray<3>(
                            authoredTransform.at("rotation_degrees"),
                            "authored prefab rotation_degrees"),
                    .scale =
                        jsonFloatArray<3>(
                            authoredTransform.at("scale"),
                            "authored prefab scale"),
                    .suppressed =
                        record.value("suppressed", false),
                    .reason =
                        record.value("reason", std::string{})};
                if (instance.stableId.empty() ||
                    instance.prototypeStableId.empty() ||
                    instance.displayName.empty() ||
                    instance.categoryPath.empty() ||
                    !finiteArray(instance.sourceTranslationCm) ||
                    !finiteArray(instance.sourceRotationDegrees) ||
                    !finiteArray(instance.sourceScale) ||
                    !finiteArray(instance.translationCm) ||
                    !finiteArray(instance.rotationDegrees) ||
                    !finiteArray(instance.scale) ||
                    std::any_of(
                        instance.sourceScale.begin(),
                        instance.sourceScale.end(),
                        [](float value) {
                            return value <= 0.0f;
                        }) ||
                    std::any_of(
                        instance.scale.begin(),
                        instance.scale.end(),
                        [](float value) {
                            return value <= 0.0f;
                        }) ||
                    !authoredIds.insert(
                        instance.stableId).second) {
                    return fail(
                        outError,
                        "Route 1 board-layout manifest contains an invalid authored prefab instance.");
                }
                decoded.authoredPrefabInstances.push_back(
                    std::move(instance));
            }
        }
        decoded.declaredLocalDeltaCount =
            static_cast<std::uint32_t>(
                decoded.localLayoutDeltas.size());
        if (decoded.coordinateSystem !=
                "source_centimetres_xyz_y_up" ||
            decoded.sourceProfileId != "lgpe_route1_road001_00" ||
            !std::isfinite(decoded.sourceUnitsToWorld) ||
            decoded.sourceUnitsToWorld <= 0.0f ||
            !std::isfinite(decoded.yawDegrees) ||
            decoded.boardCells[0] == 0u ||
            decoded.boardCells[1] == 0u) {
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

std::string serializeBoardLayoutTransform(
    const BoardLayoutTransform& transform) {
    nlohmann::json root{
        {"schema_version", 4},
        {"kind", "lgpe_route1_board_layout_delta"},
        {"coordinate_system", transform.coordinateSystem},
        {"source_profile_id", transform.sourceProfileId},
        {"source_to_world",
         {
             {"source_units_to_world",
              transform.sourceUnitsToWorld},
             {"source_anchor_cm",
              transform.sourceAnchorCm},
             {"world_anchor",
              transform.worldAnchor},
             {"yaw_degrees",
              transform.yawDegrees},
         }},
        {"board_registration",
         {
             {"board_cells", transform.boardCells},
             {"intent",
              "register the source-centimetre qualification scene "
              "under the gameplay board"},
             {"status", "global_registration_only"},
         }},
        {"fidelity_contract",
         {
             {"source_scale_preserved", true},
             {"source_elevations_preserved", true},
             {"undeclared_source_transforms_changed", false},
             {"procedural_route_environment_contribution", false},
         }},
    };
    return root.dump(2) + '\n';
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
    loaded->canonicalMeshGroups.reserve(
        loaded->source.meshes.size());
    for (const auto& mesh :
         loaded->source.meshes) {
        const std::array<float, 3> pivot{
            (mesh.boundsMinimum[0] +
             mesh.boundsMaximum[0]) *
                0.5f,
            mesh.boundsMinimum[1],
            (mesh.boundsMinimum[2] +
             mesh.boundsMaximum[2]) *
                0.5f};
        loaded->canonicalMeshGroups.push_back(
            CanonicalMeshGroup{
                .stableId =
                    canonicalMeshStableId(
                        mesh.sourceIndex),
                .displayName =
                    canonicalMeshDisplayName(
                        mesh.sourceIndex,
                        mesh.name),
                .categoryPath =
                    canonicalMeshCategory(
                        mesh.sourceIndex),
                .prefabAssetId =
                    canonicalMeshPrefabAssetId(
                        mesh.sourceIndex),
                .logicalName = mesh.name,
                .sourceMeshIndex =
                    mesh.sourceIndex,
                .sourceModelMatrix =
                    mesh.transform,
                .sourcePivotCm = pivot,
                .translationCm = pivot});
    }
    if (!loaded->splitCanonicalTreeInstances(
            outError)) {
        return false;
    }
    loaded->canonicalFrame =
        loaded->scene.frame;
    loaded->canonicalShadowFrame =
        loaded->scene.shadowFrame;

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
            const std::string logicalName =
                record.at("model").get<std::string>();
            const std::uint32_t recordIndex =
                record.at("record_index")
                    .get<std::uint32_t>();
            const auto translation =
                record.at("translation_cm")
                    .get<std::array<float, 3>>();
            loaded->encounterGrassRecords.push_back(
                EncounterGrassRecord{
                    .stableId =
                        encounterGrassStableId(
                            logicalName,
                            recordIndex),
                    .logicalName =
                        logicalName,
                    .recordIndex =
                        recordIndex,
                    .sourceTranslationCm =
                        translation,
                    .translationCm =
                        translation});
            auto expanded = expandedEncounterGrassPlacements(record);
            auto& placements =
                placementsByModel[logicalName];
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
            layer.canonicalPlacementCount =
                layer.placements.size();
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
                PlacedVegetationPlacement decodedPlacement;
                decodedPlacement.recordIndex =
                    placement.at("record_index").get<std::uint32_t>();
                decodedPlacement.stableId = placementStableId(
                    logicalName,
                    decodedPlacement.recordIndex);
                decodedPlacement.sourceTranslationCm =
                    placement.at("translation_cm")
                        .get<std::array<float, 3>>();
                decodedPlacement.sourceRotationDegrees =
                    placement.at("rotation_degrees")
                        .get<std::array<float, 3>>();
                decodedPlacement.sourceScale =
                    placement.at("scale")
                        .get<std::array<float, 3>>();
                decodedPlacement.translationCm =
                    decodedPlacement.sourceTranslationCm;
                decodedPlacement.rotationDegrees =
                    decodedPlacement.sourceRotationDegrees;
                decodedPlacement.scale =
                    decodedPlacement.sourceScale;
                decodedPlacement.modelMatrix =
                    sourcePlacementMatrix(
                        decodedPlacement.translationCm,
                        decodedPlacement.rotationDegrees,
                        decodedPlacement.scale);
                layer.placements.push_back(
                    std::move(decodedPlacement));
            }
            layer.canonicalPlacementCount =
                layer.placements.size();
            const std::size_t expectedModelInstances =
                model.at("instance_count").get<std::size_t>();
            if (layer.placements.size() !=
                expectedModelInstances) {
                return fail(
                    outError,
                    logicalName + " placement count changed.");
            }
            loadedVegetationInstances +=
                layer.placements.size();
        }
        if (loadedVegetationInstances != expectedVegetationInstances) {
            return fail(
                outError,
                "Route 1 placed-vegetation total changed.");
        }
        loaded->canonicalEncounterGrassRecordCount =
            loaded->encounterGrassRecords.size();
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

    if (!loaded->rebuildLayoutDependentState(&error)) {
        return fail(
            outError,
            "Could not apply Route 1 board layout: " +
                error);
    }
    loaded->authoredScene =
        authoredSceneFromLayout(
            loaded->layout,
            loaded->layoutObjects);
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

const engine::assets::phlosion::AuthoredSceneDocument&
RuntimeEnvironment::authoredScene() const noexcept {
    return impl_->authoredScene;
}

const std::vector<LayoutObject>&
RuntimeEnvironment::layoutObjects() const noexcept {
    static const std::vector<LayoutObject> empty;
    return impl_ ? impl_->layoutObjects : empty;
}

const RuntimeStats& RuntimeEnvironment::stats() const noexcept {
    return impl_->stats;
}

bool RuntimeEnvironment::applyBoardLayout(
    const BoardLayoutTransform& layout,
    std::string* outError) {
    if (!loaded()) {
        return fail(
            outError,
            "Route 1 must be mounted before applying a layout.");
    }
    if (layout.coordinateSystem !=
            impl_->layout.coordinateSystem ||
        layout.sourceProfileId !=
            impl_->source.profileId ||
        !std::isfinite(layout.sourceUnitsToWorld) ||
        layout.sourceUnitsToWorld <= 0.0f ||
        !std::isfinite(layout.yawDegrees) ||
        layout.boardCells[0] == 0u ||
        layout.boardCells[1] == 0u) {
        return fail(
            outError,
            "Route 1 layout metadata does not match the mounted "
            "canonical scene.");
    }

    BoardLayoutTransform previous = impl_->layout;
    impl_->layout = layout;
    impl_->layout.declaredLocalDeltaCount =
        static_cast<std::uint32_t>(
            impl_->layout.localLayoutDeltas.size());
    impl_->worldFromSource =
        boardMatrix(impl_->layout);
    impl_->sourceFromWorld =
        glm::inverse(impl_->worldFromSource);
    impl_->cloudProjectionRows =
        route1CloudProjectionRows(impl_->layout);
    std::string error;
    if (!impl_->rebuildLayoutDependentState(&error)) {
        impl_->layout = std::move(previous);
        impl_->worldFromSource =
            boardMatrix(impl_->layout);
        impl_->sourceFromWorld =
            glm::inverse(impl_->worldFromSource);
        impl_->cloudProjectionRows =
            route1CloudProjectionRows(impl_->layout);
        std::string ignored;
        impl_->rebuildLayoutDependentState(&ignored);
        return fail(
            outError,
            "Route 1 layout edit was rejected: " +
                error);
    }
    auto authored =
        authoredSceneFromLayout(
            impl_->layout,
            impl_->layoutObjects);
    inheritAuthoredSceneOrdering(
        authored,
        impl_->authoredScene);
    impl_->authoredScene = std::move(authored);
    if (outError) {
        outError->clear();
    }
    return true;
}

bool RuntimeEnvironment::applyAuthoredScene(
    const engine::assets::phlosion::AuthoredSceneDocument& document,
    std::string* outError) {
    if (!loaded()) {
        return fail(
            outError,
            "Route 1 must be mounted before applying an authored scene.");
    }
    BoardLayoutTransform registration = impl_->layout;
    registration.localLayoutDeltas.clear();
    registration.objectMetadataOverrides.clear();
    registration.authoredPrefabInstances.clear();
    registration.declaredLocalDeltaCount = 0u;
    BoardLayoutTransform composed;
    if (!boardLayoutFromAuthoredScene(
            document,
            registration,
            composed,
            outError)) {
        return false;
    }
    if (!applyBoardLayout(composed, outError)) {
        return false;
    }
    // Preserve the validated project document exactly. The adapter's
    // BoardLayoutTransform remains an internal composition representation.
    impl_->authoredScene = document;
    return true;
}

bool RuntimeEnvironment::setLayoutObjectOverride(
    const std::string& stableId,
    const std::array<float, 3>& translationCm,
    const std::array<float, 3>& rotationDegrees,
    const std::array<float, 3>& scale,
    bool suppressed,
    const std::string& reason,
    std::string* outError) {
    if (!loaded()) {
        return fail(
            outError,
            "Route 1 must be mounted before editing a layout object.");
    }
    const BoardLayoutTransform previous =
        impl_->layout;
    if (!previewLayoutObjectOverride(
            stableId,
            translationCm,
            rotationDegrees,
            scale,
            suppressed,
            reason,
            outError)) {
        return false;
    }
    const BoardLayoutTransform edited =
        impl_->layout;
    if (!applyBoardLayout(edited, outError)) {
        std::string ignored;
        applyBoardLayout(previous, &ignored);
        return false;
    }
    return true;
}

bool RuntimeEnvironment::previewLayoutObjectOverride(
    const std::string& stableId,
    const std::array<float, 3>& translationCm,
    const std::array<float, 3>& rotationDegrees,
    const std::array<float, 3>& scale,
    bool suppressed,
    const std::string& reason,
    std::string* outError) {
    const auto object = std::find_if(
        layoutObjects().begin(),
        layoutObjects().end(),
        [&](const LayoutObject& candidate) {
            return candidate.stableId == stableId;
        });
    if (object == layoutObjects().end()) {
        return fail(
            outError,
            "Unknown Route 1 layout object: " +
                stableId);
    }
    const auto finite =
        [](const std::array<float, 3>& values) {
            return std::all_of(
                values.begin(),
                values.end(),
                [](float value) {
                    return std::isfinite(value);
                });
        };
    if (!finite(translationCm) ||
        !finite(rotationDegrees) ||
        !finite(scale) ||
        std::any_of(
            scale.begin(),
            scale.end(),
            [](float value) {
                return value <= 0.0f;
            })) {
        return fail(
            outError,
            "Route 1 layout transforms must contain finite values "
            "and positive scale.");
    }
    if (object->authored) {
        BoardLayoutTransform next = impl_->layout;
        const auto authored = std::find_if(
            next.authoredPrefabInstances.begin(),
            next.authoredPrefabInstances.end(),
            [&](const AuthoredPrefabInstance& candidate) {
                return candidate.stableId == stableId;
            });
        if (authored ==
            next.authoredPrefabInstances.end()) {
            return fail(
                outError,
                "Authored Route 1 prefab instance lost its project document record: " +
                    stableId);
        }
        authored->translationCm = translationCm;
        authored->rotationDegrees = rotationDegrees;
        authored->scale = scale;
        authored->suppressed = suppressed;
        authored->reason =
            reason.empty()
            ? "editor_authored_prefab_instance"
            : reason;
        const BoardLayoutTransform previous =
            impl_->layout;
        impl_->layout = std::move(next);
        std::string error;
        if (!impl_->applyLocalDeltas(&error)) {
            impl_->layout = previous;
            std::string ignored;
            impl_->applyLocalDeltas(&ignored);
            return fail(
                outError,
                "Route 1 authored prefab preview was rejected: " +
                    error);
        }
        if (outError) {
            outError->clear();
        }
        return true;
    }
    constexpr float kTolerance = 0.0001f;
    const auto same =
        [](const std::array<float, 3>& lhs,
           const std::array<float, 3>& rhs) {
            return std::equal(
                lhs.begin(),
                lhs.end(),
                rhs.begin(),
                [](float left, float right) {
                    return std::abs(left - right) <=
                        kTolerance;
                });
        };
    if (!suppressed &&
        same(translationCm, object->sourceTranslationCm) &&
        same(rotationDegrees, object->sourceRotationDegrees) &&
        same(scale, object->sourceScale)) {
        return resetLayoutObjectOverride(
            stableId,
            outError);
    }

    BoardLayoutTransform next = impl_->layout;
    auto delta = std::find_if(
        next.localLayoutDeltas.begin(),
        next.localLayoutDeltas.end(),
        [&](const LocalLayoutDelta& candidate) {
            return candidate.targetKind ==
                    object->targetKind &&
                candidate.logicalName ==
                    object->logicalName &&
                candidate.recordIndex ==
                    object->recordIndex;
        });
    if (delta == next.localLayoutDeltas.end()) {
        next.localLayoutDeltas.push_back(
            LocalLayoutDelta{});
        delta =
            std::prev(next.localLayoutDeltas.end());
        delta->id =
            "route1-layout--" +
            object->targetKind + "--" +
            object->logicalName +
            "--record-" +
            std::to_string(object->recordIndex);
        delta->targetKind =
            object->targetKind;
        delta->logicalName =
            object->logicalName;
        delta->recordIndex =
            object->recordIndex;
        delta->expectedSourceTranslationCm =
            object->sourceTranslationCm;
        delta->expectedSourceRotationDegrees =
            object->sourceRotationDegrees;
        delta->expectedSourceScale =
            object->sourceScale;
    }
    delta->translationCm = translationCm;
    delta->rotationDegrees = rotationDegrees;
    delta->scale = scale;
    delta->suppressed = suppressed;
    delta->reason =
        reason.empty()
        ? "autochess_board_clearance"
        : reason;
    next.declaredLocalDeltaCount =
        static_cast<std::uint32_t>(
            next.localLayoutDeltas.size());
    const BoardLayoutTransform previous =
        impl_->layout;
    impl_->layout = std::move(next);
    std::string error;
    if (!impl_->applyLocalDeltas(&error)) {
        impl_->layout = previous;
        std::string ignored;
        impl_->applyLocalDeltas(&ignored);
        return fail(
            outError,
            "Route 1 live layout preview was rejected: " +
                error);
    }
    if (outError) {
        outError->clear();
    }
    return true;
}

bool RuntimeEnvironment::resetLayoutObjectOverride(
    const std::string& stableId,
    std::string* outError) {
    const auto object = std::find_if(
        layoutObjects().begin(),
        layoutObjects().end(),
        [&](const LayoutObject& candidate) {
            return candidate.stableId == stableId;
        });
    if (object == layoutObjects().end()) {
        return fail(
            outError,
            "Unknown Route 1 layout object: " +
                stableId);
    }
    if (object->authored) {
        BoardLayoutTransform next = impl_->layout;
        const auto authored = std::find_if(
            next.authoredPrefabInstances.begin(),
            next.authoredPrefabInstances.end(),
            [&](const AuthoredPrefabInstance& candidate) {
                return candidate.stableId == stableId;
            });
        if (authored ==
            next.authoredPrefabInstances.end()) {
            return fail(
                outError,
                "Authored Route 1 prefab instance lost its project document record: " +
                    stableId);
        }
        authored->translationCm =
            authored->sourceTranslationCm;
        authored->rotationDegrees =
            authored->sourceRotationDegrees;
        authored->scale = authored->sourceScale;
        authored->suppressed = false;
        return applyBoardLayout(next, outError);
    }
    BoardLayoutTransform next = impl_->layout;
    const auto previousSize =
        next.localLayoutDeltas.size();
    std::erase_if(
        next.localLayoutDeltas,
        [&](const LocalLayoutDelta& delta) {
            return delta.targetKind ==
                    object->targetKind &&
                delta.logicalName ==
                    object->logicalName &&
                delta.recordIndex ==
                    object->recordIndex;
        });
    if (next.localLayoutDeltas.size() ==
        previousSize) {
        if (outError) {
            outError->clear();
        }
        return true;
    }
    next.declaredLocalDeltaCount =
        static_cast<std::uint32_t>(
            next.localLayoutDeltas.size());
    return applyBoardLayout(next, outError);
}

bool RuntimeEnvironment::duplicateLayoutObject(
    const std::string& stableId,
    std::string& outCreatedStableId,
    std::string* outError) {
    const auto object = std::find_if(
        layoutObjects().begin(),
        layoutObjects().end(),
        [&](const LayoutObject& candidate) {
            return candidate.stableId == stableId;
        });
    if (object == layoutObjects().end()) {
        return fail(
            outError,
            "Unknown Route 1 layout object: " + stableId);
    }
    std::string prototypeStableId = stableId;
    if (object->authored) {
        const auto authored = std::find_if(
            impl_->layout.authoredPrefabInstances.begin(),
            impl_->layout.authoredPrefabInstances.end(),
            [&](const AuthoredPrefabInstance& candidate) {
                return candidate.stableId == stableId;
            });
        if (authored ==
            impl_->layout.authoredPrefabInstances.end()) {
            return fail(
                outError,
                "Authored Route 1 prefab instance lost its prototype binding.");
        }
        prototypeStableId =
            authored->prototypeStableId;
    }
    std::string slug;
    slug.reserve(prototypeStableId.size());
    bool previousSeparator = false;
    for (const unsigned char character :
         prototypeStableId) {
        if (std::isalnum(character)) {
            slug.push_back(
                static_cast<char>(
                    std::tolower(character)));
            previousSeparator = false;
        } else if (!previousSeparator &&
                   !slug.empty()) {
            slug.push_back('-');
            previousSeparator = true;
        }
    }
    while (!slug.empty() && slug.back() == '-') {
        slug.pop_back();
    }
    BoardLayoutTransform next = impl_->layout;
    std::uint32_t copyNumber = 1u;
    std::string createdId;
    do {
        createdId =
            "authored-prefab/" + slug +
            "/copy-" +
            std::to_string(copyNumber++);
    } while (std::any_of(
        next.authoredPrefabInstances.begin(),
        next.authoredPrefabInstances.end(),
        [&](const AuthoredPrefabInstance& candidate) {
            return candidate.stableId == createdId;
        }));
    const std::uint32_t displayCopyNumber =
        copyNumber - 1u;
    next.authoredPrefabInstances.push_back(
        AuthoredPrefabInstance{
            .stableId = createdId,
            .prototypeStableId = prototypeStableId,
            .displayName =
                object->displayName + " Copy " +
                std::to_string(displayCopyNumber),
            .categoryPath = object->categoryPath,
            .sourceTranslationCm =
                object->translationCm,
            .sourceRotationDegrees =
                object->rotationDegrees,
            .sourceScale = object->scale,
            .translationCm = object->translationCm,
            .rotationDegrees =
                object->rotationDegrees,
            .scale = object->scale,
            .suppressed = false,
            .reason =
                "editor_authored_prefab_instance"});
    if (!applyBoardLayout(next, outError)) {
        return false;
    }
    outCreatedStableId = createdId;
    return true;
}

bool RuntimeEnvironment::deleteLayoutObject(
    const std::string& stableId,
    std::string* outError) {
    const auto object = std::find_if(
        layoutObjects().begin(),
        layoutObjects().end(),
        [&](const LayoutObject& candidate) {
            return candidate.stableId == stableId;
        });
    if (object == layoutObjects().end()) {
        return fail(
            outError,
            "Unknown Route 1 layout object: " + stableId);
    }
    if (!object->authored) {
        return setLayoutObjectOverride(
            stableId,
            object->translationCm,
            object->rotationDegrees,
            object->scale,
            true,
            "editor_deleted_imported_source_object",
            outError);
    }
    BoardLayoutTransform next = impl_->layout;
    const std::size_t previousSize =
        next.authoredPrefabInstances.size();
    std::erase_if(
        next.authoredPrefabInstances,
        [&](const AuthoredPrefabInstance& candidate) {
            return candidate.stableId == stableId;
        });
    if (next.authoredPrefabInstances.size() ==
        previousSize) {
        return fail(
            outError,
            "Authored Route 1 prefab instance was not found in the project document.");
    }
    return applyBoardLayout(next, outError);
}

bool RuntimeEnvironment::renameLayoutObject(
    const std::string& stableId,
    const std::string& displayName,
    std::string* outError) {
    if (displayName.empty()) {
        return fail(
            outError,
            "Scene object names cannot be empty.");
    }
    const auto object = std::find_if(
        layoutObjects().begin(),
        layoutObjects().end(),
        [&](const LayoutObject& candidate) {
            return candidate.stableId == stableId;
        });
    if (object == layoutObjects().end()) {
        return fail(
            outError,
            "Unknown Route 1 layout object: " + stableId);
    }
    BoardLayoutTransform next = impl_->layout;
    if (object->authored) {
        const auto authored = std::find_if(
            next.authoredPrefabInstances.begin(),
            next.authoredPrefabInstances.end(),
            [&](const AuthoredPrefabInstance& candidate) {
                return candidate.stableId == stableId;
            });
        authored->displayName = displayName;
    } else {
        auto metadata = std::find_if(
            next.objectMetadataOverrides.begin(),
            next.objectMetadataOverrides.end(),
            [&](const LayoutObjectMetadataOverride& candidate) {
                return candidate.stableId == stableId;
            });
        if (metadata ==
            next.objectMetadataOverrides.end()) {
            next.objectMetadataOverrides.push_back(
                LayoutObjectMetadataOverride{
                    .stableId = stableId,
                    .displayName = displayName,
                    .categoryPath =
                        object->categoryPath});
        } else {
            metadata->displayName = displayName;
        }
    }
    return applyBoardLayout(next, outError);
}

bool RuntimeEnvironment::reparentLayoutObject(
    const std::string& stableId,
    const std::string& categoryPath,
    std::string* outError) {
    if (categoryPath.empty() ||
        (categoryPath != "Environment" &&
         categoryPath.rfind("Environment/", 0u) != 0u)) {
        return fail(
            outError,
            "Hierarchy folders must be Environment or begin with Environment/. ");
    }
    const auto object = std::find_if(
        layoutObjects().begin(),
        layoutObjects().end(),
        [&](const LayoutObject& candidate) {
            return candidate.stableId == stableId;
        });
    if (object == layoutObjects().end()) {
        return fail(
            outError,
            "Unknown Route 1 layout object: " + stableId);
    }
    BoardLayoutTransform next = impl_->layout;
    if (object->authored) {
        const auto authored = std::find_if(
            next.authoredPrefabInstances.begin(),
            next.authoredPrefabInstances.end(),
            [&](const AuthoredPrefabInstance& candidate) {
                return candidate.stableId == stableId;
            });
        authored->categoryPath = categoryPath;
    } else {
        auto metadata = std::find_if(
            next.objectMetadataOverrides.begin(),
            next.objectMetadataOverrides.end(),
            [&](const LayoutObjectMetadataOverride& candidate) {
                return candidate.stableId == stableId;
            });
        if (metadata ==
            next.objectMetadataOverrides.end()) {
            next.objectMetadataOverrides.push_back(
                LayoutObjectMetadataOverride{
                    .stableId = stableId,
                    .displayName =
                        object->displayName,
                    .categoryPath = categoryPath});
        } else {
            metadata->categoryPath = categoryPath;
        }
    }
    return applyBoardLayout(next, outError);
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
