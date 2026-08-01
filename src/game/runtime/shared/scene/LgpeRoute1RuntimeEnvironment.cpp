#include "game/runtime/shared/scene/LgpeRoute1RuntimeEnvironment.h"

#include "engine/assets/lgpe/LgpeCanonicalScene.h"
#include "engine/core/Environment.h"
#include "engine/core/IAssetStore.h"
#include "engine/render/LgpeFieldEncounterGrassMaterial.h"
#include "engine/render/LgpeFieldSmallGrassMaterial.h"
#include "game/runtime/shared/scene/LgpeRoute1ProjectedShadow.h"
#include "game/runtime/shared/scene/LgpeRoute1TerrainAssemblies.h"
#include "game/runtime/shared/scene/LgpeRoute1TreeInstances.h"
#include "game/runtime/shared/scene/LgpeWorldSceneAdapter.h"
#include "game/runtime/shared/scene/SharedWorldScene.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <limits>
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

bool validTerrainVisualVariant(
    std::string_view surface,
    std::string_view variant) {
    if (variant == "auto") {
        return true;
    }
    if (surface == "light_lawn" || surface == "dark_lawn") {
        return variant == "lawn_a" || variant == "lawn_b" ||
            variant == "lawn_c" || variant == "lawn_d";
    }
    if (surface != "dirt_path" ||
        !variant.starts_with("path_")) {
        return false;
    }
    const auto digits = variant.substr(5u);
    std::uint32_t mask = 0u;
    const auto result = std::from_chars(
        digits.data(), digits.data() + digits.size(), mask);
    return result.ec == std::errc{} &&
        result.ptr == digits.data() + digits.size() &&
        mask <= 15u;
}

bool automaticTerrainAppearance(std::string_view surface) {
    return surface == "light_lawn" ||
        surface == "dark_lawn" ||
        surface == "dirt_path" ||
        surface == "empty";
}

void normalizeTerrainVisualVariants(
    BoardLayoutTransform& layout) {
    for (auto& tile : layout.authoredTerrainTiles) {
        if (automaticTerrainAppearance(tile.surface)) {
            tile.visualVariant = "auto";
        }
    }
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
    std::array<float, 3> sourceBoundsMinimumCm{};
    std::array<float, 3> sourceBoundsMaximumCm{};
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
    std::array<float, 3> localBoundsMinimumCm{};
    std::array<float, 3> localBoundsMaximumCm{};
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
    std::array<float, 3> sourceBoundsMinimumCm{};
    std::array<float, 3> sourceBoundsMaximumCm{};
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
    std::array<float, 3> sourceBoundsMinimumCm{
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max()};
    std::array<float, 3> sourceBoundsMaximumCm{
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest()};
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

struct CanonicalTerrainAssembly {
    std::string stableId;
    std::string displayName;
    std::string categoryPath;
    std::string logicalName;
    std::string prefabAssetId;
    std::string profileRole;
    std::uint32_t sourceMeshIndex = 0u;
    std::uint32_t recordIndex = 0u;
    std::array<float, 16> sourceModelMatrix{};
    std::array<float, 3> sourcePivotCm{};
    std::array<float, 3> sourceBoundsMinimumCm{};
    std::array<float, 3> sourceBoundsMaximumCm{};
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

struct BoardGroundPatchPrototype {
    std::array<IRenderBackend::WorldMeshVertex, 4> vertices{};
    std::array<IRenderBackend::WorldSceneSourceVertex, 4>
        sourceVertices{};
    std::array<std::uint32_t, 6> indices{
        0u, 1u, 2u, 0u, 2u, 3u};
    IRenderBackend::WorldSceneRenderObjectHandle objectHandle{};
    std::array<float, 3> sourcePivotCm{};
    std::array<float, 3> translationCm{};
    std::array<float, 3> rotationDegrees{};
    std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
    bool suppressed = true;
    bool hasOverride = false;
    std::string reason;
};

struct TerrainTileTopPrototype {
    std::vector<IRenderBackend::WorldMeshVertex> vertices;
    std::vector<IRenderBackend::WorldSceneSourceVertex>
        sourceVertices;
    std::vector<std::uint32_t> indices;
    IRenderBackend::WorldSceneRenderObjectHandle object{};
};

struct SourceTerrainTriangle {
    std::array<glm::vec3, 3> positions{};
    std::array<glm::vec2, 3> uv0{};
    std::array<glm::vec2, 3> uv1{};
    std::array<glm::vec2, 3> uv2{};
    std::array<glm::vec4, 3> color0{};
};

struct SourceTerrainSurfaceSample {
    float y = 0.0f;
    glm::vec2 uv0{};
    glm::vec2 uv1{};
    glm::vec2 uv2{};
    glm::vec4 color0{1.0f};
};

struct TerrainTilePrototypeSet {
    // Flat tiles are generated lazily for the cells used by authored terrain.
    // Authored replacements use one continuous source-world UV field and an
    // exact plane; untouched terrain remains in the canonical source draw.
    std::map<std::string, TerrainTileTopPrototype> topPrototypes;
    std::map<std::string, TerrainTileTopPrototype> cliffPrototypes;
    IRenderBackend::WorldMeshVertex groundVertexTemplate{};
    IRenderBackend::WorldSceneSourceVertex groundSourceVertexTemplate{};
    std::uint32_t groundSourceVertexSemanticMask = 0u;
    IRenderBackend::WorldSceneMaterialHandle groundMaterialHandle{};
    std::uint8_t groundPipelineVariant = 0u;
    std::uint32_t groundCookedDrawSlot = 0u;
    std::array<IRenderBackend::WorldMeshVertex, 4>
        lightVertices{};
    std::array<IRenderBackend::WorldSceneSourceVertex, 4>
        lightSourceVertices{};
    std::array<IRenderBackend::WorldMeshVertex, 4>
        dirtVertices{};
    std::array<IRenderBackend::WorldSceneSourceVertex, 4>
        dirtSourceVertices{};
    std::array<IRenderBackend::WorldMeshVertex, 4>
        darkVertices{};
    std::array<IRenderBackend::WorldSceneSourceVertex, 4>
        darkSourceVertices{};
    std::array<IRenderBackend::WorldMeshVertex, 4>
        lightRampVertices{};
    std::array<IRenderBackend::WorldMeshVertex, 4>
        dirtRampVertices{};
    std::array<IRenderBackend::WorldMeshVertex, 4>
        darkRampVertices{};
    std::array<std::uint32_t, 6> indices{
        0u, 1u, 2u, 0u, 2u, 3u};
    IRenderBackend::WorldSceneRenderObjectHandle lightTopObject{};
    IRenderBackend::WorldSceneRenderObjectHandle dirtTopObject{};
    IRenderBackend::WorldSceneRenderObjectHandle darkTopObject{};
    IRenderBackend::WorldSceneRenderObjectHandle lightRampObject{};
    IRenderBackend::WorldSceneRenderObjectHandle dirtRampObject{};
    IRenderBackend::WorldSceneRenderObjectHandle darkRampObject{};
    IRenderBackend::WorldMeshVertex cliffVertexTemplate{};
    IRenderBackend::WorldSceneSourceVertex
        cliffSourceVertexTemplate{};
    std::uint32_t cliffSourceVertexSemanticMask = 0u;
    IRenderBackend::WorldSceneMaterialHandle cliffMaterialHandle{};
    std::uint8_t cliffPipelineVariant = 0u;
    std::uint32_t cliffCookedDrawSlot = 0u;
};

struct TerrainMaskGeometry {
    IRenderBackend::WorldSceneGeometryHandle geometryHandle{};
    std::string originalCacheKey;
    std::vector<std::uint32_t> originalIndices;
    std::vector<std::uint32_t> filteredIndices;
    std::array<float, 16> sourceModelMatrix{};
    bool cleanupOnly = false;
    bool maskWhenAnyVertexTouchesCell = false;
};

struct SceneMaterialTemplates {
    PreparedScene* scene = nullptr;
    std::vector<WorldBatch> materials;
};

std::array<float, 16> sourcePlacementMatrix(
    const std::array<float, 3>& translation,
    const std::array<float, 3>& rotation,
    const std::array<float, 3>& scale);

struct SourceBounds {
    std::array<float, 3> minimum{};
    std::array<float, 3> maximum{};
};

SourceBounds transformSourceBounds(
    const std::array<float, 3>& minimum,
    const std::array<float, 3>& maximum,
    const glm::mat4& transform) {
    glm::vec3 transformedMinimum(
        std::numeric_limits<float>::max());
    glm::vec3 transformedMaximum(
        std::numeric_limits<float>::lowest());
    for (std::uint32_t corner = 0u;
         corner < 8u;
         ++corner) {
        const glm::vec4 transformed =
            transform * glm::vec4(
                (corner & 1u) != 0u
                    ? maximum[0]
                    : minimum[0],
                (corner & 2u) != 0u
                    ? maximum[1]
                    : minimum[1],
                (corner & 4u) != 0u
                    ? maximum[2]
                    : minimum[2],
                1.0f);
        transformedMinimum = glm::min(
            transformedMinimum,
            glm::vec3(transformed));
        transformedMaximum = glm::max(
            transformedMaximum,
            glm::vec3(transformed));
    }
    return {
        {transformedMinimum.x,
         transformedMinimum.y,
         transformedMinimum.z},
        {transformedMaximum.x,
         transformedMaximum.y,
         transformedMaximum.z}};
}

SourceBounds pivotTransformedBounds(
    const std::array<float, 3>& sourceMinimum,
    const std::array<float, 3>& sourceMaximum,
    const std::array<float, 3>& sourcePivot,
    const std::array<float, 3>& translation,
    const std::array<float, 3>& rotation,
    const std::array<float, 3>& scale) {
    const glm::mat4 transform =
        glm::make_mat4(
            sourcePlacementMatrix(
                translation,
                rotation,
                scale)
                .data()) *
        glm::translate(
            glm::mat4(1.0f),
            -glm::vec3(
                sourcePivot[0],
                sourcePivot[1],
                sourcePivot[2]));
    return transformSourceBounds(
        sourceMinimum,
        sourceMaximum,
        transform);
}

SourceBounds relativePlacementTransformedBounds(
    const std::array<float, 3>& sourceMinimum,
    const std::array<float, 3>& sourceMaximum,
    const std::array<float, 3>& sourceTranslation,
    const std::array<float, 3>& sourceRotation,
    const std::array<float, 3>& sourceScale,
    const std::array<float, 3>& translation,
    const std::array<float, 3>& rotation,
    const std::array<float, 3>& scale) {
    const glm::mat4 source = glm::make_mat4(
        sourcePlacementMatrix(
            sourceTranslation,
            sourceRotation,
            sourceScale)
            .data());
    const glm::mat4 current = glm::make_mat4(
        sourcePlacementMatrix(
            translation,
            rotation,
            scale)
            .data());
    return transformSourceBounds(
        sourceMinimum,
        sourceMaximum,
        current * glm::inverse(source));
}

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

constexpr std::string_view kBoardGroundPrototypeStableId =
    "gameplay-board/ground-patch-prototype";
constexpr std::string_view kBoardGroundLogicalName =
    "autochess_board_ground_patch";
constexpr std::string_view kBoardGroundPrefabAssetId =
    "route1/autochess_board_ground_patch";
constexpr std::string_view kTerrainTileSetAssetId =
    "route1/terrain_tileset";
constexpr float kTerrainTileSizeCm = 100.0f;
constexpr float kTerrainElevationStepCm = 50.0f;
// Keep replacements just above coplanar source triangles without exposing a
// visible step against the source surface at grazing camera angles.
constexpr float kTerrainTileTopDepthBiasCm = 0.02f;

bool boardRegistrationMatchesTerrainGrid(
    const BoardLayoutTransform& layout) {
    const float expectedCellSizeWorld =
        kTerrainTileSizeCm * layout.sourceUnitsToWorld;
    const std::array<float, 3> expectedAnchor{
        (static_cast<float>(layout.terrainGridOrigin[0]) +
         static_cast<float>(layout.boardCells[0]) * 0.5f) *
            kTerrainTileSizeCm,
        static_cast<float>(layout.terrainElevationLevel) *
            kTerrainElevationStepCm,
        (static_cast<float>(layout.terrainGridOrigin[1]) +
         static_cast<float>(layout.boardCells[1]) * 0.5f) *
            kTerrainTileSizeCm};
    return std::abs(
               layout.boardCellSizeWorld -
               expectedCellSizeWorld) <= 0.0001f &&
        std::abs(layout.sourceAnchorCm[0] - expectedAnchor[0]) <=
            0.001f &&
        std::abs(layout.sourceAnchorCm[1] - expectedAnchor[1]) <=
            0.001f &&
        std::abs(layout.sourceAnchorCm[2] - expectedAnchor[2]) <=
            0.001f &&
        std::abs(layout.worldAnchor[0]) <= 0.0001f &&
        std::abs(layout.worldAnchor[2]) <= 0.0001f &&
        std::abs(layout.yawDegrees) <= 0.0001f &&
        layout.benchGapCells == 1u &&
        ((layout.boardCells[0] + layout.benchSlots) % 2u) == 0u;
}

std::string buildTerrainTileStableId(
    std::int32_t gridX,
    std::int32_t gridZ) {
    const auto coordinate = [](std::int32_t value) {
        return value < 0
            ? "n" + std::to_string(-value)
            : "p" + std::to_string(value);
    };
    return "terrain-tile/x-" + coordinate(gridX) +
        "/z-" + coordinate(gridZ);
}

std::string terrainAssemblyLogicalName(
    std::uint32_t sourceMeshIndex) {
    std::string number = std::to_string(sourceMeshIndex);
    while (number.size() < 3u) {
        number.insert(number.begin(), '0');
    }
    return "terrain_mesh_" + number;
}

std::string terrainAssemblyStableId(
    std::uint32_t sourceMeshIndex,
    std::uint32_t assemblyIndex) {
    return "canonical-terrain/mesh-" +
        std::to_string(sourceMeshIndex) +
        "/assembly-" + std::to_string(assemblyIndex);
}

std::string terrainAssemblyPrefabAssetId(
    std::uint32_t sourceMeshIndex,
    std::uint32_t assemblyIndex) {
    std::string ordinal =
        std::to_string(assemblyIndex + 1u);
    while (ordinal.size() < 2u) {
        ordinal.insert(ordinal.begin(), '0');
    }
    return "route1/" +
        terrainAssemblyLogicalName(sourceMeshIndex) +
        "_assembly_" + ordinal;
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
    if (index >= 29u && index <= 35u) {
        return {};
    }
    std::string number = std::to_string(index);
    while (number.size() < 3u) {
        number.insert(number.begin(), '0');
    }
    return "route1/source_mesh_" + number;
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
using TerrainTileBinding =
    engine::assets::phlosion::TerrainTileBinding;

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
    if (targetKind == "gameplay_board_ground_prototype" &&
        logicalName == kBoardGroundLogicalName &&
        recordIndex == 0u) {
        return std::string(kBoardGroundPrototypeStableId);
    }
    if (targetKind == "canonical_terrain_assembly") {
        constexpr std::string_view kPrefix = "terrain_mesh_";
        if (logicalName.rfind(kPrefix, 0u) != 0u) {
            return {};
        }
        try {
            const std::uint32_t sourceMeshIndex =
                static_cast<std::uint32_t>(
                    std::stoul(
                        std::string(
                            logicalName.substr(kPrefix.size()))));
            return terrainAssemblyStableId(
                sourceMeshIndex,
                recordIndex);
        } catch (...) {
            return {};
        }
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
    for (const auto& authored : layout.authoredTerrainTiles) {
        const std::string parentId = ensureAuthoredFolders(
            document,
            authored.categoryPath.empty()
                ? "Environment/Terrain/Tiles"
                : authored.categoryPath);
        const std::uint32_t siblingOrder =
            nextAuthoredSiblingOrder(document, parentId);
        document.nodes.push_back(
            AuthoredSceneNode{
                .id = authored.stableId,
                .displayName = authored.displayName,
                .parentId = parentId,
                .siblingOrder = siblingOrder,
                .enabled = true,
                .reason = authored.reason,
                .terrainTile = TerrainTileBinding{
                    .tileSetAssetId = authored.tileSetAssetId,
                    .gridX = authored.gridX,
                    .gridZ = authored.gridZ,
                    .elevationLevel = authored.elevationLevel,
                    .surface = authored.surface,
                    .shape = authored.shape,
                    .visualVariant = authored.visualVariant}});
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
    composed.authoredTerrainTiles.clear();
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
        if (node.terrainTile) {
            const auto& tile = *node.terrainTile;
            if (categoryPath.empty()) {
                return fail(
                    outError,
                    "Authored terrain tiles require a hierarchy folder: " +
                        node.id);
            }
            if (tile.tileSetAssetId != kTerrainTileSetAssetId ||
                node.id != route1TerrainTileStableId(
                    tile.gridX,
                    tile.gridZ)) {
                return fail(
                    outError,
                    "Authored Route 1 terrain tile does not match its grid binding: " +
                        node.id);
            }
            composed.authoredTerrainTiles.push_back(
                AuthoredTerrainTile{
                    .stableId = node.id,
                    .displayName = node.displayName,
                    .categoryPath = categoryPath,
                    .tileSetAssetId = tile.tileSetAssetId,
                    .gridX = tile.gridX,
                    .gridZ = tile.gridZ,
                    .elevationLevel = tile.elevationLevel,
                    .surface = tile.surface,
                    .shape = tile.shape,
                    .visualVariant = tile.visualVariant,
                    .reason = node.reason});
            continue;
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

std::string route1TerrainTileStableId(
    std::int32_t gridX,
    std::int32_t gridZ) {
    return buildTerrainTileStableId(gridX, gridZ);
}

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
    std::vector<CanonicalTerrainAssembly>
        canonicalTerrainAssemblies;
    std::vector<lgpe_world_scene::PolygonGroupStorage>
        canonicalTerrainPolygonStorage;
    BoardGroundPatchPrototype boardGroundPatch;
    TerrainTilePrototypeSet terrainTilePrototypes;
    std::vector<TerrainTileState> sourceTerrainTiles;
    std::vector<TerrainTileState> terrainTiles;
    std::vector<SourceTerrainTriangle> sourceTerrainTriangles;
    std::map<
        std::pair<std::int32_t, std::int32_t>,
        std::vector<std::size_t>>
        sourceTerrainTrianglesByCell;
    const engine::assets::lgpe::TextureSubresource*
        sourceTerrainGroundMask = nullptr;
    std::vector<TerrainMaskGeometry> terrainMaskGeometries;
    std::set<std::pair<std::int32_t, std::int32_t>>
        terrainMaskCells;
    std::set<std::pair<std::int32_t, std::int32_t>>
        terrainCleanupCells;
    std::uint32_t terrainMaskRevision = 0u;
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

    bool initializeBoardGroundPatch(
        std::string* outError) {
        const auto sourceMesh = std::find_if(
            source.meshes.begin(),
            source.meshes.end(),
            [](const auto& mesh) {
                return mesh.sourceIndex == 25u;
            });
        if (sourceMesh == source.meshes.end() ||
            sourceMesh->vertices.size() != 4u) {
            return fail(
                outError,
                "Route 1 board ground patch requires the exact four-vertex source grass plane.");
        }
        const std::size_t storageIndex =
            static_cast<std::size_t>(
                std::distance(
                    source.meshes.begin(),
                    sourceMesh));
        if (storageIndex >= scene.meshVertexStorage.size() ||
            scene.meshVertexStorage[storageIndex]
                    .vertices.size() != 4u ||
            scene.meshVertexStorage[storageIndex]
                    .sourceVertices.size() != 4u) {
            return fail(
                outError,
                "Route 1 board ground patch source vertex storage changed.");
        }
        const auto& sourceVertices =
            scene.meshVertexStorage[storageIndex]
                .vertices;
        const auto& sourceExtraVertices =
            scene.meshVertexStorage[storageIndex]
                .sourceVertices;
        float minimumX =
            std::numeric_limits<float>::max();
        float maximumX =
            std::numeric_limits<float>::lowest();
        float minimumZ =
            std::numeric_limits<float>::max();
        float maximumZ =
            std::numeric_limits<float>::lowest();
        for (const auto& vertex : sourceVertices) {
            minimumX = std::min(minimumX, vertex.x);
            maximumX = std::max(maximumX, vertex.x);
            minimumZ = std::min(minimumZ, vertex.z);
            maximumZ = std::max(maximumZ, vertex.z);
        }
        constexpr std::array<std::array<bool, 2>, 4>
            kCorners{{
                {false, false},
                {true, false},
                {true, true},
                {false, true},
            }};
        std::set<std::size_t> used;
        for (std::size_t corner = 0u;
             corner < kCorners.size();
             ++corner) {
            const float targetX =
                kCorners[corner][0]
                ? maximumX
                : minimumX;
            const float targetZ =
                kCorners[corner][1]
                ? maximumZ
                : minimumZ;
            std::size_t bestIndex = 0u;
            float bestDistance =
                std::numeric_limits<float>::max();
            for (std::size_t index = 0u;
                 index < sourceVertices.size();
                 ++index) {
                if (used.contains(index)) {
                    continue;
                }
                const float dx =
                    sourceVertices[index].x - targetX;
                const float dz =
                    sourceVertices[index].z - targetZ;
                const float distance = dx * dx + dz * dz;
                if (distance < bestDistance) {
                    bestDistance = distance;
                    bestIndex = index;
                }
            }
            used.insert(bestIndex);
            boardGroundPatch.vertices[corner] =
                sourceVertices[bestIndex];
            boardGroundPatch.sourceVertices[corner] =
                sourceExtraVertices[bestIndex];
            boardGroundPatch.vertices[corner].x =
                kCorners[corner][0] ? 50.0f : -50.0f;
            boardGroundPatch.vertices[corner].y = 0.0f;
            boardGroundPatch.vertices[corner].z =
                kCorners[corner][1] ? 50.0f : -50.0f;
        }

        const auto materialGeometry = std::find_if(
            scene.registry.geometries.begin(),
            scene.registry.geometries.end(),
            [](const auto& geometry) {
                return geometry.sourceMeshIndex == 31u &&
                    geometry.sourcePolygonGroupIndex == 0u;
            });
        if (materialGeometry ==
            scene.registry.geometries.end()) {
            return fail(
                outError,
                "Route 1 board ground patch lost its source grass material geometry.");
        }
        const auto materialObject = std::find_if(
            scene.registry.renderObjects.begin(),
            scene.registry.renderObjects.end(),
            [&](const auto& object) {
                return object.geometryHandle.id ==
                    materialGeometry->handle.id;
            });
        if (materialObject ==
            scene.registry.renderObjects.end()) {
            return fail(
                outError,
                "Route 1 board ground patch lost its source grass render object.");
        }
        const auto geometryHandle =
            shared_world_scene::ensureRigidGeometry(
                scene.registry,
                &boardGroundPatch,
                "route1:autochess-board-ground-patch",
                boardGroundPatch.vertices.data(),
                boardGroundPatch.vertices.size(),
                boardGroundPatch.indices.data(),
                boardGroundPatch.indices.size(),
                boardGroundPatch.sourceVertices.data(),
                boardGroundPatch.sourceVertices.size(),
                materialGeometry->sourceVertexSemanticMask,
                std::numeric_limits<std::uint32_t>::max(),
                0u);
        boardGroundPatch.objectHandle =
            shared_world_scene::ensureRenderObject(
                scene.registry,
                geometryHandle,
                materialObject->materialHandle,
                static_cast<shared_world_scene::PipelineVariant>(
                    materialObject->pipelineVariant),
                materialObject->cookedDrawSlot,
                false);
        boardGroundPatch.sourcePivotCm = {
            layout.sourceAnchorCm[0],
            layout.sourceAnchorCm[1] + 1.0f,
            layout.sourceAnchorCm[2]};
        boardGroundPatch.translationCm =
            boardGroundPatch.sourcePivotCm;
        IRenderBackend::WorldSceneRenderInstanceHandle instance;
        instance.id = 0xf0000000u;
        shared_world_scene::appendRigidInstance(
            scene.frame,
            boardGroundPatch.objectHandle,
            instance,
            sourcePlacementMatrix(
                boardGroundPatch.translationCm,
                {},
                {1.0f, 1.0f, 1.0f}),
            1.0f,
            1.0f,
            1.0f,
            1.0f,
            0.0f);
        return true;
    }

    bool initializeTerrainTiles(
        std::string* outError);

    bool initializeTerrainMask(
        std::string* outError);

    void applyTerrainMask();

    void rebuildTerrainTileStates();

    IRenderBackend::WorldSceneRenderObjectHandle
    ensureTerrainTopObject(
        const TerrainTileState& tile,
        std::uint32_t dirtConnectionMask);

    IRenderBackend::WorldSceneRenderObjectHandle
    ensureTerrainCliffObject(
        const TerrainTileState& tile,
        std::size_t edge,
        std::int32_t levelDifference);

    IRenderBackend::WorldSceneRenderObjectHandle
    ensureTerrainCliffCornerObject(
        const TerrainTileState& tile,
        std::size_t corner,
        std::int32_t levelDifference);

    bool sampleSourceTerrainSurface(
        const TerrainTileState& tile,
        float localX,
        float localZ,
        SourceTerrainSurfaceSample& out) const;

    bool sampleSourceTerrainGroundMaskAlpha(
        const glm::vec2& sourceUv2,
        float& outAlpha) const;

    void appendAuthoredTerrainTiles(
        IRenderBackend::WorldSceneFrame& frame);

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
                    auto& tree =
                        canonicalTreeInstances[
                            firstTree + instance];
                    const glm::mat4 sourceTransform =
                        glm::make_mat4(
                            mesh.transform.data());
                    for (const std::uint32_t vertexIndex :
                         storage.indices) {
                        const auto& position =
                            mesh.vertices[vertexIndex]
                                .position;
                        const glm::vec4 transformed =
                            sourceTransform * glm::vec4(
                                position[0],
                                position[1],
                                position[2],
                                1.0f);
                        const std::array<float, 3> values{
                            transformed.x,
                            transformed.y,
                            transformed.z};
                        for (std::size_t axis = 0u;
                             axis < 3u;
                             ++axis) {
                            tree.sourceBoundsMinimumCm[axis] =
                                std::min(
                                    tree.sourceBoundsMinimumCm[axis],
                                    values[axis]);
                            tree.sourceBoundsMaximumCm[axis] =
                                std::max(
                                    tree.sourceBoundsMaximumCm[axis],
                                    values[axis]);
                        }
                    }
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

    bool splitCanonicalTerrainAssemblies(
        std::string* outError) {
        namespace terrain =
            lgpe_route1_terrain_assemblies;
        canonicalTerrainAssemblies.clear();
        canonicalTerrainPolygonStorage.clear();

        std::map<std::uint32_t, terrain::MeshPartition>
            partitions;
        std::size_t storageCount = 0u;
        for (const auto& mesh : source.meshes) {
            if (terrain::expectedAssemblyCount(
                    mesh.sourceIndex) == 0u) {
                continue;
            }
            terrain::MeshPartition partition;
            if (!terrain::derivePartition(
                    mesh,
                    partition,
                    outError)) {
                return false;
            }
            for (const auto& assembly :
                 partition.assemblies) {
                storageCount +=
                    assembly.polygonGroups.size();
            }
            partitions.emplace(
                mesh.sourceIndex,
                std::move(partition));
        }
        canonicalTerrainPolygonStorage.reserve(
            storageCount);
        canonicalTerrainAssemblies.reserve(23u);

        using ObjectHandle =
            IRenderBackend::WorldSceneRenderObjectHandle;
        using GroupKey =
            std::pair<std::uint32_t, std::uint32_t>;
        std::map<GroupKey, std::vector<ObjectHandle>>
            objectsByGroup;
        std::unordered_map<std::uint32_t, std::uint32_t>
            instanceIdByObjectId;
        std::uint32_t nextInstanceId = 1u;
        const auto observeInstanceIds =
            [&](const IRenderBackend::WorldSceneFrame& frame) {
                for (const auto& drawClass : frame.drawClasses) {
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
            const auto& partition = partitionIt->second;
            const auto sourceMeshIt = std::find_if(
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
                    "Route 1 terrain mesh storage no longer matches the canonical source.");
            }
            const auto& vertexStorage =
                scene.meshVertexStorage[
                    sourceMeshStorageIndex];
            const std::size_t firstAssembly =
                canonicalTerrainAssemblies.size();

            for (const auto& assembly :
                 partition.assemblies) {
                const SourceBounds transformedBounds =
                    transformSourceBounds(
                        assembly.boundsMinimum,
                        assembly.boundsMaximum,
                        glm::make_mat4(
                            mesh.transform.data()));
                const glm::vec4 transformedPivot =
                    glm::make_mat4(mesh.transform.data()) *
                    glm::vec4(
                        assembly.sourcePivotCm[0],
                        assembly.sourcePivotCm[1],
                        assembly.sourcePivotCm[2],
                        1.0f);
                const bool ramp =
                    assembly.profileRole == "source_ramp";
                const std::string ordinal =
                    std::to_string(
                        assembly.assemblyIndex + 1u);
                canonicalTerrainAssemblies.push_back(
                    CanonicalTerrainAssembly{
                        .stableId =
                            terrainAssemblyStableId(
                                mesh.sourceIndex,
                                assembly.assemblyIndex),
                        .displayName =
                            (ramp
                                 ? "Source Ramp "
                                 : "Ledge / Raised Platform ") +
                            std::to_string(mesh.sourceIndex) +
                            "." + ordinal,
                        .categoryPath =
                            ramp
                            ? "Environment/Terrain/Ramps"
                            : "Environment/Terrain/Ledges and Raised Platforms",
                        .logicalName =
                            terrainAssemblyLogicalName(
                                mesh.sourceIndex),
                        .prefabAssetId =
                            terrainAssemblyPrefabAssetId(
                                mesh.sourceIndex,
                                assembly.assemblyIndex),
                        .profileRole =
                            assembly.profileRole,
                        .sourceMeshIndex =
                            mesh.sourceIndex,
                        .recordIndex =
                            assembly.assemblyIndex,
                        .sourceModelMatrix = mesh.transform,
                        .sourcePivotCm = {
                            transformedPivot.x,
                            transformedPivot.y,
                            transformedPivot.z},
                        .sourceBoundsMinimumCm =
                            transformedBounds.minimum,
                        .sourceBoundsMaximumCm =
                            transformedBounds.maximum,
                        .translationCm = {
                            transformedPivot.x,
                            transformedPivot.y,
                            transformedPivot.z}});
            }

            for (const auto& assembly :
                 partition.assemblies) {
                auto& runtimeAssembly =
                    canonicalTerrainAssemblies[
                        firstAssembly +
                        assembly.assemblyIndex];
                for (const auto& selection :
                     assembly.polygonGroups) {
                    const auto originalGeometry =
                        std::find_if(
                            scene.registry.geometries.begin(),
                            scene.registry.geometries.end(),
                            [&](const auto& candidate) {
                                return candidate.sourceMeshIndex ==
                                        mesh.sourceIndex &&
                                    candidate.sourcePolygonGroupIndex ==
                                        selection.polygonGroupIndex;
                            });
                    if (originalGeometry ==
                        scene.registry.geometries.end()) {
                        return fail(
                            outError,
                            "Route 1 terrain polygon group is missing from the prepared scene.");
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
                            "Route 1 terrain render object is missing.");
                    }
                    canonicalTerrainPolygonStorage.emplace_back();
                    auto& storage =
                        canonicalTerrainPolygonStorage.back();
                    storage.geometryCacheKey =
                        originalGeometry->geometryCacheKey +
                        ":terrain-assembly:" +
                        std::to_string(
                            assembly.assemblyIndex);
                    storage.indices = selection.indices;
                    const auto geometryHandle =
                        shared_world_scene::ensureRigidGeometry(
                            scene.registry,
                            &storage,
                            storage.geometryCacheKey.c_str(),
                            vertexStorage.vertices.data(),
                            vertexStorage.vertices.size(),
                            storage.indices.data(),
                            storage.indices.size(),
                            vertexStorage.sourceVertices.data(),
                            vertexStorage.sourceVertices.size(),
                            originalGeometry
                                ->sourceVertexSemanticMask,
                            mesh.sourceIndex,
                            selection.polygonGroupIndex);
                    const auto objectHandle =
                        shared_world_scene::ensureRenderObject(
                            scene.registry,
                            geometryHandle,
                            originalObject->materialHandle,
                            static_cast<
                                shared_world_scene::PipelineVariant>(
                                originalObject
                                    ->pipelineVariant),
                            originalObject->cookedDrawSlot,
                            originalObject->skinned);
                    objectsByGroup[
                        GroupKey{
                            mesh.sourceIndex,
                            selection.polygonGroupIndex}]
                        .push_back(objectHandle);
                    runtimeAssembly.objectHandles.push_back(
                        objectHandle);
                    instanceIdByObjectId.emplace(
                        objectHandle.id,
                        nextInstanceId++);
                }
            }
        }
        if (canonicalTerrainAssemblies.size() != 23u) {
            return fail(
                outError,
                "Route 1 terrain assembly total changed: expected 23, found " +
                    std::to_string(
                        canonicalTerrainAssemblies.size()) +
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
                    const auto* sourceGeometry = object
                        ? geometry(
                              scene.registry,
                              object->geometryHandle)
                        : nullptr;
                    const GroupKey key{
                        sourceGeometry
                            ? sourceGeometry->sourceMeshIndex
                            : 0u,
                        sourceGeometry
                            ? sourceGeometry
                                  ->sourcePolygonGroupIndex
                            : 0u};
                    const auto split = sourceGeometry
                        ? objectsByGroup.find(key)
                        : objectsByGroup.end();
                    if (split == objectsByGroup.end()) {
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
                        rebuilt.drawClassIndexByObjectId[
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
        for (auto& assembly : canonicalTerrainAssemblies) {
            assembly.translationCm =
                assembly.sourcePivotCm;
            assembly.rotationDegrees = {};
            assembly.scale = {1.0f, 1.0f, 1.0f};
            assembly.suppressed = false;
            assembly.hasOverride = false;
            assembly.reason.clear();
        }
        boardGroundPatch.translationCm =
            boardGroundPatch.sourcePivotCm;
        boardGroundPatch.rotationDegrees = {};
        boardGroundPatch.scale = {1.0f, 1.0f, 1.0f};
        boardGroundPatch.suppressed = true;
        boardGroundPatch.hasOverride = false;
        boardGroundPatch.reason.clear();
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
                "canonical_terrain_assembly") {
                auto target = std::find_if(
                    canonicalTerrainAssemblies.begin(),
                    canonicalTerrainAssemblies.end(),
                    [&](const CanonicalTerrainAssembly& assembly) {
                        return assembly.logicalName ==
                                delta.logicalName &&
                            assembly.recordIndex ==
                                delta.recordIndex;
                    });
                stableId = stableIdForImportedBinding(
                    delta.targetKind,
                    delta.logicalName,
                    delta.recordIndex);
                if (target ==
                    canonicalTerrainAssemblies.end()) {
                    return fail(
                        outError,
                        "Route 1 terrain assembly no longer exists: " +
                            stableId);
                }
                if (!sourceTransformMatches(
                        target->sourcePivotCm,
                        {0.0f, 0.0f, 0.0f},
                        {1.0f, 1.0f, 1.0f},
                        delta)) {
                    return fail(
                        outError,
                        "Route 1 terrain-assembly source pivot changed; refusing to retarget silently: " +
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
            } else if (
                delta.targetKind ==
                "gameplay_board_ground_prototype" &&
                delta.logicalName ==
                    kBoardGroundLogicalName &&
                delta.recordIndex == 0u) {
                stableId =
                    std::string(
                        kBoardGroundPrototypeStableId);
                if (!sourceTransformMatches(
                        boardGroundPatch.sourcePivotCm,
                        {0.0f, 0.0f, 0.0f},
                        {1.0f, 1.0f, 1.0f},
                        delta)) {
                    return fail(
                        outError,
                        "Route 1 board-ground prototype source transform changed; refusing to retarget silently.");
                }
                boardGroundPatch.translationCm =
                    delta.translationCm;
                boardGroundPatch.rotationDegrees =
                    delta.rotationDegrees;
                boardGroundPatch.scale = delta.scale;
                boardGroundPatch.suppressed =
                    delta.suppressed;
                boardGroundPatch.hasOverride = true;
                boardGroundPatch.reason = delta.reason;
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
                        .sourceBoundsMinimumCm = {
                            sourceRecord.sourceBoundsMinimumCm[0] +
                                authored.sourceTranslationCm[0] -
                                sourceRecord.sourceTranslationCm[0],
                            sourceRecord.sourceBoundsMinimumCm[1] +
                                authored.sourceTranslationCm[1] -
                                sourceRecord.sourceTranslationCm[1],
                            sourceRecord.sourceBoundsMinimumCm[2] +
                                authored.sourceTranslationCm[2] -
                                sourceRecord.sourceTranslationCm[2]},
                        .sourceBoundsMaximumCm = {
                            sourceRecord.sourceBoundsMaximumCm[0] +
                                authored.sourceTranslationCm[0] -
                                sourceRecord.sourceTranslationCm[0],
                            sourceRecord.sourceBoundsMaximumCm[1] +
                                authored.sourceTranslationCm[1] -
                                sourceRecord.sourceTranslationCm[1],
                            sourceRecord.sourceBoundsMaximumCm[2] +
                                authored.sourceTranslationCm[2] -
                                sourceRecord.sourceTranslationCm[2]},
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
                authored.prototypeStableId ==
                    kBoardGroundPrototypeStableId ||
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
                    }) ||
                std::any_of(
                    canonicalTerrainAssemblies.begin(),
                    canonicalTerrainAssemblies.end(),
                    [&](const CanonicalTerrainAssembly& assembly) {
                        return assembly.stableId ==
                            authored.prototypeStableId;
                    });
            if (!rigidPrototypeExists) {
                return fail(
                    outError,
                    "Route 1 authored prefab prototype no longer exists: " +
                        authored.prototypeStableId);
            }
        }

        std::set<std::pair<std::int32_t, std::int32_t>> authoredTileCells;
        for (const auto& tile : layout.authoredTerrainTiles) {
            const bool validSurface =
                tile.surface == "light_lawn" ||
                tile.surface == "dark_lawn" ||
                tile.surface == "dirt_path" ||
                tile.surface == "empty";
            const bool validShape =
                tile.shape == "flat" ||
                tile.shape == "ramp_north" ||
                tile.shape == "ramp_east" ||
                tile.shape == "ramp_south" ||
                tile.shape == "ramp_west";
            const bool sourceCellExists = std::any_of(
                sourceTerrainTiles.begin(),
                sourceTerrainTiles.end(),
                [&](const TerrainTileState& candidate) {
                    return candidate.gridX == tile.gridX &&
                        candidate.gridZ == tile.gridZ;
                });
            if (tile.tileSetAssetId != kTerrainTileSetAssetId ||
                tile.stableId != route1TerrainTileStableId(
                    tile.gridX,
                    tile.gridZ) ||
                !validSurface ||
                !validShape ||
                !validTerrainVisualVariant(
                    tile.surface,
                    tile.visualVariant) ||
                (tile.surface == "empty" &&
                 tile.shape != "flat") ||
                tile.elevationLevel < -128 ||
                tile.elevationLevel > 128 ||
                !sourceCellExists ||
                !authoredTileCells.emplace(
                    tile.gridX,
                    tile.gridZ).second) {
                return fail(
                    outError,
                    "Route 1 contains an invalid, duplicate, or out-of-bounds authored terrain tile: " +
                        tile.stableId);
            }
        }

        rebuildTerrainTileStates();
        applyTerrainMask();
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

        const auto placeTerrainAssemblies =
            [&](IRenderBackend::WorldSceneFrame& frame) {
                for (const auto& assembly :
                     canonicalTerrainAssemblies) {
                    const auto group = std::find_if(
                        canonicalMeshGroups.begin(),
                        canonicalMeshGroups.end(),
                        [&](const CanonicalMeshGroup& candidate) {
                            return candidate.sourceMeshIndex ==
                                assembly.sourceMeshIndex;
                        });
                    if (group == canonicalMeshGroups.end()) {
                        continue;
                    }
                    const glm::mat4 extra =
                        glm::make_mat4(
                            sourcePlacementMatrix(
                                assembly.translationCm,
                                assembly.rotationDegrees,
                                assembly.scale)
                                .data()) *
                        glm::translate(
                            glm::mat4(1.0f),
                            -glm::vec3(
                                assembly.sourcePivotCm[0],
                                assembly.sourcePivotCm[1],
                                assembly.sourcePivotCm[2]));
                    const glm::mat4 authored =
                        extra *
                        groupDeltaMatrix(*group) *
                        glm::make_mat4(
                            assembly.sourceModelMatrix.data());
                    for (const auto objectHandle :
                         assembly.objectHandles) {
                        if (objectHandle.id == 0u ||
                            objectHandle.id >
                                frame
                                    .drawClassIndexByObjectId
                                    .size()) {
                            continue;
                        }
                        const std::uint32_t encodedIndex =
                            frame.drawClassIndexByObjectId[
                                objectHandle.id - 1u];
                        if (encodedIndex == 0u ||
                            encodedIndex >
                                frame.drawClasses.size()) {
                            continue;
                        }
                        auto& drawClass =
                            frame.drawClasses[
                                encodedIndex - 1u];
                        if (group->suppressed ||
                            assembly.suppressed) {
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
        placeTerrainAssemblies(scene.frame);
        placeTerrainAssemblies(scene.shadowFrame);

        const auto placeBoardGroundPrototype =
            [&](IRenderBackend::WorldSceneFrame& frame) {
                const auto handle =
                    boardGroundPatch.objectHandle;
                if (handle.id == 0u ||
                    handle.id >
                        frame.drawClassIndexByObjectId.size()) {
                    return;
                }
                const std::uint32_t encodedIndex =
                    frame.drawClassIndexByObjectId[
                        handle.id - 1u];
                if (encodedIndex == 0u ||
                    encodedIndex > frame.drawClasses.size()) {
                    return;
                }
                auto& drawClass =
                    frame.drawClasses[encodedIndex - 1u];
                if (boardGroundPatch.suppressed) {
                    drawClass.instances.clear();
                    return;
                }
                const auto modelMatrix =
                    sourcePlacementMatrix(
                        boardGroundPatch.translationCm,
                        boardGroundPatch.rotationDegrees,
                        boardGroundPatch.scale);
                for (auto& instance :
                     drawClass.instances) {
                    instance.modelMatrix = modelMatrix;
                }
            };
        placeBoardGroundPrototype(scene.frame);

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
                    if (authored.prototypeStableId ==
                        kBoardGroundPrototypeStableId) {
                        append(
                            boardGroundPatch.objectHandle,
                            sourcePlacementMatrix(
                                authored.translationCm,
                                authored.rotationDegrees,
                                authored.scale));
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
                    const auto terrain = std::find_if(
                        canonicalTerrainAssemblies.begin(),
                        canonicalTerrainAssemblies.end(),
                        [&](const CanonicalTerrainAssembly& candidate) {
                            return candidate.stableId ==
                                authored.prototypeStableId;
                        });
                    if (terrain !=
                        canonicalTerrainAssemblies.end()) {
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
                                    terrain->sourcePivotCm[0],
                                    terrain->sourcePivotCm[1],
                                    terrain->sourcePivotCm[2])) *
                            glm::make_mat4(
                                terrain->sourceModelMatrix.data()));
                        for (const auto objectHandle :
                             terrain->objectHandles) {
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
        appendAuthoredTerrainTiles(scene.frame);
        appendAuthoredTerrainTiles(scene.shadowFrame);

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
            canonicalTerrainAssemblies.size() +
            canonicalTreeInstances.size() +
            encounterGrassRecords.size() +
            54u +
            1u +
            layout.authoredPrefabInstances.size());
        for (const auto& group : canonicalMeshGroups) {
            const bool treeSourceGroup =
                lgpe_route1_tree_instances::
                    expectedInstanceCount(
                        group.sourceMeshIndex) > 0u;
            const bool terrainSourceGroup =
                lgpe_route1_terrain_assemblies::
                    expectedAssemblyCount(
                        group.sourceMeshIndex) > 0u;
            if ((treeSourceGroup ||
                 terrainSourceGroup) &&
                !group.hasOverride) {
                continue;
            }
            const SourceBounds currentBounds =
                transformSourceBounds(
                    group.sourceBoundsMinimumCm,
                    group.sourceBoundsMaximumCm,
                    groupDeltaMatrix(group));
            layoutObjects.push_back(
                LayoutObject{
                    .stableId = group.stableId,
                    .displayName =
                        treeSourceGroup
                        ? group.displayName +
                              " (all instances)"
                        : terrainSourceGroup
                        ? group.displayName +
                              " (legacy whole-group override)"
                        : group.displayName,
                    .targetKind =
                        "canonical_mesh_group",
                    .categoryPath =
                        treeSourceGroup
                        ? "Environment/Vegetation/Trees/Legacy Source Group Overrides"
                        : terrainSourceGroup
                        ? "Environment/Terrain/Legacy Source Group Overrides"
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
                    .boundsMinimumCm =
                        currentBounds.minimum,
                    .boundsMaximumCm =
                        currentBounds.maximum,
                    .suppressed =
                        group.suppressed,
                    .hasOverride =
                        group.hasOverride,
                    .reason = group.reason});
        }
        for (const auto& assembly :
             canonicalTerrainAssemblies) {
            const auto group = std::find_if(
                canonicalMeshGroups.begin(),
                canonicalMeshGroups.end(),
                [&](const CanonicalMeshGroup& candidate) {
                    return candidate.sourceMeshIndex ==
                        assembly.sourceMeshIndex;
                });
            if (group == canonicalMeshGroups.end()) {
                return fail(
                    outError,
                    "Route 1 terrain assembly lost its source group while deriving authoring bounds.");
            }
            const glm::mat4 assemblyTransform =
                glm::make_mat4(
                    sourcePlacementMatrix(
                        assembly.translationCm,
                        assembly.rotationDegrees,
                        assembly.scale)
                        .data()) *
                glm::translate(
                    glm::mat4(1.0f),
                    -glm::vec3(
                        assembly.sourcePivotCm[0],
                        assembly.sourcePivotCm[1],
                        assembly.sourcePivotCm[2])) *
                groupDeltaMatrix(*group);
            const SourceBounds currentBounds =
                transformSourceBounds(
                    assembly.sourceBoundsMinimumCm,
                    assembly.sourceBoundsMaximumCm,
                    assemblyTransform);
            layoutObjects.push_back(
                LayoutObject{
                    .stableId = assembly.stableId,
                    .displayName =
                        assembly.displayName,
                    .targetKind =
                        "canonical_terrain_assembly",
                    .categoryPath =
                        assembly.categoryPath,
                    .prefabAssetId =
                        assembly.prefabAssetId,
                    .logicalName =
                        assembly.logicalName,
                    .recordIndex =
                        assembly.recordIndex,
                    .sourceTranslationCm =
                        assembly.sourcePivotCm,
                    .sourceRotationDegrees = {},
                    .sourceScale =
                        {1.0f, 1.0f, 1.0f},
                    .translationCm =
                        assembly.translationCm,
                    .rotationDegrees =
                        assembly.rotationDegrees,
                    .scale = assembly.scale,
                    .boundsMinimumCm =
                        currentBounds.minimum,
                    .boundsMaximumCm =
                        currentBounds.maximum,
                    .suppressed =
                        assembly.suppressed,
                    .hasOverride =
                        assembly.hasOverride,
                    .reason = assembly.reason});
        }
        for (const auto& tree :
             canonicalTreeInstances) {
            const std::string treeLabel =
                "Tree 00" +
                std::to_string(
                    tree.sourceMeshIndex - 9u);
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
                    "Route 1 tree instance lost its source group while deriving authoring bounds.");
            }
            const glm::mat4 treeTransform =
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
                        tree.groupBaselinePivotCm[2])) *
                groupDeltaMatrix(*group);
            const SourceBounds currentBounds =
                transformSourceBounds(
                    tree.sourceBoundsMinimumCm,
                    tree.sourceBoundsMaximumCm,
                    treeTransform);
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
                    .boundsMinimumCm =
                        currentBounds.minimum,
                    .boundsMaximumCm =
                        currentBounds.maximum,
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
            const SourceBounds currentBounds =
                pivotTransformedBounds(
                    record.sourceBoundsMinimumCm,
                    record.sourceBoundsMaximumCm,
                    record.sourceTranslationCm,
                    record.translationCm,
                    record.rotationDegrees,
                    record.scale);
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
                    .boundsMinimumCm =
                        currentBounds.minimum,
                    .boundsMaximumCm =
                        currentBounds.maximum,
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
                const SourceBounds currentBounds =
                    transformSourceBounds(
                        placement.localBoundsMinimumCm,
                        placement.localBoundsMaximumCm,
                        glm::make_mat4(
                            sourcePlacementMatrix(
                                placement.translationCm,
                                placement.rotationDegrees,
                                placement.scale)
                                .data()));
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
                        .boundsMinimumCm =
                            currentBounds.minimum,
                        .boundsMaximumCm =
                            currentBounds.maximum,
                        .suppressed =
                            placement.suppressed,
                        .hasOverride =
                            placement.hasOverride,
                    .reason = placement.reason});
            }
        }
        const std::array<float, 3> boardGroundSourceMinimum{
            boardGroundPatch.sourcePivotCm[0] - 50.0f,
            boardGroundPatch.sourcePivotCm[1],
            boardGroundPatch.sourcePivotCm[2] - 50.0f};
        const std::array<float, 3> boardGroundSourceMaximum{
            boardGroundPatch.sourcePivotCm[0] + 50.0f,
            boardGroundPatch.sourcePivotCm[1] + 1.0f,
            boardGroundPatch.sourcePivotCm[2] + 50.0f};
        const SourceBounds boardGroundBounds =
            pivotTransformedBounds(
                boardGroundSourceMinimum,
                boardGroundSourceMaximum,
                boardGroundPatch.sourcePivotCm,
                boardGroundPatch.translationCm,
                boardGroundPatch.rotationDegrees,
                boardGroundPatch.scale);
        layoutObjects.push_back(
            LayoutObject{
                .stableId =
                    std::string(
                        kBoardGroundPrototypeStableId),
                .displayName =
                    "Autochess Board Ground Patch Prototype",
                .targetKind =
                    "gameplay_board_ground_prototype",
                .categoryPath =
                    "Environment/Terrain/Gameplay Board Tools",
                .prefabAssetId =
                    std::string(
                        kBoardGroundPrefabAssetId),
                .logicalName =
                    std::string(kBoardGroundLogicalName),
                .recordIndex = 0u,
                .sourceTranslationCm =
                    boardGroundPatch.sourcePivotCm,
                .sourceRotationDegrees = {},
                .sourceScale = {1.0f, 1.0f, 1.0f},
                .translationCm =
                    boardGroundPatch.translationCm,
                .rotationDegrees =
                    boardGroundPatch.rotationDegrees,
                .scale = boardGroundPatch.scale,
                .boundsMinimumCm =
                    boardGroundBounds.minimum,
                .boundsMaximumCm =
                    boardGroundBounds.maximum,
                .suppressed =
                    boardGroundPatch.suppressed,
                .hasOverride =
                    boardGroundPatch.hasOverride,
                .reason = boardGroundPatch.reason});
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
            const SourceBounds currentBounds =
                relativePlacementTransformedBounds(
                    prototype->boundsMinimumCm,
                    prototype->boundsMaximumCm,
                    authored.sourceTranslationCm,
                    authored.sourceRotationDegrees,
                    authored.sourceScale,
                    authored.translationCm,
                    authored.rotationDegrees,
                    authored.scale);
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
                    .boundsMinimumCm =
                        currentBounds.minimum,
                    .boundsMaximumCm =
                        currentBounds.maximum,
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

bool RuntimeEnvironment::Impl::initializeTerrainTiles(
    std::string* outError) {
    sourceTerrainGroundMask = nullptr;
    const auto groundMaskTexture = std::find_if(
        source.textures.begin(),
        source.textures.end(),
        [](const auto& texture) {
            return texture.name == "glassmask01_com";
        });
    if (groundMaskTexture != source.textures.end()) {
        const auto groundMaskMip = std::find_if(
            groundMaskTexture->subresources.begin(),
            groundMaskTexture->subresources.end(),
            [](const auto& subresource) {
                return subresource.arrayLevel == 0u &&
                    subresource.mipLevel == 0u &&
                    subresource.depthLevel == 0u;
            });
        if (groundMaskMip != groundMaskTexture->subresources.end() &&
            groundMaskMip->width > 0u &&
            groundMaskMip->height > 0u &&
            groundMaskMip->rgba8.size() ==
                static_cast<std::size_t>(groundMaskMip->width) *
                    static_cast<std::size_t>(groundMaskMip->height) * 4u) {
            sourceTerrainGroundMask = &*groundMaskMip;
        }
    }
    if (!sourceTerrainGroundMask) {
        return fail(
            outError,
            "Route 1 terrain tiles require the decoded glassmask01_com mip-0 lawn/soil selector.");
    }

    const auto sourceMeshStorageIndex =
        [&](std::uint32_t sourceMeshIndex)
            -> std::optional<std::size_t> {
            const auto found = std::find_if(
                source.meshes.begin(),
                source.meshes.end(),
                [&](const auto& mesh) {
                    return mesh.sourceIndex == sourceMeshIndex;
                });
            if (found == source.meshes.end()) {
                return std::nullopt;
            }
            return static_cast<std::size_t>(
                std::distance(source.meshes.begin(), found));
        };
    const auto renderObjectFor =
        [&](std::uint32_t sourceMeshIndex,
            std::uint32_t polygonGroupIndex)
            -> const IRenderBackend::WorldSceneRenderObject* {
            const auto geometryRecord = std::find_if(
                scene.registry.geometries.begin(),
                scene.registry.geometries.end(),
                [&](const auto& candidate) {
                    return candidate.sourceMeshIndex ==
                            sourceMeshIndex &&
                        candidate.sourcePolygonGroupIndex ==
                            polygonGroupIndex;
                });
            if (geometryRecord == scene.registry.geometries.end()) {
                return nullptr;
            }
            const auto object = std::find_if(
                scene.registry.renderObjects.begin(),
                scene.registry.renderObjects.end(),
                [&](const auto& candidate) {
                    return candidate.geometryHandle.id ==
                        geometryRecord->handle.id;
                });
            return object == scene.registry.renderObjects.end()
                ? nullptr
                : &*object;
        };
    const auto geometryFor =
        [&](std::uint32_t sourceMeshIndex,
            std::uint32_t polygonGroupIndex)
            -> const IRenderBackend::WorldSceneGeometry* {
            const auto found = std::find_if(
                scene.registry.geometries.begin(),
                scene.registry.geometries.end(),
                [&](const auto& candidate) {
                    return candidate.sourceMeshIndex ==
                            sourceMeshIndex &&
                        candidate.sourcePolygonGroupIndex ==
                            polygonGroupIndex;
                });
            return found == scene.registry.geometries.end()
                ? nullptr
                : &*found;
        };

    const auto lightStorageIndex = sourceMeshStorageIndex(36u);
    const auto darkStorageIndex = sourceMeshStorageIndex(31u);
    const auto cliffStorageIndex = sourceMeshStorageIndex(32u);
    const auto* lightGeometry = geometryFor(36u, 0u);
    const auto* lightObject = renderObjectFor(36u, 0u);
    const auto* darkGeometry = geometryFor(31u, 0u);
    const auto* darkObject = renderObjectFor(31u, 0u);
    const auto* cliffGeometry = geometryFor(32u, 1u);
    const auto* cliffObject = renderObjectFor(32u, 1u);
    if (!lightStorageIndex || !darkStorageIndex ||
        !cliffStorageIndex ||
        !lightGeometry || !lightObject || !darkGeometry ||
        !darkObject || !cliffGeometry || !cliffObject ||
        *lightStorageIndex >= scene.meshVertexStorage.size() ||
        *darkStorageIndex >= scene.meshVertexStorage.size() ||
        *cliffStorageIndex >= scene.meshVertexStorage.size() ||
        scene.meshVertexStorage[*lightStorageIndex].vertices.empty() ||
        scene.meshVertexStorage[*lightStorageIndex]
            .sourceVertices.empty() ||
        scene.meshVertexStorage[*darkStorageIndex].vertices.empty() ||
        scene.meshVertexStorage[*darkStorageIndex]
            .sourceVertices.empty() ||
        scene.meshVertexStorage[*cliffStorageIndex].vertices.empty() ||
        scene.meshVertexStorage[*cliffStorageIndex]
            .sourceVertices.empty() ||
        source.meshes[*darkStorageIndex].polygonGroups.empty() ||
        source.meshes[*darkStorageIndex]
            .polygonGroups.front().indices.empty() ||
        source.meshes[*cliffStorageIndex].polygonGroups.size() <= 1u ||
        source.meshes[*cliffStorageIndex]
            .polygonGroups[1u].indices.empty()) {
        return fail(
            outError,
            "Route 1 terrain tiles require the exact light-lawn, dark-lawn, and cliff source material records.");
    }

    const auto lightTemplate =
        scene.meshVertexStorage[*lightStorageIndex].vertices.front();
    const auto lightSourceTemplate =
        scene.meshVertexStorage[*lightStorageIndex]
            .sourceVertices.front();
    terrainTilePrototypes.topPrototypes.clear();
    terrainTilePrototypes.cliffPrototypes.clear();
    terrainTilePrototypes.groundVertexTemplate = lightTemplate;
    terrainTilePrototypes.groundSourceVertexTemplate =
        lightSourceTemplate;
    terrainTilePrototypes.groundSourceVertexSemanticMask =
        lightGeometry->sourceVertexSemanticMask;
    terrainTilePrototypes.groundMaterialHandle =
        lightObject->materialHandle;
    terrainTilePrototypes.groundPipelineVariant =
        lightObject->pipelineVariant;
    terrainTilePrototypes.groundCookedDrawSlot =
        lightObject->cookedDrawSlot;
    const auto cliffVertexIndex =
        source.meshes[*cliffStorageIndex]
            .polygonGroups[1u].indices.front();
    const auto cliffTemplate =
        scene.meshVertexStorage[*cliffStorageIndex]
            .vertices[cliffVertexIndex];
    const auto cliffSourceTemplate =
        scene.meshVertexStorage[*cliffStorageIndex]
            .sourceVertices[cliffVertexIndex];
    terrainTilePrototypes.cliffVertexTemplate = cliffTemplate;
    terrainTilePrototypes.cliffSourceVertexTemplate =
        cliffSourceTemplate;
    terrainTilePrototypes.cliffSourceVertexSemanticMask =
        cliffGeometry->sourceVertexSemanticMask;
    terrainTilePrototypes.cliffMaterialHandle =
        cliffObject->materialHandle;
    terrainTilePrototypes.cliffPipelineVariant =
        cliffObject->pipelineVariant;
    terrainTilePrototypes.cliffCookedDrawSlot =
        cliffObject->cookedDrawSlot;
    constexpr std::array<std::array<float, 2>, 4> corners{{
        {-50.0f, -50.0f},
        {50.0f, -50.0f},
        {50.0f, 50.0f},
        {-50.0f, 50.0f},
    }};
    // glassmask01_com is the source-authored lawn/soil selector used by
    // FieldGroundShader01. These points sit inside verified flat-white mask
    // regions: alpha 1 selects lawn and alpha 0 selects soil. A generated
    // tile must not stretch the entire route paint atlas over one metre.
    constexpr std::array<float, 2> cleanLawnUv2{0.5f, 0.121f};
    constexpr std::array<float, 2> cleanDirtUv2{0.5f, 0.75f};
    // Recovered horizontal grass01_com_001 floor carriers resolve their
    // branch-coded Color0 values to this continuous raised-lawn tint. The
    // carrier's decoration cutout is a separate vegetation layer, so tile
    // tops use the solid FieldGround surface plus the recovered tint.
    constexpr std::array<float, 3> raisedLawnTint{
        0.180392161f, 0.482352942f, 0.431372553f};
    for (std::size_t index = 0u; index < corners.size(); ++index) {
        auto light = lightTemplate;
        light.x = corners[index][0];
        light.y = 0.0f;
        light.z = corners[index][1];
        light.nx = 0.0f;
        light.ny = 1.0f;
        light.nz = 0.0f;
        light.u = corners[index][0] / 100.0f + 0.5f;
        light.v = corners[index][1] / 100.0f + 0.5f;
        light.sourceUv1U = light.u;
        light.sourceUv1V = light.v;
        light.sourceUv2U = cleanLawnUv2[0];
        light.sourceUv2V = cleanLawnUv2[1];
        terrainTilePrototypes.lightVertices[index] = light;
        terrainTilePrototypes.lightSourceVertices[index] =
            lightSourceTemplate;
        terrainTilePrototypes.lightSourceVertices[index]
            .texcoords[0] = {light.u, light.v};
        terrainTilePrototypes.lightSourceVertices[index]
            .texcoords[1] = {light.u, light.v};
        terrainTilePrototypes.lightSourceVertices[index]
            .texcoords[2] = cleanLawnUv2;

        auto dirt = light;
        dirt.sourceUv2U = cleanDirtUv2[0];
        dirt.sourceUv2V = cleanDirtUv2[1];
        terrainTilePrototypes.dirtVertices[index] = dirt;
        terrainTilePrototypes.dirtSourceVertices[index] =
            lightSourceTemplate;
        terrainTilePrototypes.dirtSourceVertices[index]
            .texcoords[0] = {dirt.u, dirt.v};
        terrainTilePrototypes.dirtSourceVertices[index]
            .texcoords[1] = {dirt.u, dirt.v};
        terrainTilePrototypes.dirtSourceVertices[index]
            .texcoords[2] = cleanDirtUv2;

        auto dark = light;
        dark.x = corners[index][0];
        dark.y = 0.0f;
        dark.z = corners[index][1];
        dark.nx = 0.0f;
        dark.ny = 1.0f;
        dark.nz = 0.0f;
        dark.u = light.u;
        dark.v = light.v;
        dark.sourceUv1U = dark.u;
        dark.sourceUv1V = dark.v;
        dark.sourceUv2U = cleanLawnUv2[0];
        dark.sourceUv2V = cleanLawnUv2[1];
        dark.r = raisedLawnTint[0];
        dark.g = raisedLawnTint[1];
        dark.b = raisedLawnTint[2];
        dark.a = 1.0f;
        terrainTilePrototypes.darkVertices[index] = dark;
        terrainTilePrototypes.darkSourceVertices[index] =
            lightSourceTemplate;
        terrainTilePrototypes.darkSourceVertices[index]
            .texcoords[0] = {dark.u, dark.v};
        terrainTilePrototypes.darkSourceVertices[index]
            .texcoords[1] = {dark.u, dark.v};
        terrainTilePrototypes.darkSourceVertices[index]
            .texcoords[2] = cleanLawnUv2;
        terrainTilePrototypes.darkSourceVertices[index]
            .colors[0] = {
                raisedLawnTint[0],
                raisedLawnTint[1],
                raisedLawnTint[2],
                1.0f};

        terrainTilePrototypes.lightRampVertices[index] = light;
        terrainTilePrototypes.dirtRampVertices[index] = dirt;
        terrainTilePrototypes.darkRampVertices[index] =
            terrainTilePrototypes.darkVertices[index];
        if (index >= 2u) {
            terrainTilePrototypes.lightRampVertices[index].y =
                kTerrainElevationStepCm;
            terrainTilePrototypes.dirtRampVertices[index].y =
                kTerrainElevationStepCm;
            terrainTilePrototypes.darkRampVertices[index].y =
                kTerrainElevationStepCm;
        }
        constexpr float rampNormalY = 0.89442719f;
        constexpr float rampNormalZ = -0.44721360f;
        terrainTilePrototypes.lightRampVertices[index].nx = 0.0f;
        terrainTilePrototypes.lightRampVertices[index].ny = rampNormalY;
        terrainTilePrototypes.lightRampVertices[index].nz = rampNormalZ;
        terrainTilePrototypes.dirtRampVertices[index].nx = 0.0f;
        terrainTilePrototypes.dirtRampVertices[index].ny = rampNormalY;
        terrainTilePrototypes.dirtRampVertices[index].nz = rampNormalZ;
        terrainTilePrototypes.darkRampVertices[index].nx = 0.0f;
        terrainTilePrototypes.darkRampVertices[index].ny = rampNormalY;
        terrainTilePrototypes.darkRampVertices[index].nz = rampNormalZ;
    }
    const auto makeObject =
        [&](const void* identity,
            const char* key,
            const auto& vertices,
            const auto& sourceVertices,
            const IRenderBackend::WorldSceneGeometry& sourceGeometry,
            const IRenderBackend::WorldSceneRenderObject& sourceObject) {
            const auto geometryHandle =
                shared_world_scene::ensureRigidGeometry(
                    scene.registry,
                    identity,
                    key,
                    vertices.data(),
                    vertices.size(),
                    terrainTilePrototypes.indices.data(),
                    terrainTilePrototypes.indices.size(),
                    sourceVertices.data(),
                    sourceVertices.size(),
                    sourceGeometry.sourceVertexSemanticMask,
                    std::numeric_limits<std::uint32_t>::max(),
                    0u);
            return shared_world_scene::ensureRenderObject(
                scene.registry,
                geometryHandle,
                sourceObject.materialHandle,
                static_cast<shared_world_scene::PipelineVariant>(
                    sourceObject.pipelineVariant),
                sourceObject.cookedDrawSlot,
                false);
        };
    terrainTilePrototypes.lightTopObject = makeObject(
        terrainTilePrototypes.lightVertices.data(),
        "route1:terrain-tile:light-top",
        terrainTilePrototypes.lightVertices,
        terrainTilePrototypes.lightSourceVertices,
        *lightGeometry,
        *lightObject);
    terrainTilePrototypes.dirtTopObject = makeObject(
        terrainTilePrototypes.dirtVertices.data(),
        "route1:terrain-tile:dirt-top",
        terrainTilePrototypes.dirtVertices,
        terrainTilePrototypes.dirtSourceVertices,
        *lightGeometry,
        *lightObject);
    terrainTilePrototypes.darkTopObject = makeObject(
        terrainTilePrototypes.darkVertices.data(),
        "route1:terrain-tile:dark-top",
        terrainTilePrototypes.darkVertices,
        terrainTilePrototypes.darkSourceVertices,
        *lightGeometry,
        *lightObject);
    terrainTilePrototypes.lightRampObject = makeObject(
        terrainTilePrototypes.lightRampVertices.data(),
        "route1:terrain-tile:light-ramp",
        terrainTilePrototypes.lightRampVertices,
        terrainTilePrototypes.lightSourceVertices,
        *lightGeometry,
        *lightObject);
    terrainTilePrototypes.dirtRampObject = makeObject(
        terrainTilePrototypes.dirtRampVertices.data(),
        "route1:terrain-tile:dirt-ramp",
        terrainTilePrototypes.dirtRampVertices,
        terrainTilePrototypes.dirtSourceVertices,
        *lightGeometry,
        *lightObject);
    terrainTilePrototypes.darkRampObject = makeObject(
        terrainTilePrototypes.darkRampVertices.data(),
        "route1:terrain-tile:dark-ramp",
        terrainTilePrototypes.darkRampVertices,
        terrainTilePrototypes.darkSourceVertices,
        *lightGeometry,
        *lightObject);
    float minimumX = std::numeric_limits<float>::max();
    float maximumX = std::numeric_limits<float>::lowest();
    float minimumZ = std::numeric_limits<float>::max();
    float maximumZ = std::numeric_limits<float>::lowest();
    for (const auto& mesh : source.meshes) {
        if (mesh.sourceIndex < 29u || mesh.sourceIndex > 36u) {
            continue;
        }
        minimumX = std::min(minimumX, mesh.boundsMinimum[0]);
        maximumX = std::max(maximumX, mesh.boundsMaximum[0]);
        minimumZ = std::min(minimumZ, mesh.boundsMinimum[2]);
        maximumZ = std::max(maximumZ, mesh.boundsMaximum[2]);
    }
    const std::int32_t minimumGridX = static_cast<std::int32_t>(
        std::floor(minimumX / kTerrainTileSizeCm));
    const std::int32_t maximumGridX = static_cast<std::int32_t>(
        std::ceil(maximumX / kTerrainTileSizeCm)) - 1;
    const std::int32_t minimumGridZ = static_cast<std::int32_t>(
        std::floor(minimumZ / kTerrainTileSizeCm));
    const std::int32_t maximumGridZ = static_cast<std::int32_t>(
        std::ceil(maximumZ / kTerrainTileSizeCm)) - 1;
    const auto sampleTriangle =
        [](float x,
           float z,
           const glm::vec3& first,
           const glm::vec3& second,
           const glm::vec3& third,
           float& outY,
           glm::vec2& outGradient) {
            const float denominator =
                (second.z - third.z) *
                    (first.x - third.x) +
                (third.x - second.x) *
                    (first.z - third.z);
            if (std::abs(denominator) < 1.0e-5f) {
                return false;
            }
            const float firstWeight =
                ((second.z - third.z) * (x - third.x) +
                 (third.x - second.x) * (z - third.z)) /
                denominator;
            const float secondWeight =
                ((third.z - first.z) * (x - third.x) +
                 (first.x - third.x) * (z - third.z)) /
                denominator;
            const float thirdWeight =
                1.0f - firstWeight - secondWeight;
            constexpr float tolerance = -0.0005f;
            if (firstWeight < tolerance ||
                secondWeight < tolerance ||
                thirdWeight < tolerance) {
                return false;
            }
            outY = firstWeight * first.y +
                secondWeight * second.y +
                thirdWeight * third.y;
            const glm::vec3 normal = glm::cross(
                second - first,
                third - first);
            if (std::abs(normal.y) > 1.0e-5f) {
                outGradient = {
                    -normal.x / normal.y,
                    -normal.z / normal.y};
            } else {
                outGradient = {};
            }
            return true;
        };
    struct TerrainSample {
        float highestY = std::numeric_limits<float>::lowest();
        std::uint32_t material = 19u;
        glm::vec2 gradient{};
    };
    std::map<std::pair<std::int32_t, std::int32_t>, TerrainSample>
        samples;
    sourceTerrainTriangles.clear();
    sourceTerrainTrianglesByCell.clear();
    for (const auto& mesh : source.meshes) {
        if (mesh.sourceIndex < 29u || mesh.sourceIndex > 36u) {
            continue;
        }
        const glm::mat4 model = glm::make_mat4(mesh.transform.data());
        const auto position =
            [&](std::uint32_t vertexIndex) {
                const auto& value = mesh.vertices[vertexIndex].position;
                return glm::vec3(
                    model * glm::vec4(
                        value[0], value[1], value[2], 1.0f));
            };
        for (const auto& group : mesh.polygonGroups) {
            if (group.materialIndex != 12u &&
                group.materialIndex != 13u &&
                group.materialIndex != 19u) {
                continue;
            }
            for (std::size_t index = 0u;
                 index + 2u < group.indices.size();
                 index += 3u) {
                const auto firstIndex = group.indices[index];
                const auto secondIndex = group.indices[index + 1u];
                const auto thirdIndex = group.indices[index + 2u];
                if (firstIndex >= mesh.vertices.size() ||
                    secondIndex >= mesh.vertices.size() ||
                    thirdIndex >= mesh.vertices.size()) {
                    continue;
                }
                const glm::vec3 first = position(firstIndex);
                const glm::vec3 second = position(secondIndex);
                const glm::vec3 third = position(thirdIndex);
                const float triangleMinimumX = std::min({
                    first.x, second.x, third.x});
                const float triangleMaximumX = std::max({
                    first.x, second.x, third.x});
                const float triangleMinimumZ = std::min({
                    first.z, second.z, third.z});
                const float triangleMaximumZ = std::max({
                    first.z, second.z, third.z});
                const auto terrainAttribute =
                    [&](std::uint32_t vertexIndex,
                        std::size_t texcoordIndex) {
                        const auto& value =
                            mesh.vertices[vertexIndex]
                                .texcoords[texcoordIndex];
                        return glm::vec2(value[0], value[1]);
                    };
                const auto terrainColor =
                    [&](std::uint32_t vertexIndex) {
                        const auto& value =
                            mesh.vertices[vertexIndex].colors[0];
                        return glm::vec4(
                            value[0], value[1], value[2], value[3]);
                    };
                const std::size_t sourceTriangleIndex =
                    sourceTerrainTriangles.size();
                sourceTerrainTriangles.push_back(
                    SourceTerrainTriangle{
                        .positions = {first, second, third},
                        .uv0 = {
                            terrainAttribute(firstIndex, 0u),
                            terrainAttribute(secondIndex, 0u),
                            terrainAttribute(thirdIndex, 0u)},
                        .uv1 = {
                            terrainAttribute(firstIndex, 1u),
                            terrainAttribute(secondIndex, 1u),
                            terrainAttribute(thirdIndex, 1u)},
                        .uv2 = {
                            terrainAttribute(firstIndex, 2u),
                            terrainAttribute(secondIndex, 2u),
                            terrainAttribute(thirdIndex, 2u)},
                        .color0 = {
                            terrainColor(firstIndex),
                            terrainColor(secondIndex),
                            terrainColor(thirdIndex)}});
                const std::int32_t triangleFirstCellX = std::max(
                    minimumGridX,
                    static_cast<std::int32_t>(std::floor(
                        triangleMinimumX / kTerrainTileSizeCm)) - 1);
                const std::int32_t triangleLastCellX = std::min(
                    maximumGridX,
                    static_cast<std::int32_t>(std::floor(
                        triangleMaximumX / kTerrainTileSizeCm)) + 1);
                const std::int32_t triangleFirstCellZ = std::max(
                    minimumGridZ,
                    static_cast<std::int32_t>(std::floor(
                        triangleMinimumZ / kTerrainTileSizeCm)) - 1);
                const std::int32_t triangleLastCellZ = std::min(
                    maximumGridZ,
                    static_cast<std::int32_t>(std::floor(
                        triangleMaximumZ / kTerrainTileSizeCm)) + 1);
                for (std::int32_t cellZ = triangleFirstCellZ;
                     cellZ <= triangleLastCellZ;
                     ++cellZ) {
                    for (std::int32_t cellX = triangleFirstCellX;
                         cellX <= triangleLastCellX;
                         ++cellX) {
                        sourceTerrainTrianglesByCell[{cellX, cellZ}]
                            .push_back(sourceTriangleIndex);
                    }
                }
                const std::int32_t firstGridX = std::max(
                    minimumGridX,
                    static_cast<std::int32_t>(std::ceil(
                        triangleMinimumX / kTerrainTileSizeCm - 0.5f)));
                const std::int32_t lastGridX = std::min(
                    maximumGridX,
                    static_cast<std::int32_t>(std::floor(
                        triangleMaximumX / kTerrainTileSizeCm - 0.5f)));
                const std::int32_t firstGridZ = std::max(
                    minimumGridZ,
                    static_cast<std::int32_t>(std::ceil(
                        triangleMinimumZ / kTerrainTileSizeCm - 0.5f)));
                const std::int32_t lastGridZ = std::min(
                    maximumGridZ,
                    static_cast<std::int32_t>(std::floor(
                        triangleMaximumZ / kTerrainTileSizeCm - 0.5f)));
                for (std::int32_t gridZ = firstGridZ;
                     gridZ <= lastGridZ;
                     ++gridZ) {
                    for (std::int32_t gridX = firstGridX;
                         gridX <= lastGridX;
                         ++gridX) {
                        float y = 0.0f;
                        glm::vec2 gradient{};
                        if (!sampleTriangle(
                                (static_cast<float>(gridX) + 0.5f) *
                                    kTerrainTileSizeCm,
                                (static_cast<float>(gridZ) + 0.5f) *
                                    kTerrainTileSizeCm,
                                first,
                                second,
                                third,
                                y,
                                gradient)) {
                            continue;
                        }
                        auto& sample = samples[{gridX, gridZ}];
                        if (y > sample.highestY) {
                            sample.highestY = y;
                            sample.material = group.materialIndex;
                            sample.gradient = gradient;
                        }
                    }
                }
            }
        }
    }

    sourceTerrainTiles.clear();
    sourceTerrainTiles.reserve(
        static_cast<std::size_t>(
            maximumGridX - minimumGridX + 1) *
        static_cast<std::size_t>(
            maximumGridZ - minimumGridZ + 1));
    for (std::int32_t gridZ = minimumGridZ;
         gridZ <= maximumGridZ;
         ++gridZ) {
        for (std::int32_t gridX = minimumGridX;
             gridX <= maximumGridX;
             ++gridX) {
            const auto sample = samples.find({gridX, gridZ});
            const bool occupied = sample != samples.end();
            const float highestY = occupied
                ? sample->second.highestY
                : 0.0f;
            const std::uint32_t highestMaterial = occupied
                ? sample->second.material
                : 19u;
            const glm::vec2 highestGradient = occupied
                ? sample->second.gradient
                : glm::vec2{};
            const std::int32_t sourceElevationLevel = occupied
                ? static_cast<std::int32_t>(
                      std::floor(
                          (highestY + 0.01f) /
                          kTerrainElevationStepCm))
                : 0;
            std::string sourceSurface = highestMaterial == 19u
                ? "light_lawn"
                : "dark_lawn";
            if (occupied && highestMaterial == 19u) {
                const TerrainTileState sourceSampleTile{
                    .gridX = gridX,
                    .gridZ = gridZ,
                    .sourceElevationLevel = sourceElevationLevel};
                SourceTerrainSurfaceSample sourceSample;
                float groundMaskAlpha = 1.0f;
                if (sampleSourceTerrainSurface(
                        sourceSampleTile,
                        0.5f,
                        0.5f,
                        sourceSample) &&
                    sampleSourceTerrainGroundMaskAlpha(
                        sourceSample.uv2,
                        groundMaskAlpha) &&
                    groundMaskAlpha < 0.5f) {
                    sourceSurface = "dirt_path";
                }
            }
            TerrainTileState tile{
                .gridX = gridX,
                .gridZ = gridZ,
                .sourceElevationLevel = sourceElevationLevel,
                .elevationLevel = 0,
                .sourceSurface = std::move(sourceSurface),
                .surface = "light_lawn",
                .shape = "flat",
                .sourceOccupied = occupied,
                .authored = false};
            tile.elevationLevel = tile.sourceElevationLevel;
            tile.surface = tile.sourceSurface;
            if (occupied && glm::length(highestGradient) > 0.12f) {
                if (std::abs(highestGradient.x) >
                    std::abs(highestGradient.y)) {
                    tile.shape = highestGradient.x > 0.0f
                        ? "ramp_east"
                        : "ramp_west";
                } else {
                    tile.shape = highestGradient.y > 0.0f
                        ? "ramp_north"
                        : "ramp_south";
                }
            }
            sourceTerrainTiles.push_back(std::move(tile));
        }
    }
    rebuildTerrainTileStates();
    if (outError) {
        outError->clear();
    }
    return true;
}

bool RuntimeEnvironment::Impl::sampleSourceTerrainSurface(
    const TerrainTileState& tile,
    float localX,
    float localZ,
    SourceTerrainSurfaceSample& out) const {
    const auto candidates = sourceTerrainTrianglesByCell.find(
        {tile.gridX, tile.gridZ});
    if (candidates == sourceTerrainTrianglesByCell.end()) {
        return false;
    }
    const float sourceX =
        (static_cast<float>(tile.gridX) + localX) *
        kTerrainTileSizeCm;
    const float sourceZ =
        (static_cast<float>(tile.gridZ) + localZ) *
        kTerrainTileSizeCm;
    const float expectedY =
        static_cast<float>(tile.sourceElevationLevel) *
        kTerrainElevationStepCm;
    float bestDistance = std::numeric_limits<float>::max();
    float bestY = std::numeric_limits<float>::lowest();
    bool sampled = false;
    for (const std::size_t triangleIndex : candidates->second) {
        if (triangleIndex >= sourceTerrainTriangles.size()) {
            continue;
        }
        const auto& triangle =
            sourceTerrainTriangles[triangleIndex];
        const auto& first = triangle.positions[0];
        const auto& second = triangle.positions[1];
        const auto& third = triangle.positions[2];
        const float denominator =
            (second.z - third.z) * (first.x - third.x) +
            (third.x - second.x) * (first.z - third.z);
        if (std::abs(denominator) < 1.0e-5f) {
            continue;
        }
        const float firstWeight =
            ((second.z - third.z) * (sourceX - third.x) +
             (third.x - second.x) * (sourceZ - third.z)) /
            denominator;
        const float secondWeight =
            ((third.z - first.z) * (sourceX - third.x) +
             (first.x - third.x) * (sourceZ - third.z)) /
            denominator;
        const float thirdWeight =
            1.0f - firstWeight - secondWeight;
        constexpr float tolerance = -0.0015f;
        if (firstWeight < tolerance ||
            secondWeight < tolerance ||
            thirdWeight < tolerance) {
            continue;
        }
        const float y = firstWeight * first.y +
            secondWeight * second.y + thirdWeight * third.y;
        const float distance = std::abs(y - expectedY);
        if (sampled &&
            (distance > bestDistance + 0.001f ||
             (std::abs(distance - bestDistance) <= 0.001f &&
              y <= bestY))) {
            continue;
        }
        const auto interpolateVec2 =
            [&](const std::array<glm::vec2, 3>& values) {
                return values[0] * firstWeight +
                    values[1] * secondWeight +
                    values[2] * thirdWeight;
            };
        out = SourceTerrainSurfaceSample{
            .y = y,
            .uv0 = interpolateVec2(triangle.uv0),
            .uv1 = interpolateVec2(triangle.uv1),
            .uv2 = interpolateVec2(triangle.uv2),
            .color0 =
                triangle.color0[0] * firstWeight +
                triangle.color0[1] * secondWeight +
                triangle.color0[2] * thirdWeight};
        bestDistance = distance;
        bestY = y;
        sampled = true;
    }
    return sampled;
}

bool RuntimeEnvironment::Impl::sampleSourceTerrainGroundMaskAlpha(
    const glm::vec2& sourceUv2,
    float& outAlpha) const {
    if (!sourceTerrainGroundMask ||
        sourceTerrainGroundMask->width == 0u ||
        sourceTerrainGroundMask->height == 0u) {
        return false;
    }

    const auto repeat = [](float value) {
        return value - std::floor(value);
    };
    const float u = repeat(sourceUv2.x);
    // FieldGroundShader01 performs this V inversion before sampling the
    // repeat/linear source texture on every runtime backend.
    const float v = repeat(1.0f - sourceUv2.y);
    const float texelX =
        u * static_cast<float>(sourceTerrainGroundMask->width) - 0.5f;
    const float texelY =
        v * static_cast<float>(sourceTerrainGroundMask->height) - 0.5f;
    const std::int32_t firstX =
        static_cast<std::int32_t>(std::floor(texelX));
    const std::int32_t firstY =
        static_cast<std::int32_t>(std::floor(texelY));
    const float blendX = texelX - static_cast<float>(firstX);
    const float blendY = texelY - static_cast<float>(firstY);
    const auto wrap = [](std::int32_t value, std::uint32_t size) {
        const auto signedSize = static_cast<std::int32_t>(size);
        const std::int32_t remainder = value % signedSize;
        return static_cast<std::uint32_t>(
            remainder < 0 ? remainder + signedSize : remainder);
    };
    const auto alphaAt = [&](std::int32_t x, std::int32_t y) {
        const std::size_t pixel =
            static_cast<std::size_t>(
                wrap(y, sourceTerrainGroundMask->height)) *
                static_cast<std::size_t>(sourceTerrainGroundMask->width) +
            static_cast<std::size_t>(
                wrap(x, sourceTerrainGroundMask->width));
        return static_cast<float>(
                   sourceTerrainGroundMask->rgba8[pixel * 4u + 3u]) /
            255.0f;
    };
    const float top =
        alphaAt(firstX, firstY) * (1.0f - blendX) +
        alphaAt(firstX + 1, firstY) * blendX;
    const float bottom =
        alphaAt(firstX, firstY + 1) * (1.0f - blendX) +
        alphaAt(firstX + 1, firstY + 1) * blendX;
    outAlpha = top * (1.0f - blendY) + bottom * blendY;
    return true;
}

IRenderBackend::WorldSceneRenderObjectHandle
RuntimeEnvironment::Impl::ensureTerrainTopObject(
    const TerrainTileState& tile,
    std::uint32_t dirtConnectionMask) {
    constexpr std::uint32_t kGridResolution = 16u;
    constexpr std::array<float, 2> kCleanLawnUv2{
        0.5f, 0.121f};
    constexpr std::array<float, 3> kRaisedLawnTint{
        0.180392161f, 0.482352942f, 0.431372553f};

    dirtConnectionMask &= 0x0fu;

    const std::string key =
        "route1:terrain-tile:" + tile.shape + ":" +
        tile.surface + ":cell-" +
        std::to_string(tile.gridX) + "-" +
        std::to_string(tile.gridZ) +
        ":connections-" + std::to_string(dirtConnectionMask);
    auto [found, inserted] =
        terrainTilePrototypes.topPrototypes.try_emplace(key);
    auto& prototype = found->second;
    if (!inserted) {
        return prototype.object;
    }

    const std::uint32_t rowWidth = kGridResolution + 1u;
    prototype.vertices.reserve(rowWidth * rowWidth);
    prototype.sourceVertices.reserve(rowWidth * rowWidth);
    prototype.indices.reserve(
        kGridResolution * kGridResolution * 6u);
    const auto smoothStep =
        [](float begin, float end, float value) {
            const float t = std::clamp(
                (value - begin) / (end - begin),
                0.0f,
                1.0f);
            return t * t * (3.0f - 2.0f * t);
        };
    const bool dark = tile.surface == "dark_lawn";
    const bool dirt = tile.surface == "dirt_path";
    for (std::uint32_t zIndex = 0u;
         zIndex <= kGridResolution;
         ++zIndex) {
        const float localZ = static_cast<float>(zIndex) /
            static_cast<float>(kGridResolution);
        for (std::uint32_t xIndex = 0u;
             xIndex <= kGridResolution;
             ++xIndex) {
            const float localX = static_cast<float>(xIndex) /
                static_cast<float>(kGridResolution);
            auto vertex =
                terrainTilePrototypes.groundVertexTemplate;
            auto sourceVertex =
                terrainTilePrototypes.groundSourceVertexTemplate;
            SourceTerrainSurfaceSample sourceSample;
            const bool sourceSampled = sampleSourceTerrainSurface(
                tile,
                localX,
                localZ,
                sourceSample);
            const bool preserveSourceField =
                sourceSampled && !tile.authored;
            vertex.x = (localX - 0.5f) * kTerrainTileSizeCm;
            vertex.y = preserveSourceField
                ? sourceSample.y -
                    static_cast<float>(tile.sourceElevationLevel) *
                        kTerrainElevationStepCm
                : 0.0f;
            vertex.z = (localZ - 0.5f) * kTerrainTileSizeCm;
            vertex.nx = 0.0f;
            vertex.ny = 1.0f;
            vertex.nz = 0.0f;
            // Authored ramps keep the same continuous world-space texture
            // field as adjacent flat cells. Their slope is generated in the
            // actual cardinal direction rather than rotating a tile-local
            // texture island, so neither the lawn nor path restarts at the
            // ramp seam.
            constexpr float rampNormalY = 0.894427191f;
            constexpr float rampNormalSide = 0.447213596f;
            if (!preserveSourceField && tile.shape == "ramp_north") {
                vertex.y = localZ * kTerrainElevationStepCm;
                vertex.ny = rampNormalY;
                vertex.nz = -rampNormalSide;
            } else if (!preserveSourceField &&
                       tile.shape == "ramp_east") {
                vertex.y = localX * kTerrainElevationStepCm;
                vertex.nx = -rampNormalSide;
                vertex.ny = rampNormalY;
            } else if (!preserveSourceField &&
                       tile.shape == "ramp_south") {
                vertex.y = (1.0f - localZ) *
                    kTerrainElevationStepCm;
                vertex.ny = rampNormalY;
                vertex.nz = rampNormalSide;
            } else if (!preserveSourceField &&
                       tile.shape == "ramp_west") {
                vertex.y = (1.0f - localX) *
                    kTerrainElevationStepCm;
                vertex.nx = rampNormalSide;
                vertex.ny = rampNormalY;
            }

            // The canonical Route 1 mesh, not a guessed metre grid, owns the
            // UV/color field. Source-backed cells retain their exact authored
            // samples; new cells use one continuous source-world fallback
            // field instead of independent per-tile variants.
            const glm::vec2 baseUv0 = preserveSourceField
                ? sourceSample.uv0
                : glm::vec2(
                      (static_cast<float>(tile.gridX) + localX) /
                          3.0f,
                      (static_cast<float>(tile.gridZ) + localZ) /
                          3.0f);
            vertex.u = baseUv0.x;
            vertex.v = baseUv0.y;
            const glm::vec2 baseUv1 = preserveSourceField
                ? sourceSample.uv1
                : baseUv0;
            vertex.sourceUv1U = baseUv1.x;
            vertex.sourceUv1V = baseUv1.y;
            sourceVertex.texcoords[0] = {vertex.u, vertex.v};
            sourceVertex.texcoords[1] = {
                vertex.sourceUv1U,
                vertex.sourceUv1V};

            const bool preserveSourceSurface =
                preserveSourceField &&
                tile.surface == tile.sourceSurface;
            if (preserveSourceSurface) {
                vertex.sourceUv2U = sourceSample.uv2.x;
                vertex.sourceUv2V = sourceSample.uv2.y;
                sourceVertex.texcoords[2] = {
                    vertex.sourceUv2U,
                    vertex.sourceUv2V};
            } else if (dirt) {
                constexpr float edgeWidth = 0.20f;
                float lawnBlend = 0.0f;
                if ((dirtConnectionMask & 0x01u) == 0u) {
                    lawnBlend = std::max(
                        lawnBlend,
                        smoothStep(
                            1.0f - edgeWidth,
                            1.0f,
                            localZ));
                }
                if ((dirtConnectionMask & 0x02u) == 0u) {
                    lawnBlend = std::max(
                        lawnBlend,
                        smoothStep(
                            1.0f - edgeWidth,
                            1.0f,
                            localX));
                }
                if ((dirtConnectionMask & 0x04u) == 0u) {
                    lawnBlend = std::max(
                        lawnBlend,
                        1.0f - smoothStep(
                            0.0f,
                            edgeWidth,
                            localZ));
                }
                if ((dirtConnectionMask & 0x08u) == 0u) {
                    lawnBlend = std::max(
                        lawnBlend,
                        1.0f - smoothStep(
                            0.0f,
                            edgeWidth,
                            localX));
                }

                // Pixel evidence from glassmask01_com identifies the clean
                // soil side at atlas y=260 and the source's smooth lawn side
                // near y=336. V is inverted by FieldGroundShader01. Moving
                // U across the same recovered band retains its organic,
                // non-straight boundary instead of inventing a linear fade.
                // The recovered source cliff/border carriers advance the
                // mask by about 0.51 U per source metre. A continuous linear
                // world phase retains the decoded mask's real 26-pixel
                // organic contour instead of warping it through a sine.
                constexpr float sourceMaskUPerMetre = 0.51063830f;
                const float sourceU =
                    ((static_cast<float>(tile.gridX) + localX) +
                     (static_cast<float>(tile.gridZ) + localZ)) *
                    sourceMaskUPerMetre;
                const float sourcePixelY =
                    260.5f + lawnBlend * 75.0f;
                vertex.sourceUv2U = sourceU;
                vertex.sourceUv2V =
                    1.0f - sourcePixelY / 1024.0f;
                sourceVertex.texcoords[2] = {
                    vertex.sourceUv2U,
                    vertex.sourceUv2V};
            } else {
                vertex.sourceUv2U = kCleanLawnUv2[0];
                vertex.sourceUv2V = kCleanLawnUv2[1];
                sourceVertex.texcoords[2] = kCleanLawnUv2;
            }
            if (preserveSourceField &&
                tile.surface == tile.sourceSurface) {
                vertex.r = sourceSample.color0.r;
                vertex.g = sourceSample.color0.g;
                vertex.b = sourceSample.color0.b;
                vertex.a = sourceSample.color0.a;
                sourceVertex.colors[0] = {
                    sourceSample.color0.r,
                    sourceSample.color0.g,
                    sourceSample.color0.b,
                    sourceSample.color0.a};
            } else if (dark) {
                vertex.r = kRaisedLawnTint[0];
                vertex.g = kRaisedLawnTint[1];
                vertex.b = kRaisedLawnTint[2];
                vertex.a = 1.0f;
                sourceVertex.colors[0] = {
                    kRaisedLawnTint[0],
                    kRaisedLawnTint[1],
                    kRaisedLawnTint[2],
                    1.0f};
            }
            prototype.vertices.push_back(vertex);
            prototype.sourceVertices.push_back(sourceVertex);
        }
    }
    for (std::uint32_t zIndex = 0u;
         zIndex < kGridResolution;
         ++zIndex) {
        for (std::uint32_t xIndex = 0u;
             xIndex < kGridResolution;
             ++xIndex) {
            const std::uint32_t lowerLeft =
                zIndex * rowWidth + xIndex;
            const std::uint32_t lowerRight = lowerLeft + 1u;
            const std::uint32_t upperLeft = lowerLeft + rowWidth;
            const std::uint32_t upperRight = upperLeft + 1u;
            prototype.indices.insert(
                prototype.indices.end(),
                {lowerLeft, lowerRight, upperRight,
                 lowerLeft, upperRight, upperLeft});
        }
    }
    const auto geometry = shared_world_scene::ensureRigidGeometry(
        scene.registry,
        &prototype,
        key.c_str(),
        prototype.vertices.data(),
        prototype.vertices.size(),
        prototype.indices.data(),
        prototype.indices.size(),
        prototype.sourceVertices.data(),
        prototype.sourceVertices.size(),
        terrainTilePrototypes.groundSourceVertexSemanticMask,
        std::numeric_limits<std::uint32_t>::max(),
        0u);
    prototype.object = shared_world_scene::ensureRenderObject(
        scene.registry,
        geometry,
        terrainTilePrototypes.groundMaterialHandle,
        static_cast<shared_world_scene::PipelineVariant>(
            terrainTilePrototypes.groundPipelineVariant),
        terrainTilePrototypes.groundCookedDrawSlot,
        false);
    return prototype.object;
}

IRenderBackend::WorldSceneRenderObjectHandle
RuntimeEnvironment::Impl::ensureTerrainCliffObject(
    const TerrainTileState& tile,
    std::size_t edge,
    std::int32_t levelDifference) {
    if (edge >= 4u || levelDifference <= 0) {
        return {};
    }
    const std::string key =
        "route1:terrain-cliff:cell-" +
        std::to_string(tile.gridX) + "-" +
        std::to_string(tile.gridZ) + ":edge-" +
        std::to_string(edge) + ":levels-" +
        std::to_string(levelDifference);
    auto [found, inserted] =
        terrainTilePrototypes.cliffPrototypes.try_emplace(key);
    auto& prototype = found->second;
    if (!inserted) {
        return prototype.object;
    }

    constexpr std::array<std::array<std::int32_t, 2>, 4>
        directions{{
            {0, 1},
            {1, 0},
            {0, -1},
            {-1, 0},
        }};
    constexpr std::array<float, 4> rotations{
        0.0f, 90.0f, 180.0f, -90.0f};
    struct ProfileRow {
        float y;
        float outward;
        float normalY;
        float normalZ;
        float cliffV;
        float borderV;
    };
    const float height =
        static_cast<float>(levelDifference) *
        kTerrainElevationStepCm;
    std::vector<ProfileRow> rows;
    rows.reserve(5u);
    if (levelDifference > 1) {
        rows.push_back({
            0.0f,
            25.0f,
            0.0f,
            1.0f,
            -0.014f - static_cast<float>(levelDifference - 1),
            0.793f});
    }
    const float capBase = height - kTerrainElevationStepCm;
    rows.push_back({
        capBase,
        25.0f,
        0.27f,
        0.96f,
        -0.014f,
        0.793f});
    rows.push_back({
        capBase + 17.88f,
        20.0f,
        0.27f,
        0.96f,
        0.323f,
        0.850f});
    rows.push_back({
        capBase + 35.02f,
        15.0f,
        0.55f,
        0.83f,
        0.686f,
        0.850f});
    rows.push_back({
        height,
        0.0f,
        0.76f,
        0.65f,
        0.986f,
        0.850f});

    const auto direction = directions[edge];
    const float boundaryX =
        (static_cast<float>(tile.gridX) + 0.5f) *
            kTerrainTileSizeCm +
        static_cast<float>(direction[0]) *
            kTerrainTileSizeCm * 0.5f;
    const float boundaryZ =
        (static_cast<float>(tile.gridZ) + 0.5f) *
            kTerrainTileSizeCm +
        static_cast<float>(direction[1]) *
            kTerrainTileSizeCm * 0.5f;
    const glm::mat4 edgeRotation = glm::rotate(
        glm::mat4(1.0f),
        glm::radians(rotations[edge]),
        glm::vec3(0.0f, 1.0f, 0.0f));
    constexpr float cliffUPerCentimetre = 0.00516529f;
    constexpr float borderUPerCentimetre = 0.00510638f;
    for (const auto& row : rows) {
        for (const float localX : {-50.0f, 50.0f}) {
            auto vertex =
                terrainTilePrototypes.cliffVertexTemplate;
            auto sourceVertex =
                terrainTilePrototypes.cliffSourceVertexTemplate;
            vertex.x = localX;
            vertex.y = row.y;
            vertex.z = row.outward;
            vertex.nx = 0.0f;
            vertex.ny = row.normalY;
            vertex.nz = row.normalZ;
            const glm::vec3 rotated = glm::vec3(
                edgeRotation * glm::vec4(
                    vertex.x,
                    vertex.y,
                    vertex.z,
                    1.0f));
            const float sourceX = boundaryX + rotated.x;
            const float sourceZ = boundaryZ + rotated.z;
            vertex.u = sourceX / 300.0f;
            vertex.v = sourceZ / 300.0f;
            const float along = edge % 2u == 0u
                ? sourceX
                : sourceZ;
            vertex.sourceUv1U = along * cliffUPerCentimetre;
            vertex.sourceUv1V = row.cliffV;
            vertex.sourceUv2U = along * borderUPerCentimetre;
            vertex.sourceUv2V = row.borderV;
            sourceVertex.texcoords[0] = {vertex.u, vertex.v};
            sourceVertex.texcoords[1] = {
                vertex.sourceUv1U,
                vertex.sourceUv1V};
            sourceVertex.texcoords[2] = {
                vertex.sourceUv2U,
                vertex.sourceUv2V};
            prototype.vertices.push_back(vertex);
            prototype.sourceVertices.push_back(sourceVertex);
        }
    }
    for (std::uint32_t row = 0u;
         row + 1u < rows.size();
         ++row) {
        const std::uint32_t lowerLeft = row * 2u;
        const std::uint32_t lowerRight = lowerLeft + 1u;
        const std::uint32_t upperLeft = lowerLeft + 2u;
        const std::uint32_t upperRight = lowerLeft + 3u;
        prototype.indices.insert(
            prototype.indices.end(),
            {lowerLeft, lowerRight, upperRight,
             lowerLeft, upperRight, upperLeft});
    }
    const auto geometry = shared_world_scene::ensureRigidGeometry(
        scene.registry,
        &prototype,
        key.c_str(),
        prototype.vertices.data(),
        prototype.vertices.size(),
        prototype.indices.data(),
        prototype.indices.size(),
        prototype.sourceVertices.data(),
        prototype.sourceVertices.size(),
        terrainTilePrototypes.cliffSourceVertexSemanticMask,
        std::numeric_limits<std::uint32_t>::max(),
        0u);
    prototype.object = shared_world_scene::ensureRenderObject(
        scene.registry,
        geometry,
        terrainTilePrototypes.cliffMaterialHandle,
        static_cast<shared_world_scene::PipelineVariant>(
            terrainTilePrototypes.cliffPipelineVariant),
        terrainTilePrototypes.cliffCookedDrawSlot,
        false);
    return prototype.object;
}

IRenderBackend::WorldSceneRenderObjectHandle
RuntimeEnvironment::Impl::ensureTerrainCliffCornerObject(
    const TerrainTileState& tile,
    std::size_t corner,
    std::int32_t levelDifference) {
    if (corner >= 4u || levelDifference <= 0) {
        return {};
    }
    const std::string key =
        "route1:terrain-cliff-corner:cell-" +
        std::to_string(tile.gridX) + "-" +
        std::to_string(tile.gridZ) + ":corner-" +
        std::to_string(corner) + ":levels-" +
        std::to_string(levelDifference);
    auto [found, inserted] =
        terrainTilePrototypes.cliffPrototypes.try_emplace(key);
    auto& prototype = found->second;
    if (!inserted) {
        return prototype.object;
    }

    struct ProfileRow {
        float y;
        float outward;
        float normalY;
        float normalOutward;
        float cliffV;
        float borderV;
    };
    const float height =
        static_cast<float>(levelDifference) *
        kTerrainElevationStepCm;
    std::vector<ProfileRow> rows;
    rows.reserve(5u);
    if (levelDifference > 1) {
        rows.push_back({
            0.0f, 25.0f, 0.0f, 1.0f,
            -0.014f - static_cast<float>(levelDifference - 1),
            0.793f});
    }
    const float capBase = height - kTerrainElevationStepCm;
    rows.push_back({
        capBase, 25.0f, 0.27f, 0.96f, -0.014f, 0.793f});
    rows.push_back({
        capBase + 17.88f, 20.0f, 0.27f, 0.96f,
        0.323f, 0.850f});
    rows.push_back({
        capBase + 35.02f, 15.0f, 0.55f, 0.83f,
        0.686f, 0.850f});
    rows.push_back({
        height, 0.0f, 0.76f, 0.65f, 0.986f, 0.850f});

    // Clockwise quarter arcs join the adjacent bowed side profiles without
    // the square cutout left by two independent planes.
    constexpr std::array<std::array<float, 2>, 4> starts{{
        {0.0f, 1.0f},
        {1.0f, 0.0f},
        {0.0f, -1.0f},
        {-1.0f, 0.0f},
    }};
    constexpr std::array<std::array<float, 2>, 4> ends{{
        {1.0f, 0.0f},
        {0.0f, -1.0f},
        {-1.0f, 0.0f},
        {0.0f, 1.0f},
    }};
    constexpr std::array<std::array<float, 2>, 4> cornerSigns{{
        {1.0f, 1.0f},
        {1.0f, -1.0f},
        {-1.0f, -1.0f},
        {-1.0f, 1.0f},
    }};
    constexpr std::uint32_t kArcSegments = 4u;
    constexpr float kHalfPi = 1.57079632679489661923f;
    constexpr float cliffUPerCentimetre = 0.00516529f;
    constexpr float borderUPerCentimetre = 0.00510638f;
    const float tileCenterX =
        (static_cast<float>(tile.gridX) + 0.5f) *
        kTerrainTileSizeCm;
    const float tileCenterZ =
        (static_cast<float>(tile.gridZ) + 0.5f) *
        kTerrainTileSizeCm;
    const float cornerX = tileCenterX +
        cornerSigns[corner][0] * kTerrainTileSizeCm * 0.5f;
    const float cornerZ = tileCenterZ +
        cornerSigns[corner][1] * kTerrainTileSizeCm * 0.5f;
    for (const auto& row : rows) {
        for (std::uint32_t arcIndex = 0u;
             arcIndex <= kArcSegments;
             ++arcIndex) {
            const float phase = static_cast<float>(arcIndex) /
                static_cast<float>(kArcSegments);
            const float angle = phase * kHalfPi;
            const glm::vec2 outward = glm::normalize(
                glm::vec2(starts[corner][0], starts[corner][1]) *
                    std::cos(angle) +
                glm::vec2(ends[corner][0], ends[corner][1]) *
                    std::sin(angle));
            auto vertex =
                terrainTilePrototypes.cliffVertexTemplate;
            auto sourceVertex =
                terrainTilePrototypes.cliffSourceVertexTemplate;
            vertex.x = cornerSigns[corner][0] *
                    kTerrainTileSizeCm * 0.5f +
                outward.x * row.outward;
            vertex.y = row.y;
            vertex.z = cornerSigns[corner][1] *
                    kTerrainTileSizeCm * 0.5f +
                outward.y * row.outward;
            vertex.nx = outward.x * row.normalOutward;
            vertex.ny = row.normalY;
            vertex.nz = outward.y * row.normalOutward;
            const float sourceX = cornerX +
                outward.x * row.outward;
            const float sourceZ = cornerZ +
                outward.y * row.outward;
            vertex.u = sourceX / 300.0f;
            vertex.v = sourceZ / 300.0f;
            const float cornerAlong =
                0.5f * (sourceX + sourceZ);
            vertex.sourceUv1U =
                cornerAlong * cliffUPerCentimetre;
            vertex.sourceUv1V = row.cliffV;
            vertex.sourceUv2U =
                cornerAlong * borderUPerCentimetre;
            vertex.sourceUv2V = row.borderV;
            sourceVertex.texcoords[0] = {vertex.u, vertex.v};
            sourceVertex.texcoords[1] = {
                vertex.sourceUv1U, vertex.sourceUv1V};
            sourceVertex.texcoords[2] = {
                vertex.sourceUv2U, vertex.sourceUv2V};
            prototype.vertices.push_back(vertex);
            prototype.sourceVertices.push_back(sourceVertex);
        }
    }
    constexpr std::uint32_t rowWidth = kArcSegments + 1u;
    for (std::uint32_t row = 0u;
         row + 1u < rows.size();
         ++row) {
        for (std::uint32_t arc = 0u;
             arc < kArcSegments;
             ++arc) {
            const std::uint32_t lowerLeft = row * rowWidth + arc;
            const std::uint32_t lowerRight = lowerLeft + 1u;
            const std::uint32_t upperLeft = lowerLeft + rowWidth;
            const std::uint32_t upperRight = upperLeft + 1u;
            prototype.indices.insert(
                prototype.indices.end(),
                {lowerLeft, lowerRight, upperRight,
                 lowerLeft, upperRight, upperLeft});
        }
    }
    const auto geometry = shared_world_scene::ensureRigidGeometry(
        scene.registry,
        &prototype,
        key.c_str(),
        prototype.vertices.data(),
        prototype.vertices.size(),
        prototype.indices.data(),
        prototype.indices.size(),
        prototype.sourceVertices.data(),
        prototype.sourceVertices.size(),
        terrainTilePrototypes.cliffSourceVertexSemanticMask,
        std::numeric_limits<std::uint32_t>::max(),
        0u);
    prototype.object = shared_world_scene::ensureRenderObject(
        scene.registry,
        geometry,
        terrainTilePrototypes.cliffMaterialHandle,
        static_cast<shared_world_scene::PipelineVariant>(
            terrainTilePrototypes.cliffPipelineVariant),
        terrainTilePrototypes.cliffCookedDrawSlot,
        false);
    return prototype.object;
}

bool RuntimeEnvironment::Impl::initializeTerrainMask(
    std::string* outError) {
    terrainMaskGeometries.clear();
    terrainMaskCells.clear();
    terrainCleanupCells.clear();
    terrainMaskRevision = 0u;
    std::size_t terrainGeometryCount = 0u;
    for (const auto& geometry : scene.registry.geometries) {
        const bool groundReplacement =
            geometry.sourceMeshIndex <= 9u ||
            (geometry.sourceMeshIndex >= 29u &&
             geometry.sourceMeshIndex <= 36u);
        const bool flattenedGroundCleanup =
            geometry.sourceMeshIndex >= 16u &&
            geometry.sourceMeshIndex <= 28u;
        if ((groundReplacement || flattenedGroundCleanup) &&
            geometry.vertices && geometry.indices &&
            geometry.indexCount >= 3u) {
            ++terrainGeometryCount;
        }
    }
    terrainMaskGeometries.reserve(terrainGeometryCount);
    for (const auto& geometry : scene.registry.geometries) {
        const bool groundReplacement =
            geometry.sourceMeshIndex <= 9u ||
            (geometry.sourceMeshIndex >= 29u &&
             geometry.sourceMeshIndex <= 36u);
        const bool flattenedGroundCleanup =
            geometry.sourceMeshIndex >= 16u &&
            geometry.sourceMeshIndex <= 28u;
        if ((!groundReplacement && !flattenedGroundCleanup) ||
            !geometry.vertices || !geometry.indices ||
            geometry.indexCount < 3u) {
            continue;
        }
        const auto sourceMesh = std::find_if(
            source.meshes.begin(),
            source.meshes.end(),
            [&](const auto& mesh) {
                return mesh.sourceIndex == geometry.sourceMeshIndex;
            });
        if (sourceMesh == source.meshes.end()) {
            return fail(
                outError,
                "Route 1 terrain masking lost source mesh " +
                    std::to_string(geometry.sourceMeshIndex) + ".");
        }
        TerrainMaskGeometry mask{
            .geometryHandle = geometry.handle,
            .originalCacheKey = geometry.geometryCacheKey,
            .originalIndices = std::vector<std::uint32_t>(
                geometry.indices,
                geometry.indices + geometry.indexCount),
            .sourceModelMatrix = sourceMesh->transform,
            .cleanupOnly = flattenedGroundCleanup,
            // Foliage cards and low-detail overlay carriers regularly cross
            // a metre boundary. Keeping a triangle because only its centroid
            // missed the edited cell leaves the familiar floating slivers.
            // Large canonical terrain groups still use the conservative
            // centroid test to avoid opening an adjacent source cell.
            .maskWhenAnyVertexTouchesCell =
                flattenedGroundCleanup ||
                geometry.sourceMeshIndex <= 9u};
        mask.filteredIndices = mask.originalIndices;
        terrainMaskGeometries.push_back(std::move(mask));
    }
    if (terrainMaskGeometries.empty()) {
        return fail(
            outError,
            "Route 1 did not expose terrain geometry for cell-level masking.");
    }
    if (outError) {
        outError->clear();
    }
    return true;
}

void RuntimeEnvironment::Impl::applyTerrainMask() {
    std::set<std::pair<std::int32_t, std::int32_t>> nextCells;
    std::set<std::pair<std::int32_t, std::int32_t>> nextCleanupCells;
    for (const auto& tile : layout.authoredTerrainTiles) {
        const auto cell = std::pair{tile.gridX, tile.gridZ};
        nextCells.emplace(cell);
        const auto sourceTile = std::find_if(
            sourceTerrainTiles.begin(),
            sourceTerrainTiles.end(),
            [&](const TerrainTileState& candidate) {
                return candidate.gridX == tile.gridX &&
                    candidate.gridZ == tile.gridZ;
            });
        const bool explicitCleanup =
            tile.reason == "terrain_flatten_cleanup" ||
            tile.reason == "autochess_board_ground_infill";
        const bool geometryChanged =
            sourceTile == sourceTerrainTiles.end() ||
            tile.elevationLevel != sourceTile->elevationLevel ||
            tile.shape != sourceTile->shape ||
            tile.surface == "empty";
        if (explicitCleanup || geometryChanged) {
            nextCleanupCells.emplace(cell);
        }
    }
    if (nextCells == terrainMaskCells &&
        nextCleanupCells == terrainCleanupCells &&
        terrainMaskRevision != 0u) {
        return;
    }
    terrainMaskCells = std::move(nextCells);
    terrainCleanupCells = std::move(nextCleanupCells);
    ++terrainMaskRevision;
    if (terrainMaskRevision == 0u) {
        terrainMaskRevision = 1u;
    }
    for (auto& mask : terrainMaskGeometries) {
        const auto& maskedCells = mask.cleanupOnly
            ? terrainCleanupCells
            : terrainMaskCells;
        if (mask.geometryHandle.id == 0u ||
            mask.geometryHandle.id >
                scene.registry.geometries.size()) {
            continue;
        }
        auto& geometry = scene.registry.geometries[
            mask.geometryHandle.id - 1u];
        mask.filteredIndices.clear();
        mask.filteredIndices.reserve(mask.originalIndices.size());
        const glm::mat4 model = glm::make_mat4(
            mask.sourceModelMatrix.data());
        for (std::size_t index = 0u;
             index + 2u < mask.originalIndices.size();
             index += 3u) {
            const std::array<std::uint32_t, 3> triangle{
                mask.originalIndices[index],
                mask.originalIndices[index + 1u],
                mask.originalIndices[index + 2u]};
            bool valid = true;
            glm::vec3 centroid{};
            bool vertexTouchesMaskedCell = false;
            for (const auto vertexIndex : triangle) {
                if (vertexIndex >= geometry.vertexCount) {
                    valid = false;
                    break;
                }
                const auto& vertex = geometry.vertices[vertexIndex];
                const glm::vec3 sourcePosition = glm::vec3(
                    model * glm::vec4(
                        vertex.x,
                        vertex.y,
                        vertex.z,
                        1.0f));
                centroid += sourcePosition;
                if (mask.maskWhenAnyVertexTouchesCell) {
                    const auto vertexCell = std::pair{
                        static_cast<std::int32_t>(std::floor(
                            sourcePosition.x /
                                kTerrainTileSizeCm)),
                        static_cast<std::int32_t>(std::floor(
                            sourcePosition.z /
                                kTerrainTileSizeCm))};
                    vertexTouchesMaskedCell =
                        vertexTouchesMaskedCell ||
                        maskedCells.contains(vertexCell);
                }
            }
            if (!valid) {
                continue;
            }
            centroid /= 3.0f;
            const auto cell = std::pair{
                static_cast<std::int32_t>(std::floor(
                    centroid.x / kTerrainTileSizeCm)),
                static_cast<std::int32_t>(std::floor(
                    centroid.z / kTerrainTileSizeCm))};
            if (vertexTouchesMaskedCell ||
                maskedCells.contains(cell)) {
                continue;
            }
            mask.filteredIndices.insert(
                mask.filteredIndices.end(),
                triangle.begin(),
                triangle.end());
        }
        geometry.indices = mask.filteredIndices.data();
        geometry.indexCount = mask.filteredIndices.size();
        geometry.geometryCacheKey = mask.originalCacheKey +
            ":terrain-mask:" +
            std::to_string(terrainMaskRevision);
    }
    ++scene.registry.generation;
    if (scene.registry.generation == 0u) {
        scene.registry.generation = 1u;
    }
}

void RuntimeEnvironment::Impl::rebuildTerrainTileStates() {
    terrainTiles = sourceTerrainTiles;
    for (const auto& authored : layout.authoredTerrainTiles) {
        auto tile = std::find_if(
            terrainTiles.begin(),
            terrainTiles.end(),
            [&](const TerrainTileState& candidate) {
                return candidate.gridX == authored.gridX &&
                    candidate.gridZ == authored.gridZ;
            });
        if (tile == terrainTiles.end()) {
            continue;
        }
        tile->elevationLevel = authored.elevationLevel;
        tile->surface = authored.surface;
        tile->shape = authored.shape;
        tile->visualVariant = authored.visualVariant;
        tile->reason = authored.reason;
        tile->authored = true;
    }
}

void RuntimeEnvironment::Impl::appendAuthoredTerrainTiles(
    IRenderBackend::WorldSceneFrame& frame) {
    const auto findTile =
        [&](std::int32_t gridX,
            std::int32_t gridZ)
            -> const TerrainTileState* {
            const auto found = std::find_if(
                terrainTiles.begin(),
                terrainTiles.end(),
                [&](const TerrainTileState& tile) {
                    return tile.gridX == gridX &&
                        tile.gridZ == gridZ;
                });
            return found == terrainTiles.end()
                ? nullptr
                : &*found;
        };
    std::uint32_t nextInstanceId = 0xe0000000u;
    const auto append =
        [&](IRenderBackend::WorldSceneRenderObjectHandle object,
            const std::array<float, 16>& model) {
            IRenderBackend::WorldSceneRenderInstanceHandle instance;
            instance.id = nextInstanceId++;
            shared_world_scene::appendRigidInstance(
                frame,
                object,
                instance,
                model,
                1.0f,
                1.0f,
                1.0f,
                1.0f,
                0.0f);
        };
    const auto edgeHeight =
        [](const TerrainTileState& tile,
           std::int32_t directionX,
           std::int32_t directionZ) {
            std::int32_t level = tile.elevationLevel;
            if ((directionX > 0 && tile.shape == "ramp_east") ||
                (directionX < 0 && tile.shape == "ramp_west") ||
                (directionZ > 0 && tile.shape == "ramp_north") ||
                (directionZ < 0 && tile.shape == "ramp_south")) {
                ++level;
            }
            return level;
        };
    const auto hasSurface =
        [](const TerrainTileState& tile) {
            return tile.surface != "empty" &&
                (tile.sourceOccupied || tile.authored);
        };
    constexpr std::array<std::array<std::int32_t, 2>, 4>
        directions{{
            {0, 1},
            {1, 0},
            {0, -1},
            {-1, 0},
        }};
    constexpr std::array<float, 4> rotations{
        0.0f, 90.0f, 180.0f, -90.0f};
    for (const auto& tile : terrainTiles) {
        if (!tile.authored) {
            continue;
        }
        if (tile.surface == "empty") {
            continue;
        }
        const bool dirt = tile.surface == "dirt_path";
        std::uint32_t dirtConnectionMask = 0u;
        if (dirt) {
            for (std::size_t edge = 0u;
                 edge < directions.size();
                 ++edge) {
                const auto direction = directions[edge];
                const auto* neighbor = findTile(
                    tile.gridX + direction[0],
                    tile.gridZ + direction[1]);
                if (neighbor && hasSurface(*neighbor) &&
                    neighbor->surface == "dirt_path" &&
                    edgeHeight(
                        tile,
                        direction[0],
                        direction[1]) ==
                        edgeHeight(
                            *neighbor,
                            -direction[0],
                            -direction[1])) {
                    dirtConnectionMask |= 1u << edge;
                }
            }
        }
        const auto topObject = ensureTerrainTopObject(
            tile,
            dirtConnectionMask);
        const std::array<float, 3> center{
            (static_cast<float>(tile.gridX) + 0.5f) *
                kTerrainTileSizeCm,
            static_cast<float>(tile.elevationLevel) *
                    kTerrainElevationStepCm +
                kTerrainTileTopDepthBiasCm,
            (static_cast<float>(tile.gridZ) + 0.5f) *
                kTerrainTileSizeCm};
        append(
            topObject,
            sourcePlacementMatrix(
                center,
                {0.0f, 0.0f, 0.0f},
                {1.0f, 1.0f, 1.0f}));

        std::array<std::int32_t, 4> edgeDifferences{};
        std::array<std::int32_t, 4> edgeNeighborLevels{};
        for (std::size_t edge = 0u;
             edge < directions.size();
             ++edge) {
            const auto direction = directions[edge];
            const std::int32_t currentLevel = edgeHeight(
                tile,
                direction[0],
                direction[1]);
            const auto* neighbor = findTile(
                tile.gridX + direction[0],
                tile.gridZ + direction[1]);
            const std::int32_t neighborLevel = neighbor &&
                    hasSurface(*neighbor)
                ? edgeHeight(
                      *neighbor,
                      -direction[0],
                      -direction[1])
                : 0;
            edgeNeighborLevels[edge] = neighborLevel;
            edgeDifferences[edge] = currentLevel - neighborLevel;
            if (currentLevel <= neighborLevel) {
                continue;
            }
            const float halfSize =
                kTerrainTileSizeCm * 0.5f;
            const std::array<float, 3> sideCenter{
                center[0] +
                    static_cast<float>(direction[0]) * halfSize,
                static_cast<float>(neighborLevel) *
                    kTerrainElevationStepCm,
                center[2] +
                    static_cast<float>(direction[1]) * halfSize};
            append(
                ensureTerrainCliffObject(
                    tile,
                    edge,
                    currentLevel - neighborLevel),
                sourcePlacementMatrix(
                    sideCenter,
                    {0.0f, rotations[edge], 0.0f},
                    {1.0f, 1.0f, 1.0f}));
        }
        if (tile.shape == "flat") {
            constexpr std::array<std::array<std::size_t, 2>, 4>
                cornerEdges{{
                    {0u, 1u},
                    {1u, 2u},
                    {2u, 3u},
                    {3u, 0u},
                }};
            for (std::size_t corner = 0u;
                 corner < cornerEdges.size();
                 ++corner) {
                const auto firstEdge = cornerEdges[corner][0];
                const auto secondEdge = cornerEdges[corner][1];
                if (edgeDifferences[firstEdge] <= 0 ||
                    edgeDifferences[firstEdge] !=
                        edgeDifferences[secondEdge] ||
                    edgeNeighborLevels[firstEdge] !=
                        edgeNeighborLevels[secondEdge]) {
                    continue;
                }
                append(
                    ensureTerrainCliffCornerObject(
                        tile,
                        corner,
                        edgeDifferences[firstEdge]),
                    sourcePlacementMatrix(
                        {center[0],
                         static_cast<float>(
                             edgeNeighborLevels[firstEdge]) *
                             kTerrainElevationStepCm,
                         center[2]},
                        {0.0f, 0.0f, 0.0f},
                        {1.0f, 1.0f, 1.0f}));
            }
        }
    }
}

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
             schemaVersion != 4 &&
             schemaVersion != 5 &&
             schemaVersion != 6) ||
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
        if (schemaVersion >= 6 &&
            transform.contains("source_anchor_cm")) {
            throw std::runtime_error(
                "schema 6 forbids source_anchor_cm; the board anchor is derived from terrain_grid_origin.");
        }
        decoded.sourceUnitsToWorld =
            transform.at("source_units_to_world").get<float>();
        if (schemaVersion <= 5) {
            decoded.sourceAnchorCm = jsonFloatArray<3>(
                transform.at("source_anchor_cm"),
                "source_anchor_cm");
        }
        decoded.worldAnchor = jsonFloatArray<3>(
            transform.at("world_anchor"),
            "world_anchor");
        decoded.yawDegrees =
            transform.at("yaw_degrees").get<float>();
        if (schemaVersion >= 6 &&
            !root.contains("board_registration")) {
            throw std::runtime_error(
                "schema 6 requires an explicit terrain-bound board_registration.");
        }
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
            if (schemaVersion >= 6) {
                if (registration->contains("cell_size_world")) {
                    throw std::runtime_error(
                        "schema 6 forbids cell_size_world; the board cell size is the Route 1 terrain tile size.");
                }
                const auto& origin =
                    registration->at("terrain_grid_origin");
                if (!origin.is_array() || origin.size() != 2u) {
                    throw std::runtime_error(
                        "terrain_grid_origin must contain two integer cell coordinates.");
                }
                decoded.terrainGridOrigin = {
                    origin.at(0).get<std::int32_t>(),
                    origin.at(1).get<std::int32_t>()};
                decoded.terrainElevationLevel =
                    registration->at(
                        "terrain_elevation_level")
                        .get<std::int32_t>();
                const float declaredTileSize =
                    registration->at(
                        "terrain_tile_size_cm")
                        .get<float>();
                if (!std::isfinite(declaredTileSize) ||
                    std::abs(
                        declaredTileSize -
                        kTerrainTileSizeCm) > 0.0001f) {
                    throw std::runtime_error(
                        "terrain_tile_size_cm must match the recovered Route 1 tile module.");
                }
                decoded.boardCellSizeWorld =
                    kTerrainTileSizeCm *
                    decoded.sourceUnitsToWorld;
                decoded.sourceAnchorCm = {
                    (static_cast<float>(
                         decoded.terrainGridOrigin[0]) +
                     static_cast<float>(
                         decoded.boardCells[0]) *
                         0.5f) *
                        kTerrainTileSizeCm,
                    static_cast<float>(
                        decoded.terrainElevationLevel) *
                        kTerrainElevationStepCm,
                    (static_cast<float>(
                         decoded.terrainGridOrigin[1]) +
                     static_cast<float>(
                         decoded.boardCells[1]) *
                         0.5f) *
                        kTerrainTileSizeCm};
            } else {
                if (const auto cellSize =
                        registration->find("cell_size_world");
                    cellSize != registration->end()) {
                    decoded.boardCellSizeWorld =
                        cellSize->get<float>();
                }
                decoded.terrainGridOrigin = {
                    static_cast<std::int32_t>(std::llround(
                        decoded.sourceAnchorCm[0] /
                            kTerrainTileSizeCm -
                        static_cast<float>(
                            decoded.boardCells[0]) *
                            0.5f)),
                    static_cast<std::int32_t>(std::llround(
                        decoded.sourceAnchorCm[2] /
                            kTerrainTileSizeCm -
                        static_cast<float>(
                            decoded.boardCells[1]) *
                            0.5f))};
                decoded.terrainElevationLevel =
                    static_cast<std::int32_t>(std::llround(
                        decoded.sourceAnchorCm[1] /
                        kTerrainElevationStepCm));
            }
            if (const auto benchSlots =
                    registration->find("bench_slots");
                benchSlots != registration->end()) {
                decoded.benchSlots =
                    benchSlots->get<std::uint32_t>();
            }
            if (const auto benchGap =
                    registration->find("bench_gap_cells");
                benchGap != registration->end()) {
                decoded.benchGapCells =
                    benchGap->get<std::uint32_t>();
            }
            if (const auto benches =
                    registration->find("bench_sides");
                benches != registration->end()) {
                if (!benches->is_array()) {
                    throw std::runtime_error(
                        "bench_sides must be an array.");
                }
                decoded.northBench = false;
                decoded.southBench = false;
                for (const auto& side : *benches) {
                    const std::string value =
                        side.get<std::string>();
                    if (value == "north") {
                        decoded.northBench = true;
                    } else if (value == "south") {
                        decoded.southBench = true;
                    } else {
                        throw std::runtime_error(
                            "bench_sides entries must be north or south.");
                    }
                }
            }
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
                    "canonical_terrain_assembly" ||
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
            decoded.boardCells[1] == 0u ||
            !std::isfinite(decoded.boardCellSizeWorld) ||
            decoded.boardCellSizeWorld < 0.25f ||
            decoded.boardCellSizeWorld > 4.0f ||
            decoded.benchSlots == 0u ||
            (!decoded.northBench && !decoded.southBench) ||
            !boardRegistrationMatchesTerrainGrid(decoded)) {
            return fail(
                outError,
                "Route 1 board-layout manifest has invalid source metadata "
                "or a board registration that is not bound to the terrain grid.");
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

void bindBoardLayoutToTerrainGrid(
    BoardLayoutTransform& layout) noexcept {
    layout.boardCellSizeWorld =
        kTerrainTileSizeCm * layout.sourceUnitsToWorld;
    layout.sourceAnchorCm = {
        (static_cast<float>(layout.terrainGridOrigin[0]) +
         static_cast<float>(layout.boardCells[0]) * 0.5f) *
            kTerrainTileSizeCm,
        static_cast<float>(layout.terrainElevationLevel) *
            kTerrainElevationStepCm,
        (static_cast<float>(layout.terrainGridOrigin[1]) +
         static_cast<float>(layout.boardCells[1]) * 0.5f) *
            kTerrainTileSizeCm};
    layout.worldAnchor[0] = 0.0f;
    layout.worldAnchor[2] = 0.0f;
    layout.yawDegrees = 0.0f;
}

std::array<std::int32_t, 2> northBenchTerrainGridOrigin(
    const BoardLayoutTransform& layout) noexcept {
    const std::int64_t centeredOffset =
        (static_cast<std::int64_t>(layout.boardCells[0]) -
         static_cast<std::int64_t>(layout.benchSlots)) /
        2;
    return {
        static_cast<std::int32_t>(
            static_cast<std::int64_t>(
                layout.terrainGridOrigin[0]) +
            centeredOffset),
        static_cast<std::int32_t>(
            static_cast<std::int64_t>(
                layout.terrainGridOrigin[1]) +
            static_cast<std::int64_t>(layout.boardCells[1]) +
            static_cast<std::int64_t>(layout.benchGapCells))};
}

std::array<std::int32_t, 2> southBenchTerrainGridOrigin(
    const BoardLayoutTransform& layout) noexcept {
    const std::int64_t centeredOffset =
        (static_cast<std::int64_t>(layout.boardCells[0]) -
         static_cast<std::int64_t>(layout.benchSlots)) /
        2;
    return {
        static_cast<std::int32_t>(
            static_cast<std::int64_t>(
                layout.terrainGridOrigin[0]) +
            centeredOffset),
        static_cast<std::int32_t>(
            static_cast<std::int64_t>(
                layout.terrainGridOrigin[1]) -
            static_cast<std::int64_t>(layout.benchGapCells) -
            1)};
}

std::string serializeBoardLayoutTransform(
    const BoardLayoutTransform& transform) {
    const auto cleanNumber = [](float value) {
        constexpr double precision = 1'000'000.0;
        return std::round(static_cast<double>(value) * precision) /
               precision;
    };
    const auto cleanVec3 = [&](const std::array<float, 3>& value) {
        return std::array<double, 3>{
            cleanNumber(value[0]),
            cleanNumber(value[1]),
            cleanNumber(value[2])};
    };
    nlohmann::json root{
        {"schema_version", 6},
        {"kind", "lgpe_route1_board_layout_delta"},
        {"coordinate_system", transform.coordinateSystem},
        {"source_profile_id", transform.sourceProfileId},
        {"source_to_world",
         {
             {"source_units_to_world",
              cleanNumber(transform.sourceUnitsToWorld)},
             {"world_anchor",
              cleanVec3(transform.worldAnchor)},
             {"yaw_degrees",
              cleanNumber(transform.yawDegrees)},
         }},
        {"board_registration",
         {
             {"board_cells", transform.boardCells},
             {"terrain_grid_origin",
              transform.terrainGridOrigin},
             {"terrain_elevation_level",
              transform.terrainElevationLevel},
             {"terrain_tile_size_cm",
              cleanNumber(kTerrainTileSizeCm)},
             {"bench_slots", transform.benchSlots},
             {"bench_gap_cells", transform.benchGapCells},
             {"bench_sides", nlohmann::json::array()},
             {"intent",
              "register the source-centimetre qualification scene "
              "under the gameplay board and both bench rows"},
             {"status", "gameplay_footprint_terrain_grid_bound"},
         }},
        {"fidelity_contract",
         {
             {"source_scale_preserved", true},
             {"source_elevations_preserved", true},
             {"undeclared_source_transforms_changed", false},
             {"procedural_route_environment_contribution", false},
         }},
    };
    auto& benchSides =
        root["board_registration"]["bench_sides"];
    if (transform.northBench) {
        benchSides.push_back("north");
    }
    if (transform.southBench) {
        benchSides.push_back("south");
    }
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
        const SourceBounds transformedBounds =
            transformSourceBounds(
                mesh.boundsMinimum,
                mesh.boundsMaximum,
                glm::make_mat4(mesh.transform.data()));
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
                .sourceBoundsMinimumCm =
                    transformedBounds.minimum,
                .sourceBoundsMaximumCm =
                    transformedBounds.maximum,
                .translationCm = pivot});
    }
    if (!loaded->splitCanonicalTreeInstances(
            outError)) {
        return false;
    }
    if (!loaded->splitCanonicalTerrainAssemblies(
            outError)) {
        return false;
    }
    if (!loaded->initializeBoardGroundPatch(
            outError)) {
        return false;
    }
    if (!loaded->initializeTerrainTiles(outError)) {
        return false;
    }
    if (!loaded->initializeTerrainMask(outError)) {
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
            auto& sourceRecord =
                loaded->encounterGrassRecords.back();
            sourceRecord.sourceBoundsMinimumCm = {
                std::numeric_limits<float>::max(),
                translation[1],
                std::numeric_limits<float>::max()};
            sourceRecord.sourceBoundsMaximumCm = {
                std::numeric_limits<float>::lowest(),
                translation[1] + 140.0f,
                std::numeric_limits<float>::lowest()};
            constexpr float kEncounterBladeHalfSpanCm =
                55.0f;
            for (const auto& placement : expanded) {
                sourceRecord.sourceBoundsMinimumCm[0] =
                    std::min(
                        sourceRecord.sourceBoundsMinimumCm[0],
                        placement.sourceCenter[0] -
                            kEncounterBladeHalfSpanCm);
                sourceRecord.sourceBoundsMinimumCm[2] =
                    std::min(
                        sourceRecord.sourceBoundsMinimumCm[2],
                        placement.sourceCenter[2] -
                            kEncounterBladeHalfSpanCm);
                sourceRecord.sourceBoundsMaximumCm[0] =
                    std::max(
                        sourceRecord.sourceBoundsMaximumCm[0],
                        placement.sourceCenter[0] +
                            kEncounterBladeHalfSpanCm);
                sourceRecord.sourceBoundsMaximumCm[2] =
                    std::max(
                        sourceRecord.sourceBoundsMaximumCm[2],
                        placement.sourceCenter[2] +
                            kEncounterBladeHalfSpanCm);
            }
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
            std::array<float, 3> localBoundsMinimum{
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max()};
            std::array<float, 3> localBoundsMaximum{
                std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest()};
            for (const auto& sourceMesh : layer.source.meshes) {
                const SourceBounds meshBounds =
                    transformSourceBounds(
                        sourceMesh.boundsMinimum,
                        sourceMesh.boundsMaximum,
                        glm::make_mat4(
                            sourceMesh.transform.data()));
                for (std::size_t axis = 0u;
                     axis < 3u;
                     ++axis) {
                    localBoundsMinimum[axis] = std::min(
                        localBoundsMinimum[axis],
                        meshBounds.minimum[axis]);
                    localBoundsMaximum[axis] = std::max(
                        localBoundsMaximum[axis],
                        meshBounds.maximum[axis]);
                }
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
                decodedPlacement.localBoundsMinimumCm =
                    localBoundsMinimum;
                decodedPlacement.localBoundsMaximumCm =
                    localBoundsMaximum;
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

const std::vector<TerrainTileState>&
RuntimeEnvironment::terrainTiles() const noexcept {
    static const std::vector<TerrainTileState> empty;
    return impl_ ? impl_->terrainTiles : empty;
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
        layout.boardCells[1] == 0u ||
        !std::isfinite(layout.boardCellSizeWorld) ||
        layout.boardCellSizeWorld < 0.25f ||
        layout.boardCellSizeWorld > 4.0f ||
        layout.benchSlots == 0u ||
        (!layout.northBench && !layout.southBench) ||
        !boardRegistrationMatchesTerrainGrid(layout)) {
        return fail(
            outError,
            "Route 1 layout metadata does not match the mounted "
            "canonical scene or terrain grid.");
    }

    BoardLayoutTransform previous = impl_->layout;
    impl_->layout = layout;
    normalizeTerrainVisualVariants(impl_->layout);
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

bool RuntimeEnvironment::previewBoardLayout(
    const BoardLayoutTransform& layout,
    std::string* outError) {
    if (!loaded()) {
        return fail(
            outError,
            "Route 1 must be mounted before previewing a board layout.");
    }
    if (layout.coordinateSystem !=
            impl_->layout.coordinateSystem ||
        layout.sourceProfileId !=
            impl_->source.profileId ||
        !std::isfinite(layout.sourceUnitsToWorld) ||
        layout.sourceUnitsToWorld <= 0.0f ||
        !std::isfinite(layout.yawDegrees) ||
        layout.boardCells[0] == 0u ||
        layout.boardCells[1] == 0u ||
        !std::isfinite(layout.boardCellSizeWorld) ||
        layout.boardCellSizeWorld < 0.25f ||
        layout.boardCellSizeWorld > 4.0f ||
        layout.benchSlots == 0u ||
        (!layout.northBench && !layout.southBench) ||
        !boardRegistrationMatchesTerrainGrid(layout)) {
        return fail(
            outError,
            "Route 1 board-preview metadata does not match the mounted "
            "canonical scene or terrain grid.");
    }

    impl_->layout = layout;
    normalizeTerrainVisualVariants(impl_->layout);
    impl_->layout.declaredLocalDeltaCount =
        static_cast<std::uint32_t>(
            impl_->layout.localLayoutDeltas.size());
    impl_->worldFromSource =
        boardMatrix(impl_->layout);
    impl_->sourceFromWorld =
        glm::inverse(impl_->worldFromSource);
    impl_->cloudProjectionRows =
        route1CloudProjectionRows(impl_->layout);
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
    // Preserve the validated project document while migrating retired
    // manual terrain appearance values to the automatic surface contract.
    impl_->authoredScene = document;
    for (auto& node : impl_->authoredScene.nodes) {
        if (node.terrainTile &&
            automaticTerrainAppearance(
                node.terrainTile->surface)) {
            node.terrainTile->visualVariant = "auto";
        }
    }
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
