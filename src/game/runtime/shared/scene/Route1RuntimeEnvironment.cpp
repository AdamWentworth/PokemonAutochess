#include "game/runtime/shared/scene/Route1RuntimeEnvironment.h"
#include "game/runtime/shared/scene/Route1SceneVariants.h"

#include "game/assets/environment/PublishedEnvironmentScene.h"
#include "engine/assets/phlosion/PhlosionSceneArchive.h"
#include "engine/core/Environment.h"
#include "engine/core/IAssetStore.h"
#include "game/render/environment/Route1FieldEncounterGrassMaterial.h"
#include "game/render/environment/Route1FieldSmallGrassMaterial.h"
#include "game/runtime/shared/scene/Route1ProjectedShadow.h"
#include "game/runtime/shared/scene/Route1TerrainAssemblies.h"
#include "game/runtime/shared/scene/Route1TerrainContourAssembler.h"
#include "game/runtime/shared/scene/Route1TerrainLedgeResolver.h"
#include "game/runtime/shared/scene/Route1TerrainPatchCooker.h"
#include "game/runtime/shared/scene/TerrainContourMesher.h"
#include "game/runtime/shared/scene/Route1TerrainSeamResolver.h"
#include "game/runtime/shared/scene/Route1TreeInstances.h"
#include "game/runtime/shared/scene/PublishedEnvironmentSceneAdapter.h"
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

namespace game::runtime::route1_environment {
namespace {

using CanonicalScene = game::assets::published_environment::CanonicalScene;
using PreparedScene = published_environment_scene::PreparedScene;
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

bool terrainConnectionMaskFromVisualVariant(
    std::string_view variant,
    std::uint32_t& outMask) {
    if (!variant.starts_with("path_")) {
        return false;
    }
    const auto digits = variant.substr(5u);
    std::uint32_t mask = 0u;
    const auto result = std::from_chars(
        digits.data(), digits.data() + digits.size(), mask);
    const bool valid = result.ec == std::errc{} &&
        result.ptr == digits.data() + digits.size() &&
        mask <= 15u;
    if (valid) {
        outMask = mask;
    }
    return valid;
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
    std::uint32_t mask = 0u;
    return surface == "dirt_path" &&
        terrainConnectionMaskFromVisualVariant(variant, mask);
}

bool automaticTerrainAppearance(std::string_view surface) {
    return surface == "light_lawn" ||
        surface == "dark_lawn" ||
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

struct EncounterGrassDrawMaskSet {
    // Grass02 exposes five independently animated clusters, so every
    // possible visible-cluster combination fits in a five-bit mask. The
    // filtered index buffers retain the source vertices and skinning data;
    // only triangles owned by hidden clusters are omitted.
    std::array<std::vector<std::uint32_t>, 32> indices;
    std::array<
        IRenderBackend::WorldSceneRenderObjectHandle,
        32> objects{};
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
    // Joint zero is the rigid root. The remaining entries correspond to the
    // source grass clusters, so a passing unit parts nearby blades instead
    // of tilting an entire one-metre module as a single card.
    std::array<float, 6> contactBendRadians{};
    std::array<float, 6> contactCrossRadians{};
    // A source module is centered on a terrain-cell corner and its
    // independently skinned blade clusters occupy the surrounding cells.
    // Per-cell removal therefore masks clusters, not the whole module.
    std::array<bool, 6> suppressedJoints{};
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
    std::set<GridCell> sourceCoreTerrainCells;
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
    std::vector<EncounterGrassDrawMaskSet> sourceDrawMasks;
    std::vector<EncounterGrassDrawMaskSet> shadowSourceDrawMasks;
    // Source-draw-local centers derived from the vertices influenced by each
    // grass joint. The animation pivot is not necessarily the visual center
    // of that joint's blades, so it must not be used to decide which terrain
    // cell owns the rendered cluster.
    std::array<std::array<float, 3>, 6> sourceJointAnchors{};
    std::vector<std::vector<float>> skinPalettes;
    std::size_t canonicalPlacementCount = 0u;
    std::size_t instanceCount = 0u;
    std::size_t clusterCount = 0u;
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
    std::uint32_t materialIndex = 19u;
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

struct DirtTransitionUvField {
    std::array<float, 4> edgeStartU{};
    std::array<float, 4> edgeUPerCm{
        0.0036f, 0.0036f, 0.0036f, 0.0036f};
    std::uint32_t boundaryMask = 0u;
};

struct TerrainTilePrototypeSet {
    // Flat tiles are generated lazily for the cells used by authored terrain.
    // Authored replacements use one continuous source-world UV field and an
    // exact plane; untouched terrain remains in the canonical source draw.
    std::map<std::string, TerrainTileTopPrototype> topPrototypes;
    std::map<std::string, TerrainTileTopPrototype>
        authoredSurfacePrototypes;
    std::map<std::string, TerrainTileTopPrototype> cliffPrototypes;
    std::map<std::string, TerrainTileTopPrototype> fringePrototypes;
    std::map<std::string, TerrainTileTopPrototype>
        sourceReferencePrototypes;
    IRenderBackend::WorldMeshVertex groundVertexTemplate{};
    IRenderBackend::WorldSceneSourceVertex groundSourceVertexTemplate{};
    std::uint32_t groundSourceVertexSemanticMask = 0u;
    IRenderBackend::WorldSceneMaterialHandle groundMaterialHandle{};
    IRenderBackend::WorldSceneMaterialHandle
        groundShadowlessMaterialHandle{};
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
    IRenderBackend::WorldMeshVertex fringeVertexTemplate{};
    IRenderBackend::WorldSceneSourceVertex
        fringeSourceVertexTemplate{};
    std::uint32_t fringeSourceVertexSemanticMask = 0u;
    IRenderBackend::WorldSceneMaterialHandle fringeMaterialHandle{};
    std::uint8_t fringePipelineVariant = 0u;
    std::uint32_t fringeCookedDrawSlot = 0u;
};

struct TerrainMaskGeometry {
    IRenderBackend::WorldSceneGeometryHandle geometryHandle{};
    std::string originalCacheKey;
    std::vector<IRenderBackend::WorldMeshVertex> originalVertices;
    std::vector<IRenderBackend::WorldSceneSourceVertex>
        originalSourceVertices;
    std::vector<std::uint32_t> originalIndices;
    std::vector<IRenderBackend::WorldMeshVertex> filteredVertices;
    std::vector<IRenderBackend::WorldSceneSourceVertex>
        filteredSourceVertices;
    std::vector<std::uint32_t> filteredIndices;
    std::uint32_t originalSourceVertexSemanticMask =
        IRenderBackend::WorldSceneSourceVertexSemanticNone;
    std::array<float, 16> sourceModelMatrix{};
    bool cleanupOnly = false;
    bool sourceGround = false;
    bool maskWhenAnyVertexTouchesCell = false;
    bool retireWhenIntersectingRebuiltBoundary = false;
};

struct SourceTerrainFringeMaterialSegment {
    glm::vec3 startPositionCm{};
    glm::vec3 endPositionCm{};
    glm::vec3 startNormal{};
    glm::vec3 endNormal{};
    float startUv1U = 0.0f;
    float endUv1U = 0.0f;
    glm::vec4 startColor{1.0f};
    glm::vec4 endColor{1.0f};
};

struct SourceTerrainCliffGeometrySegment {
    glm::vec3 startPositionCm{};
    glm::vec3 endPositionCm{};
    glm::vec3 startNormal{};
    glm::vec3 endNormal{};
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

void initializeEncounterGrassJointAnchors(
    EncounterGrassLayer& layer,
    engine::render::route1_field_encounter_grass::SourceVariant variant) {
    std::array<glm::dvec3, 6> weightedPositions{};
    std::array<double, 6> totalWeights{};
    const std::size_t responsiveJointCount = std::min(
        layer.source.bones.size(),
        engine::render::route1_field_encounter_grass::
            sourceJointCount(variant));
    for (const auto& sourceDraw : layer.sourceDraws) {
        const auto* object = renderObject(
            layer.scene.registry,
            sourceDraw.objectHandle);
        const auto* mesh = object
            ? geometry(layer.scene.registry, object->geometryHandle)
            : nullptr;
        if (!mesh || !mesh->vertices) {
            continue;
        }
        const glm::dmat4 sourceDrawMatrix(
            glm::make_mat4(sourceDraw.modelMatrix.data()));
        for (std::size_t vertexIndex = 0u;
             vertexIndex < mesh->vertexCount;
             ++vertexIndex) {
            const auto& vertex = mesh->vertices[vertexIndex];
            const glm::dvec4 renderedPosition =
                sourceDrawMatrix *
                glm::dvec4(vertex.x, vertex.y, vertex.z, 1.0);
            const std::array<float, 4> joints{
                vertex.joint0,
                vertex.joint1,
                vertex.joint2,
                vertex.joint3};
            const std::array<float, 4> weights{
                vertex.weight0,
                vertex.weight1,
                vertex.weight2,
                vertex.weight3};
            for (std::size_t influence = 0u;
                 influence < joints.size();
                 ++influence) {
                if (!std::isfinite(joints[influence]) ||
                    !std::isfinite(weights[influence]) ||
                    weights[influence] <= 0.0f) {
                    continue;
                }
                const auto joint = static_cast<std::int32_t>(
                    std::lround(joints[influence]));
                if (joint <= 0 ||
                    static_cast<std::size_t>(joint) >=
                        responsiveJointCount ||
                    std::abs(
                        joints[influence] -
                        static_cast<float>(joint)) > 0.001f) {
                    continue;
                }
                weightedPositions[static_cast<std::size_t>(joint)] +=
                    glm::dvec3(renderedPosition) *
                    static_cast<double>(weights[influence]);
                totalWeights[static_cast<std::size_t>(joint)] +=
                    static_cast<double>(weights[influence]);
            }
        }
    }

    for (std::size_t joint = 1u;
         joint < responsiveJointCount;
         ++joint) {
        if (totalWeights[joint] <= 0.0) {
            throw std::runtime_error(
                layer.logicalName +
                " encounter grass has no rendered vertices for responsive joint " +
                std::to_string(joint) + ".");
        }
        const glm::dvec3 anchor =
            weightedPositions[joint] / totalWeights[joint];
        layer.sourceJointAnchors[joint] = {
            static_cast<float>(anchor.x),
            static_cast<float>(anchor.y),
            static_cast<float>(anchor.z)};
    }
}

void initializeEncounterGrassDrawMasks(
    EncounterGrassLayer& layer,
    const std::vector<PlacedVegetationSourceDraw>& sourceDraws,
    std::vector<EncounterGrassDrawMaskSet>& outMasks,
    engine::render::route1_field_encounter_grass::SourceVariant variant,
    std::string_view cacheRole) {
    const std::size_t responsiveJointCount = std::min(
        layer.source.bones.size(),
        engine::render::route1_field_encounter_grass::
            sourceJointCount(variant));
    if (responsiveJointCount <= 1u ||
        responsiveJointCount > 6u) {
        throw std::runtime_error(
            layer.logicalName +
            " encounter grass has an unsupported responsive-joint count.");
    }
    const std::uint32_t fullMask =
        (1u << static_cast<std::uint32_t>(responsiveJointCount - 1u)) - 1u;
    outMasks.clear();
    outMasks.resize(sourceDraws.size());
    for (std::size_t drawIndex = 0u;
         drawIndex < sourceDraws.size();
         ++drawIndex) {
        const auto sourceObject = renderObject(
            layer.scene.registry,
            sourceDraws[drawIndex].objectHandle);
        const auto sourceGeometry = sourceObject
            ? geometry(
                  layer.scene.registry,
                  sourceObject->geometryHandle)
            : nullptr;
        if (!sourceObject || !sourceGeometry ||
            !sourceGeometry->vertices || !sourceGeometry->indices ||
            sourceGeometry->indexCount < 3u) {
            throw std::runtime_error(
                layer.logicalName +
                " encounter grass lost a source draw while building cluster masks.");
        }

        // Registry growth below can reallocate its records, so copy every
        // source field needed by the generated objects before adding one.
        const auto originalObject = *sourceObject;
        const auto originalGeometry = *sourceGeometry;
        auto& maskSet = outMasks[drawIndex];
        maskSet.objects[fullMask] =
            sourceDraws[drawIndex].objectHandle;

        std::vector<std::uint8_t> triangleOwners;
        triangleOwners.reserve(originalGeometry.indexCount / 3u);
        for (std::size_t index = 0u;
             index + 2u < originalGeometry.indexCount;
             index += 3u) {
            std::array<double, 6> jointWeights{};
            for (std::size_t corner = 0u; corner < 3u; ++corner) {
                const std::uint32_t vertexIndex =
                    originalGeometry.indices[index + corner];
                if (vertexIndex >= originalGeometry.vertexCount) {
                    throw std::runtime_error(
                        layer.logicalName +
                        " encounter grass contains an out-of-range source triangle.");
                }
                const auto& vertex =
                    originalGeometry.vertices[vertexIndex];
                const std::array<float, 4> joints{
                    vertex.joint0,
                    vertex.joint1,
                    vertex.joint2,
                    vertex.joint3};
                const std::array<float, 4> weights{
                    vertex.weight0,
                    vertex.weight1,
                    vertex.weight2,
                    vertex.weight3};
                for (std::size_t influence = 0u;
                     influence < joints.size();
                     ++influence) {
                    if (!std::isfinite(joints[influence]) ||
                        !std::isfinite(weights[influence]) ||
                        weights[influence] <= 0.0f) {
                        continue;
                    }
                    const auto joint = static_cast<std::int32_t>(
                        std::lround(joints[influence]));
                    if (joint <= 0 ||
                        static_cast<std::size_t>(joint) >=
                            responsiveJointCount ||
                        std::abs(
                            joints[influence] -
                            static_cast<float>(joint)) > 0.001f) {
                        continue;
                    }
                    jointWeights[static_cast<std::size_t>(joint)] +=
                        static_cast<double>(weights[influence]);
                }
            }
            std::uint8_t owner = 0u;
            for (std::size_t joint = 1u;
                 joint < responsiveJointCount;
                 ++joint) {
                if (jointWeights[joint] > jointWeights[owner]) {
                    owner = static_cast<std::uint8_t>(joint);
                }
            }
            triangleOwners.push_back(owner);
        }

        for (std::uint32_t visibleMask = 1u;
             visibleMask < fullMask;
             ++visibleMask) {
            auto& filteredIndices = maskSet.indices[visibleMask];
            filteredIndices.reserve(originalGeometry.indexCount);
            for (std::size_t triangleIndex = 0u;
                 triangleIndex < triangleOwners.size();
                 ++triangleIndex) {
                const std::uint8_t owner =
                    triangleOwners[triangleIndex];
                if (owner != 0u &&
                    (visibleMask &
                     (1u << static_cast<std::uint32_t>(owner - 1u))) == 0u) {
                    continue;
                }
                const std::size_t sourceIndex = triangleIndex * 3u;
                filteredIndices.insert(
                    filteredIndices.end(),
                    originalGeometry.indices + sourceIndex,
                    originalGeometry.indices + sourceIndex + 3u);
            }
            if (filteredIndices.empty()) {
                continue;
            }
            const std::string geometryCacheKey =
                originalGeometry.geometryCacheKey +
                ":encounter-cluster-mask:" +
                std::string(cacheRole) + ":" +
                std::to_string(visibleMask);
            const auto geometryHandle =
                shared_world_scene::ensureRigidGeometry(
                    layer.scene.registry,
                    &filteredIndices,
                    geometryCacheKey.c_str(),
                    originalGeometry.vertices,
                    originalGeometry.vertexCount,
                    filteredIndices.data(),
                    filteredIndices.size(),
                    originalGeometry.sourceVertices,
                    originalGeometry.sourceVertexCount,
                    originalGeometry.sourceVertexSemanticMask,
                    originalGeometry.sourceMeshIndex,
                    originalGeometry.sourcePolygonGroupIndex);
            maskSet.objects[visibleMask] =
                shared_world_scene::ensureRenderObject(
                    layer.scene.registry,
                    geometryHandle,
                    originalObject.materialHandle,
                    static_cast<shared_world_scene::PipelineVariant>(
                        originalObject.pipelineVariant),
                    originalObject.cookedDrawSlot,
                    true);
        }
    }
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
    engine::render::route1_field_encounter_grass::SourceVariant variant,
    std::size_t jointCount,
    float placementPhaseCycles,
    float windPhaseCycles,
    const std::array<float, 6>& contactBendRadians,
    const std::array<float, 6>& contactCrossRadians) {
    std::vector<float> palette(jointCount * 16u, 0.0f);
    for (std::size_t joint = 0u; joint < jointCount; ++joint) {
        const auto rotation =
            engine::render::route1_field_encounter_grass::
                evaluateWindJointRotation(
                    static_cast<std::uint32_t>(joint),
                    placementPhaseCycles,
                    windPhaseCycles);
        const auto pivotValues =
            engine::render::route1_field_encounter_grass::sourceJointPivot(
                variant,
                static_cast<std::uint32_t>(joint));
        const glm::vec3 pivot{
            pivotValues[0],
            pivotValues[1],
            pivotValues[2]};
        const float contactBend =
            joint < contactBendRadians.size()
            ? contactBendRadians[joint]
            : 0.0f;
        const float contactCross =
            joint < contactCrossRadians.size()
            ? contactCrossRadians[joint]
            : 0.0f;
        const glm::mat4 jointMatrix =
            glm::translate(glm::mat4(1.0f), pivot) *
            glm::rotate(
                glm::mat4(1.0f),
                -rotation.bendRadians + contactBend,
                glm::vec3(0.0f, 0.0f, 1.0f)) *
            glm::rotate(
                glm::mat4(1.0f),
                rotation.crossRadians + contactCross,
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
    const auto variant =
        layer.logicalName == "enc_grass02"
        ? engine::render::route1_field_encounter_grass::SourceVariant::Grass02
        : engine::render::route1_field_encounter_grass::SourceVariant::Grass01;
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
        initializeEncounterGrassJointAnchors(layer, variant);
        initializeEncounterGrassDrawMasks(
            layer,
            layer.sourceDraws,
            layer.sourceDrawMasks,
            variant,
            "visible");
        initializeEncounterGrassDrawMasks(
            layer,
            layer.shadowSourceDraws,
            layer.shadowSourceDrawMasks,
            variant,
            "shadow");
    }

    shared_world_scene::beginWorldSceneFrame(layer.scene.frame);
    shared_world_scene::beginWorldSceneFrame(layer.scene.shadowFrame);
    layer.skinPalettes.resize(layer.placements.size());
    std::uint32_t instanceId = 1u;
    std::size_t visiblePlacementCount = 0u;
    std::size_t visibleClusterCount = 0u;
    const std::size_t responsiveJointCount = std::min(
        layer.source.bones.size(),
        engine::render::route1_field_encounter_grass::
            sourceJointCount(variant));
    for (std::size_t placementIndex = 0u;
         placementIndex < layer.placements.size();
         ++placementIndex) {
        const auto& placement = layer.placements[placementIndex];
        std::size_t placementVisibleClusterCount = 0u;
        std::uint32_t visibleClusterMask = 0u;
        for (std::size_t joint = 1u;
             joint < responsiveJointCount;
             ++joint) {
            if (!placement.suppressedJoints[joint]) {
                ++placementVisibleClusterCount;
                visibleClusterMask |=
                    1u << static_cast<std::uint32_t>(joint - 1u);
            }
        }
        if (placement.suppressed ||
            placementVisibleClusterCount == 0u) {
            continue;
        }
        ++visiblePlacementCount;
        visibleClusterCount += placementVisibleClusterCount;
        const auto nextPalette = encounterGrassSkinPalette(
            variant,
            layer.source.bones.size(),
            placement.phaseCycles,
            windPhaseCycles,
            placement.contactBendRadians,
            placement.contactCrossRadians);
        auto& palette = layer.skinPalettes[placementIndex];
        palette.resize(nextPalette.size());
        std::copy(
            nextPalette.begin(),
            nextPalette.end(),
            palette.begin());
        for (std::size_t drawIndex = 0u;
             drawIndex < layer.sourceDraws.size();
             ++drawIndex) {
            const auto& sourceDraw = layer.sourceDraws[drawIndex];
            if (drawIndex >= layer.sourceDrawMasks.size() ||
                visibleClusterMask >=
                    layer.sourceDrawMasks[drawIndex].objects.size()) {
                continue;
            }
            const auto objectHandle =
                layer.sourceDrawMasks[drawIndex]
                    .objects[visibleClusterMask];
            if (objectHandle.id == 0u) {
                continue;
            }
            const auto composed = toArray(
                glm::make_mat4(
                    placement.modelMatrix.data()) *
                glm::make_mat4(
                    sourceDraw.modelMatrix.data()));
            IRenderBackend::WorldSceneRenderInstanceHandle handle{};
            handle.id = instanceId++;
            shared_world_scene::appendSkinnedInstance(
                layer.scene.frame,
                objectHandle,
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
        for (std::size_t drawIndex = 0u;
             drawIndex < layer.shadowSourceDraws.size();
             ++drawIndex) {
            const auto& sourceDraw =
                layer.shadowSourceDraws[drawIndex];
            if (drawIndex >= layer.shadowSourceDrawMasks.size() ||
                visibleClusterMask >=
                    layer.shadowSourceDrawMasks[drawIndex]
                        .objects.size()) {
                continue;
            }
            const auto objectHandle =
                layer.shadowSourceDrawMasks[drawIndex]
                    .objects[visibleClusterMask];
            if (objectHandle.id == 0u) {
                continue;
            }
            const auto composed = toArray(
                glm::make_mat4(
                    placement.modelMatrix.data()) *
                glm::make_mat4(
                    sourceDraw.modelMatrix.data()));
            IRenderBackend::WorldSceneRenderInstanceHandle handle{};
            handle.id = instanceId++;
            shared_world_scene::appendSkinnedInstance(
                layer.scene.shadowFrame,
                objectHandle,
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
    layer.clusterCount = visibleClusterCount;
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
// Material-19 source ground sits on the nominal 50 cm elevation planes. Keep
// generated tops only 0.02 cm above that decoded plane: enough to win against
// masked source remnants without creating a visible height/lighting step where
// the rebuilt carrier rejoins untouched source lawn.
constexpr float kTerrainTileTopDepthBiasCm = 0.02f;
// Crack-filling lawn carriers sit below both the recovered 0.00 cm canonical
// plane and the 0.02 cm generated plane. They therefore appear only where a
// rounded ledge junction would otherwise expose the backdrop.
constexpr float kTerrainLawnUnderlayDepthCm = -0.02f;
// A convex low-side contact can retain a material-19 triangle whose geometry
// is present but whose decoded field resolves to the atlas void. A carrier
// underneath that triangle cannot repair the resulting black wedge. Put the
// donor-owned repair one hundredth of a centimetre above the rebuilt top so
// it becomes authoritative only inside the rounded contact.
constexpr float kTerrainLawnCornerRepairDepthCm =
    kTerrainTileTopDepthBiasCm + 0.01f;
// Every generated carrier that meets an edited ledge samples the same five-
// centimetre contour lattice. A coarser cliff/fringe lattice can agree at a
// tile's endpoints yet diverge from the 20-segment upper/lower lawn between
// those points, exposing thin backdrop-colored cracks at the wall contact.
constexpr std::uint32_t kTerrainLedgeContourSegments = 20u;
// Rounded turns need enough angular samples to preserve the source-style
// silhouette and advance the tangential mask without a faceted hooked cap.
constexpr std::uint32_t kTerrainLedgeCornerSegments = 8u;
// Mesh 32's four cliff bands were measured from the cooked LGPE scene at
// approximately -3.5, -7.9, -11.3, and -27.3 cm relative to the logical tile
// boundary (foot to crown). The source-authored 25/20/15/0 cm profile therefore
// needs this common inset; without it the rebuilt foot crosses the lower tile
// and the convex corner visibly overhangs its cell.
constexpr float kTerrainLedgeBaseInsetCm = -27.0f;
// The canonical material-13 crown and material-19 lawn both occupy the nominal
// source plane. Generated lawn carries only a +0.02 cm depth-safety bias, so
// leave the fringe crown at the decoded 0.00 cm plane. Their two-centimetre
// horizontal underlap closes raster gaps without covering either sloped row.
constexpr float kTerrainLedgeFringeCrownY = 0.0f;
constexpr std::array<float, 3> kTerrainLedgeFringeRelativeY{
    kTerrainLedgeFringeCrownY,
    -4.96685791f,
    -16.999969482f};
constexpr std::array<float, 3> kTerrainLedgeFringeNormalY{
    0.998535156f, 0.793945313f, 0.67578125f};
constexpr std::array<float, 3> kTerrainLedgeFringeNormalOutward{
    0.053405762f, 0.607421875f, 0.736816406f};
// The native straight ledge advances material-13 UV1.U by approximately
// 0.495 atlas repeats per metre in contour order. Its crown begins at the
// repeat-equivalent phase 0.2841. Starting generated contours at zero with a
// larger scale places the first sloped row in the mask's nearly solid region,
// exposing the pale carrier rather than its intended leafy cutout.
constexpr float kTerrainLedgeFringeMaskUOffset = 0.2841f;
constexpr float kTerrainLedgeFringeMaskUPerCentimetre = 0.00495f;
constexpr std::array<float, 3> kTerrainLedgeFringeMaskV{
    0.993270993f, 0.922996879f, 0.789638996f};
// Mesh 32's one-level source carrier shares its crown position with both the
// material-19 top and material-18 cliff. From there its two lower rows bow
// 11.09 and 22.30 cm toward the foot. These are absolute offsets from the
// logical edge after the cliff profile's common inset; treating the old
// {-15,-9,0} values as absolutes displaced the crown roughly 12 cm forward and
// exposed a doubled green shelf.
constexpr std::array<float, 3> kTerrainLedgeFringeOutwardCm{
    -27.01f, -15.92f, -4.71f};
struct TerrainConcaveCornerPoint {
    float x;
    float z;
};
// One native LGPE inside corner survives at source junction (2800,-400) in
// mesh 35. These are its four cliff rows (foot to crown) and three leafy rows
// (crown to lower carrier), expressed relative to that junction for the
// canonical edge-3 -> edge-2 turn. Reusing the decoded shape avoids both the
// offset-plane spear and an invented radial fan: the source corner is a short,
// asymmetric four-sample handoff whose rows meet the adjoining strips before
// the logical grid vertex.
constexpr std::array<std::array<TerrainConcaveCornerPoint, 4>, 4>
    kTerrainConcaveCliffPoints{{
        {{{0.43701172f, -13.69433594f},
          {-1.31152344f, -7.77539063f},
          {-5.39868164f, -3.32702637f},
          {-10.83105469f, -0.68994141f}}},
        {{{5.38500977f, -13.69433594f},
          {1.86962891f, -5.46313477f},
          {-3.05175781f, -0.21789551f},
          {-10.83105469f, 4.30566406f}}},
        {{{10.38500977f, -13.69433594f},
          {7.42529297f, -2.62451172f},
          {0.10888672f, 4.14208984f},
          {-10.83105469f, 9.30566406f}}},
        {{{25.38500977f, -13.69433594f},
          {18.34863281f, 4.91943359f},
          {7.31103516f, 16.08886719f},
          {-10.83105469f, 24.30566406f}}},
    }};
constexpr std::array<std::array<TerrainConcaveCornerPoint, 4>, 3>
    kTerrainConcaveFringePoints{{
        {{{25.42675781f, -13.04882813f},
          {17.80517578f, 5.32666016f},
          {6.70117188f, 16.43560791f},
          {-11.68408203f, 24.30566406f}}},
        {{{14.10791016f, -17.33496094f},
          {7.33813477f, -2.77148438f},
          {-1.64575195f, 6.41796875f},
          {-16.51367188f, 11.43066406f}}},
        {{{2.44067383f, -22.33544922f},
          {-2.72241211f, -11.39550781f},
          {-9.48754883f, -4.07910156f},
          {-20.55883789f, -1.11914063f}}},
    }};
// The recovered cliff foot is two centimetres inside its logical boundary.
// Rebuilt lower ground follows that same organic contour with this narrow
// underlap. It is deep enough to cover the alpha-tested foot silhouette, while
// remaining far smaller than the broad rectangular overlap that becomes a
// visible green shelf when the ledge is viewed from above.
constexpr float kTerrainLedgeFootSafetyOverlapCm = 1.50f;
// Generated ground already sits only 0.02 cm above the decoded source plane.
// No additional outer-row tuck is needed: the 1.50 cm horizontal underlap
// joins the alpha-tested foot while keeping the whole lawn carrier planar.
constexpr float kTerrainLedgeContactTuckCm = 0.0f;
// Material 18's cliff-foot foliage uses the same recovered dark-green Color0
// control as raised lawn. Fade the adjoining light-lawn control across three
// five-centimetre rows so its brighter material-19 field does not begin as a
// hard line immediately after the alpha-tested leaves.
constexpr float kTerrainLedgeFootColorBlendCm = 15.0f;
// A source-style dark-lawn plateau keeps one constant UV2 selector and
// dark-green Color0 all the way across its cap. Light-lawn ledges keep their
// independently resolved lawn fields; crown geometry must not silently change
// an authored light surface into dark lawn.
// The source material-19 cap and material-13 crown share the same contour.
// Generated carriers need a very small raster-safety underlap beneath the
// alpha-tested crown, but must not reach the fringe's second row: doing so
// turns either lawn style into a broad, visibly separate strip. Two
// centimetres closes sub-pixel gaps while leaving both leafy slopes visible.
constexpr float kTerrainLedgeCrownSafetyOverlapCm = 2.0f;

float terrainLedgeFootColorBlendWeight(
    float localX,
    float localZ,
    std::uint32_t contactMask,
    const std::array<std::array<float, 2>, 4>& endpointWeights) noexcept {
    const std::array<float, 4> contactDistancesCm{
        (1.0f - localZ) * kTerrainTileSizeCm,
        (1.0f - localX) * kTerrainTileSizeCm,
        localZ * kTerrainTileSizeCm,
        localX * kTerrainTileSizeCm};
    const std::array<float, 4> contactPhases{
        localX,
        1.0f - localZ,
        1.0f - localX,
        localZ};
    float contactBlendWeight = 0.0f;
    for (std::size_t edge = 0u; edge < 4u; ++edge) {
        if ((contactMask & (1u << edge)) == 0u) {
            continue;
        }
        const float dropWeight = std::lerp(
            endpointWeights[edge][0],
            endpointWeights[edge][1],
            contactPhases[edge]);
        float blend = std::clamp(
            (kTerrainLedgeFootColorBlendCm -
             contactDistancesCm[edge]) /
                kTerrainLedgeFootColorBlendCm,
            0.0f,
            1.0f);
        blend = blend * blend * (3.0f - 2.0f * blend);
        contactBlendWeight = std::max(
            contactBlendWeight,
            blend * dropWeight);
    }
    return contactBlendWeight;
}

// The recovered LGPE ledge is a densely tessellated contour rather than a
// ruler-straight metre strip. Reuse one deterministic source-scale profile on
// rebuilt edges so the crown and foot keep the same small organic wander.
// The envelope reaches zero with a zero derivative at each logical tile
// corner, allowing straight edges and generated corner arcs to share vertices.
float terrainLedgeContourWobbleCm(float contourDistanceCm) noexcept {
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = 2.0f * kPi;
    float tilePosition = std::fmod(contourDistanceCm, 100.0f);
    if (tilePosition < 0.0f) {
        tilePosition += 100.0f;
    }
    const float phase = tilePosition / 100.0f;
    const float envelope = std::pow(std::sin(kPi * phase), 2.0f);
    const float tileIndex = std::floor(contourDistanceCm / 100.0f);
    const float seed = std::sin(tileIndex * 1.618033989f + 0.731f);
    return envelope * (
        3.2f * std::sin(kTwoPi * phase + seed * 0.75f) +
        1.35f * std::sin(2.0f * kTwoPi * phase + 1.17f - seed));
}
// Source ground triangles do not terminate on exact metre boundaries. Let an
// authored top cross a matching source edge by one centimetre so the two
// surfaces overlap instead of exposing the backdrop through their different
// triangulations and sub-centimetre height fields.
constexpr float kTerrainSourceSeamOverlapCm = 1.0f;

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
        layout.benchGapCells <= 64u &&
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
                engine::render::route1_field_encounter_grass::
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
    const std::vector<LayoutObject>& objects,
    std::string_view sceneId =
        route1_scene_variants::kRoute1.sceneId) {
    AuthoredSceneDocument document{
        .sceneId = std::string(sceneId),
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
                    .visualVariant = authored.visualVariant,
                    .sourceReference = authored.sourceReference
                        ? std::optional<engine::assets::phlosion::
                              TerrainTileSourceReference>{
                              engine::assets::phlosion::
                                  TerrainTileSourceReference{
                                  .gridX =
                                      (*authored.sourceReference)[0],
                                  .gridZ =
                                      (*authored.sourceReference)[1]}}
                        : std::nullopt,
                    .receivesProjectedShadow =
                        authored.receivesProjectedShadow,
                    .normalizeSourceTint =
                        authored.normalizeSourceTint,
                    .suppressOverlappingVegetation =
                        authored.suppressOverlappingVegetation}});
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
    if (!route1_scene_variants::editable(document.sceneId) ||
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
                    .sourceReference = tile.sourceReference
                        ? std::optional<std::array<std::int32_t, 2>>{
                              std::array<std::int32_t, 2>{
                                  tile.sourceReference->gridX,
                                  tile.sourceReference->gridZ}}
                        : std::nullopt,
                    .receivesProjectedShadow =
                        tile.receivesProjectedShadow,
                    .normalizeSourceTint =
                        tile.normalizeSourceTint,
                    .suppressOverlappingVegetation =
                        tile.suppressOverlappingVegetation,
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

std::array<float, 4> route1SignRampDirtColor(
    float normalizedHeight,
    float normalizedCrossRamp) noexcept {
    constexpr std::array<float, 4> lowEdge{
        0.905882359f, 0.815686285f, 0.631372571f, 0.800000012f};
    constexpr std::array<float, 4> lowCenter{
        0.984313726f, 0.882352948f, 0.686274529f, 0.800000012f};
    constexpr std::array<float, 4> high{
        1.0f, 1.0f, 1.0f, 0.749019623f};
    const float heightWeight = std::clamp(normalizedHeight, 0.0f, 1.0f);
    // The source triangles place one brighter Color0 control at the ramp's
    // centre (x=1800 cm) and the darker controls about 50 cm to either side.
    // Reconstruct their linear interpolation in normalized tile space.
    const float crossRamp =
        std::clamp(normalizedCrossRamp, 0.0f, 1.0f);
    const float centerWeight =
        1.0f - std::abs(crossRamp * 2.0f - 1.0f);
    std::array<float, 4> output{};
    for (std::size_t channel = 0u; channel < output.size(); ++channel) {
        const float low = std::lerp(
            lowEdge[channel], lowCenter[channel], centerWeight);
        output[channel] = std::lerp(low, high[channel], heightWeight);
    }
    return output;
}

bool isRoute1EnvironmentProfile(std::string_view profileId) {
    return profileId == "route1_environment_road001_00" ||
           profileId == "lgpe_route1_road001_00";
}

bool route1EnvironmentProfilesCompatible(
    std::string_view left,
    std::string_view right) {
    return left == right ||
           (isRoute1EnvironmentProfile(left) &&
            isRoute1EnvironmentProfile(right));
}

std::array<float, 4> route1CleanFlatDirtColor() noexcept {
    // Material-19 vertices on canonical clean level-2 Route 1 soil contain
    // only two controls. This warm value is the 47/55 modal control; the
    // other eight vertices are white. Authored paths must not inherit the
    // blue/green Color0 paint underneath the route's former encounter grass.
    return {
        0.905882359f,
        0.815686285f,
        0.631372571f,
        1.0f};
}

std::array<float, 4> route1CleanLightLawnColor() noexcept {
    // Neutral level-2 Route 1 lawn uses the exact white Color0 control.
    // Blue/green controls around an encounter-grass footprint belong to the
    // source patch's pre-lighting and must not survive when that patch is
    // removed from an authored layout.
    return {1.0f, 1.0f, 1.0f, 1.0f};
}

std::array<std::int32_t, 2> route1EncounterGrassCoreTerrainCell(
    const std::array<float, 3>& sourceTranslationCm,
    const std::array<std::int32_t, 2>& localCell) noexcept {
    return {
        static_cast<std::int32_t>(std::floor(
            (sourceTranslationCm[0] +
             static_cast<float>(localCell[0]) * kTerrainTileSizeCm) /
            kTerrainTileSizeCm)),
        static_cast<std::int32_t>(std::floor(
            (sourceTranslationCm[2] +
             static_cast<float>(localCell[1]) * kTerrainTileSizeCm) /
            kTerrainTileSizeCm))};
}

std::array<std::array<std::int32_t, 2>, 9>
route1EncounterGrassTintFootprintOffsets() noexcept {
    // The composition manifest's accepted expansion is the unique eight-
    // neighbor ring around every collision-core cell. Ground-paint cleanup
    // must use the same footprint, including its four diagonal corners.
    return {{
        {-1, -1}, {0, -1}, {1, -1},
        {-1,  0}, {0,  0}, {1,  0},
        {-1,  1}, {0,  1}, {1,  1},
    }};
}

std::array<float, 4> route1SignRampAdjacentDirtColor(
    const std::array<float, 4>& normalDirtColor,
    float rampBoundaryWeight,
    float normalizedCrossRamp,
    bool highSide) noexcept {
    const auto boundary = route1SignRampDirtColor(
        highSide ? 1.0f : 0.0f,
        normalizedCrossRamp);
    const float weight = std::clamp(rampBoundaryWeight, 0.0f, 1.0f);
    std::array<float, 4> output{};
    for (std::size_t channel = 0u; channel < output.size(); ++channel) {
        output[channel] = std::lerp(
            normalDirtColor[channel], boundary[channel], weight);
    }
    return output;
}

std::array<float, 4> route1DirtAdjacentLawnColor(
    const std::array<float, 4>& dirtColor,
    const std::array<float, 4>& lawnColor,
    float lawnBoundaryWeight) noexcept {
    const float weight = std::clamp(
        lawnBoundaryWeight, 0.0f, 1.0f);
    std::array<float, 4> output{};
    for (std::size_t channel = 0u;
         channel < output.size();
         ++channel) {
        output[channel] = std::lerp(
            dirtColor[channel], lawnColor[channel], weight);
    }
    return output;
}

float route1DirtTransitionUv2V(
    float distanceFromLawnCm) noexcept {
    constexpr float cleanLawnV = 0.928709f;
    constexpr float boundaryLawnV = 0.932880402f;
    constexpr float boundaryDirtV = 0.991155148f;
    constexpr float cleanJoinWidthCm = 5.0f;
    constexpr float completeRibbonWidthCm = 30.0f;
    const float distance = std::clamp(
        distanceFromLawnCm, 0.0f, completeRibbonWidthCm);
    if (distance <= cleanJoinWidthCm) {
        return std::lerp(
            cleanLawnV,
            boundaryLawnV,
            distance / cleanJoinWidthCm);
    }
    return std::lerp(
        boundaryLawnV,
        boundaryDirtV,
        (distance - cleanJoinWidthCm) /
            (completeRibbonWidthCm - cleanJoinWidthCm));
}

TerrainSharedEdgeProfile route1TerrainSharedEdgeProfile(
    const TerrainTileState& tile,
    const TerrainTileState* neighbor,
    std::size_t edge) noexcept {
    // These are the two world-grid vertices at each cell edge, ordered to
    // match the clockwise cliff/fringe carrier winding.
    constexpr std::array<
        std::array<std::array<std::int32_t, 2>, 2>,
        4> endpointOffsets{{
        {{{0, 1}, {1, 1}}},
        {{{1, 1}, {1, 0}}},
        {{{1, 0}, {0, 0}}},
        {{{0, 0}, {0, 1}}},
    }};
    TerrainSharedEdgeProfile profile;
    if (edge >= endpointOffsets.size()) {
        return profile;
    }
    const auto cornerLevel = [](
                                 const TerrainTileState& sample,
                                 std::int32_t worldGridX,
                                 std::int32_t worldGridZ) {
        const std::int32_t localX = worldGridX - sample.gridX;
        const std::int32_t localZ = worldGridZ - sample.gridZ;
        const bool high =
            (sample.shape == "ramp_east" && localX == 1) ||
            (sample.shape == "ramp_west" && localX == 0) ||
            (sample.shape == "ramp_north" && localZ == 1) ||
            (sample.shape == "ramp_south" && localZ == 0);
        return sample.elevationLevel + (high ? 1 : 0);
    };
    for (std::size_t endpoint = 0u; endpoint < 2u; ++endpoint) {
        const std::int32_t worldGridX =
            tile.gridX + endpointOffsets[edge][endpoint][0];
        const std::int32_t worldGridZ =
            tile.gridZ + endpointOffsets[edge][endpoint][1];
        profile.tileLevels[endpoint] = cornerLevel(
            tile, worldGridX, worldGridZ);
        profile.neighborLevels[endpoint] = neighbor
            ? cornerLevel(*neighbor, worldGridX, worldGridZ)
            : 0;
    }
    return profile;
}

bool route1TerrainNeedsSourceSeamOverlap(
    const TerrainTileState& tile,
    const TerrainTileState* neighbor,
    std::size_t edge,
    bool tileUsesGeneratedCap) noexcept {
    if (edge >= 4u ||
        (!tile.authored && !tileUsesGeneratedCap) ||
        tile.surface == "empty" ||
        tile.sourceReference ||
        !neighbor ||
        neighbor->authored ||
        !neighbor->sourceOccupied ||
        neighbor->surface == "empty" ||
        neighbor->sourceReference ||
        neighbor->cleanSuppressedEncounterGrassTint ||
        !neighbor->surface.ends_with("lawn")) {
        return false;
    }
    const auto profile = route1TerrainSharedEdgeProfile(
        tile, neighbor, edge);
    return profile.tileLevels == profile.neighborLevels;
}

bool route1TerrainUsesExactSourceSurfaceOverride(
    const TerrainTileState& tile,
    const std::vector<TerrainTileState>& activeTiles,
    const std::vector<TerrainTileState>& sourceTiles) noexcept {
    // These controls affect runtime ownership or rendering, not the imported
    // material-19 surface itself. Keep the original triangles authoritative
    // and, when necessary, resubmit them under the requested shadow policy.
    // Encounter-grass edits are excluded because their source Color0 field is
    // deliberately normalized after the grass carrier is removed/restored.
    const bool sourceEquivalent =
        tile.authored && tile.sourceOccupied &&
        !tile.sourceReference &&
        tile.elevationLevel == tile.sourceElevationLevel &&
        tile.shape == tile.sourceShape &&
        tile.surface == tile.sourceSurface &&
        tile.visualVariant == "auto" &&
        !tile.normalizeSourceTint &&
        !tile.cleanSuppressedEncounterGrassTint &&
        !tile.reason.starts_with("terrain_encounter_grass_");
    if (!sourceEquivalent) {
        return false;
    }
    constexpr std::array<std::array<std::int32_t, 2>, 4>
        directions{{
            {0, 1},
            {1, 0},
            {0, -1},
            {-1, 0},
        }};
    const auto findAt = [](const auto& tiles,
                           std::int32_t gridX,
                           std::int32_t gridZ) {
        const auto found = std::find_if(
            tiles.begin(),
            tiles.end(),
            [&](const TerrainTileState& candidate) {
                return candidate.gridX == gridX &&
                    candidate.gridZ == gridZ;
            });
        return found == tiles.end() ? nullptr : &*found;
    };
    const auto hasSurface = [](const TerrainTileState* candidate) {
        return candidate && candidate->surface != "empty" &&
            (candidate->sourceOccupied || candidate->authored);
    };
    const auto* sourceTile = findAt(
        sourceTiles, tile.gridX, tile.gridZ);
    if (!sourceTile) {
        return false;
    }
    for (std::size_t edge = 0u;
         edge < directions.size();
         ++edge) {
        const auto direction = directions[edge];
        const auto* activeNeighbor = findAt(
            activeTiles,
            tile.gridX + direction[0],
            tile.gridZ + direction[1]);
        const auto* sourceNeighbor = findAt(
            sourceTiles,
            tile.gridX + direction[0],
            tile.gridZ + direction[1]);
        if (hasSurface(activeNeighbor) != hasSurface(sourceNeighbor)) {
            return false;
        }
        if (!hasSurface(activeNeighbor)) {
            continue;
        }
        const auto activeProfile = route1TerrainSharedEdgeProfile(
            tile, activeNeighbor, edge);
        const auto sourceProfile = route1TerrainSharedEdgeProfile(
            *sourceTile, sourceNeighbor, edge);
        if (activeNeighbor->sourceReference ||
            activeNeighbor->cleanSuppressedEncounterGrassTint ||
            activeNeighbor->surface != sourceNeighbor->surface ||
            activeProfile.tileLevels != sourceProfile.tileLevels ||
            activeProfile.neighborLevels !=
                sourceProfile.neighborLevels) {
            return false;
        }
    }
    return true;
}

float route1TerrainProfileHeightCm(
    const TerrainTileState& tile,
    float localX,
    float localZ) noexcept {
    localX = std::clamp(localX, 0.0f, 1.0f);
    localZ = std::clamp(localZ, 0.0f, 1.0f);
    float rampHeight = 0.0f;
    if (tile.shape == "ramp_north") {
        rampHeight = localZ * kTerrainElevationStepCm;
    } else if (tile.shape == "ramp_east") {
        rampHeight = localX * kTerrainElevationStepCm;
    } else if (tile.shape == "ramp_south") {
        rampHeight = (1.0f - localZ) *
            kTerrainElevationStepCm;
    } else if (tile.shape == "ramp_west") {
        rampHeight = (1.0f - localX) *
            kTerrainElevationStepCm;
    }
    return static_cast<float>(tile.elevationLevel) *
            kTerrainElevationStepCm +
        rampHeight;
}

bool route1TerrainSourceBoundaryInvalidated(
    const TerrainTileState& editedTile,
    const TerrainTileState* editedNeighbor,
    const TerrainTileState& sourceTile,
    const TerrainTileState* sourceNeighbor,
    std::size_t edge) noexcept {
    const auto editedProfile = route1TerrainSharedEdgeProfile(
        editedTile, editedNeighbor, edge);
    const auto sourceProfile = route1TerrainSharedEdgeProfile(
        sourceTile, sourceNeighbor, edge);
    return editedProfile.tileLevels != sourceProfile.tileLevels ||
        editedProfile.neighborLevels != sourceProfile.neighborLevels;
}

bool route1TerrainSourcePatchNeedsBoundarySpill(
    const TerrainTileState& tile,
    const TerrainTileState* neighbor,
    std::size_t edge) noexcept {
    const auto profile = route1TerrainSharedEdgeProfile(
        tile,
        neighbor,
        edge);
    return profile.tileLevels != profile.neighborLevels;
}

bool route1TerrainCleanupCarrierEntersNeighbor(
    const std::array<std::array<float, 3>, 3>& positionsCm,
    const std::array<std::int32_t, 2>& ownerCell,
    const std::array<std::int32_t, 2>& neighboringCell) noexcept {
    const std::int32_t deltaX =
        neighboringCell[0] - ownerCell[0];
    const std::int32_t deltaZ =
        neighboringCell[1] - ownerCell[1];
    if (std::abs(deltaX) + std::abs(deltaZ) != 1) {
        return false;
    }
    constexpr float penetrationToleranceCm = 0.01f;
    if (deltaX != 0) {
        const float boundaryX = static_cast<float>(
            std::max(ownerCell[0], neighboringCell[0])) *
            kTerrainTileSizeCm;
        return std::any_of(
            positionsCm.begin(),
            positionsCm.end(),
            [&](const auto& position) {
                return deltaX > 0
                    ? position[0] >
                          boundaryX + penetrationToleranceCm
                    : position[0] <
                          boundaryX - penetrationToleranceCm;
            });
    }
    const float boundaryZ = static_cast<float>(
        std::max(ownerCell[1], neighboringCell[1])) *
        kTerrainTileSizeCm;
    return std::any_of(
        positionsCm.begin(),
        positionsCm.end(),
        [&](const auto& position) {
            return deltaZ > 0
                ? position[2] >
                      boundaryZ + penetrationToleranceCm
                : position[2] <
                      boundaryZ - penetrationToleranceCm;
        });
}

bool route1TerrainCleanupCarrierWithinBoundaryBand(
    const std::array<std::array<float, 3>, 3>& positionsCm,
    const std::array<std::int32_t, 2>& ownerCell,
    const std::array<std::int32_t, 2>& neighboringCell) noexcept {
    const std::int32_t deltaX =
        neighboringCell[0] - ownerCell[0];
    const std::int32_t deltaZ =
        neighboringCell[1] - ownerCell[1];
    if (std::abs(deltaX) + std::abs(deltaZ) != 1) {
        return false;
    }
    // The decoded cliff profile bows at most 25 cm away from its owning
    // metre edge. A small source-coordinate tolerance retains its paired
    // triangle without claiming unrelated foliage deeper in the neighbor.
    constexpr float boundaryBandCm = 25.5f;
    constexpr float boundaryToleranceCm = 0.01f;
    const auto insideBand = [&](float coordinate, float boundary,
                                std::int32_t direction) {
        const float signedDistance =
            static_cast<float>(direction) * (coordinate - boundary);
        return signedDistance >= -boundaryToleranceCm &&
            signedDistance <= boundaryBandCm;
    };
    if (deltaX != 0) {
        const float boundaryX = static_cast<float>(
            std::max(ownerCell[0], neighboringCell[0])) *
            kTerrainTileSizeCm;
        return std::all_of(
            positionsCm.begin(),
            positionsCm.end(),
            [&](const auto& position) {
                return insideBand(position[0], boundaryX, deltaX);
            });
    }
    const float boundaryZ = static_cast<float>(
        std::max(ownerCell[1], neighboringCell[1])) *
        kTerrainTileSizeCm;
    return std::all_of(
        positionsCm.begin(),
        positionsCm.end(),
        [&](const auto& position) {
            return insideBand(position[2], boundaryZ, deltaZ);
        });
}

bool route1TerrainCleanupCarrierIntersectsBoundaryBand(
    const std::array<std::array<float, 3>, 3>& positionsCm,
    const std::array<std::int32_t, 2>& ownerCell,
    const std::array<std::int32_t, 2>& neighboringCell) noexcept {
    const std::int32_t deltaX =
        neighboringCell[0] - ownerCell[0];
    const std::int32_t deltaZ =
        neighboringCell[1] - ownerCell[1];
    if (std::abs(deltaX) + std::abs(deltaZ) != 1) {
        return false;
    }
    constexpr float boundaryBandCm = 25.5f;
    constexpr float boundaryToleranceCm = 0.01f;
    float minimumDistance = std::numeric_limits<float>::max();
    float maximumDistance = std::numeric_limits<float>::lowest();
    if (deltaX != 0) {
        const float boundaryX = static_cast<float>(
            std::max(ownerCell[0], neighboringCell[0])) *
            kTerrainTileSizeCm;
        for (const auto& position : positionsCm) {
            const float distance = static_cast<float>(deltaX) *
                (position[0] - boundaryX);
            minimumDistance = std::min(minimumDistance, distance);
            maximumDistance = std::max(maximumDistance, distance);
        }
    } else {
        const float boundaryZ = static_cast<float>(
            std::max(ownerCell[1], neighboringCell[1])) *
            kTerrainTileSizeCm;
        for (const auto& position : positionsCm) {
            const float distance = static_cast<float>(deltaZ) *
                (position[2] - boundaryZ);
            minimumDistance = std::min(minimumDistance, distance);
            maximumDistance = std::max(maximumDistance, distance);
        }
    }
    return maximumDistance >= -boundaryToleranceCm &&
        minimumDistance <= boundaryBandCm;
}

bool route1TerrainCleanupCarrierWithinRebuiltBoundaryCorridor(
    const std::array<std::array<float, 3>, 3>& positionsCm,
    const std::array<std::int32_t, 2>& ownerCell,
    const std::array<std::int32_t, 2>& neighboringCell) noexcept {
    const std::int32_t deltaX =
        neighboringCell[0] - ownerCell[0];
    const std::int32_t deltaZ =
        neighboringCell[1] - ownerCell[1];
    if (std::abs(deltaX) + std::abs(deltaZ) != 1) {
        return false;
    }
    // Source cliff/fringe carriers bow toward both sides of their logical
    // metre edge. The convex crown recovered from LGPE reaches about 31 cm
    // into the raised owner cell, so the earlier outward-only 25.5 cm test
    // left one broad source triangle crossing an otherwise rebuilt corner.
    // Require the complete triangle to stay in this narrow two-sided
    // corridor; unrelated terrain deeper in either cell remains canonical.
    constexpr float boundaryCorridorCm = 32.5f;
    constexpr float boundaryToleranceCm = 0.01f;
    const float boundary = deltaX != 0
        ? static_cast<float>(
              std::max(ownerCell[0], neighboringCell[0])) *
              kTerrainTileSizeCm
        : static_cast<float>(
              std::max(ownerCell[1], neighboringCell[1])) *
              kTerrainTileSizeCm;
    return std::all_of(
        positionsCm.begin(),
        positionsCm.end(),
        [&](const auto& position) {
            const float coordinate = deltaX != 0
                ? position[0]
                : position[2];
            return std::abs(coordinate - boundary) <=
                boundaryCorridorCm + boundaryToleranceCm;
        });
}

bool route1TerrainCleanupCarrierAtOrBelowBoundaryCeiling(
    const std::array<std::array<float, 3>, 3>& positionsCm,
    float boundaryCeilingCm) noexcept {
    constexpr float heightToleranceCm = 0.01f;
    return std::all_of(
        positionsCm.begin(),
        positionsCm.end(),
        [&](const auto& position) {
            return position[1] <=
                boundaryCeilingCm + heightToleranceCm;
        });
}

void route1TerrainClampCleanupCarrierToOwnedCell(
    std::array<std::array<float, 3>, 3>& positionsCm,
    const std::array<std::int32_t, 2>& ownerCell,
    const std::array<std::int32_t, 2>& neighboringCell) noexcept {
    const std::int32_t deltaX =
        neighboringCell[0] - ownerCell[0];
    const std::int32_t deltaZ =
        neighboringCell[1] - ownerCell[1];
    if (std::abs(deltaX) + std::abs(deltaZ) != 1) {
        return;
    }
    if (deltaX != 0) {
        const float boundaryX = static_cast<float>(
            std::max(ownerCell[0], neighboringCell[0])) *
            kTerrainTileSizeCm;
        for (auto& position : positionsCm) {
            position[0] = deltaX > 0
                ? std::min(position[0], boundaryX)
                : std::max(position[0], boundaryX);
        }
        return;
    }
    const float boundaryZ = static_cast<float>(
        std::max(ownerCell[1], neighboringCell[1])) *
        kTerrainTileSizeCm;
    for (auto& position : positionsCm) {
        position[2] = deltaZ > 0
            ? std::min(position[2], boundaryZ)
            : std::max(position[2], boundaryZ);
    }
}

bool route1TerrainMaskUsesAnyVertexOwnership(
    bool exactSourceReference) noexcept {
    // Ordinary authored replacements own every carrier touching the edited
    // cell so no source slivers survive. Exact source transplants instead
    // share their perimeter with canonical neighboring cells; assigning
    // those triangles by centroid keeps the canonical corner/fringe carrier
    // without also retaining the donor spill outside the transplant.
    return !exactSourceReference;
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
    std::vector<published_environment_scene::PolygonGroupStorage>
        canonicalTreePolygonStorage;
    std::vector<CanonicalTerrainAssembly>
        canonicalTerrainAssemblies;
    std::vector<published_environment_scene::PolygonGroupStorage>
        canonicalTerrainPolygonStorage;
    BoardGroundPatchPrototype boardGroundPatch;
    TerrainTilePrototypeSet terrainTilePrototypes;
    std::vector<TerrainTileState> sourceTerrainTiles;
    std::vector<TerrainTileState> terrainTiles;
    route1_terrain_ledges::Resolution terrainLedgeResolution;
    route1_terrain_contours::Assembly terrainContourAssembly;
    route1_terrain_patch_v2::Plan terrainPatchV2Plan;
    route1_terrain_seams::Resolution terrainSeamResolution;
    bool terrainPatchV2PreviewEnabled = false;
    std::vector<SourceTerrainTriangle> sourceTerrainTriangles;
    std::map<
        std::pair<std::int32_t, std::int32_t>,
        std::vector<std::size_t>>
        sourceTerrainTrianglesByCell;
    std::array<std::vector<SourceTerrainFringeMaterialSegment>, 3>
        sourceTerrainFringeMaterialSegments;
    std::vector<SourceTerrainCliffGeometrySegment>
        sourceTerrainCliffGeometrySegments;
    const game::assets::published_environment::TextureSubresource*
        sourceTerrainGroundMask = nullptr;
    std::vector<TerrainMaskGeometry> terrainMaskGeometries;
    std::set<std::pair<std::int32_t, std::int32_t>>
        terrainMaskCells;
    std::set<std::pair<std::int32_t, std::int32_t>>
        terrainCleanupCells;
    std::set<std::pair<std::int32_t, std::int32_t>>
        terrainSourceReferenceCells;
    std::set<std::pair<GridCell, GridCell>>
        terrainInvalidatedSourceCleanupBoundaries;
    std::uint32_t terrainMaskRevision = 0u;
    std::vector<EncounterGrassRecord> encounterGrassRecords;
    std::size_t canonicalEncounterGrassRecordCount = 0u;
    std::vector<EncounterGrassLayer> encounterGrass;
    std::vector<PlacedVegetationLayer> placedVegetation;
    route1_projected_shadow::Atlas projectedShadowAtlas;
    std::vector<PreparedScene*> scenes;
    std::vector<SceneMaterialTemplates> materialTemplates;
    std::vector<LayoutObject> layoutObjects;
    glm::mat4 worldFromSource{1.0f};
    glm::mat4 sourceFromWorld{1.0f};
    LightProjectionRows cloudProjectionRows;
    std::string materialFilter;
    float windPhaseCycles = kInitialWindPhaseCycles;
    float lastInteractionSimulationSeconds =
        std::numeric_limits<float>::quiet_NaN();
    std::vector<EncounterGrassInteractor> encounterGrassInteractors;

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
        std::uint32_t dirtConnectionMask,
        const DirtTransitionUvField& transitionUv);

    IRenderBackend::WorldSceneRenderObjectHandle
    ensureAuthoredTerrainSurfaceObject(
        bool receivesProjectedShadow);

    std::vector<IRenderBackend::WorldSceneRenderObjectHandle>
    ensureTerrainSourceReferenceObjects(
        const std::set<GridCell>& sourceCells,
        const std::set<GridCell>& blockedSpillCells,
        const std::vector<std::pair<GridCell, GridCell>>&
            requiredSpillBoundaries);

    std::vector<IRenderBackend::WorldSceneRenderObjectHandle>
    ensureTerrainExactSourceSurfaceObjects(
        const std::set<GridCell>& sourceCells,
        bool receivesProjectedShadow);

    IRenderBackend::WorldSceneRenderObjectHandle
    ensureTerrainCliffObject(
        const TerrainTileState& tile,
        std::size_t edge,
        const TerrainSharedEdgeProfile& edgeProfile,
        float contourStartCm,
        float materialContourStartCm,
        route1_terrain_ledges::Join startJoin,
        route1_terrain_ledges::Join endJoin);

    IRenderBackend::WorldSceneRenderObjectHandle
    ensureTerrainFringeObject(
        const TerrainTileState& tile,
        std::size_t edge,
        const TerrainSharedEdgeProfile& edgeProfile,
        float contourStartCm,
        float materialContourStartCm,
        route1_terrain_ledges::Join startJoin,
        route1_terrain_ledges::Join endJoin);

    IRenderBackend::WorldSceneRenderObjectHandle
    ensureTerrainRegionalCliffContourObject(
        std::uint32_t contourIndex);

    IRenderBackend::WorldSceneRenderObjectHandle
    ensureTerrainRegionalFringeContourObject(
        std::uint32_t contourIndex);

    IRenderBackend::WorldSceneRenderObjectHandle
    ensureTerrainCliffCornerObject(
        const TerrainTileState& tile,
        std::size_t corner,
        std::int32_t levelDifference,
        float materialContourCm);

    IRenderBackend::WorldSceneRenderObjectHandle
    ensureTerrainFringeCornerObject(
        const TerrainTileState& tile,
        std::size_t corner,
        std::int32_t levelDifference,
        float materialContourCm);

    IRenderBackend::WorldSceneRenderObjectHandle
    ensureTerrainConcaveCliffCornerObject(
        const route1_terrain_ledges::RebuiltEdge& incoming,
        const route1_terrain_ledges::RebuiltEdge& outgoing);

    IRenderBackend::WorldSceneRenderObjectHandle
    ensureTerrainConcaveFringeCornerObject(
        const route1_terrain_ledges::RebuiltEdge& incoming,
        const route1_terrain_ledges::RebuiltEdge& outgoing);

    IRenderBackend::WorldSceneRenderObjectHandle
    ensureTerrainConcaveCrownObject(
        const route1_terrain_ledges::RebuiltEdge& incoming,
        const route1_terrain_ledges::RebuiltEdge& outgoing);

    IRenderBackend::WorldSceneRenderObjectHandle
    ensureTerrainLawnCornerUnderlayObject(
        float sourceCornerX,
        float sourceCornerZ,
        std::int32_t elevationLevel,
        bool receivesProjectedShadow);

    IRenderBackend::WorldSceneRenderObjectHandle
    ensureTerrainConvexLawnCornerUnderlayObject(
        const TerrainTileState& donorTile,
        float sourceCornerX,
        float sourceCornerZ,
        float quadrantSignX,
        float quadrantSignZ);

    IRenderBackend::WorldSceneRenderObjectHandle
    ensureTerrainConvexLawnCornerPocketRepairObject(
        const TerrainTileState& donorTile,
        float sourceCornerX,
        float sourceCornerZ,
        float quadrantSignX,
        float quadrantSignZ,
        std::size_t half);

    IRenderBackend::WorldSceneRenderObjectHandle
    ensureTerrainConvexLawnCapUnderlayObject(
        const TerrainTileState& tile,
        std::size_t corner);

    IRenderBackend::WorldSceneRenderObjectHandle
    ensureTerrainLedgeCrownUnderlayObject(
        const TerrainTileState& tile,
        std::size_t edge);

    IRenderBackend::WorldSceneRenderObjectHandle
    ensureTerrainLedgeCrownContourUnderlayObject(
        const TerrainTileState& tile,
        std::size_t edge);

    IRenderBackend::WorldSceneRenderObjectHandle
    ensureTerrainRegionalCrownContourUnderlayObject(
        std::uint32_t contourIndex);

    IRenderBackend::WorldSceneRenderObjectHandle
    ensureTerrainConvexCrownContourUnderlayObject(
        const TerrainTileState& tile,
        std::size_t corner);

    IRenderBackend::WorldSceneRenderObjectHandle
    ensureTerrainConvexFootContourUnderlayObject(
        const TerrainTileState& tile,
        const TerrainTileState* donorTile,
        std::size_t corner,
        std::int32_t lowLevel);

    IRenderBackend::WorldSceneRenderObjectHandle
    ensureTerrainSourceHandoffUnderlayObject(
        const TerrainTileState& tile,
        const TerrainTileState& sourceNeighbor,
        std::size_t edge);

    IRenderBackend::WorldSceneRenderObjectHandle
    ensureTerrainLawnPatchObject(
        const std::string& key,
        float sourceOriginX,
        float sourceOriginZ,
        std::int32_t elevationLevel,
        bool receivesProjectedShadow,
        const TerrainTileState* donorTile,
        const std::vector<glm::vec2>& boundary,
        bool forceRaisedCrownField,
        float localDepthCm,
        const std::vector<std::uint32_t>* exactIndices = nullptr,
        const std::vector<glm::vec3>* exactNormals = nullptr,
        const std::vector<float>* exactContactWeights = nullptr);

    bool sampleSourceTerrainSurface(
        const TerrainTileState& tile,
        float localX,
        float localZ,
        SourceTerrainSurfaceSample& out) const;

    bool sampleWorldTerrainHeight(
        float worldX,
        float worldZ,
        float& outWorldY) const noexcept;

    bool sampleTargetTerrainColor(
        std::string_view surface,
        std::int32_t elevationLevel,
        float worldGridX,
        float worldGridZ,
        glm::vec4& outColor) const;

    bool sampleTargetTerrainUv2(
        std::string_view surface,
        std::int32_t elevationLevel,
        float worldGridX,
        float worldGridZ,
        glm::vec2& outUv2) const;

    bool sampleNormalizedSourceTintColor(
        const TerrainTileState& tile,
        float localX,
        float localZ,
        glm::vec4& outColor,
        float* outBoundaryWeight = nullptr) const;

    bool sampleSourceTerrainGroundMaskAlpha(
        const glm::vec2& sourceUv2,
        float& outAlpha) const;

    bool sampleSourceTerrainFringeMaterial(
        const glm::vec3 &sourcePositionCm,
        const glm::vec2 &sourceTangent,
        std::size_t row,
        float &outUv1U,
        glm::vec4 &outColor,
        glm::vec3 *outPositionCm = nullptr,
        glm::vec3 *outNormal = nullptr) const;

    bool sampleSourceTerrainCliffGeometry(
        const glm::vec3 &sourcePositionCm,
        const glm::vec2 &sourceTangent,
        glm::vec3 &outPositionCm,
        glm::vec3 &outNormal) const;

    void appendAuthoredTerrainTiles(
        IRenderBackend::WorldSceneFrame& frame);

    bool splitCanonicalTreeInstances(
        std::string* outError) {
        canonicalTreeInstances.clear();
        canonicalTreePolygonStorage.clear();

        using TreePartition =
            route1_tree_instances::MeshPartition;
        std::map<std::uint32_t, TreePartition>
            partitions;
        std::size_t storageCount = 0u;
        for (const auto& mesh : source.meshes) {
            const std::uint32_t instanceCount =
                route1_tree_instances::
                    expectedInstanceCount(
                        mesh.sourceIndex);
            if (instanceCount == 0u) {
                continue;
            }
            TreePartition partition;
            if (!route1_tree_instances::
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
                    if (!route1_tree_instances::
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
            route1_terrain_assemblies;
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
            const bool sourceReferenceExists =
                !tile.sourceReference || std::any_of(
                    sourceTerrainTiles.begin(),
                    sourceTerrainTiles.end(),
                    [&](const TerrainTileState& candidate) {
                        return candidate.sourceOccupied &&
                            candidate.gridX ==
                                (*tile.sourceReference)[0] &&
                            candidate.gridZ ==
                                (*tile.sourceReference)[1];
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
                (tile.surface == "dirt_path" &&
                 tile.visualVariant != "auto" &&
                 tile.shape != "flat") ||
                (tile.surface == "empty" &&
                 tile.shape != "flat") ||
                tile.elevationLevel < -128 ||
                tile.elevationLevel > 128 ||
                !sourceCellExists ||
                !sourceReferenceExists ||
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

        std::set<GridCell> suppressedVegetationCells;
        for (const auto& tile : layout.authoredTerrainTiles) {
            if (tile.suppressOverlappingVegetation) {
                suppressedVegetationCells.emplace(
                    tile.gridX, tile.gridZ);
            }
        }

        for (auto& layer : encounterGrass) {
            const auto variant =
                layer.logicalName == "enc_grass02"
                ? engine::render::route1_field_encounter_grass::
                      SourceVariant::Grass02
                : engine::render::route1_field_encounter_grass::
                      SourceVariant::Grass01;
            const std::size_t responsiveJointCount = std::min(
                layer.source.bones.size(),
                engine::render::route1_field_encounter_grass::
                    sourceJointCount(variant));
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
                placement.suppressed = record->suppressed;
                placement.suppressedJoints.fill(false);
                if (!record->authored) {
                    for (std::size_t joint = 1u;
                         joint < responsiveJointCount;
                         ++joint) {
                        const auto& anchor =
                            layer.sourceJointAnchors[joint];
                        const glm::vec4 renderedAnchor =
                            glm::make_mat4(
                                placement.modelMatrix.data()) *
                            glm::vec4(
                                anchor[0], anchor[1], anchor[2], 1.0f);
                        const GridCell terrainCell{
                            static_cast<std::int32_t>(std::floor(
                                renderedAnchor.x /
                                kTerrainTileSizeCm)),
                            static_cast<std::int32_t>(std::floor(
                                renderedAnchor.z /
                                kTerrainTileSizeCm))};
                        placement.suppressedJoints[joint] =
                            suppressedVegetationCells.contains(
                                terrainCell);
                    }
                }
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
                route1_tree_instances::
                    expectedInstanceCount(
                        group.sourceMeshIndex) > 0u;
            const bool terrainSourceGroup =
                route1_terrain_assemblies::
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
            stats.encounterGrassClusterCount +=
                static_cast<std::uint32_t>(
                    layer.clusterCount);
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
        stats.terrainContinuousFieldCellCount =
            terrainSeamResolution.continuousFieldCellCount;
        stats.terrainProjectedShadowMismatchEdgeCount =
            terrainSeamResolution.projectedShadowMismatchEdgeCount;
        stats.terrainRebuiltLedgeEdgeCount =
            static_cast<std::uint32_t>(
                terrainLedgeResolution.edges.size());
        stats.terrainRebuiltLedgeContourCount =
            terrainLedgeResolution.contourCount;
        stats.terrainPatchV2RegionCount = static_cast<std::uint32_t>(
            terrainPatchV2Plan.regions.size());
        stats.terrainPatchV2CoreCellCount =
            terrainPatchV2Plan.coreCellCount;
        stats.terrainPatchV2TransitionCellCount =
            terrainPatchV2Plan.transitionCellCount;
        stats.terrainPatchV2BoundaryLoopCount =
            terrainPatchV2Plan.boundaryLoopCount;
        stats.terrainPatchV2BoundaryEdgeCount =
            terrainPatchV2Plan.boundaryEdgeCount;
        stats.terrainPatchV2InvalidBoundaryCount =
            terrainPatchV2Plan.validation.openBoundaryCount +
            terrainPatchV2Plan.validation.duplicateDirectedEdgeCount +
            terrainPatchV2Plan.validation.strandedBoundaryEdgeCount;
    }

    bool rebuildLayoutDependentState(
        std::string* outError) {
        if (!applyLocalDeltas(outError)) {
            return false;
        }
        if (terrainPatchV2PreviewEnabled &&
            !terrainContourAssembly.validation.valid) {
            const auto& validation =
                terrainContourAssembly.validation;
            return fail(
                outError,
                "Route 1 Terrain Patch V2 rejected a disconnected ledge contour "
                "(missing edge samples=" +
                    std::to_string(validation.missingEdgeSamples) +
                    ", missing turn partners=" +
                    std::to_string(validation.missingTurnPartners) +
                    ", disconnected turn endpoints=" +
                    std::to_string(
                        validation.disconnectedTurnEndpoints) +
                    ", disconnected carrier rows=" +
                    std::to_string(
                        validation.disconnectedCarrierRows) +
                    ", discontinuous turn normals=" +
                    std::to_string(
                        validation.discontinuousTurnNormals) +
                    ", duplicate turn owners=" +
                    std::to_string(validation.duplicateTurnOwners) +
                    ").");
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
        const float dt = std::isfinite(lastInteractionSimulationSeconds)
            ? std::clamp(
                  simulationSeconds - lastInteractionSimulationSeconds,
                  0.0f,
                  0.05f)
            : 1.0f / 60.0f;
        lastInteractionSimulationSeconds = simulationSeconds;

        struct SourceInteractor {
            glm::vec3 position{};
            glm::vec3 motion{0.0f, 0.0f, 1.0f};
            float strength = 1.0f;
        };
        std::vector<SourceInteractor> sourceInteractors;
        sourceInteractors.reserve(encounterGrassInteractors.size());
        for (const auto& interactor : encounterGrassInteractors) {
            const glm::vec4 sourcePosition =
                sourceFromWorld * glm::vec4(
                    interactor.worldPosition[0],
                    interactor.worldPosition[1],
                    interactor.worldPosition[2],
                    1.0f);
            glm::vec3 sourceMotion = glm::vec3(
                sourceFromWorld * glm::vec4(
                    interactor.worldMotionDirection[0],
                    0.0f,
                    interactor.worldMotionDirection[2],
                    0.0f));
            sourceMotion.y = 0.0f;
            const float motionLength = glm::length(sourceMotion);
            if (motionLength > 0.001f) {
                sourceMotion /= motionLength;
            } else {
                sourceMotion = glm::vec3(0.0f, 0.0f, 1.0f);
            }
            sourceInteractors.push_back({
                .position = glm::vec3(sourcePosition),
                .motion = sourceMotion,
                .strength = std::clamp(
                    interactor.motionStrength, 0.0f, 1.0f)});
        }

        // The source rigs split each one-metre module into four (Grass01) or
        // five (Grass02) independently weighted blade clusters. Evaluate
        // contact at those recovered pivots so the unit opens a local wake;
        // rotating the whole module uniformly is visually lost in a dense
        // patch and produces the wrong card-like motion.
        constexpr float kContactRadiusCm = 110.0f;
        constexpr float kContactVerticalToleranceCm = 85.0f;
        constexpr float kMaximumContactRotationRadians = 0.36f;
        constexpr float kAttackRate = 32.0f;
        constexpr float kReleaseRate = 7.0f;
        for (auto& layer : encounterGrass) {
            const auto variant =
                layer.logicalName == "enc_grass02"
                ? engine::render::route1_field_encounter_grass::
                      SourceVariant::Grass02
                : engine::render::route1_field_encounter_grass::
                      SourceVariant::Grass01;
            const std::size_t responsiveJointCount = std::min(
                layer.source.bones.size(),
                engine::render::route1_field_encounter_grass::
                    sourceJointCount(variant));
            for (auto& placement : layer.placements) {
                if (placement.suppressed) {
                    placement.contactBendRadians.fill(0.0f);
                    placement.contactCrossRadians.fill(0.0f);
                    continue;
                }
                for (std::size_t joint = 1u;
                     joint < responsiveJointCount;
                     ++joint) {
                    if (placement.suppressedJoints[joint]) {
                        placement.contactBendRadians[joint] = 0.0f;
                        placement.contactCrossRadians[joint] = 0.0f;
                        continue;
                    }
                    const auto& anchor =
                        layer.sourceJointAnchors[joint];
                    const glm::vec4 renderedPivot =
                        glm::make_mat4(
                            placement.modelMatrix.data()) *
                        glm::vec4(
                            anchor[0], anchor[1], anchor[2], 1.0f);
                    const glm::vec2 clusterCenter(
                        renderedPivot.x,
                        renderedPivot.z);
                    float targetBend = 0.0f;
                    float targetCross = 0.0f;
                    float strongestInfluence = 0.0f;
                    for (const auto& interactor : sourceInteractors) {
                        const float verticalDistance = std::abs(
                            placement.center[1] -
                            interactor.position.y);
                        if (verticalDistance >
                            kContactVerticalToleranceCm) {
                            continue;
                        }
                        const glm::vec2 delta(
                            clusterCenter.x - interactor.position.x,
                            clusterCenter.y - interactor.position.z);
                        const float distance = glm::length(delta);
                        if (distance >= kContactRadiusCm) {
                            continue;
                        }
                        const float proximity = std::clamp(
                            1.0f - distance / kContactRadiusCm,
                            0.0f,
                            1.0f);
                        const float smoothProximity =
                            proximity * proximity *
                            (3.0f - 2.0f * proximity);
                        const float influence =
                            smoothProximity * interactor.strength;
                        if (influence <= strongestInfluence) {
                            continue;
                        }
                        strongestInfluence = influence;
                        const glm::vec2 motion(
                            interactor.motion.x,
                            interactor.motion.z);
                        glm::vec2 partDirection =
                            distance > 0.001f
                            ? delta / distance
                            : motion;
                        // Primarily part away from the unit while allowing a
                        // small directional wake in its direction of travel.
                        partDirection =
                            partDirection * 0.84f + motion * 0.16f;
                        const float directionLength =
                            glm::length(partDirection);
                        if (directionLength > 0.001f) {
                            partDirection /= directionLength;
                        } else {
                            partDirection = glm::vec2(0.0f, 1.0f);
                        }
                        const float phase =
                            simulationSeconds * 18.0f +
                            placement.phaseCycles * 6.28318530718f +
                            static_cast<float>(joint) * 0.61f;
                        const float flutter =
                            0.88f + 0.12f * std::sin(phase);
                        const float amplitude =
                            kMaximumContactRotationRadians *
                            influence * flutter;
                        // A positive Z-axis rotation bends an upright source
                        // blade toward -X; a positive X-axis rotation bends
                        // it toward +Z.
                        targetBend =
                            -partDirection.x * amplitude;
                        targetCross =
                            partDirection.y * amplitude;
                    }
                    const bool contacting = strongestInfluence > 0.0f;
                    const float response = contacting
                        ? kAttackRate
                        : kReleaseRate;
                    const float blend =
                        1.0f - std::exp(-response * dt);
                    placement.contactBendRadians[joint] +=
                        (targetBend -
                         placement.contactBendRadians[joint]) * blend;
                    placement.contactCrossRadians[joint] +=
                        (targetCross -
                         placement.contactCrossRadians[joint]) * blend;
                }
            }
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
    for (auto& row : sourceTerrainFringeMaterialSegments) {
        row.clear();
    }
    sourceTerrainCliffGeometrySegments.clear();
    // Material 13 does not use one route-wide tangential phase. Each source
    // terrain assembly preserves its own unwrapped UV1 field (mesh 31, for
    // example, advances roughly 0.53 repeats/m while mesh 32 advances roughly
    // 0.495). Cache the longitudinal edges of every decoded three-row fringe
    // so rebuilt geometry that replaces a native ledge can inherit the exact
    // local phase, scale, and Color0 rather than restarting a generic strip.
    for (const auto& mesh : source.meshes) {
        if (mesh.sourceIndex < 29u || mesh.sourceIndex > 36u) {
            continue;
        }
        const glm::mat4 model = glm::make_mat4(mesh.transform.data());
        const glm::mat3 normalMatrix = glm::transpose(glm::inverse(
            glm::mat3(model)));
        for (const auto& group : mesh.polygonGroups) {
            if (group.materialIndex != 13u ||
                (group.primitiveType != "triangles" &&
                 group.primitiveType != "Triangles")) {
                continue;
            }
            for (std::size_t index = 0u;
                 index + 2u < group.indices.size();
                 index += 3u) {
                const std::array<std::uint32_t, 3> triangle{
                    group.indices[index],
                    group.indices[index + 1u],
                    group.indices[index + 2u]};
                for (std::size_t edge = 0u; edge < 3u; ++edge) {
                    const auto firstIndex = triangle[edge];
                    const auto secondIndex = triangle[(edge + 1u) % 3u];
                    if (firstIndex >= mesh.vertices.size() ||
                        secondIndex >= mesh.vertices.size()) {
                        continue;
                    }
                    const auto& first = mesh.vertices[firstIndex];
                    const auto& second = mesh.vertices[secondIndex];
                    const auto row = std::find_if(
                        kTerrainLedgeFringeMaskV.begin(),
                        kTerrainLedgeFringeMaskV.end(),
                        [&](float maskV) {
                            return std::abs(
                                       first.texcoords[1][1] - maskV) <=
                                    0.001f &&
                                std::abs(
                                    second.texcoords[1][1] - maskV) <=
                                    0.001f;
                        });
                    if (row == kTerrainLedgeFringeMaskV.end()) {
                        continue;
                    }
                    const glm::vec3 firstPosition = glm::vec3(
                        model * glm::vec4(
                            first.position[0],
                            first.position[1],
                            first.position[2],
                            1.0f));
                    const glm::vec3 secondPosition = glm::vec3(
                        model * glm::vec4(
                            second.position[0],
                            second.position[1],
                            second.position[2],
                            1.0f));
                    const glm::vec3 segmentDelta =
                        secondPosition - firstPosition;
                    if (glm::dot(segmentDelta, segmentDelta) <= 0.0001f) {
                        continue;
                    }
                    sourceTerrainFringeMaterialSegments[static_cast<std::size_t>(std::distance(
                                                            kTerrainLedgeFringeMaskV.begin(), row))]
                        .push_back(SourceTerrainFringeMaterialSegment{
                            .startPositionCm = firstPosition,
                            .endPositionCm = secondPosition,
                            .startNormal = glm::normalize(
                                normalMatrix * glm::vec3{
                                                   first.normal[0],
                                                   first.normal[1],
                                                   first.normal[2]}),
                            .endNormal = glm::normalize(normalMatrix * glm::vec3{second.normal[0], second.normal[1], second.normal[2]}),
                            .startUv1U = first.texcoords[1][0],
                            .endUv1U = second.texcoords[1][0],
                            .startColor = glm::vec4{first.colors[0][0], first.colors[0][1], first.colors[0][2], first.colors[0][3]},
                            .endColor = glm::vec4{second.colors[0][0], second.colors[0][1], second.colors[0][2], second.colors[0][3]}});
                }
            }
        }
    }

    // Cache the four one-level cliff rows as geometry fields as well. The
    // source cap, foliage crown, and cliff crown are one authored contour;
    // inheriting only the foliage UVs while rebuilding the neighboring shape
    // leaves their positions and normals visibly disagreeing.
    for (const auto &mesh : source.meshes) {
        if (mesh.sourceIndex < 29u || mesh.sourceIndex > 36u) {
            continue;
        }
        const glm::mat4 model = glm::make_mat4(mesh.transform.data());
        const glm::mat3 normalMatrix = glm::transpose(glm::inverse(
            glm::mat3(model)));
        for (const auto &group : mesh.polygonGroups) {
            if (group.materialIndex != 18u ||
                (group.primitiveType != "triangles" &&
                 group.primitiveType != "Triangles")) {
                continue;
            }
            for (std::size_t index = 0u;
                 index + 2u < group.indices.size();
                 index += 3u) {
                const std::array<std::uint32_t, 3> triangle{
                    group.indices[index],
                    group.indices[index + 1u],
                    group.indices[index + 2u]};
                for (std::size_t edge = 0u; edge < 3u; ++edge) {
                    const auto firstIndex = triangle[edge];
                    const auto secondIndex = triangle[(edge + 1u) % 3u];
                    if (firstIndex >= mesh.vertices.size() ||
                        secondIndex >= mesh.vertices.size()) {
                        continue;
                    }
                    const auto &first = mesh.vertices[firstIndex];
                    const auto &second = mesh.vertices[secondIndex];
                    // A cliff's longitudinal rows keep UV1.V constant, but
                    // the actual V values differ between source terrain
                    // assemblies. Select the topology, not mesh-32 literals.
                    if (std::abs(
                            first.texcoords[1][1] -
                            second.texcoords[1][1]) > 0.002f) {
                        continue;
                    }
                    const glm::vec3 firstPosition = glm::vec3(
                        model * glm::vec4(
                                    first.position[0],
                                    first.position[1],
                                    first.position[2],
                                    1.0f));
                    const glm::vec3 secondPosition = glm::vec3(
                        model * glm::vec4(
                                    second.position[0],
                                    second.position[1],
                                    second.position[2],
                                    1.0f));
                    const glm::vec3 segmentDelta =
                        secondPosition - firstPosition;
                    if (glm::dot(segmentDelta, segmentDelta) <= 0.0001f) {
                        continue;
                    }
                    sourceTerrainCliffGeometrySegments.push_back(
                        SourceTerrainCliffGeometrySegment{
                            .startPositionCm = firstPosition,
                            .endPositionCm = secondPosition,
                            .startNormal = glm::normalize(
                                normalMatrix * glm::vec3{
                                                   first.normal[0],
                                                   first.normal[1],
                                                   first.normal[2]}),
                            .endNormal = glm::normalize(normalMatrix * glm::vec3{second.normal[0], second.normal[1], second.normal[2]})});
                }
            }
        }
    }

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
    const auto* fringeGeometry = geometryFor(32u, 2u);
    const auto* fringeObject = renderObjectFor(32u, 2u);
    if (!lightStorageIndex || !darkStorageIndex ||
        !cliffStorageIndex ||
        !lightGeometry || !lightObject || !darkGeometry ||
        !darkObject || !cliffGeometry || !cliffObject ||
        !fringeGeometry || !fringeObject ||
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
        source.meshes[*cliffStorageIndex].polygonGroups.size() <= 2u ||
        source.meshes[*cliffStorageIndex]
            .polygonGroups[1u].indices.empty() ||
        source.meshes[*cliffStorageIndex]
            .polygonGroups[2u].indices.empty()) {
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
    terrainTilePrototypes.authoredSurfacePrototypes.clear();
    terrainTilePrototypes.cliffPrototypes.clear();
    terrainTilePrototypes.fringePrototypes.clear();
    terrainTilePrototypes.sourceReferencePrototypes.clear();
    terrainTilePrototypes.groundVertexTemplate = lightTemplate;
    terrainTilePrototypes.groundSourceVertexTemplate =
        lightSourceTemplate;
    terrainTilePrototypes.groundSourceVertexSemanticMask =
        lightGeometry->sourceVertexSemanticMask;
    terrainTilePrototypes.groundMaterialHandle =
        lightObject->materialHandle;
    if (lightObject->materialHandle.id == 0u ||
        lightObject->materialHandle.id >
            scene.registry.materials.size()) {
        return fail(
            outError,
            "Route 1 terrain tiles lost the source ground material.");
    }
    auto shadowlessGroundMaterial =
        scene.registry.materials[
            lightObject->materialHandle.id - 1u];
    shadowlessGroundMaterial.projectedShadowEnabled = 0u;
    shadowlessGroundMaterial.sourceEnabledSwitchMask &=
        ~engine::render::backend::
            WorldSceneSourceMaterialSwitchReceiveShadow;
    terrainTilePrototypes.groundShadowlessMaterialHandle =
        shared_world_scene::ensureMaterial(
            scene.registry,
            &terrainTilePrototypes.groundShadowlessMaterialHandle,
            shadowlessGroundMaterial);
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
    const auto fringeVertexIndex =
        source.meshes[*cliffStorageIndex]
            .polygonGroups[2u].indices.front();
    terrainTilePrototypes.fringeVertexTemplate =
        scene.meshVertexStorage[*cliffStorageIndex]
            .vertices[fringeVertexIndex];
    terrainTilePrototypes.fringeSourceVertexTemplate =
        scene.meshVertexStorage[*cliffStorageIndex]
            .sourceVertices[fringeVertexIndex];
    terrainTilePrototypes.fringeSourceVertexSemanticMask =
        fringeGeometry->sourceVertexSemanticMask;
    terrainTilePrototypes.fringeMaterialHandle =
        fringeObject->materialHandle;
    terrainTilePrototypes.fringePipelineVariant =
        fringeObject->pipelineVariant;
    terrainTilePrototypes.fringeCookedDrawSlot =
        fringeObject->cookedDrawSlot;
    constexpr std::array<std::array<float, 2>, 4> corners{{
        {-50.0f, -50.0f},
        {50.0f, -50.0f},
        {50.0f, 50.0f},
        {-50.0f, 50.0f},
    }};
    // Exact material-19 Route 1 samples. V=-1.071291 is repeat-equivalent to
    // the source lawn value 0.928709 and is the nearest period to dirt, which
    // keeps any interpolation inside the original leafy transition band.
    constexpr std::array<float, 2> cleanLawnUv2{
        -0.101646f, -1.071291f};
    constexpr std::array<float, 2> cleanDirtUv2{
        -0.222876f, -1.127860f};
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
                        .materialIndex = group.materialIndex,
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
            tile.sourceShape = tile.shape;
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
        // Only FieldGroundShader01 (material 19) owns the continuous top-
        // surface UV/color field. Materials 12 and 13 are the cliff face and
        // leafy ledge lip; sampling either as a floor attribute source creates
        // the exact cell-sized tone jumps seen beside edited path tiles.
        if (triangle.materialIndex != 19u) {
            continue;
        }
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

bool RuntimeEnvironment::Impl::sampleWorldTerrainHeight(
    float worldX,
    float worldZ,
    float& outWorldY) const noexcept {
    if (!isLoaded || !std::isfinite(worldX) || !std::isfinite(worldZ)) {
        return false;
    }

    const glm::mat4 worldFromSourceTransform = boardMatrix(layout);
    const glm::vec4 sourcePoint =
        glm::inverse(worldFromSourceTransform) *
        glm::vec4(worldX, 0.0f, worldZ, 1.0f);
    if (!std::isfinite(sourcePoint.x) ||
        !std::isfinite(sourcePoint.z)) {
        return false;
    }

    const std::int32_t gridX = static_cast<std::int32_t>(
        std::floor(sourcePoint.x / kTerrainTileSizeCm));
    const std::int32_t gridZ = static_cast<std::int32_t>(
        std::floor(sourcePoint.z / kTerrainTileSizeCm));
    const auto found = std::find_if(
        terrainTiles.begin(),
        terrainTiles.end(),
        [&](const TerrainTileState& tile) {
            return tile.gridX == gridX && tile.gridZ == gridZ;
        });
    if (found == terrainTiles.end() ||
        found->surface == "empty" ||
        (!found->sourceOccupied && !found->authored)) {
        return false;
    }

    const float localX = std::clamp(
        sourcePoint.x / kTerrainTileSizeCm -
            static_cast<float>(gridX),
        0.0f,
        1.0f);
    const float localZ = std::clamp(
        sourcePoint.z / kTerrainTileSizeCm -
            static_cast<float>(gridZ),
        0.0f,
        1.0f);

    const auto terrainAt = [&](std::int32_t x,
                               std::int32_t z)
            -> const TerrainTileState* {
        const auto tile = std::find_if(
            terrainTiles.begin(),
            terrainTiles.end(),
            [&](const TerrainTileState& candidate) {
                return candidate.gridX == x &&
                    candidate.gridZ == z;
            });
        return tile == terrainTiles.end() ? nullptr : &*tile;
    };
    constexpr std::array<std::array<std::int32_t, 2>, 4>
        directions{{
            {0, 1},
            {1, 0},
            {0, -1},
            {-1, 0},
        }};
    const bool affectedSourceDirt =
        !found->authored &&
        found->surface == "dirt_path" &&
        std::any_of(
            directions.begin(),
            directions.end(),
            [&](const auto& direction) {
                const auto* neighbor = terrainAt(
                    gridX + direction[0],
                    gridZ + direction[1]);
                return neighbor && neighbor->authored;
            });

    float sourceY = route1TerrainProfileHeightCm(
        *found, localX, localZ);
    bool sampledRecoveredSurface = false;
    if (!found->authored || found->sourceReference) {
        const TerrainTileState* sampleTile = &*found;
        if (found->sourceReference) {
            const auto donor = std::find_if(
                sourceTerrainTiles.begin(),
                sourceTerrainTiles.end(),
                [&](const TerrainTileState& candidate) {
                    return candidate.gridX ==
                            (*found->sourceReference)[0] &&
                        candidate.gridZ ==
                            (*found->sourceReference)[1];
                });
            if (donor != sourceTerrainTiles.end()) {
                sampleTile = &*donor;
            }
        }
        SourceTerrainSurfaceSample sourceSample;
        if (!affectedSourceDirt &&
            sampleSourceTerrainSurface(
                *sampleTile, localX, localZ, sourceSample)) {
            sourceY = sourceSample.y;
            sampledRecoveredSurface = true;
        }
    }

    // Generated tops are intentionally lifted by the same sub-centimetre
    // depth safety margin used by their render geometry. Exact canonical and
    // exact source-reference surfaces retain the recovered source height.
    const bool generatedTop =
        (found->authored && !found->sourceReference) ||
        affectedSourceDirt ||
        found->cleanSuppressedEncounterGrassTint;
    if (generatedTop) {
        sourceY += kTerrainTileTopDepthBiasCm;
    } else if (!sampledRecoveredSurface && found->sourceReference) {
        // A donor whose ground triangle does not cover this exact sample can
        // still use its recovered logical profile without inventing a lift.
        sourceY = route1TerrainProfileHeightCm(
            *found, localX, localZ);
    }

    const glm::vec4 worldSurface = worldFromSourceTransform *
        glm::vec4(sourcePoint.x, sourceY, sourcePoint.z, 1.0f);
    if (!std::isfinite(worldSurface.y)) {
        return false;
    }
    outWorldY = worldSurface.y;
    return true;
}

bool RuntimeEnvironment::Impl::sampleTargetTerrainColor(
    std::string_view surface,
    std::int32_t elevationLevel,
    float worldGridX,
    float worldGridZ,
    glm::vec4& outColor) const {
    struct Donor {
        const TerrainTileState* tile = nullptr;
        float distanceSquared =
            std::numeric_limits<float>::max();
    };
    constexpr std::size_t kDonorCount = 8u;
    std::array<Donor, kDonorCount> nearest{};
    const auto donorLess = [](const Donor& left, const Donor& right) {
        if (left.distanceSquared != right.distanceSquared) {
            return left.distanceSquared < right.distanceSquared;
        }
        if (!left.tile || !right.tile) {
            return left.tile != nullptr;
        }
        if (left.tile->gridZ != right.tile->gridZ) {
            return left.tile->gridZ < right.tile->gridZ;
        }
        return left.tile->gridX < right.tile->gridX;
    };
    for (const auto& candidate : sourceTerrainTiles) {
        if (!candidate.sourceOccupied ||
            candidate.sourceSurface != surface ||
            candidate.sourceElevationLevel != elevationLevel) {
            continue;
        }
        const float minimumX = static_cast<float>(candidate.gridX);
        const float maximumX = minimumX + 1.0f;
        const float minimumZ = static_cast<float>(candidate.gridZ);
        const float maximumZ = minimumZ + 1.0f;
        const float deltaX = worldGridX < minimumX
            ? minimumX - worldGridX
            : (worldGridX > maximumX
                ? worldGridX - maximumX
                : 0.0f);
        const float deltaZ = worldGridZ < minimumZ
            ? minimumZ - worldGridZ
            : (worldGridZ > maximumZ
                ? worldGridZ - maximumZ
                : 0.0f);
        const Donor donor{
            .tile = &candidate,
            .distanceSquared = deltaX * deltaX + deltaZ * deltaZ};
        if (!nearest.back().tile || donorLess(donor, nearest.back())) {
            nearest.back() = donor;
            std::sort(nearest.begin(), nearest.end(), donorLess);
        }
    }

    constexpr float kExactDistanceSquared = 1.0e-8f;
    constexpr float kShepardSofteningSquared = 0.0625f;
    glm::vec4 weightedColor{0.0f};
    float totalWeight = 0.0f;
    bool hasExactDonor = false;
    for (const Donor& donor : nearest) {
        if (!donor.tile) {
            continue;
        }
        const bool exact = donor.distanceSquared <=
            kExactDistanceSquared;
        if (hasExactDonor && !exact) {
            continue;
        }
        SourceTerrainSurfaceSample sample;
        if (!sampleSourceTerrainSurface(
                *donor.tile,
                std::clamp(
                    worldGridX - static_cast<float>(donor.tile->gridX),
                    0.0f,
                    1.0f),
                std::clamp(
                    worldGridZ - static_cast<float>(donor.tile->gridZ),
                    0.0f,
                    1.0f),
                sample)) {
            continue;
        }
        if (exact && !hasExactDonor) {
            weightedColor = glm::vec4{0.0f};
            totalWeight = 0.0f;
            hasExactDonor = true;
        }
        const float weight = exact
            ? 1.0f
            : 1.0f /
                (donor.distanceSquared + kShepardSofteningSquared);
        weightedColor += sample.color0 * weight;
        totalWeight += weight;
    }
    if (totalWeight <= 0.0f) {
        return false;
    }
    outColor = weightedColor / totalWeight;
    return true;
}

bool RuntimeEnvironment::Impl::sampleTargetTerrainUv2(
    std::string_view surface,
    std::int32_t elevationLevel,
    float worldGridX,
    float worldGridZ,
    glm::vec2& outUv2) const {
    struct Donor {
        const TerrainTileState* tile = nullptr;
        float distanceSquared =
            std::numeric_limits<float>::max();
    };
    constexpr std::size_t kDonorCount = 8u;
    std::array<Donor, kDonorCount> nearest{};
    const auto donorLess = [](const Donor& left, const Donor& right) {
        if (left.distanceSquared != right.distanceSquared) {
            return left.distanceSquared < right.distanceSquared;
        }
        if (!left.tile || !right.tile) {
            return left.tile != nullptr;
        }
        if (left.tile->gridZ != right.tile->gridZ) {
            return left.tile->gridZ < right.tile->gridZ;
        }
        return left.tile->gridX < right.tile->gridX;
    };
    for (const auto& candidate : sourceTerrainTiles) {
        if (!candidate.sourceOccupied ||
            candidate.sourceSurface != surface ||
            candidate.sourceElevationLevel != elevationLevel) {
            continue;
        }
        const float minimumX = static_cast<float>(candidate.gridX);
        const float maximumX = minimumX + 1.0f;
        const float minimumZ = static_cast<float>(candidate.gridZ);
        const float maximumZ = minimumZ + 1.0f;
        const float deltaX = worldGridX < minimumX
            ? minimumX - worldGridX
            : (worldGridX > maximumX
                ? worldGridX - maximumX
                : 0.0f);
        const float deltaZ = worldGridZ < minimumZ
            ? minimumZ - worldGridZ
            : (worldGridZ > maximumZ
                ? worldGridZ - maximumZ
                : 0.0f);
        const Donor donor{
            .tile = &candidate,
            .distanceSquared = deltaX * deltaX + deltaZ * deltaZ};
        if (!nearest.back().tile || donorLess(donor, nearest.back())) {
            nearest.back() = donor;
            std::sort(nearest.begin(), nearest.end(), donorLess);
        }
    }

    constexpr float kExactDistanceSquared = 1.0e-8f;
    constexpr float kShepardSofteningSquared = 0.0625f;
    glm::vec2 weightedUv2{0.0f};
    glm::vec2 referenceUv2{0.0f};
    float totalWeight = 0.0f;
    bool hasReference = false;
    bool hasExactDonor = false;
    for (const Donor& donor : nearest) {
        if (!donor.tile) {
            continue;
        }
        const bool exact = donor.distanceSquared <=
            kExactDistanceSquared;
        if (hasExactDonor && !exact) {
            continue;
        }
        SourceTerrainSurfaceSample sample;
        if (!sampleSourceTerrainSurface(
                *donor.tile,
                std::clamp(
                    worldGridX -
                        static_cast<float>(donor.tile->gridX),
                    0.0f,
                    1.0f),
                std::clamp(
                    worldGridZ -
                        static_cast<float>(donor.tile->gridZ),
                    0.0f,
                    1.0f),
                sample)) {
            continue;
        }
        if (exact && !hasExactDonor) {
            weightedUv2 = glm::vec2{0.0f};
            totalWeight = 0.0f;
            hasReference = false;
            hasExactDonor = true;
        }
        glm::vec2 compatibleUv2 = sample.uv2;
        if (!hasReference) {
            referenceUv2 = compatibleUv2;
            hasReference = true;
        } else {
            compatibleUv2.x -= std::round(
                compatibleUv2.x - referenceUv2.x);
            compatibleUv2.y -= std::round(
                compatibleUv2.y - referenceUv2.y);
        }
        const float weight = exact
            ? 1.0f
            : 1.0f /
                (donor.distanceSquared + kShepardSofteningSquared);
        weightedUv2 += compatibleUv2 * weight;
        totalWeight += weight;
    }
    if (totalWeight <= 0.0f) {
        return false;
    }
    outUv2 = weightedUv2 / totalWeight;
    return true;
}

bool RuntimeEnvironment::Impl::sampleNormalizedSourceTintColor(
    const TerrainTileState& tile,
    float localX,
    float localZ,
    glm::vec4& outColor,
    float* outBoundaryWeight) const {
    const auto cleanLawn = route1CleanLightLawnColor();
    const glm::vec4 cleanColor{
        cleanLawn[0],
        cleanLawn[1],
        cleanLawn[2],
        cleanLawn[3]};
    outColor = cleanColor;
    if (outBoundaryWeight) {
        *outBoundaryWeight = 0.0f;
    }

    // Height still converges only near an immediate compatible boundary.
    // Color is resolved component-wide below; fading every normalized cell
    // independently to white creates a conspicuous bright tile island even
    // when the shared edge itself is mathematically continuous.
    constexpr float kBoundaryBlendWidth = 0.50f;
    constexpr std::array<std::array<std::int32_t, 2>, 4>
        directions{{
            {0, 1},
            {1, 0},
            {0, -1},
            {-1, 0},
        }};
    const std::array<float, 4> edgeDistances{
        1.0f - localZ,
        1.0f - localX,
        localZ,
        localX};
    glm::vec4 weightedBoundaryColor{0.0f};
    float totalBoundaryWeight = 0.0f;
    float strongestColorBoundaryWeight = 0.0f;
    float strongestBoundaryWeight = 0.0f;
    for (std::size_t edge = 0u; edge < directions.size(); ++edge) {
        const float weight = std::clamp(
            (kBoundaryBlendWidth - edgeDistances[edge]) /
                kBoundaryBlendWidth,
            0.0f,
            1.0f);
        if (weight <= 0.0f) {
            continue;
        }
        const auto direction = directions[edge];
        const auto neighbor = std::find_if(
            terrainTiles.begin(),
            terrainTiles.end(),
            [&](const TerrainTileState& candidate) {
                return candidate.gridX == tile.gridX + direction[0] &&
                    candidate.gridZ == tile.gridZ + direction[1];
            });
        if (neighbor == terrainTiles.end() ||
            neighbor->surface != "light_lawn" ||
            neighbor->elevationLevel != tile.elevationLevel ||
            neighbor->cleanSuppressedEncounterGrassTint) {
            continue;
        }
        const float worldGridX =
            static_cast<float>(tile.gridX) + localX;
        const float worldGridZ =
            static_cast<float>(tile.gridZ) + localZ;
        // Match the adjoining top's final light-lawn Color0 resolution.
        // Untouched/source-reference geometry renders its recovered vertex
        // field; generated authored geometry uses the continuous target field
        // or the source material control where that sparse field has a gap.
        glm::vec4 neighborColor{
            terrainTilePrototypes.groundVertexTemplate.r,
            terrainTilePrototypes.groundVertexTemplate.g,
            terrainTilePrototypes.groundVertexTemplate.b,
            terrainTilePrototypes.groundVertexTemplate.a};
        bool sampledNeighborColor = false;
        if (!neighbor->authored || neighbor->sourceReference) {
            SourceTerrainSurfaceSample sourceSample;
            sampledNeighborColor = sampleSourceTerrainSurface(
                *neighbor,
                std::clamp(
                    worldGridX - static_cast<float>(neighbor->gridX),
                    0.0f,
                    1.0f),
                std::clamp(
                    worldGridZ - static_cast<float>(neighbor->gridZ),
                    0.0f,
                    1.0f),
                sourceSample);
            if (sampledNeighborColor) {
                neighborColor = sourceSample.color0;
            }
        }
        if (!sampledNeighborColor) {
            sampleTargetTerrainColor(
                neighbor->surface,
                neighbor->elevationLevel,
                worldGridX,
                worldGridZ,
                neighborColor);
        }
        weightedBoundaryColor += neighborColor * weight;
        totalBoundaryWeight += weight;
        strongestColorBoundaryWeight = std::max(
            strongestColorBoundaryWeight, weight);
        // Authored tops use the generated top-depth bias. Pulling a
        // normalized neighbor toward its old source profile at that edge
        // creates a sub-centimetre height step and a visible lighting line.
        // Only untouched source geometry needs the recovered-height bridge.
        if (!neighbor->authored && !neighbor->sourceReference) {
            strongestBoundaryWeight = std::max(
                strongestBoundaryWeight, weight);
        }
    }
    if (outBoundaryWeight) {
        *outBoundaryWeight = strongestBoundaryWeight;
    }

    const float worldGridX =
        static_cast<float>(tile.gridX) + localX;
    const float worldGridZ =
        static_cast<float>(tile.gridZ) + localZ;
    struct Donor {
        const TerrainTileState* tile = nullptr;
        float distanceSquared =
            std::numeric_limits<float>::max();
    };
    constexpr std::size_t kDonorCount = 8u;
    std::array<Donor, kDonorCount> nearest{};
    const auto donorLess = [](const Donor& left, const Donor& right) {
        if (left.distanceSquared != right.distanceSquared) {
            return left.distanceSquared < right.distanceSquared;
        }
        if (!left.tile || !right.tile) {
            return left.tile != nullptr;
        }
        if (left.tile->gridZ != right.tile->gridZ) {
            return left.tile->gridZ < right.tile->gridZ;
        }
        return left.tile->gridX < right.tile->gridX;
    };
    for (const auto& candidate : terrainTiles) {
        if (candidate.surface != "light_lawn" ||
            candidate.elevationLevel != tile.elevationLevel ||
            candidate.cleanSuppressedEncounterGrassTint) {
            continue;
        }
        const float minimumX = static_cast<float>(candidate.gridX);
        const float maximumX = minimumX + 1.0f;
        const float minimumZ = static_cast<float>(candidate.gridZ);
        const float maximumZ = minimumZ + 1.0f;
        const float deltaX = worldGridX < minimumX
            ? minimumX - worldGridX
            : (worldGridX > maximumX
                ? worldGridX - maximumX
                : 0.0f);
        const float deltaZ = worldGridZ < minimumZ
            ? minimumZ - worldGridZ
            : (worldGridZ > maximumZ
                ? worldGridZ - maximumZ
                : 0.0f);
        const Donor donor{
            .tile = &candidate,
            .distanceSquared = deltaX * deltaX + deltaZ * deltaZ};
        if (!nearest.back().tile || donorLess(donor, nearest.back())) {
            nearest.back() = donor;
            std::sort(nearest.begin(), nearest.end(), donorLess);
        }
    }

    constexpr float kExactDistanceSquared = 1.0e-8f;
    constexpr float kShepardSofteningSquared = 0.0625f;
    glm::vec4 weightedColor{0.0f};
    float totalWeight = 0.0f;
    bool hasExactDonor = false;
    for (const Donor& donor : nearest) {
        if (!donor.tile) {
            continue;
        }
        const bool exact = donor.distanceSquared <=
            kExactDistanceSquared;
        if (hasExactDonor && !exact) {
            continue;
        }
        const float donorGridX = std::clamp(
            worldGridX,
            static_cast<float>(donor.tile->gridX),
            static_cast<float>(donor.tile->gridX + 1));
        const float donorGridZ = std::clamp(
            worldGridZ,
            static_cast<float>(donor.tile->gridZ),
            static_cast<float>(donor.tile->gridZ + 1));
        glm::vec4 donorColor{
            terrainTilePrototypes.groundVertexTemplate.r,
            terrainTilePrototypes.groundVertexTemplate.g,
            terrainTilePrototypes.groundVertexTemplate.b,
            terrainTilePrototypes.groundVertexTemplate.a};
        sampleTargetTerrainColor(
            donor.tile->surface,
            donor.tile->elevationLevel,
            donorGridX,
            donorGridZ,
            donorColor);
        if (exact && !hasExactDonor) {
            weightedColor = glm::vec4{0.0f};
            totalWeight = 0.0f;
            hasExactDonor = true;
        }
        const float weight = exact
            ? 1.0f
            : 1.0f /
                (donor.distanceSquared + kShepardSofteningSquared);
        weightedColor += donorColor * weight;
        totalWeight += weight;
    }
    if (totalWeight > 0.0f) {
        outColor = weightedColor / totalWeight;
    }
    if (totalBoundaryWeight > 0.0f) {
        outColor = glm::mix(
            outColor,
            weightedBoundaryColor / totalBoundaryWeight,
            strongestColorBoundaryWeight);
    }
    return true;
}

bool RuntimeEnvironment::Impl::sampleSourceTerrainFringeMaterial(
    const glm::vec3 &sourcePositionCm,
    const glm::vec2 &sourceTangent,
    std::size_t row,
    float &outUv1U,
    glm::vec4 &outColor,
    glm::vec3 *outPositionCm,
    glm::vec3 *outNormal) const {
    if (row >= sourceTerrainFringeMaterialSegments.size()) {
        return false;
    }
    // Restrict inheritance to a genuinely coincident decoded carrier. A new
    // authored ledge elsewhere on the route must keep its continuous contour
    // fallback rather than snapping to an unrelated nearby source wall.
    constexpr float kMaximumDistanceCm = 18.0f;
    constexpr float kMaximumDistanceSquared =
        kMaximumDistanceCm * kMaximumDistanceCm;
    float bestDistanceSquared = kMaximumDistanceSquared;
    bool sampled = false;
    for (const auto& segment :
         sourceTerrainFringeMaterialSegments[row]) {
        const glm::vec3 delta =
            segment.endPositionCm - segment.startPositionCm;
        const float lengthSquared = glm::dot(delta, delta);
        if (lengthSquared <= 0.0001f) {
            continue;
        }
        const glm::vec2 horizontalDelta{delta.x, delta.z};
        if (glm::length(horizontalDelta) <= 0.0001f ||
            glm::length(sourceTangent) <= 0.0001f ||
            std::abs(glm::dot(
                glm::normalize(horizontalDelta),
                glm::normalize(sourceTangent))) < 0.8f) {
            continue;
        }
        const float phase = std::clamp(
            glm::dot(
                sourcePositionCm - segment.startPositionCm,
                delta) /
                lengthSquared,
            0.0f,
            1.0f);
        const glm::vec3 closest =
            segment.startPositionCm + delta * phase;
        const glm::vec3 difference = sourcePositionCm - closest;
        const float distanceSquared = glm::dot(difference, difference);
        if (distanceSquared >= bestDistanceSquared) {
            continue;
        }
        bestDistanceSquared = distanceSquared;
        outUv1U = std::lerp(
            segment.startUv1U,
            segment.endUv1U,
            phase);
        outColor = glm::mix(
            segment.startColor,
            segment.endColor,
            phase);
        if (outPositionCm) {
            *outPositionCm = closest;
        }
        if (outNormal) {
            *outNormal = glm::normalize(glm::mix(
                segment.startNormal,
                segment.endNormal,
                phase));
        }
        sampled = true;
    }
    return sampled;
}

bool RuntimeEnvironment::Impl::sampleSourceTerrainCliffGeometry(
    const glm::vec3 &sourcePositionCm,
    const glm::vec2 &sourceTangent,
    glm::vec3 &outPositionCm,
    glm::vec3 &outNormal) const {
    // Authored LGPE cliff rows wander substantially at convex/concave joins.
    // An 18 cm gate rejected valid source rows there and forced the complete
    // rebuilt carrier back to its synthetic profile. A source cell is 100 cm
    // wide, so this radius remains short of the next parallel boundary;
    // tangent alignment and nearest-distance selection disambiguate rows on
    // the matching source cliff.
    constexpr float kMaximumDistanceCm = 44.0f;
    constexpr float kMaximumDistanceSquared =
        kMaximumDistanceCm * kMaximumDistanceCm;
    float bestDistanceSquared = kMaximumDistanceSquared;
    bool sampled = false;
    for (const auto &segment : sourceTerrainCliffGeometrySegments) {
        const glm::vec3 delta =
            segment.endPositionCm - segment.startPositionCm;
        const float lengthSquared = glm::dot(delta, delta);
        if (lengthSquared <= 0.0001f) {
            continue;
        }
        const glm::vec2 horizontalDelta{delta.x, delta.z};
        if (glm::length(horizontalDelta) <= 0.0001f ||
            glm::length(sourceTangent) <= 0.0001f ||
            std::abs(glm::dot(
                glm::normalize(horizontalDelta),
                glm::normalize(sourceTangent))) < 0.8f) {
            continue;
        }
        const float phase = std::clamp(
            glm::dot(
                sourcePositionCm - segment.startPositionCm,
                delta) /
                lengthSquared,
            0.0f,
            1.0f);
        const glm::vec3 closest =
            segment.startPositionCm + delta * phase;
        const glm::vec3 difference = sourcePositionCm - closest;
        const float distanceSquared = glm::dot(difference, difference);
        if (distanceSquared >= bestDistanceSquared) {
            continue;
        }
        bestDistanceSquared = distanceSquared;
        outPositionCm = closest;
        outNormal = glm::normalize(glm::mix(
            segment.startNormal,
            segment.endNormal,
            phase));
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
    std::uint32_t dirtConnectionMask,
    const DirtTransitionUvField& transitionUv) {
    // Twenty subdivisions place the recovered 30 cm inner ribbon seam on an
    // exact lattice row (6 * 5 cm). The ribbon is planar and its leaf contour
    // comes from the source atlas, so denser geometry adds no silhouette
    // detail; it only risks pushing the combined edited surface beyond the
    // renderer's indexed-mesh submission ceiling.
    constexpr std::uint32_t kGridResolution =
        kTerrainLedgeContourSegments;
    // The source material-19 path boundary is a triangulated ribbon rather
    // than a scalar fade painted independently into each grid cell. Its
    // recovered atlas endpoints are applied by route1DirtTransitionUv2V().
    constexpr float kBoundaryWidthCm = 30.0f;
    // The weighted median of the source Route 1 ground-boundary strips is
    // approximately 0.36 atlas repeats per source metre. The older 0.510638
    // value belonged to the cliff lip carrier and made ground leaves too
    // small and dense.
    constexpr std::array<float, 2> kCleanLawnUv2{
        -0.101646f, -1.071291f};
    constexpr std::array<float, 2> kCleanDirtUv2{
        -0.222876f, -1.127860f};
    constexpr std::array<float, 3> kRaisedLawnTint{
        0.180392161f, 0.482352942f, 0.431372553f};

    dirtConnectionMask &= 0x0fu;
    const bool dark = tile.surface == "dark_lawn";
    const bool dirt = tile.surface == "dirt_path";
    const bool ramp = tile.shape.starts_with("ramp_");
    // Exact material-19 Color0 controls from the dirt core of the canonical
    // ramp beside the Route 1 sign (mesh 36, z=-900..-800 cm). Its shine is
    // not a specular or cliff-rim effect: FieldGroundShader01 feeds
    // Alpha_light through (1-Color0.a), so these sub-one alpha values create
    // the source's luminous high-to-low ramp sweep.

    struct RampDirtNeighborTransition {
        std::size_t edge = 0u;
        bool highSide = false;
    };
    struct DirtLawnNeighborTransition {
        std::size_t edge = 0u;
        const TerrainTileState* lawnTile = nullptr;
    };
    constexpr std::array<std::array<std::int32_t, 2>, 4>
        rampNeighborDirections{{
            {0, 1},
            {1, 0},
            {0, -1},
            {-1, 0},
        }};
    std::array<RampDirtNeighborTransition, 4>
        rampDirtNeighborTransitions{};
    std::size_t rampDirtNeighborTransitionCount = 0u;
    if (dirt && !ramp) {
        for (std::size_t edge = 0u;
             edge < rampNeighborDirections.size();
             ++edge) {
            const auto direction = rampNeighborDirections[edge];
            const auto neighbor = std::find_if(
                terrainTiles.begin(),
                terrainTiles.end(),
                [&](const TerrainTileState& candidate) {
                    return candidate.gridX == tile.gridX + direction[0] &&
                        candidate.gridZ == tile.gridZ + direction[1];
                });
            if (neighbor == terrainTiles.end() ||
                neighbor->surface != "dirt_path" ||
                !neighbor->shape.starts_with("ramp_")) {
                continue;
            }
            // Test the ramp edge facing this flat tile. A valid neighbor is
            // either at the ramp's base level or its one-level-high side.
            const std::int32_t rampToTileX = -direction[0];
            const std::int32_t rampToTileZ = -direction[1];
            std::int32_t rampEdgeLevel = neighbor->elevationLevel;
            if ((rampToTileX > 0 && neighbor->shape == "ramp_east") ||
                (rampToTileX < 0 && neighbor->shape == "ramp_west") ||
                (rampToTileZ > 0 && neighbor->shape == "ramp_north") ||
                (rampToTileZ < 0 && neighbor->shape == "ramp_south")) {
                ++rampEdgeLevel;
            }
            if (rampEdgeLevel != tile.elevationLevel) {
                continue;
            }
            rampDirtNeighborTransitions[
                rampDirtNeighborTransitionCount++] = {
                    .edge = edge,
                    .highSide =
                        tile.elevationLevel > neighbor->elevationLevel};
        }
    }

    std::array<DirtLawnNeighborTransition, 4>
        dirtLawnNeighborTransitions{};
    std::size_t dirtLawnNeighborTransitionCount = 0u;
    if (dirt) {
        for (std::size_t edge = 0u;
             edge < rampNeighborDirections.size();
             ++edge) {
            const auto direction = rampNeighborDirections[edge];
            const auto neighbor = std::find_if(
                terrainTiles.begin(),
                terrainTiles.end(),
                [&](const TerrainTileState& candidate) {
                    return candidate.gridX ==
                            tile.gridX + direction[0] &&
                        candidate.gridZ ==
                            tile.gridZ + direction[1];
                });
            if (neighbor == terrainTiles.end() ||
                !neighbor->surface.ends_with("lawn")) {
                continue;
            }
            const auto profile = route1TerrainSharedEdgeProfile(
                tile, &*neighbor, edge);
            if (profile.tileLevels != profile.neighborLevels) {
                continue;
            }
            dirtLawnNeighborTransitions[
                dirtLawnNeighborTransitionCount++] = {
                    .edge = edge,
                    .lawnTile = &*neighbor};
        }
    }

    std::uint32_t sourceSeamOverlapMask = 0u;
    std::uint32_t ledgeCrownClipMask = 0u;
    std::uint32_t ledgeCrownConvexCornerMask = 0u;
    std::array<std::array<float, 2>, 4>
        ledgeCrownEndpointWeights{};
    std::array<float, 4> ledgeCrownContourStartCm{};
    std::array<const route1_terrain_contours::EdgeSpan*, 4>
        ledgeCrownContourEdges{};
    std::uint32_t ledgeContactOverlapMask = 0u;
    std::array<std::array<float, 2>, 4>
        ledgeContactEndpointWeights{};
    std::array<float, 4> ledgeContactContourStartCm{};
    std::array<route1_terrain_ledges::Join, 4>
        ledgeContactStartJoins{};
    std::array<route1_terrain_ledges::Join, 4>
        ledgeContactEndJoins{};
    const auto activeTile = std::find_if(
        terrainTiles.begin(),
        terrainTiles.end(),
        [&](const TerrainTileState& candidate) {
            return candidate.gridX == tile.gridX &&
                candidate.gridZ == tile.gridZ;
        });
    if (activeTile != terrainTiles.end()) {
        const bool tileUsesGeneratedCap =
            terrainMaskCells.contains({tile.gridX, tile.gridZ}) &&
            !tile.sourceReference;
        for (std::size_t edge = 0u;
             edge < rampNeighborDirections.size();
             ++edge) {
            const auto direction = rampNeighborDirections[edge];
            const auto neighbor = std::find_if(
                terrainTiles.begin(),
                terrainTiles.end(),
                [&](const TerrainTileState& candidate) {
                    return candidate.gridX ==
                            tile.gridX + direction[0] &&
                        candidate.gridZ ==
                            tile.gridZ + direction[1];
                });
            // `tile` can be promoted to generated geometry when an otherwise
            // untouched source cap is invalidated by a neighboring edit. Use
            // that effective state here; the canonical activeTile remains
            // non-authored and would incorrectly disable its source handoff.
            const bool neighborUsesGeneratedCap =
                neighbor != terrainTiles.end() &&
                terrainMaskCells.contains(
                    {neighbor->gridX, neighbor->gridZ}) &&
                !neighbor->sourceReference;
            if (!neighborUsesGeneratedCap &&
                route1TerrainNeedsSourceSeamOverlap(
                    tile,
                    neighbor == terrainTiles.end()
                        ? nullptr
                        : &*neighbor,
                    edge,
                    tileUsesGeneratedCap)) {
                sourceSeamOverlapMask |= 1u << edge;
            }
            if (neighbor == terrainTiles.end() ||
                neighbor->surface == "empty" ||
                (!neighbor->sourceOccupied && !neighbor->authored)) {
                continue;
            }
            const auto profile = route1TerrainSharedEdgeProfile(
                *activeTile, &*neighbor, edge);
            const bool firstTileHigher =
                profile.tileLevels[0] > profile.neighborLevels[0];
            const bool secondTileHigher =
                profile.tileLevels[1] > profile.neighborLevels[1];
            if (firstTileHigher || secondTileHigher) {
                const auto* resolvedTileLedge =
                    route1_terrain_ledges::find(
                        terrainLedgeResolution,
                        {activeTile->gridX, activeTile->gridZ},
                        edge);
                if (resolvedTileLedge) {
                    ledgeCrownClipMask |= 1u << edge;
                    ledgeCrownEndpointWeights[edge] = {
                        firstTileHigher ? 1.0f : 0.0f,
                        secondTileHigher ? 1.0f : 0.0f};
                    ledgeCrownContourStartCm[edge] =
                        resolvedTileLedge->contourStartCm;
                    ledgeCrownContourEdges[edge] =
                        route1_terrain_contours::findEdge(
                            terrainContourAssembly,
                            {activeTile->gridX, activeTile->gridZ},
                            edge);
                }
            }
            const bool firstNeighborHigher =
                profile.neighborLevels[0] > profile.tileLevels[0];
            const bool secondNeighborHigher =
                profile.neighborLevels[1] > profile.tileLevels[1];
            if (!firstNeighborHigher && !secondNeighborHigher) {
                continue;
            }
            const std::size_t neighborEdge = (edge + 2u) % 4u;
            const auto* resolvedNeighborLedge =
                route1_terrain_ledges::find(
                    terrainLedgeResolution,
                    {neighbor->gridX, neighbor->gridZ},
                    neighborEdge);
            if (!resolvedNeighborLedge) {
                continue;
            }
            ledgeContactOverlapMask |= 1u << edge;
            ledgeContactEndpointWeights[edge] = {
                firstNeighborHigher ? 1.0f : 0.0f,
                secondNeighborHigher ? 1.0f : 0.0f};
            ledgeContactContourStartCm[edge] =
                resolvedNeighborLedge->contourStartCm;
            ledgeContactStartJoins[edge] =
                resolvedNeighborLedge->startJoin;
            ledgeContactEndJoins[edge] =
                resolvedNeighborLedge->endJoin;
        }
        constexpr std::array<
            std::array<std::array<std::size_t, 2>, 2>,
            4> cornerEndpoints{{
            {{{0u, 1u}, {1u, 0u}}},
            {{{1u, 1u}, {2u, 0u}}},
            {{{2u, 1u}, {3u, 0u}}},
            {{{3u, 1u}, {0u, 0u}}},
        }};
        for (std::size_t corner = 0u;
             corner < cornerEndpoints.size();
             ++corner) {
            const auto first = cornerEndpoints[corner][0u];
            const auto second = cornerEndpoints[corner][1u];
            if ((ledgeCrownClipMask & (1u << first[0])) != 0u &&
                (ledgeCrownClipMask & (1u << second[0])) != 0u &&
                ledgeCrownEndpointWeights[first[0]][first[1]] > 0.5f &&
                ledgeCrownEndpointWeights[second[0]][second[1]] > 0.5f) {
                ledgeCrownConvexCornerMask |= 1u << corner;
            }
        }
    }
    const std::string key =
        "route1:terrain-tile:" + tile.shape + ":" +
        tile.surface + ":cell-" +
        std::to_string(tile.gridX) + "-" +
        std::to_string(tile.gridZ) +
        ":connections-" + std::to_string(dirtConnectionMask);
    std::string resolvedKey = key;
    if (tile.cleanSuppressedEncounterGrassTint) {
        resolvedKey += ":clean-suppressed-encounter-tint";
    }
    if (tile.rebuildContinuousMaterialFields) {
        // Neighbor edits can change field ownership without changing this
        // tile's own authored values. Keep both mesh variants in the cache so
        // a live preview cannot reuse stale source UV0/UV1 geometry.
        resolvedKey += ":continuous-material-fields";
    }
    if (sourceSeamOverlapMask != 0u) {
        resolvedKey += ":source-seam-overlap-" +
            std::to_string(sourceSeamOverlapMask);
    }
    for (std::size_t edge = 0u; edge < 4u; ++edge) {
        if ((ledgeCrownClipMask & (1u << edge)) == 0u) {
            continue;
        }
        resolvedKey += ":ledge-crown-" +
            std::to_string(edge) + "-" +
            std::to_string(static_cast<int>(
                ledgeCrownEndpointWeights[edge][0])) + "-" +
            std::to_string(static_cast<int>(
                ledgeCrownEndpointWeights[edge][1])) + "-contour-" +
            std::to_string(static_cast<std::int32_t>(std::lround(
                ledgeCrownContourStartCm[edge])));
    }
    if (ledgeCrownConvexCornerMask != 0u) {
        resolvedKey += ":ledge-crown-corners-" +
            std::to_string(ledgeCrownConvexCornerMask);
    }
    for (std::size_t edge = 0u; edge < 4u; ++edge) {
        if ((ledgeContactOverlapMask & (1u << edge)) == 0u) {
            continue;
        }
        resolvedKey += ":ledge-contact-" +
            std::to_string(edge) + "-" +
            std::to_string(static_cast<int>(
                ledgeContactEndpointWeights[edge][0])) + "-" +
            std::to_string(static_cast<int>(
                ledgeContactEndpointWeights[edge][1])) + "-contour-" +
            std::to_string(static_cast<std::int32_t>(std::lround(
                ledgeContactContourStartCm[edge]))) + "-joins-" +
            std::to_string(static_cast<std::uint32_t>(
                ledgeContactStartJoins[edge])) + "-" +
            std::to_string(static_cast<std::uint32_t>(
                ledgeContactEndJoins[edge]));
    }
    for (std::size_t transitionIndex = 0u;
         transitionIndex < rampDirtNeighborTransitionCount;
         ++transitionIndex) {
        const auto& transition =
            rampDirtNeighborTransitions[transitionIndex];
        resolvedKey += ":ramp-neighbor-" +
            std::to_string(transition.edge) +
            (transition.highSide ? "-high" : "-low");
    }
    for (std::size_t transitionIndex = 0u;
         transitionIndex < dirtLawnNeighborTransitionCount;
         ++transitionIndex) {
        resolvedKey += ":lawn-neighbor-" +
            std::to_string(
                dirtLawnNeighborTransitions[transitionIndex].edge);
    }
    if (dirt) {
        for (std::size_t edge = 0u; edge < 4u; ++edge) {
            if ((transitionUv.boundaryMask & (1u << edge)) == 0u) {
                continue;
            }
            resolvedKey += ":edge" + std::to_string(edge) + "-u" +
                std::to_string(static_cast<std::int32_t>(std::lround(
                    transitionUv.edgeStartU[edge] * 100000.0f))) + "-du" +
                std::to_string(static_cast<std::int32_t>(std::lround(
                    transitionUv.edgeUPerCm[edge] * 10000000.0f)));
        }
    }
    auto [found, inserted] =
        terrainTilePrototypes.topPrototypes.try_emplace(resolvedKey);
    auto& prototype = found->second;
    if (!inserted) {
        return prototype.object;
    }

    const std::uint32_t rowWidth = kGridResolution + 1u;
    // Radially projecting every square-grid vertex beyond a convex crown
    // onto the same short arc preserves the silhouette, but it also turns
    // grid cells wholly outside that arc into overlapping sliver triangles.
    // Remember which corner clipped each source vertex so those redundant
    // cells can be retired during index emission. Boundary cells with at
    // least one interior vertex remain and form the actual contour.
    std::vector<std::uint32_t> convexCornerClippedVertexMasks(
        rowWidth * rowWidth,
        0u);
    prototype.vertices.reserve(rowWidth * rowWidth * 2u);
    prototype.sourceVertices.reserve(rowWidth * rowWidth * 2u);
    prototype.indices.reserve(
        kGridResolution * kGridResolution * 6u);
    struct SourceCrownFieldSample {
        glm::vec3 positionCm{};
        glm::vec3 normal{};
        bool sampled = false;
    };
    struct SourceCrownField {
        std::array<SourceCrownFieldSample, kGridResolution + 1u>
            samples{};
        bool complete = false;
    };
    std::array<SourceCrownField, 4> sourceCrownFields{};
    constexpr std::array<glm::vec2, 4> sourceCrownTangents{
        glm::vec2{1.0f, 0.0f},
        glm::vec2{0.0f, -1.0f},
        glm::vec2{-1.0f, 0.0f},
        glm::vec2{0.0f, 1.0f}};
    const float terrainTileCenterX =
        (static_cast<float>(tile.gridX) + 0.5f) *
        kTerrainTileSizeCm;
    const float terrainTileCenterZ =
        (static_cast<float>(tile.gridZ) + 0.5f) *
        kTerrainTileSizeCm;
    const auto logicalBoundaryLocal =
        [](std::size_t edge, float phase) {
            return edge == 0u
                       ? glm::vec2{
                             (phase - 0.5f) * kTerrainTileSizeCm,
                             kTerrainTileSizeCm * 0.5f}
                   : edge == 1u ? glm::vec2{kTerrainTileSizeCm * 0.5f, (0.5f - phase) * kTerrainTileSizeCm}
                   : edge == 2u ? glm::vec2{(0.5f - phase) * kTerrainTileSizeCm, -kTerrainTileSizeCm * 0.5f}
                                : glm::vec2{-kTerrainTileSizeCm * 0.5f, (phase - 0.5f) * kTerrainTileSizeCm};
        };
    for (std::size_t edge = 0u; edge < sourceCrownFields.size(); ++edge) {
        if ((ledgeCrownClipMask & (1u << edge)) == 0u) {
            continue;
        }
        if (terrainPatchV2PreviewEnabled &&
            ledgeCrownContourEdges[edge] &&
            ledgeCrownContourEdges[edge]->frames.size() ==
                kGridResolution + 1u) {
            // Edited V2 carriers use the contour frame as their sole
            // geometry/normal owner. Sampling the imported crown remains a
            // material-reference operation elsewhere; independently snapping
            // this cap back to it would reopen the shared join.
            continue;
        }
        bool hasRequiredSample = false;
        bool complete = true;
        const glm::vec2 edgeDirection{
            static_cast<float>(rampNeighborDirections[edge][0]),
            static_cast<float>(rampNeighborDirections[edge][1])};
        for (std::uint32_t sampleIndex = 0u;
             sampleIndex <= kGridResolution;
             ++sampleIndex) {
            const float phase = static_cast<float>(sampleIndex) /
                                static_cast<float>(kGridResolution);
            const float weight = std::lerp(
                ledgeCrownEndpointWeights[edge][0],
                ledgeCrownEndpointWeights[edge][1],
                phase);
            if (weight <= 0.0f) {
                continue;
            }
            hasRequiredSample = true;
            const float contourDistance =
                ledgeCrownContourStartCm[edge] +
                phase * kTerrainTileSizeCm;
            const float crownOutward = weight *
                                       (kTerrainLedgeBaseInsetCm +
                                        terrainLedgeContourWobbleCm(contourDistance) +
                                        kTerrainLedgeCrownSafetyOverlapCm);
            const float inset = std::max(0.0f, -crownOutward);
            const glm::vec2 proceduralCrownLocal =
                logicalBoundaryLocal(edge, phase) -
                edgeDirection * inset;
            float unusedSourceUv1U = 0.0f;
            glm::vec4 unusedSourceColor{1.0f};
            auto &sample = sourceCrownFields[edge].samples[sampleIndex];
            sample.sampled = sampleSourceTerrainFringeMaterial(
                {terrainTileCenterX + proceduralCrownLocal.x,
                 static_cast<float>(tile.elevationLevel) *
                     kTerrainElevationStepCm,
                 terrainTileCenterZ + proceduralCrownLocal.y},
                sourceCrownTangents[edge],
                0u,
                unusedSourceUv1U,
                unusedSourceColor,
                &sample.positionCm,
                &sample.normal);
            complete = sample.sampled && complete;
        }
        sourceCrownFields[edge].complete =
            hasRequiredSample && complete;
    }
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
            glm::vec4 normalizedTintColor{1.0f};
            float normalizedBoundaryWeight = 0.0f;
            const bool normalizedTintSampled =
                tile.cleanSuppressedEncounterGrassTint &&
                sampleNormalizedSourceTintColor(
                    tile,
                    localX,
                    localZ,
                    normalizedTintColor,
                    &normalizedBoundaryWeight);
            const bool sourceTopologyMatches =
                tile.elevationLevel == tile.sourceElevationLevel &&
                tile.shape == tile.sourceShape;
            // A non-geometric edit can retain exact source UV0/UV1. Once the
            // elevation or shape changes, sampling those fields from the old
            // mesh alternates between the jagged former surface and fallback
            // strips. Reconstruct one continuous world-space field instead.
            // Tint-normalized encounter-grass cells also reconstruct these
            // fields: their old UV1 carrier contains the source patch's
            // projected-lighting coordinates and creates a vertical delimiter
            // against adjoining rebuilt lawn even when Color0 already agrees.
            // Color0 remains surface-dependent and is rebuilt below.
            const bool ledgeDeformsSurface =
                ledgeCrownClipMask != 0u ||
                ledgeContactOverlapMask != 0u;
            // Authorship also covers render-only controls such as projected
            // shadow reception. When topology is unchanged, keep the decoded
            // source surface heights even though the source triangles must be
            // resubmitted under a different material policy. Flattening every
            // authored tile to a procedural plane created the visible ruler-
            // straight seam beside otherwise untouched source ledges.
            const bool preserveSourceGeometry =
                sourceSampled && sourceTopologyMatches &&
                !ledgeDeformsSurface;
            const bool preserveSourceField =
                sourceSampled &&
                !tile.rebuildContinuousMaterialFields &&
                (!tile.authored || sourceTopologyMatches);
            vertex.x = (localX - 0.5f) * kTerrainTileSizeCm;
            vertex.y = preserveSourceGeometry
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
            if (!preserveSourceGeometry && tile.shape == "ramp_north") {
                vertex.y = localZ * kTerrainElevationStepCm;
                vertex.ny = rampNormalY;
                vertex.nz = -rampNormalSide;
            } else if (!preserveSourceGeometry &&
                       tile.shape == "ramp_east") {
                vertex.y = localX * kTerrainElevationStepCm;
                vertex.nx = -rampNormalSide;
                vertex.ny = rampNormalY;
            } else if (!preserveSourceGeometry &&
                       tile.shape == "ramp_south") {
                vertex.y = (1.0f - localZ) *
                    kTerrainElevationStepCm;
                vertex.ny = rampNormalY;
                vertex.nz = rampNormalSide;
            } else if (!preserveSourceGeometry &&
                       tile.shape == "ramp_west") {
                vertex.y = (1.0f - localX) *
                    kTerrainElevationStepCm;
                vertex.nx = rampNormalSide;
                vertex.ny = rampNormalY;
            }
            if (normalizedBoundaryWeight > 0.0f && sourceSampled) {
                const float sourceRelativeY =
                    sourceSample.y -
                    static_cast<float>(tile.sourceElevationLevel) *
                        kTerrainElevationStepCm -
                    kTerrainTileTopDepthBiasCm;
                vertex.y = std::lerp(
                    vertex.y,
                    sourceRelativeY,
                    normalizedBoundaryWeight);
            }

            glm::vec3 ledgeCrownNormalAccumulator{0.0f};
            float ledgeCrownNormalBlend = 0.0f;
            const auto clipToLedgeCrown =
                [&](std::size_t edge,
                    float phase,
                    float distanceFromBoundaryCm) {
                    if ((ledgeCrownClipMask & (1u << edge)) == 0u) {
                        return;
                    }
                    const float weight = std::lerp(
                        ledgeCrownEndpointWeights[edge][0],
                        ledgeCrownEndpointWeights[edge][1],
                        phase);
                    if (weight <= 0.0f) {
                        return;
                    }
                    const float contourDistance =
                        ledgeCrownContourStartCm[edge] +
                        phase * kTerrainTileSizeCm;
                    const float crownOutward = weight *
                        (kTerrainLedgeBaseInsetCm +
                         terrainLedgeContourWobbleCm(contourDistance) +
                         kTerrainLedgeCrownSafetyOverlapCm);
                    const float inset = std::max(0.0f, -crownOutward);
                    if (inset <= 0.0f) {
                        return;
                    }
                    const glm::vec2 edgeDirection{
                        static_cast<float>(
                            rampNeighborDirections[edge][0]),
                        static_cast<float>(
                            rampNeighborDirections[edge][1])};
                    const glm::vec2 boundaryLocal =
                        logicalBoundaryLocal(edge, phase);
                    const glm::vec2 proceduralCrownLocal =
                        boundaryLocal - edgeDirection * inset;
                    const std::uint32_t sourceCrownSampleIndex =
                        std::min(
                            kGridResolution,
                            static_cast<std::uint32_t>(std::lround(
                                phase *
                                static_cast<float>(kGridResolution))));
                    const auto &sourceCrownSample =
                        sourceCrownFields[edge]
                            .samples[sourceCrownSampleIndex];
                    const bool sourceCrownSampled =
                        sourceCrownFields[edge].complete &&
                        sourceCrownSample.sampled;
                    // Remap the complete tile interval onto the physical
                    // crown-to-interior span. Collapsing only the absent
                    // outer 27 cm into a sub-centimetre ribbon stacked six
                    // textured grid columns on the crown, producing the dark
                    // rectangular sheet and unstable layered leaves visible
                    // from above. The opposite edge remains fixed, so the
                    // rebuilt cap still meets untouched source lawn exactly.
                    const float clippedDistance = inset +
                        distanceFromBoundaryCm *
                            ((kTerrainTileSizeCm - inset) /
                             kTerrainTileSizeCm);
                    const float inward =
                        clippedDistance - distanceFromBoundaryCm;
                    vertex.x -= static_cast<float>(
                        rampNeighborDirections[edge][0]) * inward;
                    vertex.z -= static_cast<float>(
                        rampNeighborDirections[edge][1]) * inward;
                    const float sourceCrownBlend = weight * std::clamp(
                                                                1.0f -
                                                                    distanceFromBoundaryCm /
                                                                        kTerrainTileSizeCm,
                                                                0.0f,
                                                                1.0f);
                    if (sourceCrownSampled) {
                        const glm::vec2 sourceCrownLocal{
                            sourceCrownSample.positionCm.x -
                                terrainTileCenterX,
                            sourceCrownSample.positionCm.z -
                                terrainTileCenterZ};
                        // Material 13 is alpha-cut at the leaf tips. Preserve
                        // the two-centimetre lawn underlap after recovering the
                        // irregular source crown; snapping material 19 back to
                        // the exact material-13 contour exposes background in
                        // the transparent texels as a dashed black seam.
                        const glm::vec2 recoveredCapLocal =
                            sourceCrownLocal -
                            edgeDirection *
                                kTerrainLedgeCrownSafetyOverlapCm;
                        const glm::vec2 sourceAdjustment =
                            recoveredCapLocal - proceduralCrownLocal;
                        vertex.x += sourceAdjustment.x *
                                    sourceCrownBlend;
                        vertex.z += sourceAdjustment.y *
                                    sourceCrownBlend;
                        const float sourceRelativeY =
                            sourceCrownSample.positionCm.y -
                            static_cast<float>(tile.elevationLevel) *
                                kTerrainElevationStepCm;
                        vertex.y = std::lerp(
                            vertex.y,
                            sourceRelativeY,
                            sourceCrownBlend);
                    }
                    // The imported material-19 cap and material-13 crown
                    // duplicate both position and normal at their shared
                    // contour. Blend the generated cap from that recovered
                    // crown normal back to its ordinary surface normal over
                    // the complete crown-to-interior span. A flat up-normal
                    // at the boundary creates a lighting seam even when the
                    // two meshes are geometrically watertight.
                    // Match the decoded crown normal only in the physical
                    // crown band. Blending that normal across the complete
                    // metre made an otherwise source-backed transition tile
                    // read as one large rectangular lighting patch.
                    constexpr float kCrownNormalBlendDistanceCm = 35.0f;
                    const float normalBlend = weight * std::clamp(
                        1.0f -
                            distanceFromBoundaryCm /
                                kCrownNormalBlendDistanceCm,
                        0.0f,
                        1.0f);
                    const glm::vec3 crownNormal = sourceCrownSampled
                                                      ? sourceCrownSample.normal
                                                      : glm::normalize(glm::vec3{
                                                            edgeDirection.x *
                                                                kTerrainLedgeFringeNormalOutward[0u],
                                                            kTerrainLedgeFringeNormalY[0u],
                                                            edgeDirection.y *
                                                                kTerrainLedgeFringeNormalOutward[0u]});
                    constexpr float kEqualNormalBlendEpsilon = 1.0e-5f;
                    if (normalBlend >
                        ledgeCrownNormalBlend +
                            kEqualNormalBlendEpsilon) {
                        ledgeCrownNormalAccumulator =
                            crownNormal * normalBlend;
                        ledgeCrownNormalBlend = normalBlend;
                    } else if (std::abs(
                                   normalBlend -
                                   ledgeCrownNormalBlend) <=
                               kEqualNormalBlendEpsilon) {
                        // Equal influence occurs on a real corner bisector.
                        // Combine only those ties into the radial normal;
                        // weaker perpendicular edges must not tilt a straight
                        // crown several rows before the corner begins.
                        ledgeCrownNormalAccumulator +=
                            crownNormal * normalBlend;
                    }
                };
            clipToLedgeCrown(
                0u, localX,
                (1.0f - localZ) * kTerrainTileSizeCm);
            clipToLedgeCrown(
                1u, 1.0f - localZ,
                (1.0f - localX) * kTerrainTileSizeCm);
            clipToLedgeCrown(
                2u, 1.0f - localX,
                localZ * kTerrainTileSizeCm);
            clipToLedgeCrown(
                3u, localZ,
                localX * kTerrainTileSizeCm);

            if (ledgeCrownNormalBlend > 0.0f &&
                glm::length(ledgeCrownNormalAccumulator) > 0.0001f) {
                const glm::vec3 crownNormal = glm::normalize(
                    ledgeCrownNormalAccumulator);
                const glm::vec3 surfaceNormal = glm::normalize(glm::vec3{
                    vertex.nx, vertex.ny, vertex.nz});
                const glm::vec3 blendedNormal = glm::normalize(glm::mix(
                    surfaceNormal,
                    crownNormal,
                    ledgeCrownNormalBlend));
                vertex.nx = blendedNormal.x;
                vertex.ny = blendedNormal.y;
                vertex.nz = blendedNormal.z;
            }

            constexpr std::array<std::array<float, 2>, 4>
                crownCornerSigns{{
                    {1.0f, 1.0f},
                    {1.0f, -1.0f},
                    {-1.0f, -1.0f},
                    {-1.0f, 1.0f},
                }};
            constexpr float crownCornerCenter =
                kTerrainTileSizeCm * 0.5f -
                route1_terrain_ledges::kConvexCornerRadiusCm;
            constexpr float crownCornerRadius =
                route1_terrain_ledges::kConvexCornerRadiusCm +
                kTerrainLedgeBaseInsetCm +
                kTerrainLedgeCrownSafetyOverlapCm;
            for (std::size_t corner = 0u;
                 corner < crownCornerSigns.size();
                 ++corner) {
                if ((ledgeCrownConvexCornerMask & (1u << corner)) == 0u) {
                    continue;
                }
                const glm::vec2 sign{
                    crownCornerSigns[corner][0],
                    crownCornerSigns[corner][1]};
                const glm::vec2 center = sign * crownCornerCenter;
                glm::vec2 delta{
                    vertex.x - center.x,
                    vertex.z - center.y};
                if (delta.x * sign.x < 0.0f ||
                    delta.y * sign.y < 0.0f) {
                    continue;
                }
                const float distance = glm::length(delta);
                if (distance <= crownCornerRadius ||
                    distance <= 0.0001f) {
                    continue;
                }
                convexCornerClippedVertexMasks[
                    zIndex * rowWidth + xIndex] |= 1u << corner;
                delta *= crownCornerRadius / distance;
                vertex.x = center.x + delta.x;
                vertex.z = center.y + delta.y;
            }

            if (zIndex == kGridResolution &&
                (sourceSeamOverlapMask & (1u << 0u)) != 0u) {
                vertex.z += kTerrainSourceSeamOverlapCm;
            }
            if (xIndex == kGridResolution &&
                (sourceSeamOverlapMask & (1u << 1u)) != 0u) {
                vertex.x += kTerrainSourceSeamOverlapCm;
            }
            if (zIndex == 0u &&
                (sourceSeamOverlapMask & (1u << 2u)) != 0u) {
                vertex.z -= kTerrainSourceSeamOverlapCm;
            }
            if (xIndex == 0u &&
                (sourceSeamOverlapMask & (1u << 3u)) != 0u) {
                vertex.x -= kTerrainSourceSeamOverlapCm;
            }

            const auto extendLedgeContact = [&](
                    std::size_t edge,
                    float phase,
                    float distanceFromBoundaryCm) {
                if ((ledgeContactOverlapMask & (1u << edge)) == 0u) {
                    return;
                }
                const float weight = std::lerp(
                    ledgeContactEndpointWeights[edge][0],
                    ledgeContactEndpointWeights[edge][1],
                    phase);
                // The neighboring high edge runs in the opposite direction.
                // Match its recovered -2 cm foot and the exact contour wander
                // instead of pushing a constant-width lawn rectangle beneath
                // the wall.
                constexpr float kCliffFootOutwardCm =
                    25.0f + kTerrainLedgeBaseInsetCm;
                const float neighborContourPhase = 1.0f - phase;
                const float contourDistance =
                    ledgeContactContourStartCm[edge] +
                    neighborContourPhase * kTerrainTileSizeCm;
                const float cliffFootOutward = weight *
                    (kCliffFootOutwardCm +
                     terrainLedgeContourWobbleCm(contourDistance));
                const float overlap = std::max(
                    0.0f,
                    -cliffFootOutward +
                        kTerrainLedgeFootSafetyOverlapCm * weight);
                constexpr float kNormalContactBlendCm =
                    kTerrainTileSizeCm /
                    static_cast<float>(kGridResolution);
                const float normalContactWeight = std::clamp(
                    1.0f -
                        distanceFromBoundaryCm / kNormalContactBlendCm,
                    0.0f,
                    1.0f);
                vertex.x += static_cast<float>(
                    rampNeighborDirections[edge][0]) * overlap *
                    normalContactWeight;
                vertex.z += static_cast<float>(
                    rampNeighborDirections[edge][1]) * overlap *
                    normalContactWeight;
                if (!ramp && normalContactWeight > 0.0f) {
                    vertex.y = std::min(
                        vertex.y,
                        -kTerrainLedgeContactTuckCm * weight *
                            normalContactWeight);
                }

                // Keep the low lawn continuous up to the logical grid
                // corner. The rounded wall and the narrow normal underlap
                // already hide this carrier. Pulling the ground tangentially
                // toward the arc instead leaves a 32 cm triangular void
                // between each side tile and the diagonal low tile.
            };
            extendLedgeContact(
                0u,
                localX,
                (1.0f - localZ) * kTerrainTileSizeCm);
            extendLedgeContact(
                1u,
                1.0f - localZ,
                (1.0f - localX) * kTerrainTileSizeCm);
            extendLedgeContact(
                2u,
                1.0f - localX,
                localZ * kTerrainTileSizeCm);
            extendLedgeContact(
                3u,
                localZ,
                localX * kTerrainTileSizeCm);

            // A source cap promoted by a neighboring ledge edit can retain
            // its canonical material fields, but those fields must follow
            // the cap after it is clipped onto the crown/contact contour.
            // Sampling at the original square-grid coordinate stretches the
            // old corner patch across the rounded geometry, producing a dark
            // diagonal and a plainly rectangular grass sheet at the join.
            SourceTerrainSurfaceSample deformedSourceSample = sourceSample;
            bool deformedSourceSampled = sourceSampled;
            if (sourceSampled && sourceTopologyMatches &&
                ledgeDeformsSurface) {
                SourceTerrainSurfaceSample candidate;
                const float deformedLocalX = std::clamp(
                    vertex.x / kTerrainTileSizeCm + 0.5f,
                    0.0f,
                    1.0f);
                const float deformedLocalZ = std::clamp(
                    vertex.z / kTerrainTileSizeCm + 0.5f,
                    0.0f,
                    1.0f);
                if (sampleSourceTerrainSurface(
                        tile,
                        deformedLocalX,
                        deformedLocalZ,
                        candidate)) {
                    deformedSourceSample = candidate;
                    deformedSourceSampled = true;
                }
            }
            const float materialWorldGridX =
                static_cast<float>(tile.gridX) + 0.5f +
                vertex.x / kTerrainTileSizeCm;
            const float materialWorldGridZ =
                static_cast<float>(tile.gridZ) + 0.5f +
                vertex.z / kTerrainTileSizeCm;

            // The canonical Route 1 mesh, not a guessed metre grid, owns the
            // UV/color field. Source-backed cells retain their exact authored
            // samples; new cells use one continuous source-world fallback
            // field instead of independent per-tile variants.
            const glm::vec2 baseUv0 = preserveSourceField
                ? deformedSourceSample.uv0
                : glm::vec2(
                      (static_cast<float>(tile.gridX) + 0.5f +
                       vertex.x / kTerrainTileSizeCm) / 3.0f,
                      (static_cast<float>(tile.gridZ) + 0.5f +
                       vertex.z / kTerrainTileSizeCm) / 3.0f);
            vertex.u = baseUv0.x;
            vertex.v = baseUv0.y;
            const glm::vec2 baseUv1 = preserveSourceField
                ? deformedSourceSample.uv1
                : baseUv0;
            vertex.sourceUv1U = baseUv1.x;
            vertex.sourceUv1V = baseUv1.y;
            sourceVertex.texcoords[0] = {vertex.u, vertex.v};
            sourceVertex.texcoords[1] = {
                vertex.sourceUv1U,
                vertex.sourceUv1V};

            // UV2 is the lawn/soil selector. Once a cell is authored it must
            // be rebuilt from the edited neighbor topology even when its
            // surface name happens to match the source; otherwise the old
            // path boundary remains stamped into the replacement cell.
            // A canonical cap promoted to generated geometry keeps the same
            // surface and topology even though its outer vertices are moved
            // onto the resolved ledge crown. Preserve its recovered UV2 and
            // Color0 fields independently from source geometry; otherwise a
            // rebuilt contour turns every inherited cap into a visibly plain
            // grass island beside the untouched source lawn.
            const bool preserveSourceSurface =
                deformedSourceSampled && sourceTopologyMatches &&
                tile.surface == tile.sourceSurface && !dirt &&
                !tile.cleanSuppressedEncounterGrassTint &&
                !tile.rebuildContinuousMaterialFields;
            if (preserveSourceSurface) {
                vertex.sourceUv2U = deformedSourceSample.uv2.x;
                vertex.sourceUv2V = deformedSourceSample.uv2.y;
                sourceVertex.texcoords[2] = {
                    vertex.sourceUv2U,
                    vertex.sourceUv2V};
            } else if (dirt) {
                std::array<float, 4> distanceCm{
                    (1.0f - localZ) * kTerrainTileSizeCm,
                    (1.0f - localX) * kTerrainTileSizeCm,
                    localZ * kTerrainTileSizeCm,
                    localX * kTerrainTileSizeCm};
                std::array<float, 4> alongCm{
                    localX * kTerrainTileSizeCm,
                    (1.0f - localZ) * kTerrainTileSizeCm,
                    (1.0f - localX) * kTerrainTileSizeCm,
                    localZ * kTerrainTileSizeCm};
                float nearestDistance =
                    std::numeric_limits<float>::max();
                for (std::size_t edge = 0u; edge < 4u; ++edge) {
                    if ((transitionUv.boundaryMask & (1u << edge)) == 0u) {
                        continue;
                    }
                    nearestDistance = std::min(
                        nearestDistance,
                        distanceCm[edge]);
                }
                if (transitionUv.boundaryMask == 0u) {
                    vertex.sourceUv2U = kCleanDirtUv2[0];
                    vertex.sourceUv2V = kCleanDirtUv2[1];
                    sourceVertex.texcoords[2] = kCleanDirtUv2;
                } else {
                    // Blend the paired U values only where two source-style
                    // boundary strips meet. This is the grid reconstruction
                    // of the source ribbon's shared corner vertices: U stays
                    // tangential and constant across the strip instead of
                    // being diagonally sheared through it.
                    float transitionU = 0.0f;
                    float transitionWeight = 0.0f;
                    float referenceU = 0.0f;
                    bool hasReferenceU = false;
                    for (std::size_t edge = 0u; edge < 4u; ++edge) {
                        if ((transitionUv.boundaryMask & (1u << edge)) == 0u) {
                            continue;
                        }
                        const float influence = std::clamp(
                            (kBoundaryWidthCm - distanceCm[edge]) /
                                kBoundaryWidthCm,
                            0.0f,
                            1.0f);
                        constexpr float kInnerSeamEpsilonCm = 0.001f;
                        if (influence <= 0.0f &&
                            distanceCm[edge] >
                                kBoundaryWidthCm +
                                    kInnerSeamEpsilonCm) {
                            continue;
                        }
                        float candidateU = transitionUv.edgeStartU[edge] +
                            alongCm[edge] *
                                transitionUv.edgeUPerCm[edge];
                        if (!hasReferenceU) {
                            referenceU = candidateU;
                            hasReferenceU = true;
                        } else {
                            candidateU -= std::round(
                                candidateU - referenceU);
                        }
                        // The source ribbon retains its paired tangential U
                        // at the inner seam. Give that exact zero-influence
                        // row a tiny non-zero weight so it cannot fall back to
                        // the unrelated clean-dirt U coordinate.
                        const float weight = std::max(
                            influence * influence,
                            1.0e-8f);
                        transitionU += candidateU * weight;
                        transitionWeight += weight;
                    }
                    transitionU = transitionWeight > 0.0f
                        ? transitionU / transitionWeight
                        : kCleanDirtUv2[0];
                    const glm::vec2 sourceUv2{
                        transitionU,
                        route1DirtTransitionUv2V(
                            nearestDistance)};
                    vertex.sourceUv2U = sourceUv2.x;
                    vertex.sourceUv2V = sourceUv2.y;
                    sourceVertex.texcoords[2] = {
                        vertex.sourceUv2U,
                        vertex.sourceUv2V};
                }
            } else {
                const glm::vec2 cleanUv2{
                    kCleanLawnUv2[0], kCleanLawnUv2[1]};
                // UV2 selects the source grass/lawn mask. It is valid to
                // retain the source selector when an edit only changes a
                // non-geometric property such as projected-shadow receipt.
                // Once the elevation or shape changes, however, retaining
                // the old selector preserves the former ledge lip as narrow
                // lines across the rebuilt floor. Reconstruct that field
                // from compatible lawn at the target elevation instead.
                const bool preserveSourceLawnDetail =
                    deformedSourceSampled &&
                    tile.surface == "light_lawn" &&
                    (tile.sourceSurface == "light_lawn" ||
                     tile.cleanSuppressedEncounterGrassTint) &&
                    sourceTopologyMatches;
                glm::vec2 resolvedUv2 = cleanUv2;
                if (preserveSourceLawnDetail) {
                    resolvedUv2 = deformedSourceSample.uv2;
                } else if (tile.surface == "light_lawn") {
                    sampleTargetTerrainUv2(
                        tile.surface,
                        tile.elevationLevel,
                        materialWorldGridX,
                        materialWorldGridZ,
                        resolvedUv2);
                }
                vertex.sourceUv2U = resolvedUv2.x;
                vertex.sourceUv2V = resolvedUv2.y;
                sourceVertex.texcoords[2] = {
                    resolvedUv2.x, resolvedUv2.y};
            }
            glm::vec4 targetColor{1.0f};
            bool targetColorSampled = false;
            if (tile.cleanSuppressedEncounterGrassTint) {
                targetColor = normalizedTintColor;
                targetColorSampled = normalizedTintSampled;
            } else if (preserveSourceSurface) {
                targetColor = deformedSourceSample.color0;
                targetColorSampled = true;
            } else if (dirt && !ramp && tile.authored) {
                const auto cleanDirt = route1CleanFlatDirtColor();
                targetColor = glm::vec4{
                    cleanDirt[0],
                    cleanDirt[1],
                    cleanDirt[2],
                    cleanDirt[3]};
                targetColorSampled = true;
            } else if (!dark) {
                targetColorSampled = sampleTargetTerrainColor(
                    tile.surface,
                    tile.elevationLevel,
                    materialWorldGridX,
                    materialWorldGridZ,
                    targetColor);
            }
            if (targetColorSampled && !dark) {
                vertex.r = targetColor.r;
                vertex.g = targetColor.g;
                vertex.b = targetColor.b;
                vertex.a = targetColor.a;
                sourceVertex.colors[0] = {
                    targetColor.r,
                    targetColor.g,
                    targetColor.b,
                    targetColor.a};
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
            if (dirt && ramp) {
                const float highWeight = std::clamp(
                    vertex.y / kTerrainElevationStepCm,
                    0.0f,
                    1.0f);
                const float crossRamp =
                    (tile.shape == "ramp_north" ||
                     tile.shape == "ramp_south")
                    ? localX
                    : localZ;
                auto rampColor =
                    route1SignRampDirtColor(highWeight, crossRamp);
                vertex.r = rampColor[0];
                vertex.g = rampColor[1];
                vertex.b = rampColor[2];
                vertex.a = rampColor[3];
                sourceVertex.colors[0] = {
                    rampColor[0],
                    rampColor[1],
                    rampColor[2],
                    rampColor[3]};
            } else if (dirt &&
                       rampDirtNeighborTransitionCount > 0u) {
                const std::array<float, 4> normalDirtColor{
                    vertex.r, vertex.g, vertex.b, vertex.a};
                std::array<float, 4> boundaryColor{};
                std::array<float, 4> singleMixedColor{};
                float boundaryWeightSum = 0.0f;
                float remainingNormalWeight = 1.0f;
                for (std::size_t transitionIndex = 0u;
                     transitionIndex <
                         rampDirtNeighborTransitionCount;
                     ++transitionIndex) {
                    const auto& transition =
                        rampDirtNeighborTransitions[transitionIndex];
                    float boundaryWeight = 0.0f;
                    float crossRamp = 0.0f;
                    switch (transition.edge) {
                    case 0u:
                        boundaryWeight = localZ;
                        crossRamp = localX;
                        break;
                    case 1u:
                        boundaryWeight = localX;
                        crossRamp = localZ;
                        break;
                    case 2u:
                        boundaryWeight = 1.0f - localZ;
                        crossRamp = localX;
                        break;
                    default:
                        boundaryWeight = 1.0f - localX;
                        crossRamp = localZ;
                        break;
                    }
                    const auto rampBoundary =
                        route1SignRampDirtColor(
                            transition.highSide ? 1.0f : 0.0f,
                            crossRamp);
                    if (transitionIndex == 0u) {
                        singleMixedColor =
                            route1SignRampAdjacentDirtColor(
                                normalDirtColor,
                                boundaryWeight,
                                crossRamp,
                                transition.highSide);
                    }
                    for (std::size_t channel = 0u;
                         channel < boundaryColor.size();
                         ++channel) {
                        boundaryColor[channel] +=
                            rampBoundary[channel] * boundaryWeight;
                    }
                    boundaryWeightSum += boundaryWeight;
                    remainingNormalWeight *= 1.0f - boundaryWeight;
                }
                if (boundaryWeightSum > 0.0f) {
                    for (float& channel : boundaryColor) {
                        channel /= boundaryWeightSum;
                    }
                    const float combinedBoundaryWeight =
                        1.0f - remainingNormalWeight;
                    auto mixedColor = singleMixedColor;
                    // A usual flat has one ramp neighbor and follows the
                    // exact helper above. If ramps meet at a flat corner,
                    // average their source boundary controls and use smooth
                    // union coverage so neither transition is discarded.
                    if (rampDirtNeighborTransitionCount > 1u) {
                        for (std::size_t channel = 0u;
                             channel < mixedColor.size();
                             ++channel) {
                            mixedColor[channel] = std::lerp(
                                normalDirtColor[channel],
                                boundaryColor[channel],
                                combinedBoundaryWeight);
                        }
                    }
                    for (std::size_t channel = 0u;
                         channel < mixedColor.size();
                         ++channel) {
                        const float value = mixedColor[channel];
                        sourceVertex.colors[0][channel] = value;
                        switch (channel) {
                        case 0u: vertex.r = value; break;
                        case 1u: vertex.g = value; break;
                        case 2u: vertex.b = value; break;
                        default: vertex.a = value; break;
                        }
                    }
                }
            }
            // The source grass/soil ribbon controls Color0 as well as UV2.
            // Apply it to every dirt shape. Restricting this to ramps left a
            // square tint delimiter wherever an edited lawn met flat path.
            if (dirt && dirtLawnNeighborTransitionCount > 0u) {
                std::array<float, 4> dirtColor{
                    vertex.r, vertex.g, vertex.b, vertex.a};
                for (std::size_t transitionIndex = 0u;
                     transitionIndex < dirtLawnNeighborTransitionCount;
                     ++transitionIndex) {
                    const auto& transition =
                        dirtLawnNeighborTransitions[transitionIndex];
                    if (!transition.lawnTile) {
                        continue;
                    }
                    float distanceToEdgeCm = 0.0f;
                    switch (transition.edge) {
                    case 0u:
                        distanceToEdgeCm =
                            (1.0f - localZ) * kTerrainTileSizeCm;
                        break;
                    case 1u:
                        distanceToEdgeCm =
                            (1.0f - localX) * kTerrainTileSizeCm;
                        break;
                    case 2u:
                        distanceToEdgeCm =
                            localZ * kTerrainTileSizeCm;
                        break;
                    default:
                        distanceToEdgeCm =
                            localX * kTerrainTileSizeCm;
                        break;
                    }
                    const float lawnWeight = std::clamp(
                        (kBoundaryWidthCm - distanceToEdgeCm) /
                            kBoundaryWidthCm,
                        0.0f,
                        1.0f);
                    if (lawnWeight <= 0.0f) {
                        continue;
                    }
                    glm::vec4 lawnColor{1.0f};
                    if (transition.lawnTile
                            ->cleanSuppressedEncounterGrassTint) {
                        const float worldGridX =
                            static_cast<float>(tile.gridX) + localX;
                        const float worldGridZ =
                            static_cast<float>(tile.gridZ) + localZ;
                        sampleNormalizedSourceTintColor(
                            *transition.lawnTile,
                            std::clamp(
                                worldGridX - static_cast<float>(
                                    transition.lawnTile->gridX),
                                0.0f,
                                1.0f),
                            std::clamp(
                                worldGridZ - static_cast<float>(
                                    transition.lawnTile->gridZ),
                                0.0f,
                                1.0f),
                            lawnColor,
                            nullptr);
                    } else {
                        if (!sampleTargetTerrainColor(
                                transition.lawnTile->surface,
                                transition.lawnTile->elevationLevel,
                                static_cast<float>(tile.gridX) + localX,
                                static_cast<float>(tile.gridZ) + localZ,
                                lawnColor)) {
                            continue;
                        }
                    }
                    dirtColor = route1DirtAdjacentLawnColor(
                        dirtColor,
                        {lawnColor.r,
                         lawnColor.g,
                         lawnColor.b,
                         lawnColor.a},
                        lawnWeight);
                }
                vertex.r = dirtColor[0];
                vertex.g = dirtColor[1];
                vertex.b = dirtColor[2];
                vertex.a = dirtColor[3];
                sourceVertex.colors[0] = {
                    dirtColor[0],
                    dirtColor[1],
                    dirtColor[2],
                    dirtColor[3]};
            }
            if (dark && ledgeCrownClipMask != 0u &&
                !preserveSourceSurface) {
                constexpr glm::vec2 crownUv2{
                    -0.049999952f,
                    0.949999988f};
                // UV1 and UV2 jointly select the raised-lawn field. Mixing a
                // procedural crown UV2 with an unrelated source/fallback UV1
                // interpolates through an atlas void and draws a black
                // diagonal across the cap.
                vertex.sourceUv1U = crownUv2.x;
                vertex.sourceUv1V = crownUv2.y;
                vertex.sourceUv2U = crownUv2.x;
                vertex.sourceUv2V = crownUv2.y;
                sourceVertex.texcoords[1] = {
                    crownUv2.x, crownUv2.y};
                sourceVertex.texcoords[2] = {
                    crownUv2.x, crownUv2.y};
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
            if ((tile.surface == "light_lawn" || dirt) &&
                ledgeContactOverlapMask != 0u) {
                float grassCoverage = 1.0f;
                if (dirt) {
                    float sampledGrassCoverage = 0.0f;
                    if (sampleSourceTerrainGroundMaskAlpha(
                            {vertex.sourceUv2U, vertex.sourceUv2V},
                            sampledGrassCoverage)) {
                        grassCoverage = std::clamp(
                            sampledGrassCoverage,
                            0.0f,
                            1.0f);
                    } else {
                        grassCoverage = 0.0f;
                    }
                }
                const float contactBlendWeight =
                    terrainLedgeFootColorBlendWeight(
                        localX,
                        localZ,
                        ledgeContactOverlapMask,
                        ledgeContactEndpointWeights) *
                    grassCoverage;
                const glm::vec4 contactColor{
                    kRaisedLawnTint[0],
                    kRaisedLawnTint[1],
                    kRaisedLawnTint[2],
                    1.0f};
                const glm::vec4 surfaceColor{
                    vertex.r, vertex.g, vertex.b, vertex.a};
                const glm::vec4 blendedColor = glm::mix(
                    surfaceColor,
                    contactColor,
                    contactBlendWeight);
                vertex.r = blendedColor.r;
                vertex.g = blendedColor.g;
                vertex.b = blendedColor.b;
                vertex.a = blendedColor.a;
                sourceVertex.colors[0] = {
                    blendedColor.r,
                    blendedColor.g,
                    blendedColor.b,
                    blendedColor.a};
            }
            prototype.vertices.push_back(vertex);
            prototype.sourceVertices.push_back(sourceVertex);
        }
    }
    std::vector<std::uint32_t> cleanDirtVertexIndices(
        rowWidth * rowWidth,
        std::numeric_limits<std::uint32_t>::max());
    const auto cleanDirtVertex =
        [&](std::uint32_t sourceIndex) -> std::uint32_t {
        auto& cachedIndex = cleanDirtVertexIndices[sourceIndex];
        if (cachedIndex !=
            std::numeric_limits<std::uint32_t>::max()) {
            return cachedIndex;
        }
        auto vertex = prototype.vertices[sourceIndex];
        auto sourceVertex = prototype.sourceVertices[sourceIndex];
        vertex.sourceUv2U = kCleanDirtUv2[0];
        vertex.sourceUv2V = kCleanDirtUv2[1];
        sourceVertex.texcoords[2] = kCleanDirtUv2;
        cachedIndex = static_cast<std::uint32_t>(
            prototype.vertices.size());
        prototype.vertices.push_back(vertex);
        prototype.sourceVertices.push_back(sourceVertex);
        return cachedIndex;
    };
    const auto boundaryDistanceCm =
        [&](float localX, float localZ) {
        const std::array<float, 4> distanceCm{
            (1.0f - localZ) * kTerrainTileSizeCm,
            (1.0f - localX) * kTerrainTileSizeCm,
            localZ * kTerrainTileSizeCm,
            localX * kTerrainTileSizeCm};
        float nearestDistance =
            std::numeric_limits<float>::max();
        for (std::size_t edge = 0u; edge < 4u; ++edge) {
            if ((transitionUv.boundaryMask & (1u << edge)) == 0u) {
                continue;
            }
            nearestDistance = std::min(
                nearestDistance,
                distanceCm[edge]);
        }
        return nearestDistance;
    };
    const auto appendTopTriangle = [&](
            std::uint32_t emittedFirst,
            std::uint32_t emittedSecond,
            std::uint32_t emittedThird,
            std::uint32_t sourceFirst,
            std::uint32_t sourceSecond,
            std::uint32_t sourceThird) {
        const std::uint32_t commonClippedCorner =
            convexCornerClippedVertexMasks[sourceFirst] &
            convexCornerClippedVertexMasks[sourceSecond] &
            convexCornerClippedVertexMasks[sourceThird];
        const std::uint32_t anyClippedCorner =
            convexCornerClippedVertexMasks[sourceFirst] |
            convexCornerClippedVertexMasks[sourceSecond] |
            convexCornerClippedVertexMasks[sourceThird];
        if (commonClippedCorner != 0u ||
            (terrainPatchV2PreviewEnabled && anyClippedCorner != 0u)) {
            return;
        }
        const auto& first = prototype.vertices[emittedFirst];
        const auto& second = prototype.vertices[emittedSecond];
        const auto& third = prototype.vertices[emittedThird];
        if (terrainPatchV2PreviewEnabled) {
            const auto horizontalEdgeLength = [](const auto& left,
                                                 const auto& right) {
                const float dx = right.x - left.x;
                const float dz = right.z - left.z;
                return std::sqrt(dx * dx + dz * dz);
            };
            const float maximumHorizontalEdgeCm = std::max({
                horizontalEdgeLength(first, second),
                horizontalEdgeLength(second, third),
                horizontalEdgeLength(third, first)});
            // The generated top is a five-centimetre lattice. A much longer
            // edge can only be a projection sliver produced where two old
            // tile-local crown clips disagree at a contour handoff. Retiring
            // it is safe because the regional crown owns that narrow contact
            // band and prevents the former green spear from reappearing.
            constexpr float kMaximumRegionalTopTriangleEdgeCm = 8.0f;
            if (maximumHorizontalEdgeCm >
                kMaximumRegionalTopTriangleEdgeCm) {
                return;
            }
        }
        const float signedAreaTwice =
            (second.x - first.x) * (third.z - first.z) -
            (second.z - first.z) * (third.x - first.x);
        constexpr float kMinimumTriangleAreaTwiceCm2 = 0.0001f;
        if (std::abs(signedAreaTwice) <=
            kMinimumTriangleAreaTwiceCm2) {
            return;
        }
        if (signedAreaTwice < 0.0f) {
            std::swap(emittedSecond, emittedThird);
        }
        prototype.indices.insert(
            prototype.indices.end(),
            {emittedFirst, emittedSecond, emittedThird});
    };
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
            bool cleanDirtCell = false;
            if (dirt && transitionUv.boundaryMask != 0u) {
                const float localX0 =
                    static_cast<float>(xIndex) /
                    static_cast<float>(kGridResolution);
                const float localX1 =
                    static_cast<float>(xIndex + 1u) /
                    static_cast<float>(kGridResolution);
                const float localZ0 =
                    static_cast<float>(zIndex) /
                    static_cast<float>(kGridResolution);
                const float localZ1 =
                    static_cast<float>(zIndex + 1u) /
                    static_cast<float>(kGridResolution);
                const float minimumDistance = std::min({
                    boundaryDistanceCm(localX0, localZ0),
                    boundaryDistanceCm(localX1, localZ0),
                    boundaryDistanceCm(localX0, localZ1),
                    boundaryDistanceCm(localX1, localZ1)});
                cleanDirtCell =
                    minimumDistance >= kBoundaryWidthCm - 0.001f;
            }
            if (cleanDirtCell) {
                const std::uint32_t cleanLowerLeft =
                    cleanDirtVertex(lowerLeft);
                const std::uint32_t cleanLowerRight =
                    cleanDirtVertex(lowerRight);
                const std::uint32_t cleanUpperLeft =
                    cleanDirtVertex(upperLeft);
                const std::uint32_t cleanUpperRight =
                    cleanDirtVertex(upperRight);
                appendTopTriangle(
                    cleanLowerLeft,
                    cleanLowerRight,
                    cleanUpperRight,
                    lowerLeft,
                    lowerRight,
                    upperRight);
                appendTopTriangle(
                    cleanLowerLeft,
                    cleanUpperRight,
                    cleanUpperLeft,
                    lowerLeft,
                    upperRight,
                    upperLeft);
                continue;
            }
            appendTopTriangle(
                lowerLeft,
                lowerRight,
                upperRight,
                lowerLeft,
                lowerRight,
                upperRight);
            appendTopTriangle(
                lowerLeft,
                upperRight,
                upperLeft,
                lowerLeft,
                upperRight,
                upperLeft);
        }
    }

    const auto geometry = shared_world_scene::ensureRigidGeometry(
        scene.registry,
        &prototype,
        resolvedKey.c_str(),
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
RuntimeEnvironment::Impl::ensureAuthoredTerrainSurfaceObject(
    bool receivesProjectedShadow) {
    std::uint64_t surfaceSignature = 1469598103934665603ull;
    const auto mixByte = [&](std::uint8_t value) {
        surfaceSignature ^= value;
        surfaceSignature *= 1099511628211ull;
    };
    const auto mixInteger = [&](std::int32_t value) {
        const auto unsignedValue = static_cast<std::uint32_t>(value);
        for (std::uint32_t shift = 0u; shift < 32u; shift += 8u) {
            mixByte(static_cast<std::uint8_t>(
                (unsignedValue >> shift) & 0xffu));
        }
    };
    const auto mixString = [&](std::string_view value) {
        for (const char character : value) {
            mixByte(static_cast<std::uint8_t>(character));
        }
        mixByte(0xffu);
    };
    for (const auto& tile : terrainTiles) {
        if (route1TerrainUsesExactSourceSurfaceOverride(
                tile, terrainTiles, sourceTerrainTiles)) {
            continue;
        }
        const bool rebuildsMaskedSourceSurface =
            terrainMaskCells.contains({tile.gridX, tile.gridZ}) &&
            !tile.sourceReference;
        if (!tile.authored &&
            !tile.cleanSuppressedEncounterGrassTint &&
            !rebuildsMaskedSourceSurface) {
            continue;
        }
        mixInteger(tile.gridX);
        mixInteger(tile.gridZ);
        mixInteger(tile.elevationLevel);
        mixString(tile.surface);
        mixString(tile.shape);
        mixString(tile.visualVariant);
        mixString(
            tile.receivesProjectedShadow
            ? "receives-projected-shadow"
            : "ignores-projected-shadow");
        if (tile.cleanSuppressedEncounterGrassTint) {
            mixString("clean-suppressed-encounter-tint");
        }
        if (tile.sourceReference) {
            mixInteger((*tile.sourceReference)[0]);
            mixInteger((*tile.sourceReference)[1]);
        }
    }
    const std::string key =
        "route1:terrain-authored-surface:signature-" +
        std::to_string(surfaceSignature) +
        (receivesProjectedShadow
             ? ":shadow-receiver"
             : ":shadowless");
    auto [found, inserted] =
        terrainTilePrototypes.authoredSurfacePrototypes
            .try_emplace(key);
    auto& prototype = found->second;
    if (!inserted) {
        return prototype.object;
    }

    const auto findTile =
        [&](std::int32_t gridX,
            std::int32_t gridZ)
            -> const TerrainTileState* {
            const auto tile = std::find_if(
                terrainTiles.begin(),
                terrainTiles.end(),
                [&](const TerrainTileState& candidate) {
                    return candidate.gridX == gridX &&
                        candidate.gridZ == gridZ;
                });
            return tile == terrainTiles.end() ? nullptr : &*tile;
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

    struct SurfaceTile {
        TerrainTileState tile;
        std::uint32_t dirtConnectionMask = 0u;
        DirtTransitionUvField transitionUv;
    };
    std::vector<SurfaceTile> surfaceTiles;
    std::vector<GridCell> surfaceVertexOwners;
    surfaceTiles.reserve(terrainTiles.size());
    for (const auto& sourceTile : terrainTiles) {
        if (sourceTile.surface == "empty") {
            continue;
        }
        // A source-reference cell is rendered from the canonical donor's
        // clipped source triangles below. Generating a second flat/ramp top
        // would destroy the donor's irregular multi-level profile.
        if (sourceTile.sourceReference) {
            continue;
        }
        if (route1TerrainUsesExactSourceSurfaceOverride(
                sourceTile, terrainTiles, sourceTerrainTiles)) {
            continue;
        }
        // Dirt owns the glassmask01 leafy boundary. When an edited neighbor
        // changes that boundary, the untouched source dirt cell must rebuild
        // its UV2 topology too; rebuilding only the lawn side leaves the old
        // straight/no-edge source dirt in place. Limit this dependency ring
        // strictly to dirt cells directly touching an authored cell so the
        // replacement never spreads through the canonical route.
        const bool affectedSourceDirt =
            !sourceTile.authored &&
            sourceTile.surface == "dirt_path" &&
            std::any_of(
                directions.begin(),
                directions.end(),
                [&](const auto& direction) {
                    const auto* neighbor = findTile(
                        sourceTile.gridX + direction[0],
                        sourceTile.gridZ + direction[1]);
                    return neighbor && neighbor->authored;
                });
        const bool affectedSourceLawn =
            sourceTile.cleanSuppressedEncounterGrassTint;
        // Lowering an adjacent cell invalidates the canonical ledge profile,
        // including the high cell's material-19 cap. Rebuild that cap with
        // the generated crown contour; otherwise the untouched rectangular
        // source top survives over the inset cliff/fringe and produces the
        // doubled leafy shelf visible at edited ledges.
        const bool affectedSourceLedgeTop =
            terrainMaskCells.contains(
                {sourceTile.gridX, sourceTile.gridZ});
        if (!sourceTile.authored &&
            !affectedSourceDirt &&
            !affectedSourceLawn &&
            !affectedSourceLedgeTop) {
            continue;
        }
        TerrainTileState tile = sourceTile;
        if (affectedSourceDirt || affectedSourceLedgeTop) {
            // A masked canonical top is now generated geometry even when its
            // authored values are unchanged. Sampling the retired source
            // surface along the new rounded boundary can hit the old cliff
            // wall instead of its cap, dragging outer grid rows downward and
            // leaving a visible pit or hanging triangle. Preserve the source
            // material fields below, but rebuild this carrier on the active
            // tile profile just like a directly authored elevation edit.
            tile.authored = true;
        }
        if (tile.receivesProjectedShadow !=
            receivesProjectedShadow) {
            continue;
        }
        std::uint32_t dirtConnectionMask = 0u;
        for (std::size_t edge = 0u;
             edge < directions.size();
             ++edge) {
            const auto direction = directions[edge];
            const auto* neighbor = findTile(
                tile.gridX + direction[0],
                tile.gridZ + direction[1]);
            const auto sharedEdgeProfile =
                route1TerrainSharedEdgeProfile(tile, neighbor, edge);
            const bool connectedSurface =
                neighbor && hasSurface(*neighbor) &&
                neighbor->surface == tile.surface &&
                sharedEdgeProfile.tileLevels ==
                    sharedEdgeProfile.neighborLevels;
            if (!connectedSurface) {
                continue;
            }
            if (tile.surface == "dirt_path") {
                dirtConnectionMask |= 1u << edge;
            }
        }
        std::uint32_t manualConnectionMask = 0u;
        if (tile.surface == "dirt_path" &&
            tile.visualVariant != "auto" &&
            terrainConnectionMaskFromVisualVariant(
                tile.visualVariant,
                manualConnectionMask)) {
            dirtConnectionMask = manualConnectionMask;
        }
        surfaceTiles.push_back(
            SurfaceTile{
                .tile = std::move(tile),
                .dirtConnectionMask = dirtConnectionMask});
    }

    // Build the missing-side edges into deterministic clockwise contours.
    // Source Route 1 uses one paired UV2 ribbon around a whole dirt region;
    // it does not restart or rotate one generic fade independently per cell.
    // Chaining the editable grid boundary first gives every straight run and
    // corner a continuous tangential U coordinate before tile meshes are
    // generated.
    using BoundaryPoint =
        std::tuple<std::int32_t, std::int32_t, std::int32_t>;
    struct BoundaryEdge {
        std::size_t tileIndex = 0u;
        std::size_t edge = 0u;
        BoundaryPoint start{};
        BoundaryPoint end{};
    };
    std::vector<BoundaryEdge> boundaryEdges;
    for (std::size_t tileIndex = 0u;
         tileIndex < surfaceTiles.size();
         ++tileIndex) {
        auto& surfaceTile = surfaceTiles[tileIndex];
        if (surfaceTile.tile.surface != "dirt_path") {
            continue;
        }
        surfaceTile.transitionUv.boundaryMask =
            (~surfaceTile.dirtConnectionMask) & 0x0fu;
        const std::int32_t x0 = surfaceTile.tile.gridX;
        const std::int32_t x1 = x0 + 1;
        const std::int32_t z0 = surfaceTile.tile.gridZ;
        const std::int32_t z1 = z0 + 1;
        const std::int32_t level = surfaceTile.tile.elevationLevel;
        const std::array<BoundaryPoint, 4> starts{{
            {x0, z1, level},
            {x1, z1, level},
            {x1, z0, level},
            {x0, z0, level}}};
        const std::array<BoundaryPoint, 4> ends{{
            {x1, z1, level},
            {x1, z0, level},
            {x0, z0, level},
            {x0, z1, level}}};
        for (std::size_t edge = 0u; edge < 4u; ++edge) {
            if ((surfaceTile.transitionUv.boundaryMask &
                 (1u << edge)) == 0u) {
                continue;
            }
            boundaryEdges.push_back(
                BoundaryEdge{
                    .tileIndex = tileIndex,
                    .edge = edge,
                    .start = starts[edge],
                    .end = ends[edge]});
        }
    }
    std::map<BoundaryPoint, std::vector<std::size_t>> edgesByStart;
    for (std::size_t edgeIndex = 0u;
         edgeIndex < boundaryEdges.size();
         ++edgeIndex) {
        edgesByStart[boundaryEdges[edgeIndex].start]
            .push_back(edgeIndex);
    }
    for (auto& [point, indices] : edgesByStart) {
        (void)point;
        std::sort(
            indices.begin(),
            indices.end(),
            [&](std::size_t left, std::size_t right) {
                return boundaryEdges[left].edge <
                    boundaryEdges[right].edge;
            });
    }
    constexpr float kGroundBoundaryUPerCell = 0.36f;
    std::vector<bool> visitedBoundaryEdges(
        boundaryEdges.size(), false);
    for (std::size_t firstEdge = 0u;
         firstEdge < boundaryEdges.size();
         ++firstEdge) {
        if (visitedBoundaryEdges[firstEdge]) {
            continue;
        }
        const auto [startX, startZ, startLevel] =
            boundaryEdges[firstEdge].start;
        float cursorU =
            static_cast<float>(startX) * 0.371f +
            static_cast<float>(startZ) * 0.193f +
            static_cast<float>(startLevel) * 0.117f;
        std::vector<std::size_t> contour;
        std::size_t current = firstEdge;
        while (!visitedBoundaryEdges[current]) {
            visitedBoundaryEdges[current] = true;
            const auto& edge = boundaryEdges[current];
            contour.push_back(current);
            const auto candidates = edgesByStart.find(edge.end);
            if (candidates == edgesByStart.end()) {
                break;
            }
            std::size_t next = boundaryEdges.size();
            std::uint32_t bestTurnRank = 4u;
            for (const std::size_t candidate : candidates->second) {
                if (visitedBoundaryEdges[candidate]) {
                    continue;
                }
                const std::uint32_t turn = static_cast<std::uint32_t>(
                    (boundaryEdges[candidate].edge + 4u - edge.edge) % 4u);
                const std::uint32_t rank =
                    turn == 1u ? 0u :
                    turn == 0u ? 1u :
                    turn == 3u ? 2u : 3u;
                if (rank < bestTurnRank) {
                    bestTurnRank = rank;
                    next = candidate;
                }
            }
            if (next >= boundaryEdges.size()) {
                break;
            }
            current = next;
        }
        const bool closed = !contour.empty() &&
            boundaryEdges[contour.back()].end ==
                boundaryEdges[contour.front()].start;
        float edgeUAdvance = kGroundBoundaryUPerCell;
        if (closed) {
            const float wholeRepeats = std::max(
                1.0f,
                std::round(
                    static_cast<float>(contour.size()) *
                    kGroundBoundaryUPerCell));
            const float fittedAdvance = wholeRepeats /
                static_cast<float>(contour.size());
            // The source stretches long closed ribbons just enough to meet
            // on a whole repeat. Very small tile islands would require a
            // conspicuous 30%+ density change, so retain the measured source
            // density there and put the unavoidable phase reset at a corner.
            if (std::abs(
                    fittedAdvance - kGroundBoundaryUPerCell) <=
                kGroundBoundaryUPerCell * 0.15f) {
                edgeUAdvance = fittedAdvance;
            }
        }
        for (const std::size_t edgeIndex : contour) {
            const auto& edge = boundaryEdges[edgeIndex];
            surfaceTiles[edge.tileIndex]
                .transitionUv.edgeStartU[edge.edge] = cursorU;
            surfaceTiles[edge.tileIndex]
                .transitionUv.edgeUPerCm[edge.edge] =
                    edgeUAdvance / kTerrainTileSizeCm;
            cursorU += edgeUAdvance;
        }
    }

    for (const auto& surfaceTile : surfaceTiles) {
        const auto& tile = surfaceTile.tile;
        const auto object = ensureTerrainTopObject(
            tile,
            surfaceTile.dirtConnectionMask,
            surfaceTile.transitionUv);
        if (object.id == 0u ||
            object.id > scene.registry.renderObjects.size()) {
            continue;
        }
        const auto& renderObject = scene.registry.renderObjects[
            object.id - 1u];
        if (renderObject.geometryHandle.id == 0u ||
            renderObject.geometryHandle.id >
                scene.registry.geometries.size()) {
            continue;
        }
        const auto& geometry = scene.registry.geometries[
            renderObject.geometryHandle.id - 1u];
        if (!geometry.vertices || !geometry.indices ||
            !geometry.sourceVertices ||
            geometry.sourceVertexCount != geometry.vertexCount) {
            continue;
        }
        const auto vertexOffset = static_cast<std::uint32_t>(
            prototype.vertices.size());
        const float centerX =
            (static_cast<float>(tile.gridX) + 0.5f) *
            kTerrainTileSizeCm;
        const float centerY =
            static_cast<float>(tile.elevationLevel) *
                kTerrainElevationStepCm +
            kTerrainTileTopDepthBiasCm;
        const float centerZ =
            (static_cast<float>(tile.gridZ) + 0.5f) *
            kTerrainTileSizeCm;
        for (std::size_t vertexIndex = 0u;
             vertexIndex < geometry.vertexCount;
             ++vertexIndex) {
            auto vertex = geometry.vertices[vertexIndex];
            vertex.x += centerX;
            vertex.y += centerY;
            vertex.z += centerZ;
            prototype.vertices.push_back(vertex);
            prototype.sourceVertices.push_back(
                geometry.sourceVertices[vertexIndex]);
            surfaceVertexOwners.emplace_back(
                tile.gridX, tile.gridZ);
        }
        for (std::size_t index = 0u;
             index < geometry.indexCount;
             ++index) {
            prototype.indices.push_back(
                vertexOffset + geometry.indices[index]);
        }
    }
    if (terrainPatchV2PreviewEnabled &&
        !prototype.vertices.empty() &&
        surfaceVertexOwners.size() == prototype.vertices.size()) {
        // The production path above deliberately preserves independently
        // generated tile vertices. V2 makes compatible regional edges one
        // topological edge: all triangles on both sides reference the same
        // deterministic vertex. The transition ring has already normalized
        // its source-world material fields, so retaining the first authored
        // sample is stable and avoids interpolating periodic texture UVs.
        using PositionKey =
            std::tuple<std::int64_t, std::int64_t, std::int64_t>;
        std::map<PositionKey, std::vector<std::uint32_t>> byPosition;
        constexpr double kPositionQuantization = 1000.0;
        for (std::size_t index = 0u;
             index < prototype.vertices.size();
             ++index) {
            const auto& vertex = prototype.vertices[index];
            byPosition[{
                static_cast<std::int64_t>(std::llround(
                    static_cast<double>(vertex.x) *
                    kPositionQuantization)),
                static_cast<std::int64_t>(std::llround(
                    static_cast<double>(vertex.y) *
                    kPositionQuantization)),
                static_cast<std::int64_t>(std::llround(
                    static_cast<double>(vertex.z) *
                    kPositionQuantization))}].push_back(
                        static_cast<std::uint32_t>(index));
        }
        std::vector<std::uint32_t> representative(
            prototype.vertices.size());
        for (std::size_t index = 0u;
             index < representative.size();
             ++index) {
            representative[index] =
                static_cast<std::uint32_t>(index);
        }
        const auto compatibleOwners = [&](const GridCell& left,
                                          const GridCell& right) {
            if (left == right) {
                return false;
            }
            const auto* leftTile = findTile(left.first, left.second);
            const auto* rightTile = findTile(right.first, right.second);
            if (!leftTile || !rightTile ||
                leftTile->terrainPatchV2RegionId == 0u ||
                leftTile->terrainPatchV2RegionId !=
                    rightTile->terrainPatchV2RegionId ||
                leftTile->surface != rightTile->surface) {
                return false;
            }
            const std::int32_t dx = right.first - left.first;
            const std::int32_t dz = right.second - left.second;
            std::size_t edge = 4u;
            if (dx == 0 && dz == 1) {
                edge = 0u;
            } else if (dx == 1 && dz == 0) {
                edge = 1u;
            } else if (dx == 0 && dz == -1) {
                edge = 2u;
            } else if (dx == -1 && dz == 0) {
                edge = 3u;
            }
            if (edge >= 4u) {
                return false;
            }
            const auto profile = route1TerrainSharedEdgeProfile(
                *leftTile, rightTile, edge);
            return profile.tileLevels == profile.neighborLevels;
        };
        for (const auto& [position, indices] : byPosition) {
            (void)position;
            for (std::size_t index = 1u;
                 index < indices.size();
                 ++index) {
                const auto candidate = indices[index];
                for (std::size_t prior = 0u;
                     prior < index;
                     ++prior) {
                    const auto anchor = indices[prior];
                    if (!compatibleOwners(
                            surfaceVertexOwners[candidate],
                            surfaceVertexOwners[anchor])) {
                        continue;
                    }
                    representative[candidate] =
                        representative[anchor];
                    break;
                }
            }
        }
        for (auto& index : prototype.indices) {
            while (representative[index] != index) {
                index = representative[index];
            }
        }
    }
    if (prototype.vertices.empty() || prototype.indices.empty()) {
        return {};
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
        receivesProjectedShadow
            ? terrainTilePrototypes.groundMaterialHandle
            : terrainTilePrototypes.groundShadowlessMaterialHandle,
        static_cast<shared_world_scene::PipelineVariant>(
            terrainTilePrototypes.groundPipelineVariant),
        terrainTilePrototypes.groundCookedDrawSlot,
        false);
    return prototype.object;
}

std::vector<IRenderBackend::WorldSceneRenderObjectHandle>
RuntimeEnvironment::Impl::ensureTerrainSourceReferenceObjects(
    const std::set<GridCell>& sourceCells,
    const std::set<GridCell>& blockedSpillCells,
    const std::vector<std::pair<GridCell, GridCell>>&
        requiredSpillBoundaries) {
    std::vector<IRenderBackend::WorldSceneRenderObjectHandle> out;
    if (sourceCells.empty()) {
        return out;
    }
    std::string sourcePatchKey;
    for (const auto& [sourceGridX, sourceGridZ] : sourceCells) {
        sourcePatchKey += std::to_string(sourceGridX) + "," +
            std::to_string(sourceGridZ) + ";";
    }
    sourcePatchKey += "blocked:";
    for (const auto& [sourceGridX, sourceGridZ] : blockedSpillCells) {
        sourcePatchKey += std::to_string(sourceGridX) + "," +
            std::to_string(sourceGridZ) + ";";
    }
    sourcePatchKey += "required:";
    for (const auto& [ownerCell, spillCell] :
         requiredSpillBoundaries) {
        sourcePatchKey += std::to_string(ownerCell.first) + "," +
            std::to_string(ownerCell.second) + ">" +
            std::to_string(spillCell.first) + "," +
            std::to_string(spillCell.second) + ";";
    }
    std::vector<std::pair<GridCell, GridCell>> blockedBoundaries;
    for (const auto& sourceCell : sourceCells) {
        for (const auto& blockedCell : blockedSpillCells) {
            if (std::abs(sourceCell.first - blockedCell.first) +
                    std::abs(sourceCell.second - blockedCell.second) ==
                1) {
                blockedBoundaries.emplace_back(
                    sourceCell, blockedCell);
            }
        }
    }
    for (const auto& mask : terrainMaskGeometries) {
        if (mask.geometryHandle.id == 0u ||
            mask.geometryHandle.id > scene.registry.geometries.size()) {
            continue;
        }
        const auto& sourceGeometry = scene.registry.geometries[
            mask.geometryHandle.id - 1u];
        if (mask.originalVertices.empty() ||
            mask.originalIndices.size() < 3u) {
            continue;
        }
        const auto sourceObject = std::find_if(
            scene.registry.renderObjects.begin(),
            scene.registry.renderObjects.end(),
            [&](const auto& candidate) {
                return candidate.geometryHandle.id ==
                    mask.geometryHandle.id;
            });
        if (sourceObject == scene.registry.renderObjects.end()) {
            continue;
        }
        const std::string key =
            "route1:terrain-source-reference-patch:" +
            sourcePatchKey + ":geometry-" +
            std::to_string(mask.geometryHandle.id);
        auto [found, inserted] =
            terrainTilePrototypes.sourceReferencePrototypes
                .try_emplace(key);
        auto& prototype = found->second;
        if (inserted) {
            const glm::mat4 sourceModel = glm::make_mat4(
                mask.sourceModelMatrix.data());
            const glm::mat3 sourceNormal = glm::transpose(
                glm::inverse(glm::mat3(sourceModel)));
            const auto transformDirection =
                [&](float x, float y, float z) {
                    const glm::vec3 transformed = sourceNormal *
                        glm::vec3(x, y, z);
                    const float length = glm::length(transformed);
                    return length > 1.0e-6f
                        ? transformed / length
                        : transformed;
                };
            for (std::size_t index = 0u;
                 index + 2u < mask.originalIndices.size();
                 index += 3u) {
                const std::array<std::uint32_t, 3> triangle{
                    mask.originalIndices[index],
                    mask.originalIndices[index + 1u],
                    mask.originalIndices[index + 2u]};
                bool valid = true;
                bool touchesSourcePatch = false;
                glm::vec3 centroid{};
                std::array<glm::vec3, 3> positions{};
                for (std::size_t corner = 0u;
                     corner < triangle.size();
                     ++corner) {
                    const std::uint32_t vertexIndex = triangle[corner];
                    if (vertexIndex >= mask.originalVertices.size()) {
                        valid = false;
                        break;
                    }
                    positions[corner] = glm::vec3(
                        sourceModel * glm::vec4(
                            mask.originalVertices[vertexIndex].x,
                            mask.originalVertices[vertexIndex].y,
                            mask.originalVertices[vertexIndex].z,
                            1.0f));
                    centroid += positions[corner];
                    const std::pair<std::int32_t, std::int32_t>
                        vertexCell{
                            static_cast<std::int32_t>(std::floor(
                                positions[corner].x /
                                    kTerrainTileSizeCm)),
                            static_cast<std::int32_t>(std::floor(
                                positions[corner].z /
                                    kTerrainTileSizeCm))};
                    touchesSourcePatch = touchesSourcePatch ||
                        sourceCells.contains(vertexCell);
                }
                if (!valid) {
                    continue;
                }
                centroid /= 3.0f;
                const std::pair<std::int32_t, std::int32_t>
                    centroidCell{
                        static_cast<std::int32_t>(std::floor(
                            centroid.x / kTerrainTileSizeCm)),
                        static_cast<std::int32_t>(std::floor(
                            centroid.z / kTerrainTileSizeCm))};
                if (blockedSpillCells.contains(centroidCell)) {
                    continue;
                }
                std::array<std::array<float, 3>, 3>
                    positionValues{{
                        {positions[0].x,
                         positions[0].y,
                         positions[0].z},
                        {positions[1].x,
                         positions[1].y,
                         positions[1].z},
                        {positions[2].x,
                         positions[2].y,
                         positions[2].z}}};
                const bool belongsToRequiredSpillBand =
                    mask.cleanupOnly &&
                    std::any_of(
                        requiredSpillBoundaries.begin(),
                        requiredSpillBoundaries.end(),
                        [&](const auto& boundary) {
                            const auto& [ownerCell, spillCell] =
                                boundary;
                            return centroidCell == spillCell &&
                                route1TerrainCleanupCarrierWithinBoundaryBand(
                                    positionValues,
                                    {ownerCell.first,
                                     ownerCell.second},
                                    {spillCell.first,
                                     spillCell.second});
                        });
                const bool belongsToSourcePatch =
                    mask.maskWhenAnyVertexTouchesCell
                    ? touchesSourcePatch ||
                        belongsToRequiredSpillBand
                    : sourceCells.contains(centroidCell);
                if (!belongsToSourcePatch) {
                    continue;
                }
                if (mask.cleanupOnly) {
                    for (const auto& [ownerCell, blockedCell] :
                         blockedBoundaries) {
                        route1TerrainClampCleanupCarrierToOwnedCell(
                            positionValues,
                            {ownerCell.first, ownerCell.second},
                            {blockedCell.first, blockedCell.second});
                    }
                    for (std::size_t corner = 0u;
                         corner < positions.size();
                         ++corner) {
                        positions[corner] = glm::vec3(
                            positionValues[corner][0],
                            positionValues[corner][1],
                            positionValues[corner][2]);
                    }
                }
                for (std::size_t corner = 0u;
                     corner < triangle.size();
                     ++corner) {
                    const std::uint32_t sourceVertexIndex =
                        triangle[corner];
                    auto vertex =
                        mask.originalVertices[sourceVertexIndex];
                    vertex.x = positions[corner].x;
                    vertex.y = positions[corner].y;
                    vertex.z = positions[corner].z;
                    const glm::vec3 normal = transformDirection(
                        vertex.nx, vertex.ny, vertex.nz);
                    vertex.nx = normal.x;
                    vertex.ny = normal.y;
                    vertex.nz = normal.z;
                    const glm::vec3 tangent = transformDirection(
                        vertex.tx, vertex.ty, vertex.tz);
                    vertex.tx = tangent.x;
                    vertex.ty = tangent.y;
                    vertex.tz = tangent.z;
                    prototype.vertices.push_back(vertex);
                    if (!mask.originalSourceVertices.empty() &&
                        sourceVertexIndex <
                            mask.originalSourceVertices.size()) {
                        auto sourceVertex =
                            mask.originalSourceVertices[
                                sourceVertexIndex];
                        const glm::vec3 bitangent =
                            transformDirection(
                                sourceVertex.bitangent[0],
                                sourceVertex.bitangent[1],
                                sourceVertex.bitangent[2]);
                        sourceVertex.bitangent[0] = bitangent.x;
                        sourceVertex.bitangent[1] = bitangent.y;
                        sourceVertex.bitangent[2] = bitangent.z;
                        prototype.sourceVertices.push_back(
                            sourceVertex);
                    }
                    prototype.indices.push_back(
                        static_cast<std::uint32_t>(
                            prototype.vertices.size() - 1u));
                }
            }
            if (!prototype.vertices.empty() &&
                !prototype.indices.empty()) {
                const bool hasSourceVertices =
                    prototype.sourceVertices.size() ==
                        prototype.vertices.size();
                if (!hasSourceVertices) {
                    prototype.sourceVertices.clear();
                }
                const auto geometry =
                    shared_world_scene::ensureRigidGeometry(
                        scene.registry,
                        &prototype,
                        key.c_str(),
                        prototype.vertices.data(),
                        prototype.vertices.size(),
                        prototype.indices.data(),
                        prototype.indices.size(),
                        hasSourceVertices
                            ? prototype.sourceVertices.data()
                            : nullptr,
                        hasSourceVertices
                            ? prototype.sourceVertices.size()
                            : 0u,
                        hasSourceVertices
                            ? sourceGeometry.sourceVertexSemanticMask
                            : IRenderBackend::
                                WorldSceneSourceVertexSemanticNone,
                        std::numeric_limits<std::uint32_t>::max(),
                        0u);
                prototype.object =
                    shared_world_scene::ensureRenderObject(
                        scene.registry,
                        geometry,
                        sourceObject->materialHandle,
                        static_cast<shared_world_scene::PipelineVariant>(
                            sourceObject->pipelineVariant),
                        sourceObject->cookedDrawSlot,
                        false);
            }
        }
        if (prototype.object.id != 0u) {
            out.push_back(prototype.object);
        }
    }
    return out;
}

std::vector<IRenderBackend::WorldSceneRenderObjectHandle>
RuntimeEnvironment::Impl::ensureTerrainExactSourceSurfaceObjects(
    const std::set<GridCell>& sourceCells,
    bool receivesProjectedShadow) {
    std::vector<IRenderBackend::WorldSceneRenderObjectHandle> out;
    if (sourceCells.empty()) {
        return out;
    }

    std::string patchKey = receivesProjectedShadow
        ? "shadow:"
        : "shadowless:";
    for (const auto& [gridX, gridZ] : sourceCells) {
        patchKey += std::to_string(gridX) + "," +
            std::to_string(gridZ) + ";";
    }
    for (const auto& mask : terrainMaskGeometries) {
        if (!mask.sourceGround ||
            mask.geometryHandle.id == 0u ||
            mask.geometryHandle.id > scene.registry.geometries.size() ||
            mask.originalVertices.empty() ||
            mask.originalIndices.size() < 3u) {
            continue;
        }
        const auto& sourceGeometry = scene.registry.geometries[
            mask.geometryHandle.id - 1u];
        const std::string key =
            "route1:terrain-exact-source-surface:" + patchKey +
            ":geometry-" + std::to_string(mask.geometryHandle.id);
        auto [found, inserted] =
            terrainTilePrototypes.sourceReferencePrototypes
                .try_emplace(key);
        auto& prototype = found->second;
        if (inserted) {
            const glm::mat4 sourceModel = glm::make_mat4(
                mask.sourceModelMatrix.data());
            const glm::mat3 sourceNormal = glm::transpose(
                glm::inverse(glm::mat3(sourceModel)));
            const auto transformDirection =
                [&](float x, float y, float z) {
                    const glm::vec3 transformed = sourceNormal *
                        glm::vec3{x, y, z};
                    const float length = glm::length(transformed);
                    return length > 1.0e-6f
                        ? transformed / length
                        : transformed;
                };
            for (std::size_t index = 0u;
                 index + 2u < mask.originalIndices.size();
                 index += 3u) {
                const std::array<std::uint32_t, 3> triangle{
                    mask.originalIndices[index],
                    mask.originalIndices[index + 1u],
                    mask.originalIndices[index + 2u]};
                bool valid = true;
                glm::vec3 centroid{0.0f};
                std::array<glm::vec3, 3> positions{};
                for (std::size_t corner = 0u;
                     corner < triangle.size();
                     ++corner) {
                    const std::uint32_t vertexIndex = triangle[corner];
                    if (vertexIndex >= mask.originalVertices.size()) {
                        valid = false;
                        break;
                    }
                    const auto& sourceVertex =
                        mask.originalVertices[vertexIndex];
                    positions[corner] = glm::vec3(
                        sourceModel * glm::vec4{
                            sourceVertex.x,
                            sourceVertex.y,
                            sourceVertex.z,
                            1.0f});
                    centroid += positions[corner];
                }
                if (!valid) {
                    continue;
                }
                centroid /= 3.0f;
                const GridCell centroidCell{
                    static_cast<std::int32_t>(std::floor(
                        centroid.x / kTerrainTileSizeCm)),
                    static_cast<std::int32_t>(std::floor(
                        centroid.z / kTerrainTileSizeCm))};
                if (!sourceCells.contains(centroidCell)) {
                    continue;
                }
                for (std::size_t corner = 0u;
                     corner < triangle.size();
                     ++corner) {
                    const std::uint32_t sourceVertexIndex =
                        triangle[corner];
                    auto vertex =
                        mask.originalVertices[sourceVertexIndex];
                    vertex.x = positions[corner].x;
                    vertex.y = positions[corner].y;
                    vertex.z = positions[corner].z;
                    const glm::vec3 normal = transformDirection(
                        vertex.nx, vertex.ny, vertex.nz);
                    vertex.nx = normal.x;
                    vertex.ny = normal.y;
                    vertex.nz = normal.z;
                    const glm::vec3 tangent = transformDirection(
                        vertex.tx, vertex.ty, vertex.tz);
                    vertex.tx = tangent.x;
                    vertex.ty = tangent.y;
                    vertex.tz = tangent.z;
                    prototype.vertices.push_back(vertex);
                    if (!mask.originalSourceVertices.empty() &&
                        sourceVertexIndex <
                            mask.originalSourceVertices.size()) {
                        auto authoredVertex =
                            mask.originalSourceVertices[
                                sourceVertexIndex];
                        const glm::vec3 bitangent =
                            transformDirection(
                                authoredVertex.bitangent[0],
                                authoredVertex.bitangent[1],
                                authoredVertex.bitangent[2]);
                        authoredVertex.bitangent[0] = bitangent.x;
                        authoredVertex.bitangent[1] = bitangent.y;
                        authoredVertex.bitangent[2] = bitangent.z;
                        prototype.sourceVertices.push_back(
                            authoredVertex);
                    }
                    prototype.indices.push_back(
                        static_cast<std::uint32_t>(
                            prototype.vertices.size() - 1u));
                }
            }
            if (!prototype.vertices.empty() &&
                !prototype.indices.empty()) {
                const bool hasSourceVertices =
                    prototype.sourceVertices.size() ==
                    prototype.vertices.size();
                if (!hasSourceVertices) {
                    prototype.sourceVertices.clear();
                }
                const auto geometry =
                    shared_world_scene::ensureRigidGeometry(
                        scene.registry,
                        &prototype,
                        key.c_str(),
                        prototype.vertices.data(),
                        prototype.vertices.size(),
                        prototype.indices.data(),
                        prototype.indices.size(),
                        hasSourceVertices
                            ? prototype.sourceVertices.data()
                            : nullptr,
                        hasSourceVertices
                            ? prototype.sourceVertices.size()
                            : 0u,
                        hasSourceVertices
                            ? sourceGeometry.sourceVertexSemanticMask
                            : IRenderBackend::
                                WorldSceneSourceVertexSemanticNone,
                        std::numeric_limits<std::uint32_t>::max(),
                        0u);
                prototype.object =
                    shared_world_scene::ensureRenderObject(
                        scene.registry,
                        geometry,
                        receivesProjectedShadow
                            ? terrainTilePrototypes.groundMaterialHandle
                            : terrainTilePrototypes
                                  .groundShadowlessMaterialHandle,
                        static_cast<
                            shared_world_scene::PipelineVariant>(
                            terrainTilePrototypes
                                .groundPipelineVariant),
                        terrainTilePrototypes.groundCookedDrawSlot,
                        false);
            }
        }
        if (prototype.object.id != 0u) {
            out.push_back(prototype.object);
        }
    }
    return out;
}

IRenderBackend::WorldSceneRenderObjectHandle
RuntimeEnvironment::Impl::ensureTerrainCliffObject(
    const TerrainTileState& tile,
    std::size_t edge,
    const TerrainSharedEdgeProfile& edgeProfile,
    float contourStartCm,
    float materialContourStartCm,
    route1_terrain_ledges::Join startJoin,
    route1_terrain_ledges::Join endJoin) {
    const std::array<std::int32_t, 2> levelDifferences{
        edgeProfile.tileLevels[0] - edgeProfile.neighborLevels[0],
        edgeProfile.tileLevels[1] - edgeProfile.neighborLevels[1]};
    const std::int32_t maximumLevelDifference = std::max(
        levelDifferences[0], levelDifferences[1]);
    if (edge >= 4u || maximumLevelDifference <= 0) {
        return {};
    }
    const auto* contourEdge = route1_terrain_contours::findEdge(
        terrainContourAssembly,
        {tile.gridX, tile.gridZ},
        edge);
    const std::string key =
        "route1:terrain-cliff:cell-" +
        std::to_string(tile.gridX) + "-" +
        std::to_string(tile.gridZ) + ":edge-" +
        std::to_string(edge) + ":tile-levels-" +
        std::to_string(edgeProfile.tileLevels[0]) + "-" +
        std::to_string(edgeProfile.tileLevels[1]) +
        ":neighbor-levels-" +
        std::to_string(edgeProfile.neighborLevels[0]) + "-" +
        std::to_string(edgeProfile.neighborLevels[1]) +
        ":contour-cm-" +
        std::to_string(static_cast<std::int32_t>(
            std::lround(contourStartCm))) +
        ":material-contour-cm-" +
        std::to_string(static_cast<std::int32_t>(
            std::lround(materialContourStartCm))) +
        ":joins-" +
        std::to_string(static_cast<std::uint32_t>(startJoin)) + "-" +
        std::to_string(static_cast<std::uint32_t>(endJoin));
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
    const std::int32_t anchorLevel = std::min(
        edgeProfile.neighborLevels[0],
        edgeProfile.neighborLevels[1]);
    const std::size_t rowCount = maximumLevelDifference > 1
        ? 5u
        : 4u;
    std::array<std::vector<ProfileRow>, 2> endpointRows;
    // Source cliff feet and material-19 ground share the nominal elevation.
    // Sink the generated foot by the depth epsilon so its alpha-tested foliage
    // retains a small vertical raster overlap with the +0.02 cm lawn.
    constexpr float kGeneratedSeamY = -0.02f;
    for (std::size_t endpoint = 0u; endpoint < 2u; ++endpoint) {
        auto& rows = endpointRows[endpoint];
        rows.reserve(rowCount);
        const std::int32_t tileLevel =
            edgeProfile.tileLevels[endpoint];
        const std::int32_t neighborLevel = std::min(
            tileLevel,
            edgeProfile.neighborLevels[endpoint]);
        const std::int32_t levelDifference =
            tileLevel - neighborLevel;
        const float baseOffset =
            static_cast<float>(neighborLevel - anchorLevel) *
            kTerrainElevationStepCm;
        if (levelDifference <= 0) {
            const float meetingHeight =
                static_cast<float>(tileLevel - anchorLevel) *
                kTerrainElevationStepCm;
            rows.assign(
                rowCount,
                ProfileRow{
                    meetingHeight + kGeneratedSeamY,
                    0.0f,
                    0.76f,
                    0.65f,
                    0.9975f,
                    0.850f});
            continue;
        }
        const float height =
            static_cast<float>(levelDifference) *
            kTerrainElevationStepCm;
        const float capBase = height - kTerrainElevationStepCm;
        if (rowCount == 5u) {
            rows.push_back({
                baseOffset +
                    (levelDifference > 1 ? 0.0f : capBase) +
                    kGeneratedSeamY,
                25.0f,
                levelDifference > 1 ? 0.0f : 0.27f,
                levelDifference > 1 ? 1.0f : 0.96f,
                levelDifference > 1
                    ? 0.00249964f -
                        static_cast<float>(levelDifference - 1)
                    : 0.00249964f,
                0.79334f});
        }
        rows.push_back({
            baseOffset + capBase + kGeneratedSeamY,
            25.0f,
            0.27f,
            0.96f,
            0.00249964f,
            0.79334f});
        rows.push_back({
            baseOffset + capBase + 16.875f + kGeneratedSeamY,
            20.0f,
            0.27f,
            0.96f,
            0.338313f,
            0.850f});
        rows.push_back({
            baseOffset + capBase + 35.02f + kGeneratedSeamY,
            15.0f,
            0.55f,
            0.83f,
            0.699455f,
            0.850f});
        rows.push_back({
            baseOffset + capBase + 48.0f + kGeneratedSeamY,
            0.0f,
            0.76f,
            0.65f,
            0.9975f,
            0.850f});
    }

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
    const glm::mat3 inverseEdgeRotation = glm::transpose(
        glm::mat3(edgeRotation));
    constexpr std::array<glm::vec2, 4> sourceTangents{
        glm::vec2{1.0f, 0.0f},
        glm::vec2{0.0f, -1.0f},
        glm::vec2{-1.0f, 0.0f},
        glm::vec2{0.0f, 1.0f}};
    const glm::vec2 sourceTangent = sourceTangents[edge];
    constexpr float cliffUPerCentimetre = 0.00516529f;
    constexpr float borderUPerCentimetre = 0.00510638f;
    constexpr std::uint32_t kEdgeSegments =
        kTerrainLedgeContourSegments;
    constexpr std::uint32_t kEdgeSamples = kEdgeSegments + 1u;
    const bool usesSharedContourFrame =
        terrainPatchV2PreviewEnabled && contourEdge &&
        contourEdge->frames.size() == kEdgeSamples &&
        startJoin != route1_terrain_ledges::Join::Concave &&
        endJoin != route1_terrain_ledges::Join::Concave;
    const float materialStraightLengthCm =
        route1_terrain_ledges::materialStraightLengthCm(
            startJoin,
            endJoin);
    constexpr std::array<float, 4> kWhite{
        1.0f, 1.0f, 1.0f, 1.0f};
    constexpr std::array<float, 4> kLowerBandColor{
        0.180392161f, 0.482352942f, 0.431372553f, 1.0f};
    std::vector<glm::vec3> inheritedSourcePositions;
    std::vector<glm::vec3> inheritedSourceNormals;
    inheritedSourcePositions.reserve(
        (rowCount - 1u) * 2u * kEdgeSamples);
    inheritedSourceNormals.reserve(
        (rowCount - 1u) * 2u * kEdgeSamples);
    bool inheritsCompleteSourceGeometry = rowCount == 4u;
    for (std::uint32_t band = 0u;
         band + 1u < rowCount;
         ++band) {
        const std::uint32_t firstVertex =
            static_cast<std::uint32_t>(prototype.vertices.size());
        for (std::size_t rowInBand = 0u;
             rowInBand < 2u;
             ++rowInBand) {
            const std::size_t rowIndex = band + rowInBand;
            const auto& startRow = endpointRows[0u][rowIndex];
            const auto& endRow = endpointRows[1u][rowIndex];
            const float startEffectiveOutward =
                levelDifferences[0u] > 0
                ? startRow.outward + kTerrainLedgeBaseInsetCm
                : 0.0f;
            const float endEffectiveOutward =
                levelDifferences[1u] > 0
                ? endRow.outward + kTerrainLedgeBaseInsetCm
                : 0.0f;
            const std::size_t cornerRow = rowCount == 5u
                ? (rowIndex == 0u ? 0u : rowIndex - 1u)
                : rowIndex;
            const auto& sourceStartCorner =
                kTerrainConcaveCliffPoints[cornerRow][3u];
            const auto& sourceEndCorner =
                kTerrainConcaveCliffPoints[cornerRow][0u];
            const float concaveStartOutward = -sourceStartCorner.z;
            const float concaveEndOutward = -sourceEndCorner.x;
            const float startAlong =
                startJoin == route1_terrain_ledges::Join::Concave &&
                    levelDifferences[0u] > 0
                ? -50.0f - sourceStartCorner.x
                : route1_terrain_ledges::endpointAlongCm(
                      startJoin,
                      true,
                      startEffectiveOutward);
            const float endAlong =
                endJoin == route1_terrain_ledges::Join::Concave &&
                    levelDifferences[1u] > 0
                ? 50.0f + sourceEndCorner.z
                : route1_terrain_ledges::endpointAlongCm(
                      endJoin,
                      false,
                      endEffectiveOutward);
            for (std::uint32_t sample = 0u;
                 sample < kEdgeSamples;
                 ++sample) {
                const float t = static_cast<float>(sample) /
                    static_cast<float>(kEdgeSegments);
                const ProfileRow row{
                    .y = std::lerp(startRow.y, endRow.y, t),
                    .outward = std::lerp(
                        startRow.outward, endRow.outward, t),
                    .normalY = std::lerp(
                        startRow.normalY, endRow.normalY, t),
                    .normalZ = std::lerp(
                        startRow.normalZ, endRow.normalZ, t),
                    .cliffV = std::lerp(
                        startRow.cliffV, endRow.cliffV, t),
                    .borderV = std::lerp(
                        startRow.borderV, endRow.borderV, t)};
                const float logicalAlong = std::lerp(-50.0f, 50.0f, t);
                const float contourDistance = usesSharedContourFrame
                    ? contourEdge->frames[sample].logicalContourCm
                    : contourStartCm + logicalAlong +
                        kTerrainTileSizeCm * 0.5f;
                const float materialContourDistance =
                    usesSharedContourFrame
                    ? contourEdge->frames[sample].materialContourCm
                    : materialContourStartCm +
                        t * materialStraightLengthCm;
                const float dropWeight = std::lerp(
                    levelDifferences[0u] > 0 ? 1.0f : 0.0f,
                    levelDifferences[1u] > 0 ? 1.0f : 0.0f,
                    t);
                const float effectiveOutward = std::lerp(
                    startEffectiveOutward,
                    endEffectiveOutward,
                    t);
                float geometryAlong =
                    std::lerp(startAlong, endAlong, t);
                float geometryOutward = effectiveOutward +
                    terrainLedgeContourWobbleCm(contourDistance) *
                        dropWeight;
                constexpr float kCornerBlendPhase = 0.25f;
                if (startJoin ==
                        route1_terrain_ledges::Join::Concave &&
                    levelDifferences[0u] > 0 &&
                    t < kCornerBlendPhase) {
                    float blend = 1.0f - t / kCornerBlendPhase;
                    blend = blend * blend * (3.0f - 2.0f * blend);
                    geometryOutward = std::lerp(
                        geometryOutward,
                        concaveStartOutward,
                        blend);
                }
                if (endJoin ==
                        route1_terrain_ledges::Join::Concave &&
                    levelDifferences[1u] > 0 &&
                    t > 1.0f - kCornerBlendPhase) {
                    float blend =
                        (t - (1.0f - kCornerBlendPhase)) /
                        kCornerBlendPhase;
                    blend = blend * blend * (3.0f - 2.0f * blend);
                    geometryOutward = std::lerp(
                        geometryOutward,
                        concaveEndOutward,
                        blend);
                }
                if (usesSharedContourFrame) {
                    const auto sourcePosition =
                        route1_terrain_contours::offset(
                            contourEdge->frames[sample],
                            geometryOutward);
                    const glm::vec3 localPosition =
                        inverseEdgeRotation * glm::vec3{
                            sourcePosition.x - boundaryX,
                            0.0f,
                            sourcePosition.z - boundaryZ};
                    geometryAlong = localPosition.x;
                    geometryOutward = localPosition.z;
                }
                auto vertex =
                    terrainTilePrototypes.cliffVertexTemplate;
                auto sourceVertex =
                    terrainTilePrototypes.cliffSourceVertexTemplate;
                vertex.x = geometryAlong;
                vertex.y = row.y;
                vertex.z = geometryOutward;
                vertex.nx = 0.0f;
                vertex.ny = row.normalY;
                vertex.nz = row.normalZ;
                const glm::vec3 rotated = glm::vec3(
                    edgeRotation * glm::vec4(
                        geometryAlong,
                        vertex.y,
                        vertex.z,
                        1.0f));
                const float sourceX = boundaryX + rotated.x;
                const float sourceZ = boundaryZ + rotated.z;
                glm::vec3 inheritedSourcePosition{};
                glm::vec3 inheritedSourceNormal{};
                const bool sourceGeometrySampled =
                    rowCount == 4u &&
                    sampleSourceTerrainCliffGeometry(
                        {sourceX,
                         static_cast<float>(anchorLevel) *
                                 kTerrainElevationStepCm +
                             vertex.y,
                         sourceZ},
                        sourceTangent,
                        inheritedSourcePosition,
                        inheritedSourceNormal);
                inheritsCompleteSourceGeometry =
                    sourceGeometrySampled &&
                    inheritsCompleteSourceGeometry;
                inheritedSourcePositions.push_back(
                    inheritedSourcePosition);
                inheritedSourceNormals.push_back(
                    inheritedSourceNormal);
                vertex.u = sourceX / 300.0f;
                vertex.v = sourceZ / 300.0f;
                vertex.sourceUv1U =
                    materialContourDistance *
                    cliffUPerCentimetre;
                vertex.sourceUv1V = row.cliffV;
                // Mesh 32 duplicates each horizontal band. Only its lower
                // cliff bands consume the advancing border field; upper
                // bands deliberately pin UV2 to the source's neutral value.
                const bool usesContourBorder =
                    band < rowCount - 3u;
                vertex.sourceUv2U = usesContourBorder
                    ? materialContourDistance *
                        borderUPerCentimetre
                    : -0.05f;
                vertex.sourceUv2V = usesContourBorder
                    ? row.borderV
                    : 0.85f;
                const auto& color = rowIndex < rowCount - 3u
                    ? kLowerBandColor
                    : kWhite;
                vertex.r = color[0];
                vertex.g = color[1];
                vertex.b = color[2];
                vertex.a = color[3];
                sourceVertex.texcoords[0] = {vertex.u, vertex.v};
                sourceVertex.texcoords[1] = {
                    vertex.sourceUv1U,
                    vertex.sourceUv1V};
                sourceVertex.texcoords[2] = {
                    vertex.sourceUv2U,
                    vertex.sourceUv2V};
                sourceVertex.colors[0] = color;
                prototype.vertices.push_back(vertex);
                prototype.sourceVertices.push_back(sourceVertex);
            }
        }
        for (std::uint32_t sample = 0u;
             sample < kEdgeSegments;
             ++sample) {
            const std::uint32_t lowerLeft = firstVertex + sample;
            const std::uint32_t lowerRight = lowerLeft + 1u;
            const std::uint32_t upperLeft =
                firstVertex + kEdgeSamples + sample;
            const std::uint32_t upperRight = upperLeft + 1u;
            prototype.indices.insert(
                prototype.indices.end(),
                {lowerLeft, lowerRight, upperRight,
                 lowerLeft, upperRight, upperLeft});
        }
    }
    if (!usesSharedContourFrame &&
        inheritsCompleteSourceGeometry &&
        inheritedSourcePositions.size() == prototype.vertices.size()) {
        const glm::vec3 sourceAnchor{
            boundaryX,
            static_cast<float>(anchorLevel) *
                kTerrainElevationStepCm,
            boundaryZ};
        for (std::size_t vertexIndex = 0u;
             vertexIndex < prototype.vertices.size();
             ++vertexIndex) {
            const glm::vec3 localPosition = inverseEdgeRotation *
                                            (inheritedSourcePositions[vertexIndex] - sourceAnchor);
            const glm::vec3 localNormal = glm::normalize(
                inverseEdgeRotation *
                inheritedSourceNormals[vertexIndex]);
            auto &vertex = prototype.vertices[vertexIndex];
            vertex.x = localPosition.x;
            vertex.y = localPosition.y;
            vertex.z = localPosition.z;
            vertex.nx = localNormal.x;
            vertex.ny = localNormal.y;
            vertex.nz = localNormal.z;
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

IRenderBackend::WorldSceneRenderObjectHandle
RuntimeEnvironment::Impl::ensureTerrainRegionalCliffContourObject(
    std::uint32_t contourIndex) {
    const auto* contourRun = route1_terrain_contours::findRun(
        terrainContourAssembly, contourIndex);
    if (!contourRun || contourRun->frames.size() < 2u) {
        return {};
    }

    std::int32_t highLevel = 0;
    std::int32_t lowLevel = 0;
    bool foundEdge = false;
    for (const auto& resolved : terrainLedgeResolution.edges) {
        if (resolved.contourIndex != contourIndex) {
            continue;
        }
        const auto tile = std::find_if(
            terrainTiles.begin(),
            terrainTiles.end(),
            [&](const TerrainTileState& candidate) {
                return candidate.gridX == resolved.ownerCell.first &&
                    candidate.gridZ == resolved.ownerCell.second;
            });
        const bool completeFlatDrop =
            tile != terrainTiles.end() && tile->shape == "flat" &&
            resolved.profile.tileLevels[0u] ==
                resolved.profile.tileLevels[1u] &&
            resolved.profile.neighborLevels[0u] ==
                resolved.profile.neighborLevels[1u] &&
            resolved.profile.tileLevels[0u] >
                resolved.profile.neighborLevels[0u];
        if (!completeFlatDrop) {
            return {};
        }
        if (!foundEdge) {
            foundEdge = true;
            highLevel = resolved.profile.tileLevels[0u];
            lowLevel = resolved.profile.neighborLevels[0u];
        } else if (
            highLevel != resolved.profile.tileLevels[0u] ||
            lowLevel != resolved.profile.neighborLevels[0u]) {
            return {};
        }
    }
    if (!foundEdge) {
        return {};
    }

    const std::int32_t levelDifference = highLevel - lowLevel;
    const std::string key =
        "route1:terrain-regional-cliff-contour:contour-" +
        std::to_string(contourIndex) + ":levels-" +
        std::to_string(highLevel) + "-" + std::to_string(lowLevel) +
        (contourRun->closed ? ":closed" : ":open");
    auto [found, inserted] =
        terrainTilePrototypes.cliffPrototypes.try_emplace(key);
    auto& prototype = found->second;
    if (!inserted) {
        return prototype.object;
    }

    struct ProfileRow {
        float yFromHigh;
        float outward;
        float normalY;
        float normalOutward;
        float cliffV;
        float borderV;
    };
    constexpr float kGeneratedSeamY = -0.02f;
    const float height = static_cast<float>(levelDifference) *
        kTerrainElevationStepCm;
    const float capBase = height - kTerrainElevationStepCm;
    std::vector<ProfileRow> rows;
    rows.reserve(5u);
    if (levelDifference > 1) {
        rows.push_back({
            kGeneratedSeamY - height,
            25.0f,
            0.0f,
            1.0f,
            0.00249964f - static_cast<float>(levelDifference - 1),
            0.79334f});
    }
    rows.push_back({
        capBase + kGeneratedSeamY - height,
        25.0f,
        0.27f,
        0.96f,
        0.00249964f,
        0.79334f});
    rows.push_back({
        capBase + 16.875f + kGeneratedSeamY - height,
        20.0f,
        0.27f,
        0.96f,
        0.338313f,
        0.850f});
    rows.push_back({
        capBase + 35.02f + kGeneratedSeamY - height,
        15.0f,
        0.55f,
        0.83f,
        0.699455f,
        0.850f});
    rows.push_back({
        kGeneratedSeamY,
        0.0f,
        0.76f,
        0.65f,
        0.9975f,
        0.850f});

    constexpr float kCliffUPerCentimetre = 0.00516529f;
    constexpr float kBorderUPerCentimetre = 0.00510638f;
    constexpr std::array<float, 4> kWhite{
        1.0f, 1.0f, 1.0f, 1.0f};
    constexpr std::array<float, 4> kLowerBandColor{
        0.180392161f, 0.482352942f, 0.431372553f, 1.0f};
    const std::uint32_t rowWidth = static_cast<std::uint32_t>(
        contourRun->frames.size());
    const std::uint32_t segmentCount = contourRun->closed
        ? rowWidth
        : rowWidth - 1u;
    prototype.vertices.reserve(
        (rows.size() - 1u) * 2u * rowWidth);
    prototype.sourceVertices.reserve(
        (rows.size() - 1u) * 2u * rowWidth);
    prototype.indices.reserve(
        (rows.size() - 1u) * segmentCount * 6u);
    for (std::uint32_t band = 0u;
         band + 1u < rows.size();
         ++band) {
        const std::uint32_t firstVertex =
            static_cast<std::uint32_t>(prototype.vertices.size());
        const bool usesContourBorder = band < rows.size() - 3u;
        for (std::size_t rowInBand = 0u;
             rowInBand < 2u;
             ++rowInBand) {
            const std::size_t rowIndex = band + rowInBand;
            const auto& row = rows[rowIndex];
            const auto& color = rowIndex < rows.size() - 3u
                ? kLowerBandColor
                : kWhite;
            for (const auto& frame : contourRun->frames) {
                const bool straightFrame =
                    std::abs(frame.outward.x) <= 0.0001f ||
                    std::abs(frame.outward.z) <= 0.0001f;
                const float contourWobbleCm = straightFrame
                    ? terrainLedgeContourWobbleCm(
                          frame.logicalContourCm)
                    : 0.0f;
                const auto position = route1_terrain_contours::offset(
                    frame,
                    row.outward + kTerrainLedgeBaseInsetCm +
                        contourWobbleCm);
                auto vertex = terrainTilePrototypes.cliffVertexTemplate;
                auto sourceVertex =
                    terrainTilePrototypes.cliffSourceVertexTemplate;
                vertex.x = position.x;
                vertex.y = row.yFromHigh;
                vertex.z = position.z;
                vertex.nx = frame.outward.x * row.normalOutward;
                vertex.ny = row.normalY;
                vertex.nz = frame.outward.z * row.normalOutward;
                vertex.u = position.x / 300.0f;
                vertex.v = position.z / 300.0f;
                vertex.sourceUv1U =
                    frame.materialContourCm * kCliffUPerCentimetre;
                vertex.sourceUv1V = row.cliffV;
                vertex.sourceUv2U = usesContourBorder
                    ? frame.materialContourCm * kBorderUPerCentimetre
                    : -0.05f;
                vertex.sourceUv2V = usesContourBorder
                    ? row.borderV
                    : 0.85f;
                vertex.r = color[0u];
                vertex.g = color[1u];
                vertex.b = color[2u];
                vertex.a = color[3u];
                sourceVertex.texcoords[0] = {vertex.u, vertex.v};
                sourceVertex.texcoords[1] = {
                    vertex.sourceUv1U, vertex.sourceUv1V};
                sourceVertex.texcoords[2] = {
                    vertex.sourceUv2U, vertex.sourceUv2V};
                sourceVertex.colors[0] = color;
                prototype.vertices.push_back(vertex);
                prototype.sourceVertices.push_back(sourceVertex);
            }
        }
        for (std::uint32_t sample = 0u;
             sample < segmentCount;
             ++sample) {
            const std::uint32_t next = (sample + 1u) % rowWidth;
            const std::uint32_t lowerLeft = firstVertex + sample;
            const std::uint32_t lowerRight = firstVertex + next;
            const std::uint32_t upperLeft =
                firstVertex + rowWidth + sample;
            const std::uint32_t upperRight =
                firstVertex + rowWidth + next;
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

IRenderBackend::WorldSceneRenderObjectHandle
RuntimeEnvironment::Impl::ensureTerrainRegionalFringeContourObject(
    std::uint32_t contourIndex) {
    const auto* contourRun = route1_terrain_contours::findRun(
        terrainContourAssembly, contourIndex);
    if (!contourRun || contourRun->frames.size() < 2u) {
        return {};
    }

    std::int32_t elevationLevel = 0;
    bool foundEdge = false;
    for (const auto& resolved : terrainLedgeResolution.edges) {
        if (resolved.contourIndex != contourIndex) {
            continue;
        }
        const auto tile = std::find_if(
            terrainTiles.begin(),
            terrainTiles.end(),
            [&](const TerrainTileState& candidate) {
                return candidate.gridX == resolved.ownerCell.first &&
                    candidate.gridZ == resolved.ownerCell.second;
            });
        const bool completeFlatDrop =
            tile != terrainTiles.end() && tile->shape == "flat" &&
            resolved.profile.tileLevels[0u] ==
                resolved.profile.tileLevels[1u] &&
            resolved.profile.neighborLevels[0u] ==
                resolved.profile.neighborLevels[1u] &&
            resolved.profile.tileLevels[0u] >
                resolved.profile.neighborLevels[0u];
        if (!completeFlatDrop) {
            return {};
        }
        if (!foundEdge) {
            foundEdge = true;
            elevationLevel = resolved.profile.tileLevels[0u];
        } else if (elevationLevel != resolved.profile.tileLevels[0u]) {
            return {};
        }
    }
    if (!foundEdge) {
        return {};
    }

    const std::string key =
        "route1:terrain-regional-fringe-contour:contour-" +
        std::to_string(contourIndex) + ":level-" +
        std::to_string(elevationLevel) +
        (contourRun->closed ? ":closed" : ":open");
    auto [found, inserted] =
        terrainTilePrototypes.fringePrototypes.try_emplace(key);
    auto& prototype = found->second;
    if (!inserted) {
        return prototype.object;
    }

    constexpr auto kRelativeY = kTerrainLedgeFringeRelativeY;
    constexpr auto kOutward = kTerrainLedgeFringeOutwardCm;
    constexpr auto kNormalY = kTerrainLedgeFringeNormalY;
    constexpr auto kNormalOutward =
        kTerrainLedgeFringeNormalOutward;
    constexpr auto kMaskV = kTerrainLedgeFringeMaskV;
    constexpr std::array<float, 2> kUv2{
        -0.049999952f, 0.949999988f};
    constexpr std::array<std::array<float, 4>, 3> kColors{{
        {0.180392161f, 0.482352942f, 0.431372553f, 1.0f},
        {0.686274529f, 0.796078444f, 0.780392170f, 1.0f},
        {0.686274529f, 0.796078444f, 0.780392170f, 1.0f},
    }};
    const std::uint32_t rowWidth = static_cast<std::uint32_t>(
        contourRun->frames.size());
    const std::uint32_t segmentCount = contourRun->closed
        ? rowWidth
        : rowWidth - 1u;
    float contourMaterialPhaseOffset = 0.0f;
    for (const auto& frame : contourRun->frames) {
        const bool straightFrame =
            std::abs(frame.outward.x) <= 0.0001f ||
            std::abs(frame.outward.z) <= 0.0001f;
        const float contourWobbleCm = straightFrame
            ? terrainLedgeContourWobbleCm(frame.logicalContourCm)
            : 0.0f;
        const auto anchor = route1_terrain_contours::offset(
            frame, kOutward[0u] + contourWobbleCm);
        const float generatedU = kTerrainLedgeFringeMaskUOffset +
            frame.materialContourCm *
                kTerrainLedgeFringeMaskUPerCentimetre;
        float sourceU = generatedU;
        glm::vec4 sourceColor{1.0f};
        if (!sampleSourceTerrainFringeMaterial(
                {anchor.x,
                 static_cast<float>(elevationLevel) *
                         kTerrainElevationStepCm +
                     kRelativeY[0u],
                 anchor.z},
                {frame.tangent.x, frame.tangent.z},
                0u,
                sourceU,
                sourceColor)) {
            continue;
        }
        // The atlas repeats. Align one complete contour with a single
        // decoded source phase instead of independently snapping endpoints;
        // the latter stretches alpha-cutout leaves into visible green posts.
        contourMaterialPhaseOffset = sourceU - generatedU;
        contourMaterialPhaseOffset -=
            std::round(contourMaterialPhaseOffset);
        break;
    }
    prototype.vertices.reserve(kRelativeY.size() * rowWidth);
    prototype.sourceVertices.reserve(kRelativeY.size() * rowWidth);
    prototype.indices.reserve(
        (kRelativeY.size() - 1u) * segmentCount * 6u);
    for (std::size_t row = 0u; row < kRelativeY.size(); ++row) {
        for (const auto& frame : contourRun->frames) {
            const bool straightFrame =
                std::abs(frame.outward.x) <= 0.0001f ||
                std::abs(frame.outward.z) <= 0.0001f;
            const float contourWobbleCm = straightFrame
                ? terrainLedgeContourWobbleCm(frame.logicalContourCm)
                : 0.0f;
            const auto position = route1_terrain_contours::offset(
                frame, kOutward[row] + contourWobbleCm);
            const float generatedMaterialU =
                kTerrainLedgeFringeMaskUOffset +
                frame.materialContourCm *
                    kTerrainLedgeFringeMaskUPerCentimetre +
                contourMaterialPhaseOffset;
            const float materialU = generatedMaterialU;
            glm::vec4 materialColor{
                kColors[row][0u],
                kColors[row][1u],
                kColors[row][2u],
                kColors[row][3u]};
            glm::vec3 generatedPosition{
                position.x,
                static_cast<float>(elevationLevel) *
                        kTerrainElevationStepCm +
                    kRelativeY[row],
                position.z};
            glm::vec3 generatedNormal = glm::normalize(glm::vec3{
                frame.outward.x * kNormalOutward[row],
                kNormalY[row],
                frame.outward.z * kNormalOutward[row]});
            auto vertex = terrainTilePrototypes.fringeVertexTemplate;
            auto sourceVertex =
                terrainTilePrototypes.fringeSourceVertexTemplate;
            vertex.x = generatedPosition.x;
            vertex.y = generatedPosition.y -
                static_cast<float>(elevationLevel) *
                    kTerrainElevationStepCm;
            vertex.z = generatedPosition.z;
            vertex.nx = generatedNormal.x;
            vertex.ny = generatedNormal.y;
            vertex.nz = generatedNormal.z;
            vertex.u = generatedPosition.x / 300.0f;
            vertex.v = generatedPosition.z / 300.0f;
            vertex.sourceUv1U = materialU;
            vertex.sourceUv1V = kMaskV[row];
            vertex.sourceUv2U = kUv2[0u];
            vertex.sourceUv2V = kUv2[1u];
            vertex.r = materialColor.r;
            vertex.g = materialColor.g;
            vertex.b = materialColor.b;
            vertex.a = materialColor.a;
            sourceVertex.texcoords[0] = {vertex.u, vertex.v};
            sourceVertex.texcoords[1] = {
                vertex.sourceUv1U, vertex.sourceUv1V};
            sourceVertex.texcoords[2] = kUv2;
            sourceVertex.colors[0] = {
                materialColor.r,
                materialColor.g,
                materialColor.b,
                materialColor.a};
            prototype.vertices.push_back(vertex);
            prototype.sourceVertices.push_back(sourceVertex);
        }
    }
    for (std::uint32_t row = 0u;
         row + 1u < kRelativeY.size();
         ++row) {
        for (std::uint32_t sample = 0u;
             sample < segmentCount;
             ++sample) {
            const std::uint32_t next = (sample + 1u) % rowWidth;
            const std::uint32_t lowerLeft = row * rowWidth + sample;
            const std::uint32_t lowerRight = row * rowWidth + next;
            const std::uint32_t upperLeft =
                (row + 1u) * rowWidth + sample;
            const std::uint32_t upperRight =
                (row + 1u) * rowWidth + next;
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
        terrainTilePrototypes.fringeSourceVertexSemanticMask,
        std::numeric_limits<std::uint32_t>::max(),
        0u);
    prototype.object = shared_world_scene::ensureRenderObject(
        scene.registry,
        geometry,
        terrainTilePrototypes.fringeMaterialHandle,
        static_cast<shared_world_scene::PipelineVariant>(
            terrainTilePrototypes.fringePipelineVariant),
        terrainTilePrototypes.fringeCookedDrawSlot,
        false);
    return prototype.object;
}

IRenderBackend::WorldSceneRenderObjectHandle
RuntimeEnvironment::Impl::ensureTerrainFringeObject(
    const TerrainTileState& tile,
    std::size_t edge,
    const TerrainSharedEdgeProfile& edgeProfile,
    float contourStartCm,
    float materialContourStartCm,
    route1_terrain_ledges::Join startJoin,
    route1_terrain_ledges::Join endJoin) {
    const std::array<std::int32_t, 2> levelDifferences{
        edgeProfile.tileLevels[0] - edgeProfile.neighborLevels[0],
        edgeProfile.tileLevels[1] - edgeProfile.neighborLevels[1]};
    if (edge >= 4u ||
        (levelDifferences[0] <= 0 && levelDifferences[1] <= 0)) {
        return {};
    }
    const auto* contourEdge = route1_terrain_contours::findEdge(
        terrainContourAssembly,
        {tile.gridX, tile.gridZ},
        edge);
    const std::string key =
        "route1:terrain-fringe:cell-" +
        std::to_string(tile.gridX) + "-" +
        std::to_string(tile.gridZ) + ":edge-" +
        std::to_string(edge) + ":tile-levels-" +
        std::to_string(edgeProfile.tileLevels[0]) + "-" +
        std::to_string(edgeProfile.tileLevels[1]) +
        ":neighbor-levels-" +
        std::to_string(edgeProfile.neighborLevels[0]) + "-" +
        std::to_string(edgeProfile.neighborLevels[1]) +
        ":contour-cm-" +
        std::to_string(static_cast<std::int32_t>(
            std::lround(contourStartCm))) +
        ":material-contour-cm-" +
        std::to_string(static_cast<std::int32_t>(
            std::lround(materialContourStartCm))) +
        ":joins-" +
        std::to_string(static_cast<std::uint32_t>(startJoin)) + "-" +
        std::to_string(static_cast<std::uint32_t>(endJoin));
    auto [found, inserted] =
        terrainTilePrototypes.fringePrototypes.try_emplace(key);
    auto& prototype = found->second;
    if (!inserted) {
        return prototype.object;
    }

    // Exact three-row material-13 side-ledge profile decoded from mesh 32,
    // group 2. The near-horizontal dark-green crown and both sloped carrier
    // bands are required: retaining only the lower card turns an edited
    // ledge into a bright placeholder line instead of the source's leafy lip.
    constexpr std::uint32_t kEdgeSegments =
        kTerrainLedgeContourSegments;
    constexpr std::uint32_t kEdgeSamples = kEdgeSegments + 1u;
    const bool usesSharedContourFrame =
        terrainPatchV2PreviewEnabled && contourEdge &&
        contourEdge->frames.size() == kEdgeSamples &&
        startJoin != route1_terrain_ledges::Join::Concave &&
        endJoin != route1_terrain_ledges::Join::Concave;
    const float materialStraightLengthCm =
        route1_terrain_ledges::materialStraightLengthCm(
            startJoin,
            endJoin);
    constexpr auto kRelativeY = kTerrainLedgeFringeRelativeY;
    constexpr auto kRelativeOutward =
        kTerrainLedgeFringeOutwardCm;
    constexpr auto kNormalY = kTerrainLedgeFringeNormalY;
    constexpr auto kNormalOutward =
        kTerrainLedgeFringeNormalOutward;
    constexpr auto kMaskV = kTerrainLedgeFringeMaskV;
    constexpr std::array<float, 2> kUv2{
        -0.049999952f, 0.949999988f};
    constexpr std::array<std::array<float, 4>, 3> kColors{{
        {0.180392161f, 0.482352942f, 0.431372553f, 1.0f},
        {0.686274529f, 0.796078444f, 0.780392170f, 1.0f},
        {0.686274529f, 0.796078444f, 0.780392170f, 1.0f},
    }};
    constexpr std::array<std::array<std::int32_t, 2>, 4>
        directions{{
            {0, 1},
            {1, 0},
            {0, -1},
            {-1, 0},
        }};
    constexpr std::array<float, 4> rotations{
        0.0f, 90.0f, 180.0f, -90.0f};
    const auto direction = directions[edge];
    constexpr std::array<glm::vec2, 4> tangents{
        glm::vec2{1.0f, 0.0f},
        glm::vec2{0.0f, -1.0f},
        glm::vec2{-1.0f, 0.0f},
        glm::vec2{0.0f, 1.0f}};
    const glm::vec2 sourceTangent = tangents[edge];
    const std::int32_t anchorLevel = std::min(
        edgeProfile.tileLevels[0], edgeProfile.tileLevels[1]);
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
    const glm::mat3 inverseEdgeRotation = glm::transpose(
        glm::mat3(edgeRotation));
    std::array<glm::vec3, kRelativeY.size() * kEdgeSamples>
        inheritedSourcePositions{};
    std::array<glm::vec3, kRelativeY.size() * kEdgeSamples>
        inheritedSourceNormals{};
    bool inheritsCompleteSourceMaterialField = true;
    for (std::size_t row = 0u;
         row < kRelativeY.size();
         ++row) {
        const bool startHasDrop = levelDifferences[0u] > 0;
        const bool endHasDrop = levelDifferences[1u] > 0;
        const auto& sourceStartCorner =
            kTerrainConcaveFringePoints[row][3u];
        const auto& sourceEndCorner =
            kTerrainConcaveFringePoints[row][0u];
        const float concaveStartOutward = -sourceStartCorner.z;
        const float concaveEndOutward = -sourceEndCorner.x;
        const float startAlong = startHasDrop
            ? (startJoin == route1_terrain_ledges::Join::Concave
                   ? -50.0f - sourceStartCorner.x
                   : route1_terrain_ledges::endpointAlongCm(
                         startJoin,
                         true,
                         kRelativeOutward[row]))
            : -50.0f;
        const float endAlong = endHasDrop
            ? (endJoin == route1_terrain_ledges::Join::Concave
                   ? 50.0f + sourceEndCorner.z
                   : route1_terrain_ledges::endpointAlongCm(
                         endJoin,
                         false,
                         kRelativeOutward[row]))
            : 50.0f;
        for (std::uint32_t sample = 0u;
             sample < kEdgeSamples;
             ++sample) {
            const float t = static_cast<float>(sample) /
                static_cast<float>(kEdgeSegments);
            const float dropWeight = std::lerp(
                startHasDrop ? 1.0f : 0.0f,
                endHasDrop ? 1.0f : 0.0f,
                t);
            const float logicalAlong = std::lerp(-50.0f, 50.0f, t);
            const float contourDistance = usesSharedContourFrame
                ? contourEdge->frames[sample].logicalContourCm
                : contourStartCm + logicalAlong +
                    kTerrainTileSizeCm * 0.5f;
            const float materialContourDistance =
                usesSharedContourFrame
                ? contourEdge->frames[sample].materialContourCm
                : materialContourStartCm +
                    t * materialStraightLengthCm;
            float geometryAlong =
                std::lerp(startAlong, endAlong, t);
            float geometryOutward =
                kRelativeOutward[row] * dropWeight +
                terrainLedgeContourWobbleCm(contourDistance) *
                    dropWeight;
            constexpr float kCornerBlendPhase = 0.25f;
            if (startJoin ==
                    route1_terrain_ledges::Join::Concave &&
                startHasDrop && t < kCornerBlendPhase) {
                float blend = 1.0f - t / kCornerBlendPhase;
                blend = blend * blend * (3.0f - 2.0f * blend);
                geometryOutward = std::lerp(
                    geometryOutward,
                    concaveStartOutward,
                    blend);
            }
            if (endJoin ==
                    route1_terrain_ledges::Join::Concave &&
                endHasDrop && t > 1.0f - kCornerBlendPhase) {
                float blend =
                    (t - (1.0f - kCornerBlendPhase)) /
                    kCornerBlendPhase;
                blend = blend * blend * (3.0f - 2.0f * blend);
                geometryOutward = std::lerp(
                    geometryOutward,
                    concaveEndOutward,
                    blend);
            }
            if (usesSharedContourFrame) {
                const auto sourcePosition =
                    route1_terrain_contours::offset(
                        contourEdge->frames[sample],
                        geometryOutward);
                const glm::vec3 localPosition =
                    inverseEdgeRotation * glm::vec3{
                        sourcePosition.x - boundaryX,
                        0.0f,
                        sourcePosition.z - boundaryZ};
                geometryAlong = localPosition.x;
                geometryOutward = localPosition.z;
            }
            auto vertex =
                terrainTilePrototypes.fringeVertexTemplate;
            auto sourceVertex =
                terrainTilePrototypes.fringeSourceVertexTemplate;
            vertex.x = geometryAlong;
            vertex.y =
                std::lerp(
                    static_cast<float>(
                        edgeProfile.tileLevels[0u] - anchorLevel),
                    static_cast<float>(
                        edgeProfile.tileLevels[1u] - anchorLevel),
                    t) * kTerrainElevationStepCm +
                std::lerp(
                    kTerrainLedgeFringeCrownY,
                    kRelativeY[row],
                    dropWeight);
            vertex.z = geometryOutward;
            vertex.nx = 0.0f;
            vertex.ny = std::lerp(1.0f, kNormalY[row], dropWeight);
            vertex.nz = kNormalOutward[row] * dropWeight;
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
            float materialUv1U =
                kTerrainLedgeFringeMaskUOffset +
                materialContourDistance *
                    kTerrainLedgeFringeMaskUPerCentimetre;
            glm::vec4 materialColor{
                kColors[row][0],
                kColors[row][1],
                kColors[row][2],
                kColors[row][3]};
            const std::size_t vertexIndex =
                row * kEdgeSamples + sample;
            const bool sourceFieldSampled =
                sampleSourceTerrainFringeMaterial(
                    {sourceX,
                     static_cast<float>(anchorLevel) *
                             kTerrainElevationStepCm +
                         vertex.y,
                     sourceZ},
                    sourceTangent,
                    row,
                    materialUv1U,
                    materialColor,
                    &inheritedSourcePositions[vertexIndex],
                    &inheritedSourceNormals[vertexIndex]);
            inheritsCompleteSourceMaterialField =
                sourceFieldSampled &&
                inheritsCompleteSourceMaterialField;
            vertex.sourceUv1U = materialUv1U;
            vertex.sourceUv1V = kMaskV[row];
            vertex.sourceUv2U = kUv2[0];
            vertex.sourceUv2V = kUv2[1];
            vertex.r = materialColor.r;
            vertex.g = materialColor.g;
            vertex.b = materialColor.b;
            vertex.a = materialColor.a;
            sourceVertex.texcoords[0] = {vertex.u, vertex.v};
            sourceVertex.texcoords[1] = {
                vertex.sourceUv1U,
                vertex.sourceUv1V};
            sourceVertex.texcoords[2] = kUv2;
            sourceVertex.colors[0] = {
                materialColor.r,
                materialColor.g,
                materialColor.b,
                materialColor.a};
            prototype.vertices.push_back(vertex);
            prototype.sourceVertices.push_back(sourceVertex);
        }
    }
    if (inheritsCompleteSourceMaterialField &&
        !usesSharedContourFrame) {
        const glm::vec3 sourceAnchor{
            boundaryX,
            static_cast<float>(anchorLevel) *
                kTerrainElevationStepCm,
            boundaryZ};
        for (std::size_t vertexIndex = 0u;
             vertexIndex < prototype.vertices.size();
             ++vertexIndex) {
            const glm::vec3 localPosition = inverseEdgeRotation *
                                            (inheritedSourcePositions[vertexIndex] - sourceAnchor);
            const glm::vec3 localNormal = glm::normalize(
                inverseEdgeRotation *
                inheritedSourceNormals[vertexIndex]);
            auto &vertex = prototype.vertices[vertexIndex];
            vertex.x = localPosition.x;
            vertex.y = localPosition.y;
            vertex.z = localPosition.z;
            vertex.nx = localNormal.x;
            vertex.ny = localNormal.y;
            vertex.nz = localNormal.z;
        }
    } else if (!inheritsCompleteSourceMaterialField) {
        for (std::size_t row = 0u;
             row < kRelativeY.size();
             ++row) {
            for (std::uint32_t sample = 0u;
                 sample < kEdgeSamples;
                 ++sample) {
                const float t = static_cast<float>(sample) /
                    static_cast<float>(kEdgeSegments);
                const std::size_t vertexIndex =
                    row * kEdgeSamples + sample;
                const float fallbackUv1U =
                    kTerrainLedgeFringeMaskUOffset +
                    (materialContourStartCm +
                     t * materialStraightLengthCm) *
                        kTerrainLedgeFringeMaskUPerCentimetre;
                auto& vertex = prototype.vertices[vertexIndex];
                auto& sourceVertex =
                    prototype.sourceVertices[vertexIndex];
                vertex.sourceUv1U = fallbackUv1U;
                vertex.r = kColors[row][0];
                vertex.g = kColors[row][1];
                vertex.b = kColors[row][2];
                vertex.a = kColors[row][3];
                sourceVertex.texcoords[1][0] = fallbackUv1U;
                sourceVertex.colors[0] = kColors[row];
            }
        }
    }
    for (std::uint32_t row = 0u;
         row + 1u < kRelativeY.size();
         ++row) {
        for (std::uint32_t sample = 0u;
             sample < kEdgeSegments;
             ++sample) {
            const std::uint32_t lowerLeft =
                row * kEdgeSamples + sample;
            const std::uint32_t lowerRight = lowerLeft + 1u;
            const std::uint32_t upperLeft =
                lowerLeft + kEdgeSamples;
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
        terrainTilePrototypes.fringeSourceVertexSemanticMask,
        std::numeric_limits<std::uint32_t>::max(),
        0u);
    prototype.object = shared_world_scene::ensureRenderObject(
        scene.registry,
        geometry,
        terrainTilePrototypes.fringeMaterialHandle,
        static_cast<shared_world_scene::PipelineVariant>(
            terrainTilePrototypes.fringePipelineVariant),
        terrainTilePrototypes.fringeCookedDrawSlot,
        false);
    return prototype.object;
}

IRenderBackend::WorldSceneRenderObjectHandle
RuntimeEnvironment::Impl::ensureTerrainFringeCornerObject(
    const TerrainTileState& tile,
    std::size_t corner,
    std::int32_t levelDifference,
    float materialContourCm) {
    if (corner >= 4u || levelDifference <= 0) {
        return {};
    }
    const auto* contourTurn =
        route1_terrain_contours::findConvexTurn(
            terrainContourAssembly,
            {tile.gridX, tile.gridZ},
            corner);
    const std::string key =
        "route1:terrain-fringe-corner:cell-" +
        std::to_string(tile.gridX) + "-" +
        std::to_string(tile.gridZ) + ":corner-" +
        std::to_string(corner) + ":levels-" +
        std::to_string(levelDifference) + ":contour-cm-" +
        std::to_string(static_cast<std::int32_t>(
            std::lround(materialContourCm)));
    auto [found, inserted] =
        terrainTilePrototypes.fringePrototypes.try_emplace(key);
    auto& prototype = found->second;
    if (!inserted) {
        return prototype.object;
    }

    constexpr auto kRelativeY = kTerrainLedgeFringeRelativeY;
    constexpr auto kOutward = kTerrainLedgeFringeOutwardCm;
    constexpr auto kNormalY = kTerrainLedgeFringeNormalY;
    constexpr auto kNormalOutward =
        kTerrainLedgeFringeNormalOutward;
    constexpr auto kMaskV = kTerrainLedgeFringeMaskV;
    constexpr std::array<float, 2> kUv2{
        -0.049999952f, 0.949999988f};
    constexpr std::array<std::array<float, 4>, 3> kColors{{
        {0.180392161f, 0.482352942f, 0.431372553f, 1.0f},
        {0.686274529f, 0.796078444f, 0.780392170f, 1.0f},
        {0.686274529f, 0.796078444f, 0.780392170f, 1.0f},
    }};
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
    constexpr std::uint32_t kArcSegments =
        kTerrainLedgeCornerSegments;
    constexpr float kHalfPi = 1.57079632679489661923f;
    const float height =
        static_cast<float>(levelDifference) *
        kTerrainElevationStepCm;
    const float tileCenterX =
        (static_cast<float>(tile.gridX) + 0.5f) *
        kTerrainTileSizeCm;
    const float tileCenterZ =
        (static_cast<float>(tile.gridZ) + 0.5f) *
        kTerrainTileSizeCm;
    const float cornerCenterX = cornerSigns[corner][0] *
        (kTerrainTileSizeCm * 0.5f -
         route1_terrain_ledges::kConvexCornerRadiusCm);
    const float cornerCenterZ = cornerSigns[corner][1] *
        (kTerrainTileSizeCm * 0.5f -
         route1_terrain_ledges::kConvexCornerRadiusCm);
    std::vector<glm::vec3> inheritedSourcePositions;
    std::vector<glm::vec3> inheritedSourceNormals;
    inheritedSourcePositions.reserve(
        kRelativeY.size() * (kArcSegments + 1u));
    inheritedSourceNormals.reserve(
        kRelativeY.size() * (kArcSegments + 1u));
    const bool usesSharedContourFrame =
        terrainPatchV2PreviewEnabled && contourTurn &&
        contourTurn->frames.size() == kArcSegments + 1u;
    bool inheritsCompleteSourceMaterialField = true;
    for (std::size_t row = 0u; row < kRelativeY.size(); ++row) {
        for (std::uint32_t arcIndex = 0u;
             arcIndex <= kArcSegments;
             ++arcIndex) {
            const float phase = static_cast<float>(arcIndex) /
                static_cast<float>(kArcSegments);
            glm::vec2 outward;
            if (usesSharedContourFrame) {
                outward = {
                    contourTurn->frames[arcIndex].outward.x,
                    contourTurn->frames[arcIndex].outward.z};
            } else {
                const float angle = phase * kHalfPi;
                outward =
                    glm::vec2(
                        starts[corner][0], starts[corner][1]) *
                        std::cos(angle) +
                    glm::vec2(
                        ends[corner][0], ends[corner][1]) *
                        std::sin(angle);
                if (glm::length(outward) > 0.0f) {
                    outward = glm::normalize(outward);
                }
            }
            const glm::vec2 sourceTangent{
                outward.y, -outward.x};
            auto vertex = terrainTilePrototypes.fringeVertexTemplate;
            auto sourceVertex =
                terrainTilePrototypes.fringeSourceVertexTemplate;
            if (usesSharedContourFrame) {
                const auto sourcePosition =
                    route1_terrain_contours::offset(
                        contourTurn->frames[arcIndex],
                        kOutward[row]);
                vertex.x = sourcePosition.x - tileCenterX;
                vertex.z = sourcePosition.z - tileCenterZ;
            } else {
                const float radius =
                    route1_terrain_ledges::kConvexCornerRadiusCm +
                    kOutward[row];
                vertex.x = cornerCenterX + outward.x * radius;
                vertex.z = cornerCenterZ + outward.y * radius;
            }
            vertex.y = height + kRelativeY[row];
            vertex.nx = outward.x * kNormalOutward[row];
            vertex.ny = kNormalY[row];
            vertex.nz = outward.y * kNormalOutward[row];
            const float sourceX = tileCenterX + vertex.x;
            const float sourceZ = tileCenterZ + vertex.z;
            vertex.u = sourceX / 300.0f;
            vertex.v = sourceZ / 300.0f;
            float materialUv1U =
                kTerrainLedgeFringeMaskUOffset +
                (usesSharedContourFrame
                     ? contourTurn->frames[arcIndex].materialContourCm
                     : materialContourCm + phase *
                         route1_terrain_ledges::
                             kConvexCornerArcLengthCm) *
                    kTerrainLedgeFringeMaskUPerCentimetre;
            glm::vec4 materialColor{
                kColors[row][0],
                kColors[row][1],
                kColors[row][2],
                kColors[row][3]};
            glm::vec3 inheritedSourcePosition{};
            glm::vec3 inheritedSourceNormal{};
            const bool sourceFieldSampled =
                sampleSourceTerrainFringeMaterial(
                    {sourceX,
                     static_cast<float>(tile.elevationLevel) *
                             kTerrainElevationStepCm +
                         kRelativeY[row],
                     sourceZ},
                    sourceTangent,
                    row,
                    materialUv1U,
                    materialColor,
                    &inheritedSourcePosition,
                    &inheritedSourceNormal);
            inheritsCompleteSourceMaterialField =
                sourceFieldSampled &&
                inheritsCompleteSourceMaterialField;
            inheritedSourcePositions.push_back(
                inheritedSourcePosition);
            inheritedSourceNormals.push_back(
                inheritedSourceNormal);
            vertex.sourceUv1U = materialUv1U;
            vertex.sourceUv1V = kMaskV[row];
            vertex.sourceUv2U = kUv2[0];
            vertex.sourceUv2V = kUv2[1];
            vertex.r = materialColor.r;
            vertex.g = materialColor.g;
            vertex.b = materialColor.b;
            vertex.a = materialColor.a;
            sourceVertex.texcoords[0] = {vertex.u, vertex.v};
            sourceVertex.texcoords[1] = {
                vertex.sourceUv1U, vertex.sourceUv1V};
            sourceVertex.texcoords[2] = kUv2;
            sourceVertex.colors[0] = {
                materialColor.r,
                materialColor.g,
                materialColor.b,
                materialColor.a};
            prototype.vertices.push_back(vertex);
            prototype.sourceVertices.push_back(sourceVertex);
        }
    }
    if (!usesSharedContourFrame &&
        inheritsCompleteSourceMaterialField &&
        inheritedSourcePositions.size() == prototype.vertices.size()) {
        const glm::vec3 sourceAnchor{
            tileCenterX,
            static_cast<float>(
                tile.elevationLevel - levelDifference) *
                kTerrainElevationStepCm,
            tileCenterZ};
        for (std::size_t vertexIndex = 0u;
             vertexIndex < prototype.vertices.size();
             ++vertexIndex) {
            const glm::vec3 localPosition =
                inheritedSourcePositions[vertexIndex] - sourceAnchor;
            auto &vertex = prototype.vertices[vertexIndex];
            vertex.x = localPosition.x;
            vertex.y = localPosition.y;
            vertex.z = localPosition.z;
            vertex.nx = inheritedSourceNormals[vertexIndex].x;
            vertex.ny = inheritedSourceNormals[vertexIndex].y;
            vertex.nz = inheritedSourceNormals[vertexIndex].z;
        }
    } else if (!inheritsCompleteSourceMaterialField) {
        for (std::size_t row = 0u;
             row < kRelativeY.size();
             ++row) {
            for (std::uint32_t arcIndex = 0u;
                 arcIndex <= kArcSegments;
                 ++arcIndex) {
                const float phase = static_cast<float>(arcIndex) /
                    static_cast<float>(kArcSegments);
                const std::size_t vertexIndex =
                    row * (kArcSegments + 1u) + arcIndex;
                const float fallbackUv1U =
                    kTerrainLedgeFringeMaskUOffset +
                    (usesSharedContourFrame
                         ? contourTurn->frames[arcIndex]
                               .materialContourCm
                         : materialContourCm + phase *
                             route1_terrain_ledges::
                                 kConvexCornerArcLengthCm) *
                        kTerrainLedgeFringeMaskUPerCentimetre;
                auto& vertex = prototype.vertices[vertexIndex];
                auto& sourceVertex =
                    prototype.sourceVertices[vertexIndex];
                vertex.sourceUv1U = fallbackUv1U;
                vertex.r = kColors[row][0];
                vertex.g = kColors[row][1];
                vertex.b = kColors[row][2];
                vertex.a = kColors[row][3];
                sourceVertex.texcoords[1][0] = fallbackUv1U;
                sourceVertex.colors[0] = kColors[row];
            }
        }
    }
    constexpr std::uint32_t kRowWidth = kArcSegments + 1u;
    for (std::uint32_t row = 0u;
         row + 1u < kRelativeY.size();
         ++row) {
        for (std::uint32_t arc = 0u;
             arc < kArcSegments;
             ++arc) {
            const std::uint32_t lowerLeft = row * kRowWidth + arc;
            const std::uint32_t lowerRight = lowerLeft + 1u;
            const std::uint32_t upperLeft = lowerLeft + kRowWidth;
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
        terrainTilePrototypes.fringeSourceVertexSemanticMask,
        std::numeric_limits<std::uint32_t>::max(),
        0u);
    prototype.object = shared_world_scene::ensureRenderObject(
        scene.registry,
        geometry,
        terrainTilePrototypes.fringeMaterialHandle,
        static_cast<shared_world_scene::PipelineVariant>(
            terrainTilePrototypes.fringePipelineVariant),
        terrainTilePrototypes.fringeCookedDrawSlot,
        false);
    return prototype.object;
}

IRenderBackend::WorldSceneRenderObjectHandle
RuntimeEnvironment::Impl::ensureTerrainCliffCornerObject(
    const TerrainTileState& tile,
    std::size_t corner,
    std::int32_t levelDifference,
    float materialContourCm) {
    if (corner >= 4u || levelDifference <= 0) {
        return {};
    }
    const auto* contourTurn =
        route1_terrain_contours::findConvexTurn(
            terrainContourAssembly,
            {tile.gridX, tile.gridZ},
            corner);
    const std::string key =
        "route1:terrain-cliff-corner:cell-" +
        std::to_string(tile.gridX) + "-" +
        std::to_string(tile.gridZ) + ":corner-" +
        std::to_string(corner) + ":levels-" +
        std::to_string(levelDifference) + ":contour-cm-" +
        std::to_string(static_cast<std::int32_t>(
            std::lround(materialContourCm)));
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
    constexpr float kGeneratedSeamY = -0.02f;
    std::vector<ProfileRow> rows;
    rows.reserve(5u);
    if (levelDifference > 1) {
        rows.push_back({
            kGeneratedSeamY, 25.0f, 0.0f, 1.0f,
            0.00249964f - static_cast<float>(levelDifference - 1),
            0.79334f});
    }
    const float capBase = height - kTerrainElevationStepCm;
    rows.push_back({
        capBase + kGeneratedSeamY,
        25.0f, 0.27f, 0.96f, 0.00249964f, 0.79334f});
    rows.push_back({
        capBase + 16.875f + kGeneratedSeamY,
        20.0f, 0.27f, 0.96f,
        0.338313f, 0.850f});
    rows.push_back({
        capBase + 35.02f + kGeneratedSeamY,
        15.0f, 0.55f, 0.83f,
        0.699455f, 0.850f});
    rows.push_back({
        height + kGeneratedSeamY,
        0.0f, 0.76f, 0.65f, 0.9975f, 0.850f});

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
    constexpr std::uint32_t kArcSegments =
        kTerrainLedgeCornerSegments;
    const bool usesSharedContourFrame =
        terrainPatchV2PreviewEnabled && contourTurn &&
        contourTurn->frames.size() == kArcSegments + 1u;
    constexpr float kHalfPi = 1.57079632679489661923f;
    constexpr float cliffUPerCentimetre = 0.00516529f;
    constexpr float borderUPerCentimetre = 0.00510638f;
    const float tileCenterX =
        (static_cast<float>(tile.gridX) + 0.5f) *
        kTerrainTileSizeCm;
    const float tileCenterZ =
        (static_cast<float>(tile.gridZ) + 0.5f) *
        kTerrainTileSizeCm;
    const float cornerCenterX = cornerSigns[corner][0] *
        (kTerrainTileSizeCm * 0.5f -
         route1_terrain_ledges::kConvexCornerRadiusCm);
    const float cornerCenterZ = cornerSigns[corner][1] *
        (kTerrainTileSizeCm * 0.5f -
         route1_terrain_ledges::kConvexCornerRadiusCm);
    constexpr std::array<float, 4> kWhite{
        1.0f, 1.0f, 1.0f, 1.0f};
    constexpr std::array<float, 4> kLowerBandColor{
        0.180392161f, 0.482352942f, 0.431372553f, 1.0f};
    constexpr std::uint32_t rowWidth = kArcSegments + 1u;
    std::vector<glm::vec3> inheritedSourcePositions;
    std::vector<glm::vec3> inheritedSourceNormals;
    inheritedSourcePositions.reserve(
        (rows.size() - 1u) * 2u * rowWidth);
    inheritedSourceNormals.reserve(
        (rows.size() - 1u) * 2u * rowWidth);
    bool inheritsCompleteSourceGeometry = levelDifference == 1;
    for (std::uint32_t band = 0u;
         band + 1u < rows.size();
         ++band) {
        const std::uint32_t firstVertex =
            static_cast<std::uint32_t>(prototype.vertices.size());
        const bool usesContourBorder =
            band < rows.size() - 3u;
        for (std::size_t rowInBand = 0u;
             rowInBand < 2u;
             ++rowInBand) {
            const std::size_t rowIndex = band + rowInBand;
            const auto& row = rows[rowIndex];
            const auto& color = rowIndex < rows.size() - 3u
                ? kLowerBandColor
                : kWhite;
            for (std::uint32_t arcIndex = 0u;
                 arcIndex <= kArcSegments;
                 ++arcIndex) {
                const float phase = static_cast<float>(arcIndex) /
                    static_cast<float>(kArcSegments);
                glm::vec2 outward;
                if (usesSharedContourFrame) {
                    outward = {
                        contourTurn->frames[arcIndex].outward.x,
                        contourTurn->frames[arcIndex].outward.z};
                } else {
                    const float angle = phase * kHalfPi;
                    outward = glm::normalize(
                        glm::vec2(
                            starts[corner][0], starts[corner][1]) *
                            std::cos(angle) +
                        glm::vec2(
                            ends[corner][0], ends[corner][1]) *
                            std::sin(angle));
                }
                auto vertex =
                    terrainTilePrototypes.cliffVertexTemplate;
                auto sourceVertex =
                    terrainTilePrototypes.cliffSourceVertexTemplate;
                const float profileOffset =
                    row.outward + kTerrainLedgeBaseInsetCm;
                const float radius =
                    route1_terrain_ledges::kConvexCornerRadiusCm +
                    profileOffset;
                if (usesSharedContourFrame) {
                    const auto sourcePosition =
                        route1_terrain_contours::offset(
                            contourTurn->frames[arcIndex],
                            profileOffset);
                    vertex.x = sourcePosition.x - tileCenterX;
                    vertex.z = sourcePosition.z - tileCenterZ;
                } else {
                    vertex.x = cornerCenterX + outward.x * radius;
                    vertex.z = cornerCenterZ + outward.y * radius;
                }
                vertex.y = row.y;
                vertex.nx = outward.x * row.normalOutward;
                vertex.ny = row.normalY;
                vertex.nz = outward.y * row.normalOutward;
                const float sourceX = tileCenterX + vertex.x;
                const float sourceZ = tileCenterZ + vertex.z;
                const glm::vec2 sourceTangent{
                    outward.y, -outward.x};
                glm::vec3 inheritedSourcePosition{};
                glm::vec3 inheritedSourceNormal{};
                const bool sourceGeometrySampled =
                    levelDifference == 1 &&
                    sampleSourceTerrainCliffGeometry(
                        {sourceX,
                         static_cast<float>(
                             tile.elevationLevel -
                             levelDifference) *
                                 kTerrainElevationStepCm +
                             vertex.y,
                         sourceZ},
                        sourceTangent,
                        inheritedSourcePosition,
                        inheritedSourceNormal);
                inheritsCompleteSourceGeometry =
                    sourceGeometrySampled &&
                    inheritsCompleteSourceGeometry;
                inheritedSourcePositions.push_back(
                    inheritedSourcePosition);
                inheritedSourceNormals.push_back(
                    inheritedSourceNormal);
                vertex.u = sourceX / 300.0f;
                vertex.v = sourceZ / 300.0f;
                const float cornerAlong = usesSharedContourFrame
                    ? contourTurn->frames[arcIndex].materialContourCm
                    : materialContourCm + phase *
                        route1_terrain_ledges::
                            kConvexCornerArcLengthCm;
                vertex.sourceUv1U =
                    cornerAlong * cliffUPerCentimetre;
                vertex.sourceUv1V = row.cliffV;
                vertex.sourceUv2U = usesContourBorder
                    ? cornerAlong * borderUPerCentimetre
                    : -0.05f;
                vertex.sourceUv2V = usesContourBorder
                    ? row.borderV
                    : 0.85f;
                vertex.r = color[0];
                vertex.g = color[1];
                vertex.b = color[2];
                vertex.a = color[3];
                sourceVertex.texcoords[0] = {vertex.u, vertex.v};
                sourceVertex.texcoords[1] = {
                    vertex.sourceUv1U, vertex.sourceUv1V};
                sourceVertex.texcoords[2] = {
                    vertex.sourceUv2U, vertex.sourceUv2V};
                sourceVertex.colors[0] = color;
                prototype.vertices.push_back(vertex);
                prototype.sourceVertices.push_back(sourceVertex);
            }
        }
        for (std::uint32_t arc = 0u;
             arc < kArcSegments;
             ++arc) {
            const std::uint32_t lowerLeft = firstVertex + arc;
            const std::uint32_t lowerRight = lowerLeft + 1u;
            const std::uint32_t upperLeft = lowerLeft + rowWidth;
            const std::uint32_t upperRight = upperLeft + 1u;
            prototype.indices.insert(
                prototype.indices.end(),
                {lowerLeft, lowerRight, upperRight,
                 lowerLeft, upperRight, upperLeft});
        }
    }
    if (!usesSharedContourFrame &&
        inheritsCompleteSourceGeometry &&
        inheritedSourcePositions.size() == prototype.vertices.size()) {
        const glm::vec3 sourceAnchor{
            tileCenterX,
            static_cast<float>(
                tile.elevationLevel - levelDifference) *
                kTerrainElevationStepCm,
            tileCenterZ};
        for (std::size_t vertexIndex = 0u;
             vertexIndex < prototype.vertices.size();
             ++vertexIndex) {
            const glm::vec3 localPosition =
                inheritedSourcePositions[vertexIndex] - sourceAnchor;
            auto &vertex = prototype.vertices[vertexIndex];
            vertex.x = localPosition.x;
            vertex.y = localPosition.y;
            vertex.z = localPosition.z;
            vertex.nx = inheritedSourceNormals[vertexIndex].x;
            vertex.ny = inheritedSourceNormals[vertexIndex].y;
            vertex.nz = inheritedSourceNormals[vertexIndex].z;
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

IRenderBackend::WorldSceneRenderObjectHandle
RuntimeEnvironment::Impl::ensureTerrainConcaveCliffCornerObject(
    const route1_terrain_ledges::RebuiltEdge& incoming,
    const route1_terrain_ledges::RebuiltEdge& outgoing) {
    if (incoming.edge >= 4u || outgoing.edge >= 4u ||
        incoming.endJoin != route1_terrain_ledges::Join::Concave ||
        outgoing.startJoin != route1_terrain_ledges::Join::Concave) {
        return {};
    }
    const std::int32_t tileLevel = incoming.profile.tileLevels[1u];
    const std::int32_t neighborLevel =
        incoming.profile.neighborLevels[1u];
    const std::int32_t levelDifference = tileLevel - neighborLevel;
    if (levelDifference <= 0 ||
        outgoing.profile.tileLevels[0u] != tileLevel ||
        outgoing.profile.neighborLevels[0u] != neighborLevel) {
        return {};
    }
    const std::string key =
        "route1:terrain-cliff-concave-corner:cell-" +
        std::to_string(incoming.ownerCell.first) + "-" +
        std::to_string(incoming.ownerCell.second) + ":edge-" +
        std::to_string(incoming.edge) + ":next-cell-" +
        std::to_string(outgoing.ownerCell.first) + "-" +
        std::to_string(outgoing.ownerCell.second) + ":next-edge-" +
        std::to_string(outgoing.edge) + ":levels-" +
        std::to_string(levelDifference) + ":contour-cm-" +
        std::to_string(static_cast<std::int32_t>(std::lround(
            incoming.materialContourStartCm +
            route1_terrain_ledges::materialStraightLengthCm(
                incoming.startJoin, incoming.endJoin))));
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
    constexpr float kGeneratedSeamY = -0.02f;
    const float height = static_cast<float>(levelDifference) *
        kTerrainElevationStepCm;
    std::vector<ProfileRow> rows;
    rows.reserve(5u);
    if (levelDifference > 1) {
        rows.push_back({
            kGeneratedSeamY, 25.0f, 0.0f, 1.0f,
            0.00249964f - static_cast<float>(levelDifference - 1),
            0.79334f});
    }
    const float capBase = height - kTerrainElevationStepCm;
    rows.push_back({
        capBase + kGeneratedSeamY,
        25.0f, 0.27f, 0.96f, 0.00249964f, 0.79334f});
    rows.push_back({
        capBase + 16.875f + kGeneratedSeamY,
        20.0f, 0.27f, 0.96f, 0.338313f, 0.850f});
    rows.push_back({
        capBase + 35.02f + kGeneratedSeamY,
        15.0f, 0.55f, 0.83f, 0.699455f, 0.850f});
    rows.push_back({
        height + kGeneratedSeamY,
        0.0f, 0.76f, 0.65f, 0.9975f, 0.850f});

    constexpr std::array<glm::vec2, 4> kDirections{
        glm::vec2{0.0f, 1.0f},
        glm::vec2{1.0f, 0.0f},
        glm::vec2{0.0f, -1.0f},
        glm::vec2{-1.0f, 0.0f}};
    constexpr std::array<std::array<std::int32_t, 2>, 4>
        kEndOffsets{{
            {1, 1},
            {1, 0},
            {0, 0},
            {0, 1},
        }};
    const float sourceCornerX = static_cast<float>(
        incoming.ownerCell.first + kEndOffsets[incoming.edge][0]) *
        kTerrainTileSizeCm;
    const float sourceCornerZ = static_cast<float>(
        incoming.ownerCell.second + kEndOffsets[incoming.edge][1]) *
        kTerrainTileSizeCm;
    const glm::vec2 incomingOutward = kDirections[incoming.edge];
    const glm::vec2 outgoingOutward = kDirections[outgoing.edge];
    const float materialContourCm = incoming.materialContourStartCm +
        route1_terrain_ledges::materialStraightLengthCm(
            incoming.startJoin, incoming.endJoin);
    constexpr float kCliffUPerCentimetre = 0.00516529f;
    constexpr float kBorderUPerCentimetre = 0.00510638f;
    constexpr std::array<float, 4> kWhite{
        1.0f, 1.0f, 1.0f, 1.0f};
    constexpr std::array<float, 4> kLowerBandColor{
        0.180392161f, 0.482352942f, 0.431372553f, 1.0f};
    constexpr std::uint32_t kSegments = kTerrainLedgeCornerSegments;
    constexpr std::uint32_t kRowWidth = kSegments + 1u;
    for (std::uint32_t band = 0u;
         band + 1u < rows.size();
         ++band) {
        const std::uint32_t firstVertex =
            static_cast<std::uint32_t>(prototype.vertices.size());
        const bool usesContourBorder = band < rows.size() - 3u;
        for (std::size_t rowInBand = 0u;
             rowInBand < 2u;
             ++rowInBand) {
            const std::size_t rowIndex = band + rowInBand;
            const auto& row = rows[rowIndex];
            const auto& color = rowIndex < rows.size() - 3u
                ? kLowerBandColor
                : kWhite;
            const std::size_t cornerRow = rows.size() == 5u
                ? (rowIndex == 0u ? 0u : rowIndex - 1u)
                : rowIndex;
            for (std::uint32_t sample = 0u;
                 sample <= kSegments;
                 ++sample) {
                const float phase = static_cast<float>(sample) /
                    static_cast<float>(kSegments);
                const float sourceSample = phase * 3.0f;
                const std::size_t sourceIndex = std::min<std::size_t>(
                    static_cast<std::size_t>(sourceSample), 2u);
                const float sourcePhase =
                    sourceSample - static_cast<float>(sourceIndex);
                const auto& sourceA =
                    kTerrainConcaveCliffPoints[cornerRow][sourceIndex];
                const auto& sourceB =
                    kTerrainConcaveCliffPoints[cornerRow][sourceIndex + 1u];
                const TerrainConcaveCornerPoint sourcePoint{
                    std::lerp(sourceA.x, sourceB.x, sourcePhase),
                    std::lerp(sourceA.z, sourceB.z, sourcePhase)};
                const float angle = phase *
                    1.57079632679489661923f;
                const glm::vec2 normalDirection = glm::normalize(
                    incomingOutward * std::cos(angle) +
                    outgoingOutward * std::sin(angle));
                const glm::vec2 position =
                    -sourcePoint.x * incomingOutward -
                    sourcePoint.z * outgoingOutward;
                auto vertex = terrainTilePrototypes.cliffVertexTemplate;
                auto sourceVertex =
                    terrainTilePrototypes.cliffSourceVertexTemplate;
                vertex.x = position.x;
                vertex.y = row.y;
                vertex.z = position.y;
                vertex.nx = normalDirection.x * row.normalOutward;
                vertex.ny = row.normalY;
                vertex.nz = normalDirection.y * row.normalOutward;
                vertex.u = (sourceCornerX + vertex.x) / 300.0f;
                vertex.v = (sourceCornerZ + vertex.z) / 300.0f;
                const float cornerAlong = materialContourCm +
                    phase *
                        route1_terrain_ledges::
                            kConcaveCornerMaterialLengthCm;
                vertex.sourceUv1U =
                    cornerAlong * kCliffUPerCentimetre;
                vertex.sourceUv1V = row.cliffV;
                vertex.sourceUv2U = usesContourBorder
                    ? cornerAlong * kBorderUPerCentimetre
                    : -0.05f;
                vertex.sourceUv2V = usesContourBorder
                    ? row.borderV
                    : 0.85f;
                vertex.r = color[0];
                vertex.g = color[1];
                vertex.b = color[2];
                vertex.a = color[3];
                sourceVertex.texcoords[0] = {vertex.u, vertex.v};
                sourceVertex.texcoords[1] = {
                    vertex.sourceUv1U, vertex.sourceUv1V};
                sourceVertex.texcoords[2] = {
                    vertex.sourceUv2U, vertex.sourceUv2V};
                sourceVertex.colors[0] = color;
                prototype.vertices.push_back(vertex);
                prototype.sourceVertices.push_back(sourceVertex);
            }
        }
        for (std::uint32_t sample = 0u;
             sample < kSegments;
             ++sample) {
            const std::uint32_t lowerLeft = firstVertex + sample;
            const std::uint32_t lowerRight = lowerLeft + 1u;
            const std::uint32_t upperLeft = lowerLeft + kRowWidth;
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

IRenderBackend::WorldSceneRenderObjectHandle
RuntimeEnvironment::Impl::ensureTerrainConcaveFringeCornerObject(
    const route1_terrain_ledges::RebuiltEdge& incoming,
    const route1_terrain_ledges::RebuiltEdge& outgoing) {
    if (incoming.edge >= 4u || outgoing.edge >= 4u ||
        incoming.endJoin != route1_terrain_ledges::Join::Concave ||
        outgoing.startJoin != route1_terrain_ledges::Join::Concave) {
        return {};
    }
    const std::int32_t tileLevel = incoming.profile.tileLevels[1u];
    const std::int32_t neighborLevel =
        incoming.profile.neighborLevels[1u];
    const std::int32_t levelDifference = tileLevel - neighborLevel;
    if (levelDifference <= 0 ||
        outgoing.profile.tileLevels[0u] != tileLevel ||
        outgoing.profile.neighborLevels[0u] != neighborLevel) {
        return {};
    }
    const std::string key =
        "route1:terrain-fringe-concave-corner:cell-" +
        std::to_string(incoming.ownerCell.first) + "-" +
        std::to_string(incoming.ownerCell.second) + ":edge-" +
        std::to_string(incoming.edge) + ":next-cell-" +
        std::to_string(outgoing.ownerCell.first) + "-" +
        std::to_string(outgoing.ownerCell.second) + ":next-edge-" +
        std::to_string(outgoing.edge) + ":levels-" +
        std::to_string(levelDifference) + ":contour-cm-" +
        std::to_string(static_cast<std::int32_t>(std::lround(
            incoming.materialContourStartCm +
            route1_terrain_ledges::materialStraightLengthCm(
                incoming.startJoin, incoming.endJoin))));
    auto [found, inserted] =
        terrainTilePrototypes.fringePrototypes.try_emplace(key);
    auto& prototype = found->second;
    if (!inserted) {
        return prototype.object;
    }

    constexpr auto kRelativeY = kTerrainLedgeFringeRelativeY;
    constexpr auto kOutward = kTerrainLedgeFringeOutwardCm;
    constexpr auto kNormalY = kTerrainLedgeFringeNormalY;
    constexpr auto kNormalOutward =
        kTerrainLedgeFringeNormalOutward;
    constexpr auto kMaskV = kTerrainLedgeFringeMaskV;
    constexpr std::array<float, 2> kUv2{
        -0.049999952f, 0.949999988f};
    constexpr std::array<std::array<float, 4>, 3> kColors{{
        {0.180392161f, 0.482352942f, 0.431372553f, 1.0f},
        {0.686274529f, 0.796078444f, 0.780392170f, 1.0f},
        {0.686274529f, 0.796078444f, 0.780392170f, 1.0f},
    }};
    constexpr std::array<glm::vec2, 4> kDirections{
        glm::vec2{0.0f, 1.0f},
        glm::vec2{1.0f, 0.0f},
        glm::vec2{0.0f, -1.0f},
        glm::vec2{-1.0f, 0.0f}};
    constexpr std::array<std::array<std::int32_t, 2>, 4>
        kEndOffsets{{
            {1, 1},
            {1, 0},
            {0, 0},
            {0, 1},
        }};
    const float sourceCornerX = static_cast<float>(
        incoming.ownerCell.first + kEndOffsets[incoming.edge][0]) *
        kTerrainTileSizeCm;
    const float sourceCornerZ = static_cast<float>(
        incoming.ownerCell.second + kEndOffsets[incoming.edge][1]) *
        kTerrainTileSizeCm;
    const glm::vec2 incomingOutward = kDirections[incoming.edge];
    const glm::vec2 outgoingOutward = kDirections[outgoing.edge];
    const float materialContourCm = incoming.materialContourStartCm +
        route1_terrain_ledges::materialStraightLengthCm(
            incoming.startJoin, incoming.endJoin);
    constexpr std::uint32_t kSegments = kTerrainLedgeCornerSegments;
    constexpr std::uint32_t kRowWidth = kSegments + 1u;
    std::vector<glm::vec3> inheritedSourcePositions;
    std::vector<glm::vec3> inheritedSourceNormals;
    inheritedSourcePositions.reserve(kRelativeY.size() * kRowWidth);
    inheritedSourceNormals.reserve(kRelativeY.size() * kRowWidth);
    bool inheritsCompleteSourceMaterialField = true;
    for (std::size_t row = 0u; row < kRelativeY.size(); ++row) {
        for (std::uint32_t sample = 0u;
             sample <= kSegments;
             ++sample) {
            const float phase = static_cast<float>(sample) /
                static_cast<float>(kSegments);
            const float sourceSample = phase * 3.0f;
            const std::size_t sourceIndex = std::min<std::size_t>(
                static_cast<std::size_t>(sourceSample), 2u);
            const float sourcePhase =
                sourceSample - static_cast<float>(sourceIndex);
            const auto& sourceA =
                kTerrainConcaveFringePoints[row][sourceIndex];
            const auto& sourceB =
                kTerrainConcaveFringePoints[row][sourceIndex + 1u];
            const TerrainConcaveCornerPoint sourcePoint{
                std::lerp(sourceA.x, sourceB.x, sourcePhase),
                std::lerp(sourceA.z, sourceB.z, sourcePhase)};
            const float angle = phase *
                1.57079632679489661923f;
            const glm::vec2 normalDirection = glm::normalize(
                incomingOutward * std::cos(angle) +
                outgoingOutward * std::sin(angle));
            const glm::vec2 sourceTangent{
                normalDirection.y, -normalDirection.x};
            const glm::vec2 position =
                -sourcePoint.x * incomingOutward -
                sourcePoint.z * outgoingOutward;
            auto vertex = terrainTilePrototypes.fringeVertexTemplate;
            auto sourceVertex =
                terrainTilePrototypes.fringeSourceVertexTemplate;
            vertex.x = position.x;
            vertex.y = kRelativeY[row];
            vertex.z = position.y;
            vertex.nx = normalDirection.x * kNormalOutward[row];
            vertex.ny = kNormalY[row];
            vertex.nz = normalDirection.y * kNormalOutward[row];
            vertex.u = (sourceCornerX + vertex.x) / 300.0f;
            vertex.v = (sourceCornerZ + vertex.z) / 300.0f;
            const float cornerAlong = materialContourCm +
                phase *
                    route1_terrain_ledges::
                        kConcaveCornerMaterialLengthCm;
            float materialUv1U =
                kTerrainLedgeFringeMaskUOffset +
                cornerAlong *
                    kTerrainLedgeFringeMaskUPerCentimetre;
            glm::vec4 materialColor{
                kColors[row][0],
                kColors[row][1],
                kColors[row][2],
                kColors[row][3]};
            glm::vec3 inheritedSourcePosition{};
            glm::vec3 inheritedSourceNormal{};
            const bool sourceFieldSampled =
                sampleSourceTerrainFringeMaterial(
                    {sourceCornerX + vertex.x,
                     static_cast<float>(tileLevel) *
                             kTerrainElevationStepCm +
                         kRelativeY[row],
                     sourceCornerZ + vertex.z},
                    sourceTangent,
                    row,
                    materialUv1U,
                    materialColor,
                    &inheritedSourcePosition,
                    &inheritedSourceNormal);
            inheritsCompleteSourceMaterialField =
                sourceFieldSampled &&
                inheritsCompleteSourceMaterialField;
            inheritedSourcePositions.push_back(
                inheritedSourcePosition);
            inheritedSourceNormals.push_back(
                inheritedSourceNormal);
            vertex.sourceUv1U = materialUv1U;
            vertex.sourceUv1V = kMaskV[row];
            vertex.sourceUv2U = kUv2[0];
            vertex.sourceUv2V = kUv2[1];
            vertex.r = materialColor.r;
            vertex.g = materialColor.g;
            vertex.b = materialColor.b;
            vertex.a = materialColor.a;
            sourceVertex.texcoords[0] = {vertex.u, vertex.v};
            sourceVertex.texcoords[1] = {
                vertex.sourceUv1U, vertex.sourceUv1V};
            sourceVertex.texcoords[2] = kUv2;
            sourceVertex.colors[0] = {
                materialColor.r,
                materialColor.g,
                materialColor.b,
                materialColor.a};
            prototype.vertices.push_back(vertex);
            prototype.sourceVertices.push_back(sourceVertex);
        }
    }
    if (inheritsCompleteSourceMaterialField &&
        inheritedSourcePositions.size() == prototype.vertices.size()) {
        const glm::vec3 sourceAnchor{
            sourceCornerX,
            static_cast<float>(tileLevel) *
                kTerrainElevationStepCm,
            sourceCornerZ};
        for (std::size_t vertexIndex = 0u;
             vertexIndex < prototype.vertices.size();
             ++vertexIndex) {
            const glm::vec3 localPosition =
                inheritedSourcePositions[vertexIndex] - sourceAnchor;
            auto &vertex = prototype.vertices[vertexIndex];
            vertex.x = localPosition.x;
            vertex.y = localPosition.y;
            vertex.z = localPosition.z;
            vertex.nx = inheritedSourceNormals[vertexIndex].x;
            vertex.ny = inheritedSourceNormals[vertexIndex].y;
            vertex.nz = inheritedSourceNormals[vertexIndex].z;
        }
    } else {
        for (std::size_t row = 0u;
             row < kRelativeY.size();
             ++row) {
            for (std::uint32_t sample = 0u;
                 sample <= kSegments;
                 ++sample) {
                const float phase = static_cast<float>(sample) /
                    static_cast<float>(kSegments);
                const std::size_t vertexIndex =
                    row * kRowWidth + sample;
                const float fallbackUv1U =
                    kTerrainLedgeFringeMaskUOffset +
                    (materialContourCm +
                     phase * route1_terrain_ledges::
                         kConcaveCornerMaterialLengthCm) *
                        kTerrainLedgeFringeMaskUPerCentimetre;
                auto& vertex = prototype.vertices[vertexIndex];
                auto& sourceVertex =
                    prototype.sourceVertices[vertexIndex];
                vertex.sourceUv1U = fallbackUv1U;
                vertex.r = kColors[row][0];
                vertex.g = kColors[row][1];
                vertex.b = kColors[row][2];
                vertex.a = kColors[row][3];
                sourceVertex.texcoords[1][0] = fallbackUv1U;
                sourceVertex.colors[0] = kColors[row];
            }
        }
    }
    for (std::uint32_t row = 0u;
         row + 1u < kRelativeY.size();
         ++row) {
        for (std::uint32_t sample = 0u;
             sample < kSegments;
             ++sample) {
            const std::uint32_t lowerLeft = row * kRowWidth + sample;
            const std::uint32_t lowerRight = lowerLeft + 1u;
            const std::uint32_t upperLeft = lowerLeft + kRowWidth;
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
        terrainTilePrototypes.fringeSourceVertexSemanticMask,
        std::numeric_limits<std::uint32_t>::max(),
        0u);
    prototype.object = shared_world_scene::ensureRenderObject(
        scene.registry,
        geometry,
        terrainTilePrototypes.fringeMaterialHandle,
        static_cast<shared_world_scene::PipelineVariant>(
            terrainTilePrototypes.fringePipelineVariant),
        terrainTilePrototypes.fringeCookedDrawSlot,
        false);
    return prototype.object;
}

IRenderBackend::WorldSceneRenderObjectHandle
RuntimeEnvironment::Impl::ensureTerrainConcaveCrownObject(
    const route1_terrain_ledges::RebuiltEdge& incoming,
    const route1_terrain_ledges::RebuiltEdge& outgoing) {
    if (incoming.edge >= 4u || outgoing.edge >= 4u ||
        incoming.endJoin != route1_terrain_ledges::Join::Concave ||
        outgoing.startJoin != route1_terrain_ledges::Join::Concave ||
        incoming.profile.tileLevels[1u] !=
            outgoing.profile.tileLevels[0u]) {
        return {};
    }
    const std::string key =
        "route1:terrain-concave-crown:cell-" +
        std::to_string(incoming.ownerCell.first) + "-" +
        std::to_string(incoming.ownerCell.second) + ":edge-" +
        std::to_string(incoming.edge) + ":next-cell-" +
        std::to_string(outgoing.ownerCell.first) + "-" +
        std::to_string(outgoing.ownerCell.second) + ":next-edge-" +
        std::to_string(outgoing.edge) + ":level-" +
        std::to_string(incoming.profile.tileLevels[1u]);
    auto [found, inserted] =
        terrainTilePrototypes.topPrototypes.try_emplace(key);
    auto& prototype = found->second;
    if (!inserted) {
        return prototype.object;
    }

    constexpr std::array<glm::vec2, 4> kDirections{
        glm::vec2{0.0f, 1.0f},
        glm::vec2{1.0f, 0.0f},
        glm::vec2{0.0f, -1.0f},
        glm::vec2{-1.0f, 0.0f}};
    constexpr std::array<std::array<std::int32_t, 2>, 4>
        kEndOffsets{{
            {1, 1},
            {1, 0},
            {0, 0},
            {0, 1},
        }};
    const float sourceCornerX = static_cast<float>(
        incoming.ownerCell.first + kEndOffsets[incoming.edge][0]) *
        kTerrainTileSizeCm;
    const float sourceCornerZ = static_cast<float>(
        incoming.ownerCell.second + kEndOffsets[incoming.edge][1]) *
        kTerrainTileSizeCm;
    const glm::vec2 incomingOutward = kDirections[incoming.edge];
    const glm::vec2 outgoingOutward = kDirections[outgoing.edge];
    constexpr std::uint32_t kSegments = kTerrainLedgeCornerSegments;
    constexpr std::uint32_t kRowWidth = kSegments + 1u;
    constexpr float kInteriorOverlapCm = 35.0f;
    constexpr float kFringeUnderlapCm =
        kTerrainLedgeCrownSafetyOverlapCm;
    for (std::uint32_t row = 0u; row < 2u; ++row) {
        for (std::uint32_t sample = 0u;
             sample <= kSegments;
             ++sample) {
            const float phase = static_cast<float>(sample) /
                static_cast<float>(kSegments);
            const float sourceSample = phase * 3.0f;
            const std::size_t sourceIndex = std::min<std::size_t>(
                static_cast<std::size_t>(sourceSample), 2u);
            const float sourcePhase =
                sourceSample - static_cast<float>(sourceIndex);
            const auto& sourceA =
                kTerrainConcaveFringePoints[0u][sourceIndex];
            const auto& sourceB =
                kTerrainConcaveFringePoints[0u][sourceIndex + 1u];
            const TerrainConcaveCornerPoint sourcePoint{
                std::lerp(sourceA.x, sourceB.x, sourcePhase),
                std::lerp(sourceA.z, sourceB.z, sourcePhase)};
            const float angle = phase *
                1.57079632679489661923f;
            const glm::vec2 outward = glm::normalize(
                incomingOutward * std::cos(angle) +
                outgoingOutward * std::sin(angle));
            glm::vec2 position =
                -sourcePoint.x * incomingOutward -
                sourcePoint.z * outgoingOutward;
            position += outward * (
                row == 0u
                    ? kFringeUnderlapCm
                    : -kInteriorOverlapCm);

            auto vertex = terrainTilePrototypes.groundVertexTemplate;
            auto sourceVertex =
                terrainTilePrototypes.groundSourceVertexTemplate;
            vertex.x = position.x;
            // This is a carrier beneath the ordinary high lawn, not a second
            // visible lawn layer.  Keeping it fractionally lower lets the
            // surrounding tile win the depth test wherever both exist while
            // the carrier still closes the concave-corner cutout.
            vertex.y = kTerrainLawnUnderlayDepthCm;
            vertex.z = position.y;
            vertex.nx = 0.0f;
            vertex.ny = 1.0f;
            vertex.nz = 0.0f;
            const float sourceX = sourceCornerX + position.x;
            const float sourceZ = sourceCornerZ + position.y;
            const std::int32_t gridX = static_cast<std::int32_t>(
                std::floor(sourceX / kTerrainTileSizeCm));
            const std::int32_t gridZ = static_cast<std::int32_t>(
                std::floor(sourceZ / kTerrainTileSizeCm));
            const auto tile = std::find_if(
                terrainTiles.begin(),
                terrainTiles.end(),
                [&](const TerrainTileState& candidate) {
                    return candidate.gridX == gridX &&
                        candidate.gridZ == gridZ;
                });
            SourceTerrainSurfaceSample sampled;
            const bool sampledSource =
                tile != terrainTiles.end() &&
                sampleSourceTerrainSurface(
                    *tile,
                    std::clamp(
                        sourceX / kTerrainTileSizeCm -
                            static_cast<float>(gridX),
                        0.0f,
                        1.0f),
                    std::clamp(
                        sourceZ / kTerrainTileSizeCm -
                            static_cast<float>(gridZ),
                        0.0f,
                        1.0f),
                    sampled);
            const glm::vec2 uv0 = sampledSource
                ? sampled.uv0
                : glm::vec2(sourceX / 300.0f, sourceZ / 300.0f);
            const glm::vec2 uv1 = sampledSource ? sampled.uv1 : uv0;
            const glm::vec2 uv2 = sampledSource
                ? sampled.uv2
                : glm::vec2(
                      vertex.sourceUv2U,
                      vertex.sourceUv2V);
            const glm::vec4 color = sampledSource
                ? sampled.color0
                : glm::vec4(vertex.r, vertex.g, vertex.b, vertex.a);
            vertex.u = uv0.x;
            vertex.v = uv0.y;
            vertex.sourceUv1U = uv1.x;
            vertex.sourceUv1V = uv1.y;
            vertex.sourceUv2U = uv2.x;
            vertex.sourceUv2V = uv2.y;
            vertex.r = color.r;
            vertex.g = color.g;
            vertex.b = color.b;
            vertex.a = color.a;
            sourceVertex.texcoords[0] = {uv0.x, uv0.y};
            sourceVertex.texcoords[1] = {uv1.x, uv1.y};
            sourceVertex.texcoords[2] = {uv2.x, uv2.y};
            sourceVertex.colors[0] = {
                color.r, color.g, color.b, color.a};
            prototype.vertices.push_back(vertex);
            prototype.sourceVertices.push_back(sourceVertex);
        }
    }
    for (std::uint32_t sample = 0u;
         sample < kSegments;
         ++sample) {
        const std::uint32_t outerLeft = sample;
        const std::uint32_t outerRight = sample + 1u;
        const std::uint32_t innerLeft = kRowWidth + sample;
        const std::uint32_t innerRight = innerLeft + 1u;
        prototype.indices.insert(
            prototype.indices.end(),
            {outerLeft, outerRight, innerRight,
             outerLeft, innerRight, innerLeft});
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
RuntimeEnvironment::Impl::ensureTerrainLawnPatchObject(
    const std::string& key,
    float sourceOriginX,
    float sourceOriginZ,
    std::int32_t elevationLevel,
    bool receivesProjectedShadow,
    const TerrainTileState* donorTile,
    const std::vector<glm::vec2>& boundary,
    bool forceRaisedCrownField,
    float localDepthCm,
    const std::vector<std::uint32_t>* exactIndices,
    const std::vector<glm::vec3>* exactNormals,
    const std::vector<float>* exactContactWeights) {
    auto [found, inserted] =
        terrainTilePrototypes.topPrototypes.try_emplace(key);
    auto& prototype = found->second;
    if (!inserted) {
        return prototype.object;
    }
    if (boundary.size() < 3u) {
        return {};
    }
    const bool tessellateContourCap = key.starts_with(
        "route1:terrain-convex-lawn-cap-underlay:");
    const bool tessellatePatch = tessellateContourCap || key.starts_with(
        "route1:terrain-convex-lawn-corner-underlay:") || key.starts_with(
        "route1:terrain-convex-lawn-corner-pocket-repair:") ||
        key.starts_with(
            "route1:terrain-regional-convex-foot-pocket:");
    prototype.vertices.reserve(
        tessellatePatch ? 600u : boundary.size() + 1u);
    prototype.sourceVertices.reserve(
        tessellatePatch ? 600u : boundary.size() + 1u);
    prototype.indices.reserve(
        tessellatePatch ? 2400u : boundary.size() * 3u);

    const auto appendVertex = [&](float localX, float localZ) {
        auto vertex = terrainTilePrototypes.groundVertexTemplate;
        auto sourceVertex =
            terrainTilePrototypes.groundSourceVertexTemplate;
        vertex.x = localX;
        vertex.y = localDepthCm;
        vertex.z = localZ;
        vertex.nx = 0.0f;
        vertex.ny = 1.0f;
        vertex.nz = 0.0f;
        const std::size_t vertexIndex = prototype.vertices.size();
        if (exactNormals && vertexIndex < exactNormals->size()) {
            const glm::vec3 normal = glm::normalize(
                (*exactNormals)[vertexIndex]);
            vertex.nx = normal.x;
            vertex.ny = normal.y;
            vertex.nz = normal.z;
        }
        const float sourceX = sourceOriginX + localX;
        const float sourceZ = sourceOriginZ + localZ;
        const float worldGridX = sourceX / kTerrainTileSizeCm;
        const float worldGridZ = sourceZ / kTerrainTileSizeCm;

        const TerrainTileState* materialTile = donorTile;
        if (!materialTile) {
            float nearestDistanceSquared =
                std::numeric_limits<float>::max();
            for (const auto& candidate : terrainTiles) {
                if (candidate.elevationLevel != elevationLevel ||
                    candidate.surface == "empty" ||
                    (!candidate.sourceOccupied && !candidate.authored)) {
                    continue;
                }
                const float minimumX =
                    static_cast<float>(candidate.gridX);
                const float maximumX = minimumX + 1.0f;
                const float minimumZ =
                    static_cast<float>(candidate.gridZ);
                const float maximumZ = minimumZ + 1.0f;
                const float deltaX = worldGridX < minimumX
                    ? minimumX - worldGridX
                    : (worldGridX > maximumX
                        ? worldGridX - maximumX
                        : 0.0f);
                const float deltaZ = worldGridZ < minimumZ
                    ? minimumZ - worldGridZ
                    : (worldGridZ > maximumZ
                        ? worldGridZ - maximumZ
                        : 0.0f);
                const float distanceSquared =
                    deltaX * deltaX + deltaZ * deltaZ;
                if (distanceSquared >= nearestDistanceSquared) {
                    continue;
                }
                nearestDistanceSquared = distanceSquared;
                materialTile = &candidate;
            }
        }

        SourceTerrainSurfaceSample sampledSource;
        bool sourceSampled = false;
        bool sourceTopologyMatches = false;
        if (materialTile) {
            sourceSampled = sampleSourceTerrainSurface(
                *materialTile,
                std::clamp(
                    worldGridX - static_cast<float>(materialTile->gridX),
                    0.0f,
                    1.0f),
                std::clamp(
                    worldGridZ - static_cast<float>(materialTile->gridZ),
                    0.0f,
                    1.0f),
                sampledSource);
            sourceTopologyMatches =
                materialTile->elevationLevel ==
                    materialTile->sourceElevationLevel &&
                materialTile->shape == materialTile->sourceShape;
        }
        const bool preserveSourceField =
            sourceSampled && materialTile &&
            !materialTile->rebuildContinuousMaterialFields &&
            (!materialTile->authored || sourceTopologyMatches);
        const glm::vec2 fallbackUv{
            sourceX / 300.0f,
            sourceZ / 300.0f};
        const glm::vec2 uv0 = preserveSourceField
            ? sampledSource.uv0
            : fallbackUv;
        glm::vec2 uv1 = preserveSourceField
            ? sampledSource.uv1
            : fallbackUv;
        vertex.u = uv0.x;
        vertex.v = uv0.y;

        glm::vec2 uv2{
            vertex.sourceUv2U,
            vertex.sourceUv2V};
        glm::vec4 color{
            vertex.r, vertex.g, vertex.b, vertex.a};
        constexpr std::array<float, 3> kCarrierRaisedLawnTint{
            0.180392161f, 0.482352942f, 0.431372553f};
        constexpr glm::vec2 kCarrierRaisedLawnFieldUv{
            -0.049999952f, 0.949999988f};
        constexpr glm::vec2 kCarrierOpaqueLightLawnUv2{
            -0.101646f, -1.071291f};
        const bool preserveSourceSurface =
            sourceSampled && materialTile && sourceTopologyMatches &&
            materialTile->surface == materialTile->sourceSurface &&
            !materialTile->cleanSuppressedEncounterGrassTint &&
            !materialTile->rebuildContinuousMaterialFields;
        if (forceRaisedCrownField ||
            (materialTile && materialTile->surface == "dark_lawn")) {
            if (forceRaisedCrownField) {
                // A source light-lawn triangle can carry a different UV1
                // selector right beside the rebuilt raised crown. Keeping
                // that raw selector while forcing only UV2 makes the shader
                // interpolate through unrelated atlas regions, drawing a
                // black diagonal across the cap. The raised carrier is one
                // semantic field, so keep both selector channels coherent.
                uv1 = kCarrierRaisedLawnFieldUv;
            }
            uv2 = kCarrierRaisedLawnFieldUv;
            color = glm::vec4{
                kCarrierRaisedLawnTint[0],
                kCarrierRaisedLawnTint[1],
                kCarrierRaisedLawnTint[2],
                1.0f};
        } else if (materialTile) {
            if (preserveSourceSurface) {
                uv2 = sampledSource.uv2;
                color = sampledSource.color0;
            } else {
                sampleTargetTerrainUv2(
                    materialTile->surface,
                    elevationLevel,
                    worldGridX,
                    worldGridZ,
                    uv2);
                sampleTargetTerrainColor(
                    materialTile->surface,
                    elevationLevel,
                    worldGridX,
                    worldGridZ,
                    color);
            }
            if (materialTile->cleanSuppressedEncounterGrassTint) {
                sampleNormalizedSourceTintColor(
                    *materialTile,
                    std::clamp(
                        worldGridX - static_cast<float>(
                            materialTile->gridX),
                        0.0f,
                        1.0f),
                    std::clamp(
                        worldGridZ - static_cast<float>(
                            materialTile->gridZ),
                        0.0f,
                        1.0f),
                    color,
                    nullptr);
            }
        }
        const bool regionalCrownCarrier = key.starts_with(
            "route1:terrain-regional-crown-contour-underlay:");
        const bool contourCrownCarrier =
            key.find("crown-contour-underlay") != std::string::npos &&
            !regionalCrownCarrier;
        if (!forceRaisedCrownField && !tessellateContourCap &&
            !contourCrownCarrier &&
            materialTile &&
            materialTile->surface == "light_lawn") {
            // These meshes exist only to fill geometric coverage holes.
            // Source UV2 near a ledge deliberately enters the alpha-cut leaf
            // ribbon, which makes a valid carrier cut itself back into the
            // same black hole. Keep the donor's UV0/UV1 and Color0, but use
            // the decoded opaque lawn selector for the hidden fill.
            uv2 = kCarrierOpaqueLightLawnUv2;
        }
        if (!forceRaisedCrownField && exactContactWeights &&
            vertexIndex < exactContactWeights->size()) {
            const float contactWeight = std::clamp(
                (*exactContactWeights)[vertexIndex], 0.0f, 1.0f);
            const glm::vec4 contactColor{
                kCarrierRaisedLawnTint[0],
                kCarrierRaisedLawnTint[1],
                kCarrierRaisedLawnTint[2],
                1.0f};
            color = glm::mix(color, contactColor, contactWeight);
        }
        vertex.sourceUv2U = uv2.x;
        vertex.sourceUv2V = uv2.y;
        vertex.sourceUv1U = uv1.x;
        vertex.sourceUv1V = uv1.y;
        vertex.r = color.r;
        vertex.g = color.g;
        vertex.b = color.b;
        vertex.a = color.a;
        sourceVertex.texcoords[0] = {uv0.x, uv0.y};
        sourceVertex.texcoords[1] = {uv1.x, uv1.y};
        sourceVertex.texcoords[2] = {uv2.x, uv2.y};
        sourceVertex.colors[0] = {
            color.r, color.g, color.b, color.a};
        prototype.vertices.push_back(vertex);
        prototype.sourceVertices.push_back(sourceVertex);
    };

    if (exactIndices) {
        prototype.vertices.reserve(boundary.size());
        prototype.sourceVertices.reserve(boundary.size());
        prototype.indices.reserve(exactIndices->size());
        for (const auto& point : boundary) {
            appendVertex(point.x, point.y);
        }
        prototype.indices = *exactIndices;
    } else if (tessellatePatch) {
        // A single fan interpolates the source Color0/UV fields from one
        // distant centroid. Where a carrier becomes visible through a retired
        // source triangle, those long fan diagonals become tonal seams even
        // when coverage is watertight. Clip the same five-centimetre triangle
        // lattice used by the authored terrain top to every convex-corner
        // carrier, matching its local material interpolation as well as its
        // geometry density.
        float signedBoundaryArea = 0.0f;
        glm::vec2 minimum = boundary.front();
        glm::vec2 maximum = boundary.front();
        for (std::size_t index = 0u; index < boundary.size(); ++index) {
            const glm::vec2& point = boundary[index];
            const glm::vec2& next =
                boundary[(index + 1u) % boundary.size()];
            signedBoundaryArea +=
                point.x * next.y - next.x * point.y;
            minimum = glm::min(minimum, point);
            maximum = glm::max(maximum, point);
        }
        const float orientation =
            signedBoundaryArea >= 0.0f ? 1.0f : -1.0f;
        constexpr float kLatticeStepCm =
            kTerrainTileSizeCm /
            static_cast<float>(kTerrainLedgeContourSegments);
        const float latticeMinimumX =
            std::floor(minimum.x / kLatticeStepCm) *
            kLatticeStepCm;
        const float latticeMinimumZ =
            std::floor(minimum.y / kLatticeStepCm) *
            kLatticeStepCm;
        const float latticeMaximumX =
            std::ceil(maximum.x / kLatticeStepCm) *
            kLatticeStepCm;
        const float latticeMaximumZ =
            std::ceil(maximum.y / kLatticeStepCm) *
            kLatticeStepCm;
        std::map<
            std::pair<std::int64_t, std::int64_t>,
            std::uint32_t> sharedLatticeVertices;
        const auto sharedLatticeVertex =
            [&](const glm::vec2& point) {
                constexpr double kVertexQuantization = 10000.0;
                const auto key = std::pair{
                    static_cast<std::int64_t>(std::llround(
                        static_cast<double>(point.x) *
                        kVertexQuantization)),
                    static_cast<std::int64_t>(std::llround(
                        static_cast<double>(point.y) *
                        kVertexQuantization))};
                if (const auto found =
                        sharedLatticeVertices.find(key);
                    found != sharedLatticeVertices.end()) {
                    return found->second;
                }
                const auto vertexIndex =
                    static_cast<std::uint32_t>(
                        prototype.vertices.size());
                appendVertex(point.x, point.y);
                sharedLatticeVertices.emplace(key, vertexIndex);
                return vertexIndex;
            };
        const auto clippedToBoundary =
            [&](std::vector<glm::vec2> polygon) {
                constexpr float kInsideToleranceCm = 0.0001f;
                for (std::size_t edgeIndex = 0u;
                     edgeIndex < boundary.size() &&
                     !polygon.empty();
                     ++edgeIndex) {
                    const glm::vec2 clipStart =
                        boundary[edgeIndex];
                    const glm::vec2 clipEnd =
                        boundary[(edgeIndex + 1u) %
                                 boundary.size()];
                    const glm::vec2 clipEdge =
                        clipEnd - clipStart;
                    const auto signedDistance =
                        [&](const glm::vec2& point) {
                            const glm::vec2 relative =
                                point - clipStart;
                            return orientation *
                                (clipEdge.x * relative.y -
                                 clipEdge.y * relative.x);
                        };
                    std::vector<glm::vec2> clipped;
                    clipped.reserve(polygon.size() + 1u);
                    glm::vec2 previous = polygon.back();
                    float previousDistance =
                        signedDistance(previous);
                    bool previousInside =
                        previousDistance >=
                        -kInsideToleranceCm;
                    for (const glm::vec2& current : polygon) {
                        const float currentDistance =
                            signedDistance(current);
                        const bool currentInside =
                            currentDistance >=
                            -kInsideToleranceCm;
                        if (currentInside != previousInside) {
                            const float denominator =
                                previousDistance -
                                currentDistance;
                            const float phase =
                                std::abs(denominator) > 1.0e-8f
                                ? previousDistance / denominator
                                : 0.0f;
                            clipped.push_back(glm::mix(
                                previous,
                                current,
                                std::clamp(phase, 0.0f, 1.0f)));
                        }
                        if (currentInside) {
                            clipped.push_back(current);
                        }
                        previous = current;
                        previousDistance = currentDistance;
                        previousInside = currentInside;
                    }
                    polygon = std::move(clipped);
                }
                return polygon;
            };
        const auto appendClippedTriangle =
            [&](glm::vec2 first,
                glm::vec2 second,
                glm::vec2 third) {
                auto polygon = clippedToBoundary(
                    {first, second, third});
                if (polygon.size() < 3u) {
                    return;
                }
                for (std::uint32_t index = 1u;
                     index + 1u < polygon.size();
                     ++index) {
                    const glm::vec2& polygonFirst = polygon[0u];
                    const glm::vec2& polygonSecond = polygon[index];
                    const glm::vec2& polygonThird =
                        polygon[index + 1u];
                    const float signedAreaTwice =
                        (polygonSecond.x - polygonFirst.x) *
                            (polygonThird.y - polygonFirst.y) -
                        (polygonSecond.y - polygonFirst.y) *
                            (polygonThird.x - polygonFirst.x);
                    constexpr float kMinimumAreaTwiceCm2 =
                        0.0001f;
                    if (std::abs(signedAreaTwice) <=
                        kMinimumAreaTwiceCm2) {
                        continue;
                    }
                    const std::uint32_t firstVertex =
                        sharedLatticeVertex(polygonFirst);
                    const std::uint32_t secondVertex =
                        sharedLatticeVertex(polygonSecond);
                    const std::uint32_t thirdVertex =
                        sharedLatticeVertex(polygonThird);
                    if (signedAreaTwice > 0.0f) {
                        prototype.indices.insert(
                            prototype.indices.end(),
                            {firstVertex,
                             secondVertex,
                             thirdVertex});
                    } else {
                        prototype.indices.insert(
                            prototype.indices.end(),
                            {firstVertex,
                             thirdVertex,
                             secondVertex});
                    }
                }
            };
        for (float z = latticeMinimumZ;
             z < latticeMaximumZ - 0.001f;
             z += kLatticeStepCm) {
            for (float x = latticeMinimumX;
                 x < latticeMaximumX - 0.001f;
                 x += kLatticeStepCm) {
                const glm::vec2 lowerLeft{x, z};
                const glm::vec2 lowerRight{
                    x + kLatticeStepCm, z};
                const glm::vec2 upperLeft{
                    x, z + kLatticeStepCm};
                const glm::vec2 upperRight{
                    x + kLatticeStepCm,
                    z + kLatticeStepCm};
                appendClippedTriangle(
                    lowerLeft, lowerRight, upperRight);
                appendClippedTriangle(
                    lowerLeft, upperRight, upperLeft);
            }
        }
    } else {
        glm::vec2 center{0.0f};
        for (const auto& point : boundary) {
            center += point;
        }
        center /= static_cast<float>(boundary.size());
        appendVertex(center.x, center.y);
        for (const auto& point : boundary) {
            appendVertex(point.x, point.y);
        }
        for (std::uint32_t sample = 0u;
             sample < boundary.size();
             ++sample) {
            const std::uint32_t next =
                (sample + 1u) %
                static_cast<std::uint32_t>(boundary.size());
            prototype.indices.insert(
                prototype.indices.end(),
                {0u, sample + 1u, next + 1u});
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
        receivesProjectedShadow
            ? terrainTilePrototypes.groundMaterialHandle
            : terrainTilePrototypes.groundShadowlessMaterialHandle,
        static_cast<shared_world_scene::PipelineVariant>(
            terrainTilePrototypes.groundPipelineVariant),
        terrainTilePrototypes.groundCookedDrawSlot,
        false);
    return prototype.object;
}

IRenderBackend::WorldSceneRenderObjectHandle
RuntimeEnvironment::Impl::ensureTerrainLawnCornerUnderlayObject(
    float sourceCornerX,
    float sourceCornerZ,
    std::int32_t elevationLevel,
    bool receivesProjectedShadow) {
    const std::string key =
        "route1:terrain-lawn-corner-underlay:x-" +
        std::to_string(static_cast<std::int32_t>(
            std::lround(sourceCornerX))) + ":z-" +
        std::to_string(static_cast<std::int32_t>(
            std::lround(sourceCornerZ))) + ":level-" +
        std::to_string(elevationLevel) +
        (receivesProjectedShadow ? ":shadow" : ":shadowless");
    constexpr std::uint32_t kSegments = 24u;
    constexpr float kRadiusCm = 20.0f;
    constexpr float kTau = 6.28318530717958647692f;
    std::vector<glm::vec2> boundary;
    boundary.reserve(kSegments);
    for (std::uint32_t sample = 0u; sample < kSegments; ++sample) {
        const float angle =
            static_cast<float>(sample) /
                static_cast<float>(kSegments) *
            kTau;
        boundary.emplace_back(
            std::cos(angle) * kRadiusCm,
            std::sin(angle) * kRadiusCm);
    }
    return ensureTerrainLawnPatchObject(
        key,
        sourceCornerX,
        sourceCornerZ,
        elevationLevel,
        receivesProjectedShadow,
        nullptr,
        boundary,
        false,
        kTerrainLawnUnderlayDepthCm);
}

IRenderBackend::WorldSceneRenderObjectHandle
RuntimeEnvironment::Impl::ensureTerrainConvexLawnCornerUnderlayObject(
    const TerrainTileState& donorTile,
    float sourceCornerX,
    float sourceCornerZ,
    float quadrantSignX,
    float quadrantSignZ) {
    const std::string key =
        "route1:terrain-convex-lawn-corner-underlay:cell-" +
        std::to_string(donorTile.gridX) + "-" +
        std::to_string(donorTile.gridZ) + ":x-" +
        std::to_string(static_cast<std::int32_t>(
            std::lround(sourceCornerX))) + ":z-" +
        std::to_string(static_cast<std::int32_t>(
            std::lround(sourceCornerZ))) + ":level-" +
        std::to_string(donorTile.elevationLevel) + ":surface-" +
        donorTile.surface +
        (donorTile.receivesProjectedShadow
            ? ":shadow"
            : ":shadowless");
    constexpr std::uint32_t kSegments = 8u;
    // Rebuilding either ledge side retires source ground triangles that cross
    // the shared grid vertex. Those source triangles reach well beyond the
    // 32 cm wall turn itself, so a wall-radius disk can still leave the outer
    // half of a retired triangle showing the clear color. Cover one complete
    // half-tile footprint (plus the two-centimetre raster underlap) while each
    // quadrant continues to sample only its own donor tile.
    constexpr float kRadiusCm =
        kTerrainTileSizeCm * 0.5f + 2.0f;
    constexpr float kQuarterTurn =
        1.57079632679489661923f;
    const float centerAngle = std::atan2(
        quadrantSignZ, quadrantSignX);
    const float startAngle = centerAngle - kQuarterTurn * 0.5f;
    std::vector<glm::vec2> boundary;
    boundary.reserve(kSegments + 2u);
    boundary.emplace_back(0.0f, 0.0f);
    for (std::uint32_t sample = 0u;
         sample <= kSegments;
         ++sample) {
        const float angle = startAngle +
            static_cast<float>(sample) /
                static_cast<float>(kSegments) *
            kQuarterTurn;
        boundary.emplace_back(
            std::cos(angle) * kRadiusCm,
            std::sin(angle) * kRadiusCm);
    }
    return ensureTerrainLawnPatchObject(
        key,
        sourceCornerX,
        sourceCornerZ,
        donorTile.elevationLevel,
        donorTile.receivesProjectedShadow,
        &donorTile,
        boundary,
        false,
        kTerrainLawnCornerRepairDepthCm);
}

IRenderBackend::WorldSceneRenderObjectHandle
RuntimeEnvironment::Impl::ensureTerrainConvexLawnCornerPocketRepairObject(
    const TerrainTileState& donorTile,
    float sourceCornerX,
    float sourceCornerZ,
    float quadrantSignX,
    float quadrantSignZ,
    std::size_t half) {
    if (half >= 2u) {
        return {};
    }
    const std::string key =
        "route1:terrain-convex-lawn-corner-pocket-repair:cell-" +
        std::to_string(donorTile.gridX) + "-" +
        std::to_string(donorTile.gridZ) + ":x-" +
        std::to_string(static_cast<std::int32_t>(
            std::lround(sourceCornerX))) + ":z-" +
        std::to_string(static_cast<std::int32_t>(
            std::lround(sourceCornerZ))) + ":half-" +
        std::to_string(half) + ":level-" +
        std::to_string(donorTile.elevationLevel) + ":surface-" +
        donorTile.surface +
        (donorTile.receivesProjectedShadow
            ? ":shadow"
            : ":shadowless");
    constexpr std::uint32_t kSegments = 4u;
    constexpr float kRadiusCm =
        route1_terrain_ledges::kConvexCornerRadiusCm + 2.0f;
    constexpr float kHalfSector =
        0.78539816339744830962f;
    const float centerAngle = std::atan2(
        quadrantSignZ, quadrantSignX);
    const float startAngle = centerAngle - kHalfSector +
        static_cast<float>(half) * kHalfSector;
    std::vector<glm::vec2> boundary;
    boundary.reserve(kSegments + 2u);
    boundary.emplace_back(0.0f, 0.0f);
    for (std::uint32_t sample = 0u;
         sample <= kSegments;
         ++sample) {
        const float angle = startAngle +
            static_cast<float>(sample) /
                static_cast<float>(kSegments) *
            kHalfSector;
        boundary.emplace_back(
            std::cos(angle) * kRadiusCm,
            std::sin(angle) * kRadiusCm);
    }
    return ensureTerrainLawnPatchObject(
        key,
        sourceCornerX,
        sourceCornerZ,
        donorTile.elevationLevel,
        donorTile.receivesProjectedShadow,
        &donorTile,
        boundary,
        false,
        kTerrainLawnCornerRepairDepthCm);
}

IRenderBackend::WorldSceneRenderObjectHandle
RuntimeEnvironment::Impl::ensureTerrainConvexLawnCapUnderlayObject(
    const TerrainTileState& tile,
    std::size_t corner) {
    if (corner >= 4u) {
        return {};
    }
    const std::string key =
        "route1:terrain-convex-lawn-cap-underlay:cell-" +
        std::to_string(tile.gridX) + "-" +
        std::to_string(tile.gridZ) + ":corner-" +
        std::to_string(corner) + ":level-" +
        std::to_string(tile.elevationLevel) + ":surface-" +
        tile.surface +
        (tile.receivesProjectedShadow ? ":shadow" : ":shadowless");
    constexpr std::array<glm::vec2, 4> kCornerSigns{
        glm::vec2{1.0f, 1.0f},
        glm::vec2{1.0f, -1.0f},
        glm::vec2{-1.0f, -1.0f},
        glm::vec2{-1.0f, 1.0f}};
    constexpr std::uint32_t kArcSegments =
        kTerrainLedgeCornerSegments;
    constexpr float kHalfPi = 1.57079632679489661923f;
    constexpr float kContourCenterCm =
        route1_terrain_ledges::kConvexCornerRadiusCm;
    constexpr float kInnerContourRadiusCm =
        route1_terrain_ledges::kConvexCornerRadiusCm +
        kTerrainLedgeBaseInsetCm +
        kTerrainLedgeCrownSafetyOverlapCm;
    // The deformed corner occupies the first half metre of the owner tile.
    // Carry its safety patch one complete lattice cell past that region, then
    // hand back to the ordinary watertight grid. Extending this through the
    // full metre resamples four times as much source material for no visible
    // coverage benefit.
    constexpr float kInteriorExtentCm = 60.0f;
    const glm::vec2 outwardSign = kCornerSigns[corner];
    const glm::vec2 inwardSign = -outwardSign;
    const glm::vec2 logicalCornerLocal =
        outwardSign * (kTerrainTileSizeCm * 0.5f);
    const auto toTileLocal = [&](float inwardX, float inwardZ) {
        return logicalCornerLocal + glm::vec2{
            inwardSign.x * inwardX,
            inwardSign.y * inwardZ};
    };
    std::vector<glm::vec2> boundary;
    boundary.reserve(kArcSegments + 4u);
    for (std::uint32_t sample = 0u;
         sample <= kArcSegments;
         ++sample) {
        const float angle = 3.14159265358979323846f +
            static_cast<float>(sample) /
                static_cast<float>(kArcSegments) *
            kHalfPi;
        boundary.push_back(toTileLocal(
            kContourCenterCm +
                std::cos(angle) * kInnerContourRadiusCm,
            kContourCenterCm +
                std::sin(angle) * kInnerContourRadiusCm));
    }
    boundary.push_back(toTileLocal(
        kInteriorExtentCm,
        kContourCenterCm - kInnerContourRadiusCm));
    boundary.push_back(toTileLocal(
        kInteriorExtentCm,
        kInteriorExtentCm));
    boundary.push_back(toTileLocal(
        kContourCenterCm - kInnerContourRadiusCm,
        kInteriorExtentCm));
    return ensureTerrainLawnPatchObject(
        key,
        (static_cast<float>(tile.gridX) + 0.5f) *
            kTerrainTileSizeCm,
        (static_cast<float>(tile.gridZ) + 0.5f) *
            kTerrainTileSizeCm,
        tile.elevationLevel,
        tile.receivesProjectedShadow,
        &tile,
        boundary,
        true,
        kTerrainLawnUnderlayDepthCm);
}

IRenderBackend::WorldSceneRenderObjectHandle
RuntimeEnvironment::Impl::ensureTerrainLedgeCrownUnderlayObject(
    const TerrainTileState& tile,
    std::size_t edge) {
    if (edge >= 4u) {
        return {};
    }
    const std::string key =
        "route1:terrain-ledge-crown-underlay:cell-" +
        std::to_string(tile.gridX) + "-" +
        std::to_string(tile.gridZ) + ":edge-" +
        std::to_string(edge) + ":level-" +
        std::to_string(tile.elevationLevel) + ":surface-" +
        tile.surface +
        (tile.receivesProjectedShadow ? ":shadow" : ":shadowless");
    constexpr std::array<glm::vec2, 4> kOutward{
        glm::vec2{0.0f, 1.0f},
        glm::vec2{1.0f, 0.0f},
        glm::vec2{0.0f, -1.0f},
        glm::vec2{-1.0f, 0.0f}};
    constexpr float kHalfLengthCm =
        kTerrainTileSizeCm * 0.5f +
        kTerrainLedgeCrownSafetyOverlapCm;
    // The visible material-13 leaf crown sits about 25 cm inside the logical
    // tile plane. Cover a wider hidden band on both sides of that contour so
    // its alpha-cut tips can never expose clear color, while leaving the
    // ordinary cap and its source material field in charge everywhere else.
    constexpr float kOuterInwardCm = 12.0f;
    constexpr float kInnerInwardCm = 40.0f;
    const glm::vec2 outward = kOutward[edge];
    const glm::vec2 tangent{outward.y, -outward.x};
    const glm::vec2 logicalEdge =
        outward * (kTerrainTileSizeCm * 0.5f);
    const glm::vec2 outer =
        logicalEdge - outward * kOuterInwardCm;
    const glm::vec2 inner =
        logicalEdge - outward * kInnerInwardCm;
    const std::vector<glm::vec2> boundary{
        outer - tangent * kHalfLengthCm,
        outer + tangent * kHalfLengthCm,
        inner + tangent * kHalfLengthCm,
        inner - tangent * kHalfLengthCm};
    return ensureTerrainLawnPatchObject(
        key,
        (static_cast<float>(tile.gridX) + 0.5f) *
            kTerrainTileSizeCm,
        (static_cast<float>(tile.gridZ) + 0.5f) *
            kTerrainTileSizeCm,
        tile.elevationLevel,
        tile.receivesProjectedShadow,
        &tile,
        boundary,
        tile.surface == "dark_lawn",
        kTerrainLawnUnderlayDepthCm);
}

IRenderBackend::WorldSceneRenderObjectHandle
RuntimeEnvironment::Impl::ensureTerrainRegionalCrownContourUnderlayObject(
    std::uint32_t contourIndex) {
    const auto* contourRun = route1_terrain_contours::findRun(
        terrainContourAssembly, contourIndex);
    if (!contourRun || contourRun->frames.size() < 2u) {
        return {};
    }

    const TerrainTileState* firstOwner = nullptr;
    std::int32_t elevationLevel = 0;
    bool receivesProjectedShadow = true;
    bool foundEdge = false;
    for (const auto& resolved : terrainLedgeResolution.edges) {
        if (resolved.contourIndex != contourIndex) {
            continue;
        }
        const auto tile = std::find_if(
            terrainTiles.begin(),
            terrainTiles.end(),
            [&](const TerrainTileState& candidate) {
                return candidate.gridX == resolved.ownerCell.first &&
                    candidate.gridZ == resolved.ownerCell.second;
            });
        const bool completeFlatDrop =
            tile != terrainTiles.end() && tile->shape == "flat" &&
            resolved.profile.tileLevels[0u] ==
                resolved.profile.tileLevels[1u] &&
            resolved.profile.neighborLevels[0u] ==
                resolved.profile.neighborLevels[1u] &&
            resolved.profile.tileLevels[0u] >
                resolved.profile.neighborLevels[0u];
        if (!completeFlatDrop) {
            // Ramps and asymmetric/concave source handoffs retain their
            // decoded specialist paths. A regional flat contour must never
            // flatten one of those profiles merely to close a render strip.
            return {};
        }
        if (!foundEdge) {
            foundEdge = true;
            firstOwner = &*tile;
            elevationLevel = resolved.profile.tileLevels[0u];
            receivesProjectedShadow = tile->receivesProjectedShadow;
        } else if (
            elevationLevel != resolved.profile.tileLevels[0u] ||
            receivesProjectedShadow != tile->receivesProjectedShadow) {
            // One render object has one height anchor and shadow material.
            // Split-cap support can be added when a real source contour needs
            // it; falling back is safer than welding incompatible fields.
            return {};
        }
    }
    if (!foundEdge || !firstOwner) {
        return {};
    }

    const std::string key =
        "route1:terrain-regional-crown-contour-underlay:contour-" +
        std::to_string(contourIndex) + ":level-" +
        std::to_string(elevationLevel) +
        (receivesProjectedShadow ? ":shadow" : ":shadowless") +
        (contourRun->closed ? ":closed" : ":open");
    constexpr float kOuterOverlapCm = 3.0f;
    constexpr float kInnerOverlapCm = 4.0f;
    std::vector<terrain_contours::StripSample> contourSamples;
    contourSamples.reserve(contourRun->frames.size());
    for (const auto& frame : contourRun->frames) {
        const bool straightFrame =
            std::abs(frame.outward.x) <= 0.0001f ||
            std::abs(frame.outward.z) <= 0.0001f;
        const float contourWobbleCm = straightFrame
            ? terrainLedgeContourWobbleCm(frame.logicalContourCm)
            : 0.0f;
        const float baseOffsetCm =
            kTerrainLedgeBaseInsetCm + contourWobbleCm +
            kTerrainLedgeCrownSafetyOverlapCm;
        const auto outer = route1_terrain_contours::offset(
            frame, baseOffsetCm + kOuterOverlapCm);
        const auto inner = route1_terrain_contours::offset(
            frame, baseOffsetCm - kInnerOverlapCm);
        contourSamples.push_back({
            .outer = outer,
            .inner = inner});
    }
    const auto contourMesh = terrain_contours::makeStrip(
        contourSamples, contourRun->closed);
    if (!terrain_contours::validate(contourMesh).valid) {
        return {};
    }

    std::vector<glm::vec2> boundary;
    boundary.reserve(contourMesh.vertices.size());
    for (const auto& point : contourMesh.vertices) {
        boundary.emplace_back(point.x, point.z);
    }
    std::vector<glm::vec3> normals;
    normals.reserve(contourMesh.vertices.size());
    for (const auto& frame : contourRun->frames) {
        normals.emplace_back(
            frame.outward.x * kTerrainLedgeFringeNormalOutward[0u],
            kTerrainLedgeFringeNormalY[0u],
            frame.outward.z * kTerrainLedgeFringeNormalOutward[0u]);
    }
    normals.insert(
        normals.end(),
        contourRun->frames.size(),
        glm::vec3{0.0f, 1.0f, 0.0f});
    std::vector<float> contactWeights(
        contourMesh.vertices.size(), 0.0f);
    std::fill_n(
        contactWeights.begin(), contourRun->frames.size(), 1.0f);
    return ensureTerrainLawnPatchObject(
        key,
        0.0f,
        0.0f,
        elevationLevel,
        receivesProjectedShadow,
        nullptr,
        boundary,
        false,
        kTerrainLawnUnderlayDepthCm,
        &contourMesh.indices,
        &normals,
        &contactWeights);
}

IRenderBackend::WorldSceneRenderObjectHandle
RuntimeEnvironment::Impl::ensureTerrainLedgeCrownContourUnderlayObject(
    const TerrainTileState& tile,
    std::size_t edge) {
    if (edge >= 4u) {
        return {};
    }
    const auto* resolvedLedge = route1_terrain_ledges::find(
        terrainLedgeResolution,
        {tile.gridX, tile.gridZ},
        edge);
    const auto* contourEdge = route1_terrain_contours::findEdge(
        terrainContourAssembly,
        {tile.gridX, tile.gridZ},
        edge);
    if (!resolvedLedge || !contourEdge) {
        return {};
    }
    const std::string key =
        "route1:terrain-ledge-crown-contour-underlay:cell-" +
        std::to_string(tile.gridX) + "-" +
        std::to_string(tile.gridZ) + ":edge-" +
        std::to_string(edge) + ":level-" +
        std::to_string(tile.elevationLevel) + ":surface-" +
        tile.surface + ":contour-" +
        std::to_string(static_cast<std::int32_t>(std::lround(
            resolvedLedge->contourStartCm))) + ":joins-" +
        std::to_string(static_cast<std::uint32_t>(
            resolvedLedge->startJoin)) + "-" +
        std::to_string(static_cast<std::uint32_t>(
            resolvedLedge->endJoin)) +
        (tile.receivesProjectedShadow ? ":shadow" : ":shadowless");
    constexpr std::array<std::array<std::int32_t, 2>, 4>
        kDirections{{
            {0, 1},
            {1, 0},
            {0, -1},
            {-1, 0},
        }};
    const auto neighbor = std::find_if(
        terrainTiles.begin(),
        terrainTiles.end(),
        [&](const TerrainTileState& candidate) {
            return candidate.gridX ==
                    tile.gridX + kDirections[edge][0] &&
                candidate.gridZ ==
                    tile.gridZ + kDirections[edge][1];
        });
    const auto profile = route1TerrainSharedEdgeProfile(
        tile,
        neighbor == terrainTiles.end() ? nullptr : &*neighbor,
        edge);
    const std::array<float, 2> endpointWeights{
        profile.tileLevels[0] > profile.neighborLevels[0]
            ? 1.0f
            : 0.0f,
        profile.tileLevels[1] > profile.neighborLevels[1]
            ? 1.0f
            : 0.0f};
    constexpr std::uint32_t kSegments =
        kTerrainLedgeContourSegments;
    constexpr std::uint32_t kRowWidth = kSegments + 1u;
    if (contourEdge->frames.size() != kRowWidth) {
        return {};
    }
    // This is an alpha-safety gasket, not a replacement lawn patch. Its two
    // rows hug the same recovered ledge contour within an eleven-centimetre
    // band. It cannot form the 28x104 cm or 52x52 cm rectangular sheets that
    // the legacy safety meshes exposed from an oblique camera.
    constexpr float kOuterOverlapCm = 3.0f;
    constexpr float kInnerOverlapCm = 12.0f;
    // The recovered crown radius is seven centimetres. Collapsing a
    // twelve-centimetre inner offset to the turn centre creates a long fan
    // triangle (the green "spear" visible at convex joins). Keep a positive
    // three-centimetre inner radius at the turn and let the ordinary cap own
    // everything farther inside.
    constexpr float kConvexInnerOverlapCm = 4.0f;
    const float centerX =
        (static_cast<float>(tile.gridX) + 0.5f) *
        kTerrainTileSizeCm;
    const float centerZ =
        (static_cast<float>(tile.gridZ) + 0.5f) *
        kTerrainTileSizeCm;
    std::vector<terrain_contours::StripSample> contourSamples;
    contourSamples.reserve(kRowWidth);
    for (std::uint32_t sample = 0u;
         sample <= kSegments;
         ++sample) {
        terrain_contours::StripSample stripSample;
        const auto& contourFrame = contourEdge->frames[sample];
        for (std::uint32_t row = 0u; row < 2u; ++row) {
            const float phase = static_cast<float>(sample) /
                static_cast<float>(kSegments);
            const float weight = std::lerp(
                endpointWeights[0], endpointWeights[1], phase);
            const float contourDistance =
                contourFrame.logicalContourCm;
            const float capContourOffset = weight *
                (kTerrainLedgeBaseInsetCm +
                 terrainLedgeContourWobbleCm(contourDistance) +
                 kTerrainLedgeCrownSafetyOverlapCm);
            float innerOverlapCm = kInnerOverlapCm;
            constexpr float kConvexTaperPhase = 0.25f;
            if (row == 1u &&
                resolvedLedge->startJoin ==
                    route1_terrain_ledges::Join::Convex &&
                phase < kConvexTaperPhase) {
                innerOverlapCm = std::lerp(
                    kConvexInnerOverlapCm,
                    innerOverlapCm,
                    phase / kConvexTaperPhase);
            }
            if (row == 1u &&
                resolvedLedge->endJoin ==
                    route1_terrain_ledges::Join::Convex &&
                phase > 1.0f - kConvexTaperPhase) {
                innerOverlapCm = std::lerp(
                    kConvexInnerOverlapCm,
                    innerOverlapCm,
                    (1.0f - phase) / kConvexTaperPhase);
            }
            const float rowOffset = capContourOffset + weight *
                (row == 0u ? kOuterOverlapCm : -innerOverlapCm);
            const auto sourcePosition =
                route1_terrain_contours::offset(
                    contourFrame, rowOffset);
            const terrain_contours::Point position{
                sourcePosition.x - centerX,
                sourcePosition.z - centerZ};

            auto& contourPoint = row == 0u
                ? stripSample.outer
                : stripSample.inner;
            contourPoint = position;
        }
        contourSamples.push_back(stripSample);
    }
    const auto contourMesh = terrain_contours::makeStrip(
        contourSamples);
    const auto contourValidation = terrain_contours::validate(
        contourMesh);
    if (!contourValidation.valid) {
        return {};
    }
    std::vector<glm::vec2> boundary;
    boundary.reserve(contourMesh.vertices.size());
    for (const auto& point : contourMesh.vertices) {
        boundary.emplace_back(point.x, point.z);
    }
    std::vector<glm::vec3> normals;
    normals.reserve(contourMesh.vertices.size());
    for (const auto& contourFrame : contourEdge->frames) {
        normals.emplace_back(
            contourFrame.outward.x *
                kTerrainLedgeFringeNormalOutward[0u],
            kTerrainLedgeFringeNormalY[0u],
            contourFrame.outward.z *
                kTerrainLedgeFringeNormalOutward[0u]);
    }
    normals.insert(
        normals.end(),
        contourEdge->frames.size(),
        glm::vec3{0.0f, 1.0f, 0.0f});
    std::vector<float> contactWeights(
        contourMesh.vertices.size(), 0.0f);
    std::fill_n(
        contactWeights.begin(), contourEdge->frames.size(), 1.0f);
    return ensureTerrainLawnPatchObject(
        key,
        centerX,
        centerZ,
        tile.elevationLevel,
        tile.receivesProjectedShadow,
        &tile,
        boundary,
        tile.surface == "dark_lawn",
        kTerrainLawnUnderlayDepthCm,
        &contourMesh.indices,
        &normals,
        &contactWeights);
}

IRenderBackend::WorldSceneRenderObjectHandle
RuntimeEnvironment::Impl::ensureTerrainConvexCrownContourUnderlayObject(
    const TerrainTileState& tile,
    std::size_t corner) {
    if (corner >= 4u) {
        return {};
    }
    const auto* contourTurn =
        route1_terrain_contours::findConvexTurn(
            terrainContourAssembly,
            {tile.gridX, tile.gridZ},
            corner);
    if (!contourTurn) {
        return {};
    }
    const std::string key =
        "route1:terrain-convex-crown-contour-underlay:cell-" +
        std::to_string(tile.gridX) + "-" +
        std::to_string(tile.gridZ) + ":corner-" +
        std::to_string(corner) + ":level-" +
        std::to_string(tile.elevationLevel) + ":surface-" +
        tile.surface +
        (tile.receivesProjectedShadow ? ":shadow" : ":shadowless");
    constexpr std::uint32_t kSegments =
        kTerrainLedgeCornerSegments;
    constexpr std::uint32_t kRowWidth = kSegments + 1u;
    if (contourTurn->frames.size() != kRowWidth) {
        return {};
    }
    constexpr float kBaseOffsetCm =
        kTerrainLedgeBaseInsetCm +
        kTerrainLedgeCrownSafetyOverlapCm;
    constexpr std::array<float, 2> kOffsetsCm{
        kBaseOffsetCm + 3.0f,
        kBaseOffsetCm - 4.0f};
    const float tileCenterX =
        (static_cast<float>(tile.gridX) + 0.5f) *
        kTerrainTileSizeCm;
    const float tileCenterZ =
        (static_cast<float>(tile.gridZ) + 0.5f) *
        kTerrainTileSizeCm;
    std::vector<terrain_contours::StripSample> contourSamples;
    contourSamples.reserve(kRowWidth);
    for (std::uint32_t sample = 0u;
         sample <= kSegments;
         ++sample) {
        const auto& contourFrame = contourTurn->frames[sample];
        const auto outer = route1_terrain_contours::offset(
            contourFrame, kOffsetsCm[0u]);
        const auto inner = route1_terrain_contours::offset(
            contourFrame, kOffsetsCm[1u]);
        contourSamples.push_back({
            .outer = {
                outer.x - tileCenterX,
                outer.z - tileCenterZ},
            .inner = {
                inner.x - tileCenterX,
                inner.z - tileCenterZ}});
    }
    const auto contourMesh = terrain_contours::makeCappedStrip(
        contourSamples,
        {contourTurn->center.x - tileCenterX,
         contourTurn->center.z - tileCenterZ});
    const auto contourValidation = terrain_contours::validate(
        contourMesh);
    if (!contourValidation.valid) {
        return {};
    }
    std::vector<glm::vec2> boundary;
    boundary.reserve(contourMesh.vertices.size());
    for (const auto& point : contourMesh.vertices) {
        boundary.emplace_back(point.x, point.z);
    }
    std::vector<glm::vec3> normals;
    normals.reserve(contourMesh.vertices.size());
    for (const auto& contourFrame : contourTurn->frames) {
        normals.emplace_back(
            contourFrame.outward.x *
                kTerrainLedgeFringeNormalOutward[0u],
            kTerrainLedgeFringeNormalY[0u],
            contourFrame.outward.z *
                kTerrainLedgeFringeNormalOutward[0u]);
    }
    normals.insert(
        normals.end(),
        contourTurn->frames.size(),
        glm::vec3{0.0f, 1.0f, 0.0f});
    normals.emplace_back(0.0f, 1.0f, 0.0f);
    std::vector<float> contactWeights(
        contourMesh.vertices.size(), 0.0f);
    std::fill_n(
        contactWeights.begin(), contourTurn->frames.size(), 1.0f);
    return ensureTerrainLawnPatchObject(
        key,
        tileCenterX,
        tileCenterZ,
        tile.elevationLevel,
        tile.receivesProjectedShadow,
        &tile,
        boundary,
        tile.surface == "dark_lawn",
        kTerrainLawnUnderlayDepthCm,
        &contourMesh.indices,
        &normals,
        &contactWeights);
}

IRenderBackend::WorldSceneRenderObjectHandle
RuntimeEnvironment::Impl::ensureTerrainConvexFootContourUnderlayObject(
    const TerrainTileState& tile,
    const TerrainTileState* donorTile,
    std::size_t corner,
    std::int32_t lowLevel) {
    if (corner >= 4u) {
        return {};
    }
    const auto* contourTurn =
        route1_terrain_contours::findConvexTurn(
            terrainContourAssembly,
            {tile.gridX, tile.gridZ},
            corner);
    if (!contourTurn || contourTurn->frames.size() < 2u) {
        return {};
    }
    const float tileCenterX =
        (static_cast<float>(tile.gridX) + 0.5f) *
        kTerrainTileSizeCm;
    const float tileCenterZ =
        (static_cast<float>(tile.gridZ) + 0.5f) *
        kTerrainTileSizeCm;
    // The cliff foot sits at radius 30 cm. Carry the low lawn three
    // centimetres behind it, so alpha-tested foot foliage can never expose
    // the clear colour, while the patch remains the curved corner pocket
    // rather than a square or quadrant-sized overlay.
    constexpr float kCliffFootOffsetCm =
        25.0f + kTerrainLedgeBaseInsetCm;
    constexpr float kFootUnderlapCm = 3.0f;
    std::vector<terrain_contours::Point> footBoundary;
    footBoundary.reserve(contourTurn->frames.size());
    for (const auto& frame : contourTurn->frames) {
        const auto sourcePosition =
            route1_terrain_contours::offset(
                frame,
                kCliffFootOffsetCm - kFootUnderlapCm);
        footBoundary.push_back({
            sourcePosition.x - tileCenterX,
            sourcePosition.z - tileCenterZ});
    }
    const auto contourMesh = terrain_contours::makeFan(
        {contourTurn->logicalCorner.x - tileCenterX,
         contourTurn->logicalCorner.z - tileCenterZ},
        footBoundary);
    if (!terrain_contours::validate(contourMesh).valid) {
        return {};
    }
    std::vector<glm::vec2> boundary;
    boundary.reserve(contourMesh.vertices.size());
    for (const auto& point : contourMesh.vertices) {
        boundary.emplace_back(point.x, point.z);
    }
    const bool receivesProjectedShadow = donorTile
        ? donorTile->receivesProjectedShadow
        : tile.receivesProjectedShadow;
    const std::string key =
        "route1:terrain-regional-convex-foot-pocket:cell-" +
        std::to_string(tile.gridX) + "-" +
        std::to_string(tile.gridZ) + ":corner-" +
        std::to_string(corner) + ":level-" +
        std::to_string(lowLevel) + ":surface-" +
        (donorTile ? donorTile->surface : std::string("nearest")) +
        (receivesProjectedShadow ? ":shadow" : ":shadowless");
    return ensureTerrainLawnPatchObject(
        key,
        tileCenterX,
        tileCenterZ,
        lowLevel,
        receivesProjectedShadow,
        donorTile,
        boundary,
        false,
        kTerrainLawnCornerRepairDepthCm);
}

IRenderBackend::WorldSceneRenderObjectHandle
RuntimeEnvironment::Impl::ensureTerrainSourceHandoffUnderlayObject(
    const TerrainTileState& tile,
    const TerrainTileState& sourceNeighbor,
    std::size_t edge) {
    if (edge >= 4u) {
        return {};
    }
    const std::string key =
        "route1:terrain-source-handoff-underlay:cell-" +
        std::to_string(tile.gridX) + "-" +
        std::to_string(tile.gridZ) + ":neighbor-" +
        std::to_string(sourceNeighbor.gridX) + "-" +
        std::to_string(sourceNeighbor.gridZ) + ":edge-" +
        std::to_string(edge) + ":level-" +
        std::to_string(tile.elevationLevel) + ":surface-" +
        sourceNeighbor.surface +
        (sourceNeighbor.receivesProjectedShadow
            ? ":shadow"
            : ":shadowless");
    constexpr std::array<glm::vec2, 4> kOutward{
        glm::vec2{0.0f, 1.0f},
        glm::vec2{1.0f, 0.0f},
        glm::vec2{0.0f, -1.0f},
        glm::vec2{-1.0f, 0.0f}};
    constexpr std::uint32_t kSegments =
        kTerrainLedgeContourSegments;
    // The one-centimetre-wide source-material ribbon matches the adjoining
    // source field. Put it one hundredth of a centimetre above the rebuilt
    // 0.02 cm plane: an atlas-void boundary triangle can still write depth,
    // so a ribbon between the source and rebuilt planes cannot repair it.
    constexpr float kHalfWidthCm = 1.0f;
    constexpr float kHalfLengthCm =
        kTerrainTileSizeCm * 0.5f;
    constexpr float kSourceHandoffDepthCm =
        kTerrainLawnCornerRepairDepthCm;
    const glm::vec2 outward = kOutward[edge];
    const glm::vec2 tangent{outward.y, -outward.x};
    const glm::vec2 edgeCenter = outward * kHalfLengthCm;
    std::vector<glm::vec2> boundary;
    boundary.reserve((kSegments + 1u) * 2u);
    for (std::uint32_t sample = 0u;
         sample <= kSegments;
         ++sample) {
        const float along = std::lerp(
            -kHalfLengthCm,
            kHalfLengthCm,
            static_cast<float>(sample) /
                static_cast<float>(kSegments));
        boundary.push_back(
            edgeCenter - outward * kHalfWidthCm + tangent * along);
    }
    for (std::uint32_t sample = 0u;
         sample <= kSegments;
         ++sample) {
        const float along = std::lerp(
            kHalfLengthCm,
            -kHalfLengthCm,
            static_cast<float>(sample) /
                static_cast<float>(kSegments));
        boundary.push_back(
            edgeCenter + outward * kHalfWidthCm + tangent * along);
    }
    return ensureTerrainLawnPatchObject(
        key,
        (static_cast<float>(tile.gridX) + 0.5f) *
            kTerrainTileSizeCm,
        (static_cast<float>(tile.gridZ) + 0.5f) *
            kTerrainTileSizeCm,
        tile.elevationLevel,
        sourceNeighbor.receivesProjectedShadow,
        &sourceNeighbor,
        boundary,
        sourceNeighbor.surface == "dark_lawn" ||
            sourceNeighbor.elevationLevel > 0,
        kSourceHandoffDepthCm);
}

bool RuntimeEnvironment::Impl::initializeTerrainMask(
    std::string* outError) {
    terrainMaskGeometries.clear();
    terrainMaskCells.clear();
    terrainCleanupCells.clear();
    terrainSourceReferenceCells.clear();
    terrainInvalidatedSourceCleanupBoundaries.clear();
    terrainMaskRevision = 0u;
    terrainMaskGeometries.reserve(scene.registry.geometries.size());
    for (const auto& geometry : scene.registry.geometries) {
        if (!geometry.vertices || !geometry.indices ||
            geometry.indexCount < 3u ||
            geometry.sourceMeshIndex ==
                std::numeric_limits<std::uint32_t>::max()) {
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
        if (geometry.sourcePolygonGroupIndex >=
            sourceMesh->polygonGroups.size()) {
            return fail(
                outError,
                "Route 1 terrain masking lost polygon group " +
                    std::to_string(
                        geometry.sourcePolygonGroupIndex) +
                    " on source mesh " +
                    std::to_string(geometry.sourceMeshIndex) + ".");
        }
        const std::uint32_t materialIndex =
            sourceMesh->polygonGroups[
                geometry.sourcePolygonGroupIndex]
                .materialIndex;
        const bool terrainAssembly =
            geometry.sourceMeshIndex >= 29u &&
            geometry.sourceMeshIndex <= 36u;
        const bool sourceGround =
            terrainAssembly && materialIndex == 19u;
        const bool groundOverlay =
            geometry.sourceMeshIndex <= 9u;
        const bool flattenedGroundCleanup =
            (geometry.sourceMeshIndex >= 16u &&
             geometry.sourceMeshIndex <= 28u) ||
            (terrainAssembly && materialIndex != 19u);
        if (!sourceGround && !groundOverlay &&
            !flattenedGroundCleanup) {
            continue;
        }
        TerrainMaskGeometry mask{
            .geometryHandle = geometry.handle,
            .originalCacheKey = geometry.geometryCacheKey,
            .originalVertices =
                std::vector<IRenderBackend::WorldMeshVertex>(
                    geometry.vertices,
                    geometry.vertices + geometry.vertexCount),
            .originalSourceVertices = geometry.sourceVertices
                ? std::vector<
                      IRenderBackend::WorldSceneSourceVertex>(
                      geometry.sourceVertices,
                      geometry.sourceVertices +
                          geometry.sourceVertexCount)
                : std::vector<
                      IRenderBackend::WorldSceneSourceVertex>{},
            .originalIndices = std::vector<std::uint32_t>(
                geometry.indices,
                geometry.indices + geometry.indexCount),
            .originalSourceVertexSemanticMask =
                geometry.sourceVertexSemanticMask,
            .sourceModelMatrix = sourceMesh->transform,
            .cleanupOnly = flattenedGroundCleanup,
            .sourceGround = sourceGround,
            // Foliage cards and low-detail overlay carriers regularly cross
            // a metre boundary. Keeping a triangle because only its centroid
            // missed the edited cell leaves the familiar floating slivers.
            // Large canonical terrain groups still use the conservative
            // centroid test to avoid opening an adjacent source cell.
            .maskWhenAnyVertexTouchesCell =
                flattenedGroundCleanup ||
                geometry.sourceMeshIndex <= 9u,
            // Meshes 16-28 contain broad baked foliage/cleanup cards. A
            // triangle can reach the rebuilt ledge band while one of its
            // other vertices sits deep inside the raised tile; all-vertex
            // ownership therefore leaves the visible rectangular sheets.
            // Terrain assemblies 29-36 retain conservative ownership so a
            // changed edge cannot erase an adjoining canonical cliff.
            .retireWhenIntersectingRebuiltBoundary =
                geometry.sourceMeshIndex >= 16u &&
                geometry.sourceMeshIndex <= 28u};
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
    std::set<std::pair<std::int32_t, std::int32_t>>
        nextSourceReferenceCells;
    for (const auto& tile : layout.authoredTerrainTiles) {
        const auto cell = std::pair{tile.gridX, tile.gridZ};
        const auto sourceTile = std::find_if(
            sourceTerrainTiles.begin(),
            sourceTerrainTiles.end(),
            [&](const TerrainTileState& candidate) {
                return candidate.gridX == tile.gridX &&
                    candidate.gridZ == tile.gridZ;
            });
        const auto activeTile = std::find_if(
            terrainTiles.begin(),
            terrainTiles.end(),
            [&](const TerrainTileState& candidate) {
                return candidate.gridX == tile.gridX &&
                    candidate.gridZ == tile.gridZ;
            });
        const bool exactSourceSurface =
            activeTile != terrainTiles.end() &&
            route1TerrainUsesExactSourceSurfaceOverride(
                *activeTile, terrainTiles, sourceTerrainTiles);
        // A source-identical tile with the ordinary receive-shadow policy can
        // remain in the imported batch verbatim. A shadowless tile still has
        // to leave that batch, but its exact source triangles are resubmitted
        // later rather than being replaced with a procedural grid.
        if (!exactSourceSurface ||
            !tile.receivesProjectedShadow) {
            nextCells.emplace(cell);
        }
        const bool explicitCleanup =
            tile.reason == "terrain_flatten_cleanup" ||
            tile.reason == "autochess_board_ground_infill";
        const bool geometryChanged =
            sourceTile == sourceTerrainTiles.end() ||
            tile.elevationLevel != sourceTile->elevationLevel ||
            tile.shape != sourceTile->shape ||
            tile.surface == "empty" ||
            tile.sourceReference.has_value();
        if (tile.sourceReference) {
            nextSourceReferenceCells.emplace(cell);
        }
        if (explicitCleanup || geometryChanged) {
            nextCleanupCells.emplace(cell);
        }
    }
    // Terrain Patch V2 expands an edit into a connected regional replacement
    // with a one-cell source transition ring. Only the preview's generated
    // ground carrier is expanded here: canonical cliffs and foliage remain
    // authoritative unless the underlying authored edit already invalidated
    // them. This makes the experiment reversible and avoids turning a visual
    // transition into a destructive source-geometry cleanup.
    if (terrainPatchV2PreviewEnabled &&
        terrainPatchV2Plan.validation.valid) {
        for (const auto& region : terrainPatchV2Plan.regions) {
            for (const auto& patchCell : region.cells) {
                const auto activeTile = std::find_if(
                    terrainTiles.begin(),
                    terrainTiles.end(),
                    [&](const TerrainTileState& candidate) {
                        return candidate.gridX == patchCell.cell.first &&
                            candidate.gridZ == patchCell.cell.second;
                    });
                if (activeTile != terrainTiles.end() &&
                    activeTile->surface != "empty" &&
                    !activeTile->sourceReference) {
                    nextCells.emplace(patchCell.cell);
                }
            }
        }
    }
    // The recovered fringe/cliff carriers are not tile-local. Matching-height
    // neighbors keep their cleanup geometry on their own side of the shared
    // source-metre plane. A height-changing edge is different: the exact
    // donor patch owns the complete cliff/underside strip, so remove the
    // canonical strip that would otherwise overlap it.
    const auto exactSourceReferenceCells =
        nextSourceReferenceCells;
    std::vector<std::pair<GridCell, GridCell>>
        planeClippedCleanupBoundaries;
    std::vector<std::pair<GridCell, GridCell>>
        donorOwnedSpillBoundaries;
    std::set<std::pair<GridCell, GridCell>>
        nextInvalidatedSourceCleanupBoundaries;
    const auto findTerrainTile =
        [&](const auto& cell) -> const TerrainTileState* {
            const auto found = std::find_if(
                terrainTiles.begin(),
                terrainTiles.end(),
                [&](const TerrainTileState& candidate) {
                    return candidate.gridX == cell.first &&
                        candidate.gridZ == cell.second;
                });
            return found == terrainTiles.end()
                ? nullptr
                : &*found;
        };
    const auto findSourceTerrainTile =
        [&](const auto& cell) -> const TerrainTileState* {
            const auto found = std::find_if(
                sourceTerrainTiles.begin(),
                sourceTerrainTiles.end(),
                [&](const TerrainTileState& candidate) {
                    return candidate.gridX == cell.first &&
                        candidate.gridZ == cell.second;
                });
            return found == sourceTerrainTiles.end()
                ? nullptr
                : &*found;
        };
    const auto tileCeilingLevel =
        [](const TerrainTileState* tile) {
            if (!tile) {
                return std::numeric_limits<std::int32_t>::lowest();
            }
            return tile->elevationLevel +
                (tile->shape.starts_with("ramp_") ? 1 : 0);
        };
    const auto cleanupCeilingLevel =
        [&](const auto& cell) {
            return std::max(
                tileCeilingLevel(findTerrainTile(cell)),
                tileCeilingLevel(findSourceTerrainTile(cell)));
        };
    constexpr std::array<std::array<std::int32_t, 2>, 4>
        directions{{
            {0, 1},
            {1, 0},
            {0, -1},
            {-1, 0},
        }};
    for (const auto& cell : exactSourceReferenceCells) {
        const auto* tile = findTerrainTile(cell);
        if (!tile || tile->surface == "empty") {
            continue;
        }
        for (std::size_t edge = 0u;
             edge < directions.size();
             ++edge) {
            const auto& direction = directions[edge];
            const GridCell neighborCell{
                cell.first + direction[0],
                cell.second + direction[1]};
            if (exactSourceReferenceCells.contains(neighborCell)) {
                continue;
            }
            const auto* neighbor = findTerrainTile(neighborCell);
            if (!neighbor || neighbor->surface == "empty") {
                continue;
            }
            if (route1TerrainSourcePatchNeedsBoundarySpill(
                    *tile, neighbor, edge)) {
                donorOwnedSpillBoundaries.emplace_back(
                    neighborCell, cell);
            } else {
                planeClippedCleanupBoundaries.emplace_back(
                    neighborCell, cell);
            }
        }
    }
    // A decoded ledge/fringe band straddles its owning source edge. Removing
    // only triangles whose vertices enter an edited cell leaves the paired
    // underside triangle a few centimetres inside the unchanged neighbor.
    // When the edit changes the source edge profile, retire that complete
    // 25 cm source band on the neighbor side as well. Exact source-reference
    // patches retain their separate donor-spill ownership rules above.
    for (const auto& cell : nextCleanupCells) {
        if (exactSourceReferenceCells.contains(cell)) {
            continue;
        }
        const auto* tile = findTerrainTile(cell);
        const auto* sourceTile = findSourceTerrainTile(cell);
        if (!tile || tile->surface == "empty" || !sourceTile) {
            continue;
        }
        for (std::size_t edge = 0u;
             edge < directions.size();
             ++edge) {
            const auto& direction = directions[edge];
            const GridCell neighborCell{
                cell.first + direction[0],
                cell.second + direction[1]};
            if (nextCleanupCells.contains(neighborCell) ||
                exactSourceReferenceCells.contains(neighborCell)) {
                continue;
            }
            const auto* neighbor = findTerrainTile(neighborCell);
            const auto* sourceNeighbor =
                findSourceTerrainTile(neighborCell);
            const auto* activeNeighbor =
                neighbor && neighbor->surface != "empty"
                ? neighbor
                : nullptr;
            const auto* activeSourceNeighbor =
                sourceNeighbor && sourceNeighbor->surface != "empty"
                ? sourceNeighbor
                : nullptr;
            if (route1TerrainSourceBoundaryInvalidated(
                    *tile,
                    activeNeighbor,
                    *sourceTile,
                    activeSourceNeighbor,
                    edge)) {
                nextInvalidatedSourceCleanupBoundaries.emplace(
                    neighborCell, cell);
                // Ground caps are separate material-19 carriers and are not
                // covered by cleanup-only cliff/fringe masking. Replace the
                // neighboring source cap whenever this shared source edge is
                // rebuilt so one clipped top, not the old square cap plus a
                // generated crown, owns the ledge.
                if (activeNeighbor) {
                    const auto activeProfile =
                        route1TerrainSharedEdgeProfile(
                            *tile, activeNeighbor, edge);
                    const bool neighborOwnsRaisedCap =
                        activeProfile.neighborLevels[0] >
                            activeProfile.tileLevels[0] ||
                        activeProfile.neighborLevels[1] >
                            activeProfile.tileLevels[1];
                    if (neighborOwnsRaisedCap) {
                        nextCells.emplace(neighborCell);
                    }
                }
            }
        }
    }
    // Ledge ownership depends on current/source endpoint profiles, not on
    // whether the set of masked cells changed. Resolve it before the mask
    // cache early-out so repeated live edits at the same cells still rebuild
    // their contour geometry and texture coordinates.
    terrainLedgeResolution = route1_terrain_ledges::resolve(
        terrainTiles,
        sourceTerrainTiles);
    terrainContourAssembly = route1_terrain_contours::assemble(
        terrainLedgeResolution,
        kTerrainLedgeContourSegments,
        kTerrainLedgeCornerSegments);
    for (const auto& ledge : terrainLedgeResolution.edges) {
        if (!ledge.rebuildsJoinedSourceBoundary || ledge.edge >= 4u) {
            continue;
        }
        const auto& direction = directions[ledge.edge];
        constexpr std::array<std::array<std::int32_t, 2>, 4>
            tangents{{
                {1, 0},
                {0, -1},
                {-1, 0},
                {0, 1},
            }};
        const auto& tangent = tangents[ledge.edge];
        const GridCell neighborCell{
            ledge.ownerCell.first + direction[0],
            ledge.ownerCell.second + direction[1]};
        nextInvalidatedSourceCleanupBoundaries.emplace(
            ledge.ownerCell,
            neighborCell);
        // A propagated source ledge is only half of the handoff. Its rounded
        // corner also reserves ground on both sides of the logical grid
        // corner. Rebuild the raised cap and the adjoining lower contact tile
        // together; retaining either square source top leaves a rectangular
        // flap through the arc or a clear-color triangle beneath it.
        if (const auto* owner = findTerrainTile(ledge.ownerCell);
            owner && owner->surface != "empty") {
            nextCells.emplace(ledge.ownerCell);
        }
        if (const auto* neighbor = findTerrainTile(neighborCell);
            neighbor && neighbor->surface != "empty") {
            nextCells.emplace(neighborCell);
        }
        // At a convex turn, the generated wall and its clipped high cap
        // reserve part of the diagonally adjacent low-side cell.
        // Canonical ground triangles routinely cross that grid vertex and
        // are retired with either side contact. Rebuild the diagonal carrier
        // too so the junction cannot expose a triangular clear-color hole.
        const auto includeCornerContact = [&](GridCell cornerCell) {
            if (const auto* corner = findTerrainTile(cornerCell);
                corner && corner->surface != "empty" &&
                !corner->sourceReference &&
                corner->shape == "flat") {
                nextCells.emplace(cornerCell);
            }
        };
        if (ledge.startJoin ==
            route1_terrain_ledges::Join::Convex) {
            includeCornerContact({
                neighborCell.first - tangent[0],
                neighborCell.second - tangent[1]});
        }
        if (ledge.endJoin ==
            route1_terrain_ledges::Join::Convex) {
            includeCornerContact({
                neighborCell.first + tangent[0],
                neighborCell.second + tangent[1]});
        }

        // A concave turn has the inverse ownership: three high cells wrap
        // the low-side corner. The two clipped edge caps reserve the corner
        // of the diagonally adjacent high cell, and canonical metre-scale
        // triangles can be retired when either rebuilt edge touches them.
        // Promote that one diagonal crown carrier with the joined edges so
        // the inside turn cannot expose a triangular hole between the caps.
        const auto includeConcaveCrown = [&](GridCell cornerCell) {
            if (const auto* corner = findTerrainTile(cornerCell);
                corner && corner->surface != "empty" &&
                !corner->sourceReference &&
                corner->shape == "flat") {
                nextCells.emplace(cornerCell);
            }
        };
        if (ledge.startJoin ==
            route1_terrain_ledges::Join::Concave) {
            includeConcaveCrown({
                ledge.ownerCell.first - tangent[0],
                ledge.ownerCell.second - tangent[1]});
        }
        if (ledge.endJoin ==
            route1_terrain_ledges::Join::Concave) {
            includeConcaveCrown({
                ledge.ownerCell.first + tangent[0],
                ledge.ownerCell.second + tangent[1]});
        }
    }
    if (nextCells == terrainMaskCells &&
        nextCleanupCells == terrainCleanupCells &&
        nextSourceReferenceCells == terrainSourceReferenceCells &&
        nextInvalidatedSourceCleanupBoundaries ==
            terrainInvalidatedSourceCleanupBoundaries &&
        terrainMaskRevision != 0u) {
        return;
    }
    terrainMaskCells = std::move(nextCells);
    terrainCleanupCells = std::move(nextCleanupCells);
    terrainSourceReferenceCells =
        std::move(nextSourceReferenceCells);
    terrainInvalidatedSourceCleanupBoundaries =
        std::move(nextInvalidatedSourceCleanupBoundaries);
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
        mask.filteredVertices.clear();
        mask.filteredSourceVertices.clear();
        mask.filteredIndices.clear();
        mask.filteredIndices.reserve(mask.originalIndices.size());
        if (mask.cleanupOnly) {
            mask.filteredVertices.reserve(
                mask.originalIndices.size());
            if (mask.originalSourceVertices.size() ==
                mask.originalVertices.size()) {
                mask.filteredSourceVertices.reserve(
                    mask.originalIndices.size());
            }
        }
        const glm::mat4 model = glm::make_mat4(
            mask.sourceModelMatrix.data());
        const glm::mat4 inverseModel = glm::inverse(model);
        for (std::size_t index = 0u;
             index + 2u < mask.originalIndices.size();
             index += 3u) {
            const std::array<std::uint32_t, 3> triangle{
                mask.originalIndices[index],
                mask.originalIndices[index + 1u],
                mask.originalIndices[index + 2u]};
            bool valid = true;
            glm::vec3 centroid{};
            std::array<glm::vec3, 3> positions{};
            bool vertexTouchesMaskedCell = false;
            for (std::size_t corner = 0u;
                 corner < triangle.size();
                 ++corner) {
                const auto vertexIndex = triangle[corner];
                if (vertexIndex >= mask.originalVertices.size()) {
                    valid = false;
                    break;
                }
                const auto& vertex =
                    mask.originalVertices[vertexIndex];
                positions[corner] = glm::vec3(
                    model * glm::vec4(
                        vertex.x,
                        vertex.y,
                        vertex.z,
                        1.0f));
                centroid += positions[corner];
                if (mask.maskWhenAnyVertexTouchesCell) {
                    const auto vertexCell = std::pair{
                        static_cast<std::int32_t>(std::floor(
                            positions[corner].x /
                                kTerrainTileSizeCm)),
                        static_cast<std::int32_t>(std::floor(
                            positions[corner].z /
                                kTerrainTileSizeCm))};
                    vertexTouchesMaskedCell =
                        vertexTouchesMaskedCell ||
                        (maskedCells.contains(vertexCell) &&
                         route1TerrainMaskUsesAnyVertexOwnership(
                             terrainSourceReferenceCells.contains(
                                 vertexCell)));
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
            std::array<std::array<float, 3>, 3>
                positionValues{{
                    {positions[0].x,
                     positions[0].y,
                     positions[0].z},
                    {positions[1].x,
                     positions[1].y,
                     positions[1].z},
                    {positions[2].x,
                     positions[2].y,
                     positions[2].z}}};
            const bool replacedByDonorSpill =
                mask.cleanupOnly &&
                std::any_of(
                    donorOwnedSpillBoundaries.begin(),
                    donorOwnedSpillBoundaries.end(),
                    [&](const auto& boundary) {
                        const auto& [ownerCell, referenceCell] =
                            boundary;
                        const bool cleanupEligibleHeight =
                            geometry.sourceMeshIndex != 28u ||
                            route1TerrainCleanupCarrierAtOrBelowBoundaryCeiling(
                                positionValues,
                                static_cast<float>(std::max(
                                    cleanupCeilingLevel(ownerCell),
                                    cleanupCeilingLevel(referenceCell))) *
                                    kTerrainElevationStepCm);
                        return cell == ownerCell &&
                            cleanupEligibleHeight &&
                            (route1TerrainCleanupCarrierEntersNeighbor(
                                 positionValues,
                                 {ownerCell.first, ownerCell.second},
                                 {referenceCell.first,
                                  referenceCell.second}) ||
                             route1TerrainCleanupCarrierWithinBoundaryBand(
                                 positionValues,
                                 {referenceCell.first,
                                  referenceCell.second},
                                 {ownerCell.first,
                                  ownerCell.second}));
                    });
            const bool replacedByInvalidatedSourceBoundary =
                mask.cleanupOnly &&
                std::any_of(
                    terrainInvalidatedSourceCleanupBoundaries.begin(),
                    terrainInvalidatedSourceCleanupBoundaries.end(),
                    [&](const auto& boundary) {
                        const auto& [ownerCell, editedCell] =
                            boundary;
                        const std::int32_t boundaryCeilingLevel =
                            std::max(
                                cleanupCeilingLevel(ownerCell),
                                cleanupCeilingLevel(editedCell));
                        // Mesh 28 is a compound two-storey source carrier. Its
                        // lower sheet intersects the newly rebuilt L0/L1 wall,
                        // but several of those broad triangles also reach the
                        // independent L1/L2 cliff above. Retire only the part
                        // at or below this boundary's highest current/source
                        // profile; otherwise editing the lower shelf erases
                        // the complete upper wall and exposes the dark lawn
                        // behind it.
                        const bool cleanupEligibleHeight =
                            geometry.sourceMeshIndex != 28u ||
                            route1TerrainCleanupCarrierAtOrBelowBoundaryCeiling(
                                positionValues,
                                static_cast<float>(boundaryCeilingLevel) *
                                    kTerrainElevationStepCm);
                        const bool rebuiltCorridorCarrier =
                            route1TerrainCleanupCarrierAtOrBelowBoundaryCeiling(
                                positionValues,
                                static_cast<float>(boundaryCeilingLevel) *
                                    kTerrainElevationStepCm) &&
                            route1TerrainCleanupCarrierWithinRebuiltBoundaryCorridor(
                                positionValues,
                                {ownerCell.first, ownerCell.second},
                                {editedCell.first, editedCell.second});
                        return cell == ownerCell &&
                            cleanupEligibleHeight &&
                            (route1TerrainCleanupCarrierEntersNeighbor(
                                 positionValues,
                                 {ownerCell.first, ownerCell.second},
                                 {editedCell.first,
                                  editedCell.second}) ||
                             route1TerrainCleanupCarrierWithinBoundaryBand(
                                 positionValues,
                                 {editedCell.first,
                                  editedCell.second},
                                  {ownerCell.first,
                                   ownerCell.second}) ||
                             rebuiltCorridorCarrier ||
                             (mask.retireWhenIntersectingRebuiltBoundary &&
                              route1TerrainCleanupCarrierIntersectsBoundaryBand(
                                  positionValues,
                                  {editedCell.first,
                                   editedCell.second},
                                  {ownerCell.first,
                                   ownerCell.second})));
                    });
            if (replacedByDonorSpill ||
                replacedByInvalidatedSourceBoundary) {
                continue;
            }
            std::int32_t directMaskCeilingLevel =
                std::numeric_limits<std::int32_t>::lowest();
            if (maskedCells.contains(cell)) {
                directMaskCeilingLevel = std::max(
                    directMaskCeilingLevel,
                    cleanupCeilingLevel(cell));
            }
            if (vertexTouchesMaskedCell) {
                for (const auto& position : positions) {
                    const auto vertexCell = std::pair{
                        static_cast<std::int32_t>(std::floor(
                            position.x / kTerrainTileSizeCm)),
                        static_cast<std::int32_t>(std::floor(
                            position.z / kTerrainTileSizeCm))};
                    if (maskedCells.contains(vertexCell) &&
                        route1TerrainMaskUsesAnyVertexOwnership(
                            terrainSourceReferenceCells.contains(
                                vertexCell))) {
                        directMaskCeilingLevel = std::max(
                            directMaskCeilingLevel,
                            cleanupCeilingLevel(vertexCell));
                    }
                }
            }
            const bool directMaskEligibleHeight =
                geometry.sourceMeshIndex != 28u ||
                directMaskCeilingLevel ==
                    std::numeric_limits<std::int32_t>::lowest() ||
                route1TerrainCleanupCarrierAtOrBelowBoundaryCeiling(
                    positionValues,
                    static_cast<float>(directMaskCeilingLevel) *
                        kTerrainElevationStepCm);
            // Apply the same storey guard to ordinary cell ownership. Without
            // it, the boundary cleanup above preserves mesh 28's upper wall
            // while this generic path still punches out its centre section.
            if ((vertexTouchesMaskedCell ||
                 maskedCells.contains(cell)) &&
                directMaskEligibleHeight) {
                continue;
            }
            if (!mask.cleanupOnly) {
                mask.filteredIndices.insert(
                    mask.filteredIndices.end(),
                    triangle.begin(),
                    triangle.end());
                continue;
            }
            for (const auto& [ownerCell, referenceCell] :
                 planeClippedCleanupBoundaries) {
                if (cell != ownerCell) {
                    continue;
                }
                route1TerrainClampCleanupCarrierToOwnedCell(
                    positionValues,
                    {ownerCell.first, ownerCell.second},
                    {referenceCell.first, referenceCell.second});
            }
            for (std::size_t corner = 0u;
                 corner < triangle.size();
                 ++corner) {
                auto vertex =
                    mask.originalVertices[triangle[corner]];
                const glm::vec3 localPosition = glm::vec3(
                    inverseModel * glm::vec4(
                        positionValues[corner][0],
                        positionValues[corner][1],
                        positionValues[corner][2],
                        1.0f));
                vertex.x = localPosition.x;
                vertex.y = localPosition.y;
                vertex.z = localPosition.z;
                mask.filteredVertices.push_back(vertex);
                if (mask.originalSourceVertices.size() ==
                    mask.originalVertices.size()) {
                    mask.filteredSourceVertices.push_back(
                        mask.originalSourceVertices[
                            triangle[corner]]);
                }
                mask.filteredIndices.push_back(
                    static_cast<std::uint32_t>(
                        mask.filteredVertices.size() - 1u));
            }
        }
        if (mask.cleanupOnly) {
            geometry.vertices = mask.filteredVertices.data();
            geometry.vertexCount = mask.filteredVertices.size();
            geometry.sourceVertices =
                mask.filteredSourceVertices.empty()
                ? nullptr
                : mask.filteredSourceVertices.data();
            geometry.sourceVertexCount =
                mask.filteredSourceVertices.size();
            geometry.sourceVertexSemanticMask =
                mask.filteredSourceVertices.empty()
                ? IRenderBackend::
                      WorldSceneSourceVertexSemanticNone
                : mask.originalSourceVertexSemanticMask;
        } else {
            geometry.vertices = mask.originalVertices.data();
            geometry.vertexCount = mask.originalVertices.size();
            geometry.sourceVertices =
                mask.originalSourceVertices.empty()
                ? nullptr
                : mask.originalSourceVertices.data();
            geometry.sourceVertexCount =
                mask.originalSourceVertices.size();
            geometry.sourceVertexSemanticMask =
                mask.originalSourceVertices.empty()
                ? IRenderBackend::
                      WorldSceneSourceVertexSemanticNone
                : mask.originalSourceVertexSemanticMask;
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
        const TerrainTileState* referencedSource = nullptr;
        if (authored.sourceReference) {
            const auto found = std::find_if(
                sourceTerrainTiles.begin(),
                sourceTerrainTiles.end(),
                [&](const TerrainTileState& candidate) {
                    return candidate.gridX ==
                            (*authored.sourceReference)[0] &&
                        candidate.gridZ ==
                            (*authored.sourceReference)[1];
                });
            if (found != sourceTerrainTiles.end()) {
                referencedSource = &*found;
            }
        }
        // A source_reference is an exact geometry transplant. Its logical
        // elevation/surface/shape must therefore come from the same donor as
        // its rendered carriers; stale authored labels would make boundary
        // ownership disagree with the mesh that is actually on screen.
        tile->elevationLevel = referencedSource
            ? referencedSource->elevationLevel
            : authored.elevationLevel;
        tile->surface = referencedSource
            ? referencedSource->surface
            : authored.surface;
        tile->shape = referencedSource
            ? referencedSource->shape
            : authored.shape;
        tile->visualVariant = authored.visualVariant;
        tile->sourceReference = authored.sourceReference;
        tile->receivesProjectedShadow =
            authored.receivesProjectedShadow;
        tile->normalizeSourceTint =
            authored.normalizeSourceTint;
        tile->suppressOverlappingVegetation =
            authored.suppressOverlappingVegetation;
        tile->reason = authored.reason;
        tile->authored = true;
    }

    // Encounter-grass Color0 paint extends through the core cells and the
    // manifest's complete eight-neighbor fringe. When a source patch is
    // deliberately suppressed,
    // retain the canonical lawn geometry/UVs but remove that now-orphaned
    // blue-green paint. The fringe is derived from the collision footprint,
    // not from a board-specific rectangle; for source record 3 this includes
    // both exposed east-side corner cells rather than stopping at z=-15/-17.
    std::set<GridCell> suppressedEncounterTintCells;
    const auto footprintOffsets =
        route1EncounterGrassTintFootprintOffsets();
    const std::size_t canonicalRecordCount = std::min(
        canonicalEncounterGrassRecordCount,
        encounterGrassRecords.size());
    for (std::size_t recordIndex = 0u;
         recordIndex < canonicalRecordCount;
         ++recordIndex) {
        const auto& record = encounterGrassRecords[recordIndex];
        if (!record.suppressed) {
            continue;
        }
        for (const auto& coreCell : record.sourceCoreTerrainCells) {
            for (const auto& offset : footprintOffsets) {
                suppressedEncounterTintCells.emplace(
                    coreCell.first + offset[0],
                    coreCell.second + offset[1]);
            }
        }
    }
    for (auto& tile : terrainTiles) {
        if (tile.sourceOccupied &&
            tile.surface == "light_lawn" &&
            (tile.normalizeSourceTint ||
             (!tile.authored &&
              suppressedEncounterTintCells.contains(
                  {tile.gridX, tile.gridZ})))) {
            tile.cleanSuppressedEncounterGrassTint = true;
        }
    }
    terrainSeamResolution =
        route1_terrain_seams::resolve(terrainTiles);
    terrainPatchV2Plan =
        route1_terrain_patch_v2::cook(terrainTiles);
    for (auto& tile : terrainTiles) {
        tile.terrainPatchV2RegionId = 0u;
        tile.terrainPatchV2Core = false;
        tile.terrainPatchV2CoreBoundaryMask = 0u;
        tile.terrainPatchV2SourceBoundaryMask = 0u;
    }
    const auto findMutableTile = [&](const GridCell& cell)
        -> TerrainTileState* {
        const auto found = std::find_if(
            terrainTiles.begin(),
            terrainTiles.end(),
            [&](const TerrainTileState& candidate) {
                return candidate.gridX == cell.first &&
                    candidate.gridZ == cell.second;
            });
        return found == terrainTiles.end() ? nullptr : &*found;
    };
    for (const auto& region : terrainPatchV2Plan.regions) {
        for (const auto& patchCell : region.cells) {
            auto* tile = findMutableTile(patchCell.cell);
            if (!tile) {
                continue;
            }
            tile->terrainPatchV2RegionId = region.id;
            tile->terrainPatchV2Core = patchCell.role ==
                route1_terrain_patch_v2::CellRole::Core;
            // The transition ring is source terrain, not an enlarged edit.
            // Keep its decoded UV/color fields authoritative so the outer
            // edge of a regional patch cannot merely move a square material
            // delimiter one cell farther into the location. Only the core
            // may synthesize continuous fields; transition cells retain the
            // result chosen by Route1TerrainSeamResolver above.
            if (terrainPatchV2PreviewEnabled &&
                patchCell.role ==
                    route1_terrain_patch_v2::CellRole::Core) {
                tile->rebuildContinuousMaterialFields = true;
            }
        }
        for (const auto& boundary : region.boundaries) {
            for (const auto& edge : boundary.edges) {
                auto* tile = findMutableTile(edge.ownerCell);
                if (!tile || edge.edge >= 4u) {
                    continue;
                }
                auto& mask = boundary.kind ==
                        route1_terrain_patch_v2::BoundaryKind::CoreToTransition
                    ? tile->terrainPatchV2CoreBoundaryMask
                    : tile->terrainPatchV2SourceBoundaryMask;
                mask |= static_cast<std::uint8_t>(1u << edge.edge);
            }
        }
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
    const auto cleanupCell =
        [&](std::int32_t gridX,
            std::int32_t gridZ) {
            return terrainCleanupCells.contains({gridX, gridZ});
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

    for (const bool receivesProjectedShadow : {true, false}) {
        const auto authoredSurface =
            ensureAuthoredTerrainSurfaceObject(
                receivesProjectedShadow);
        if (authoredSurface.id != 0u) {
            append(
                authoredSurface,
                sourcePlacementMatrix(
                    {0.0f, 0.0f, 0.0f},
                    {0.0f, 0.0f, 0.0f},
                    {1.0f, 1.0f, 1.0f}));
        }

        std::set<GridCell> exactSourceSurfaceCells;
        for (const auto& tile : terrainTiles) {
            const GridCell cell{tile.gridX, tile.gridZ};
            if (tile.receivesProjectedShadow ==
                    receivesProjectedShadow &&
                terrainMaskCells.contains(cell) &&
                route1TerrainUsesExactSourceSurfaceOverride(
                    tile, terrainTiles, sourceTerrainTiles)) {
                exactSourceSurfaceCells.emplace(cell);
            }
        }
        for (const auto object :
             ensureTerrainExactSourceSurfaceObjects(
                 exactSourceSurfaceCells,
                 receivesProjectedShadow)) {
            append(
                object,
                sourcePlacementMatrix(
                    {0.0f, 0.0f, 0.0f},
                    {0.0f, 0.0f, 0.0f},
                    {1.0f, 1.0f, 1.0f}));
        }
    }

    // Source-reference cells that share a translation are one connected
    // transplant. Union their donor cells before clipping so triangles that
    // touch an internal cell boundary are retained exactly once instead of
    // being cloned independently by both neighboring tiles.
    std::map<GridCell, std::set<GridCell>> sourceReferencePatches;
    for (const auto& tile : terrainTiles) {
        if (tile.surface == "empty" || !tile.sourceReference) {
            continue;
        }
        const GridCell translation{
            tile.gridX - (*tile.sourceReference)[0],
            tile.gridZ - (*tile.sourceReference)[1]};
        sourceReferencePatches[translation].emplace(
            (*tile.sourceReference)[0],
            (*tile.sourceReference)[1]);
    }
    for (const auto& [translation, sourceCells] :
         sourceReferencePatches) {
        std::set<GridCell> blockedSpillCells;
        std::vector<std::pair<GridCell, GridCell>>
            requiredSpillBoundaries;
        for (const auto& sourceCell : sourceCells) {
            const auto* targetTile = findTile(
                sourceCell.first + translation.first,
                sourceCell.second + translation.second);
            if (!targetTile || !hasSurface(*targetTile)) {
                continue;
            }
            for (std::size_t edge = 0u;
                 edge < directions.size();
                 ++edge) {
                const auto direction = directions[edge];
                const GridCell sourceNeighbor{
                    sourceCell.first + direction[0],
                    sourceCell.second + direction[1]};
                if (sourceCells.contains(sourceNeighbor)) {
                    continue;
                }
                const auto* targetNeighbor = findTile(
                    targetTile->gridX + direction[0],
                    targetTile->gridZ + direction[1]);
                if (!targetNeighbor || !hasSurface(*targetNeighbor)) {
                    continue;
                }
                if (!route1TerrainSourcePatchNeedsBoundarySpill(
                        *targetTile, targetNeighbor, edge)) {
                    blockedSpillCells.emplace(sourceNeighbor);
                } else {
                    requiredSpillBoundaries.emplace_back(
                        sourceCell, sourceNeighbor);
                }
            }
        }
        const float deltaX = static_cast<float>(translation.first) *
            kTerrainTileSizeCm;
        const float deltaZ = static_cast<float>(translation.second) *
            kTerrainTileSizeCm;
        for (const auto object :
             ensureTerrainSourceReferenceObjects(
                 sourceCells,
                 blockedSpillCells,
                 requiredSpillBoundaries)) {
            append(
                object,
                sourcePlacementMatrix(
                    {deltaX, 0.0f, deltaZ},
                    {0.0f, 0.0f, 0.0f},
                    {1.0f, 1.0f, 1.0f}));
        }
    }

    std::vector<bool> regionalCrownContourSubmitted(
        terrainLedgeResolution.contourCount, false);
    std::vector<bool> regionalCliffContourSubmitted(
        terrainLedgeResolution.contourCount, false);
    std::vector<bool> regionalFringeContourSubmitted(
        terrainLedgeResolution.contourCount, false);
    if (terrainPatchV2PreviewEnabled) {
        for (std::uint32_t contourIndex = 0u;
             contourIndex < terrainLedgeResolution.contourCount;
             ++contourIndex) {
            const auto crownObject =
                ensureTerrainRegionalCrownContourUnderlayObject(
                    contourIndex);
            const auto cliffObject =
                ensureTerrainRegionalCliffContourObject(
                    contourIndex);
            const auto fringeObject =
                ensureTerrainRegionalFringeContourObject(
                    contourIndex);
            if (!crownObject && !cliffObject && !fringeObject) {
                continue;
            }
            const auto owner = std::find_if(
                terrainLedgeResolution.edges.begin(),
                terrainLedgeResolution.edges.end(),
                [&](const auto& edge) {
                    return edge.contourIndex == contourIndex;
                });
            if (owner == terrainLedgeResolution.edges.end()) {
                continue;
            }
            const auto placement = sourcePlacementMatrix(
                {0.0f,
                 static_cast<float>(owner->profile.tileLevels[0u]) *
                     kTerrainElevationStepCm,
                 0.0f},
                {0.0f, 0.0f, 0.0f},
                {1.0f, 1.0f, 1.0f});
            if (crownObject) {
                append(crownObject, placement);
                regionalCrownContourSubmitted[contourIndex] = true;
            }
            if (cliffObject) {
                append(cliffObject, placement);
                regionalCliffContourSubmitted[contourIndex] = true;
            }
            if (fringeObject) {
                append(fringeObject, placement);
                regionalFringeContourSubmitted[contourIndex] = true;
            }
        }
    }

    for (const auto& tile : terrainTiles) {
        if (tile.surface == "empty") {
            continue;
        }
        if (tile.sourceReference) {
            continue;
        }
        const bool affected = tile.authored ||
            terrainMaskCells.contains({tile.gridX, tile.gridZ}) ||
            cleanupCell(tile.gridX, tile.gridZ) ||
            std::any_of(
                directions.begin(),
                directions.end(),
                [&](const auto& direction) {
                    const auto* neighbor = findTile(
                        tile.gridX + direction[0],
                        tile.gridZ + direction[1]);
                    return (neighbor && neighbor->authored) ||
                        cleanupCell(
                            tile.gridX + direction[0],
                            tile.gridZ + direction[1]);
                });
        if (!affected) {
            continue;
        }
        const std::array<float, 3> center{
            (static_cast<float>(tile.gridX) + 0.5f) *
                kTerrainTileSizeCm,
            static_cast<float>(tile.elevationLevel) *
                    kTerrainElevationStepCm +
                kTerrainTileTopDepthBiasCm,
            (static_cast<float>(tile.gridZ) + 0.5f) *
                kTerrainTileSizeCm};
        std::array<std::array<std::int32_t, 2>, 4>
            edgeDifferences{};
        std::array<std::array<std::int32_t, 2>, 4>
            edgeNeighborLevels{};
        std::array<bool, 4> edgeRebuilt{};
        const bool tileUsesGeneratedSourceCap =
            terrainMaskCells.contains({tile.gridX, tile.gridZ}) &&
            !tile.sourceReference;
        for (std::size_t edge = 0u;
             edge < directions.size();
             ++edge) {
            const auto direction = directions[edge];
            const auto* neighbor = findTile(
                tile.gridX + direction[0],
                tile.gridZ + direction[1]);
            const bool neighborUsesGeneratedSourceCap =
                neighbor &&
                terrainMaskCells.contains(
                    {neighbor->gridX, neighbor->gridZ}) &&
                !neighbor->sourceReference;
            const bool authoredSideOwnsHandoff =
                neighborUsesGeneratedSourceCap &&
                route1TerrainNeedsSourceSeamOverlap(
                    tile, neighbor, edge, false);
            const bool promotedGeneratedSideOwnsHandoff =
                tileUsesGeneratedSourceCap &&
                !neighborUsesGeneratedSourceCap &&
                route1TerrainNeedsSourceSeamOverlap(
                    tile,
                    neighbor,
                    edge,
                    tileUsesGeneratedSourceCap);
            if (!terrainPatchV2PreviewEnabled &&
                (authoredSideOwnsHandoff ||
                 promotedGeneratedSideOwnsHandoff)) {
                append(
                    ensureTerrainSourceHandoffUnderlayObject(
                        tile, *neighbor, edge),
                    sourcePlacementMatrix(
                        {center[0],
                         static_cast<float>(tile.elevationLevel) *
                             kTerrainElevationStepCm,
                         center[2]},
                        {0.0f, 0.0f, 0.0f},
                        {1.0f, 1.0f, 1.0f}));
            }
            if (neighbor && neighbor->sourceReference) {
                // The canonical donor already carries the complete shared
                // boundary. Do not layer a synthesized cliff on top of it.
                continue;
            }
            const auto edgeProfile = route1TerrainSharedEdgeProfile(
                tile,
                neighbor && hasSurface(*neighbor) ? neighbor : nullptr,
                edge);
            for (std::size_t endpoint = 0u;
                 endpoint < 2u;
                 ++endpoint) {
                edgeNeighborLevels[edge][endpoint] =
                    edgeProfile.neighborLevels[endpoint];
                edgeDifferences[edge][endpoint] =
                    edgeProfile.tileLevels[endpoint] -
                    edgeProfile.neighborLevels[endpoint];
            }
            if (edgeDifferences[edge][0] <= 0 &&
                edgeDifferences[edge][1] <= 0) {
                continue;
            }
            const auto* resolvedLedge =
                route1_terrain_ledges::find(
                    terrainLedgeResolution,
                    {tile.gridX, tile.gridZ},
                    edge);
            if (!resolvedLedge) {
                continue;
            }
            const float contourStartCm =
                resolvedLedge->contourStartCm;
            const float materialContourStartCm =
                resolvedLedge->materialContourStartCm;
            const auto startJoin = resolvedLedge->startJoin;
            const auto endJoin = resolvedLedge->endJoin;
            edgeRebuilt[edge] = true;
            if (tile.shape == "flat" &&
                edgeDifferences[edge][0] > 0 &&
                edgeDifferences[edge][1] > 0) {
                const bool regionalOwner =
                    resolvedLedge->contourIndex <
                        regionalCrownContourSubmitted.size() &&
                    regionalCrownContourSubmitted[
                        resolvedLedge->contourIndex];
                if (!regionalOwner) {
                    append(
                        terrainPatchV2PreviewEnabled
                            ? ensureTerrainLedgeCrownContourUnderlayObject(
                                  tile, edge)
                            : ensureTerrainLedgeCrownUnderlayObject(
                                  tile, edge),
                        sourcePlacementMatrix(
                            {center[0],
                             static_cast<float>(tile.elevationLevel) *
                                 kTerrainElevationStepCm,
                             center[2]},
                            {0.0f, 0.0f, 0.0f},
                            {1.0f, 1.0f, 1.0f}));
                }
            }
            const float halfSize =
                kTerrainTileSizeCm * 0.5f;
            const std::int32_t cliffAnchorLevel = std::min(
                edgeProfile.neighborLevels[0],
                edgeProfile.neighborLevels[1]);
            const std::int32_t fringeAnchorLevel = std::min(
                edgeProfile.tileLevels[0],
                edgeProfile.tileLevels[1]);
            const std::array<float, 3> sideCenter{
                center[0] +
                    static_cast<float>(direction[0]) * halfSize,
                static_cast<float>(cliffAnchorLevel) *
                    kTerrainElevationStepCm,
                center[2] +
                    static_cast<float>(direction[1]) * halfSize};
            const bool regionalCliffOwner =
                resolvedLedge->contourIndex <
                    regionalCliffContourSubmitted.size() &&
                regionalCliffContourSubmitted[
                    resolvedLedge->contourIndex];
            if (!regionalCliffOwner) {
                append(
                    ensureTerrainCliffObject(
                        tile,
                        edge,
                        edgeProfile,
                        contourStartCm,
                        materialContourStartCm,
                        startJoin,
                        endJoin),
                    sourcePlacementMatrix(
                        sideCenter,
                        {0.0f, rotations[edge], 0.0f},
                        {1.0f, 1.0f, 1.0f}));
            }
            const bool regionalFringeOwner =
                resolvedLedge->contourIndex <
                    regionalFringeContourSubmitted.size() &&
                regionalFringeContourSubmitted[
                    resolvedLedge->contourIndex];
            if (!regionalFringeOwner) {
                append(
                    ensureTerrainFringeObject(
                        tile,
                        edge,
                        edgeProfile,
                        contourStartCm,
                        materialContourStartCm,
                        startJoin,
                        endJoin),
                    sourcePlacementMatrix(
                        {sideCenter[0],
                         static_cast<float>(fringeAnchorLevel) *
                             kTerrainElevationStepCm,
                         sideCenter[2]},
                        {0.0f, rotations[edge], 0.0f},
                        {1.0f, 1.0f, 1.0f}));
            }
        }
        if (tile.shape == "flat") {
            constexpr std::array<std::array<std::size_t, 2>, 4>
                cornerEdges{{
                    {0u, 1u},
                    {1u, 2u},
                    {2u, 3u},
                    {3u, 0u},
                }};
            constexpr std::array<std::array<std::int32_t, 2>, 4>
                cornerOffsets{{
                    {1, 1},
                    {1, 0},
                    {0, 0},
                    {0, 1},
                }};
            for (std::size_t corner = 0u;
                 corner < cornerEdges.size();
                 ++corner) {
                const auto firstEdge = cornerEdges[corner][0];
                const auto secondEdge = cornerEdges[corner][1];
                const std::int32_t firstDifference =
                    edgeDifferences[firstEdge][1];
                const std::int32_t secondDifference =
                    edgeDifferences[secondEdge][0];
                const std::int32_t firstNeighborLevel =
                    edgeNeighborLevels[firstEdge][1];
                const std::int32_t secondNeighborLevel =
                    edgeNeighborLevels[secondEdge][0];
                if (!(edgeRebuilt[firstEdge] ||
                      edgeRebuilt[secondEdge]) ||
                    firstDifference <= 0 ||
                    firstDifference != secondDifference ||
                    firstNeighborLevel != secondNeighborLevel) {
                    continue;
                }
                const auto* firstResolved =
                    route1_terrain_ledges::find(
                        terrainLedgeResolution,
                        {tile.gridX, tile.gridZ},
                        firstEdge);
                const auto* secondResolved =
                    route1_terrain_ledges::find(
                        terrainLedgeResolution,
                        {tile.gridX, tile.gridZ},
                        secondEdge);
                if (!route1_terrain_ledges::formsConvexCorner(
                        firstResolved, secondResolved)) {
                    continue;
                }
                const float cornerMaterialContourCm =
                    firstResolved->materialContourStartCm +
                    route1_terrain_ledges::materialStraightLengthCm(
                        firstResolved->startJoin,
                        firstResolved->endJoin);
                const bool regionalCliffOwner =
                    firstResolved->contourIndex <
                        regionalCliffContourSubmitted.size() &&
                    regionalCliffContourSubmitted[
                        firstResolved->contourIndex];
                if (!regionalCliffOwner) {
                    append(
                        ensureTerrainCliffCornerObject(
                            tile,
                            corner,
                            firstDifference,
                            cornerMaterialContourCm),
                        sourcePlacementMatrix(
                            {center[0],
                             static_cast<float>(
                                 firstNeighborLevel) *
                                 kTerrainElevationStepCm,
                             center[2]},
                            {0.0f, 0.0f, 0.0f},
                            {1.0f, 1.0f, 1.0f}));
                }
                const bool regionalFringeOwner =
                    firstResolved->contourIndex <
                        regionalFringeContourSubmitted.size() &&
                    regionalFringeContourSubmitted[
                        firstResolved->contourIndex];
                if (!regionalFringeOwner) {
                    append(
                        ensureTerrainFringeCornerObject(
                            tile,
                            corner,
                            firstDifference,
                            cornerMaterialContourCm),
                        sourcePlacementMatrix(
                            {center[0],
                             static_cast<float>(
                                 firstNeighborLevel) *
                                 kTerrainElevationStepCm,
                             center[2]},
                            {0.0f, 0.0f, 0.0f},
                            {1.0f, 1.0f, 1.0f}));
                }
                const bool regionalCrownOwner =
                    firstResolved->contourIndex <
                        regionalCrownContourSubmitted.size() &&
                    regionalCrownContourSubmitted[
                        firstResolved->contourIndex];
                if (terrainPatchV2PreviewEnabled &&
                    !regionalCrownOwner) {
                    append(
                        ensureTerrainConvexCrownContourUnderlayObject(
                            tile, corner),
                        sourcePlacementMatrix(
                            {center[0],
                             static_cast<float>(tile.elevationLevel) *
                                 kTerrainElevationStepCm,
                             center[2]},
                            {0.0f, 0.0f, 0.0f},
                            {1.0f, 1.0f, 1.0f}));
                }
                const auto firstDirection = directions[firstEdge];
                const auto secondDirection = directions[secondEdge];
                const auto* firstLowTile = findTile(
                    tile.gridX + firstDirection[0],
                    tile.gridZ + firstDirection[1]);
                const auto* secondLowTile = findTile(
                    tile.gridX + secondDirection[0],
                    tile.gridZ + secondDirection[1]);
                const auto* diagonalLowTile = findTile(
                    tile.gridX + firstDirection[0] +
                        secondDirection[0],
                    tile.gridZ + firstDirection[1] +
                        secondDirection[1]);
                const auto validLowDonor = [&](
                        const TerrainTileState* candidate) {
                    return candidate && hasSurface(*candidate) &&
                        candidate->elevationLevel == firstNeighborLevel;
                };
                const TerrainTileState* contourFootDonor =
                    validLowDonor(diagonalLowTile)
                    ? diagonalLowTile
                    : (validLowDonor(firstLowTile)
                        ? firstLowTile
                        : (validLowDonor(secondLowTile)
                            ? secondLowTile
                            : nullptr));
                if (terrainPatchV2PreviewEnabled) {
                    append(
                        ensureTerrainConvexFootContourUnderlayObject(
                            tile,
                            contourFootDonor,
                            corner,
                            firstNeighborLevel),
                        sourcePlacementMatrix(
                            {center[0],
                             static_cast<float>(firstNeighborLevel) *
                                 kTerrainElevationStepCm,
                             center[2]},
                            {0.0f, 0.0f, 0.0f},
                            {1.0f, 1.0f, 1.0f}));
                }
                const float sourceCornerX = static_cast<float>(
                    tile.gridX + cornerOffsets[corner][0]) *
                    kTerrainTileSizeCm;
                const float sourceCornerZ = static_cast<float>(
                    tile.gridZ + cornerOffsets[corner][1]) *
                    kTerrainTileSizeCm;
                const auto appendLowCornerSector = [&](
                        const TerrainTileState* donor) {
                    if (!donor || !hasSurface(*donor) ||
                        donor->elevationLevel != firstNeighborLevel) {
                        return;
                    }
                    const float quadrantSignX =
                        donor->gridX >=
                            tile.gridX + cornerOffsets[corner][0]
                        ? 1.0f
                        : -1.0f;
                    const float quadrantSignZ =
                        donor->gridZ >=
                            tile.gridZ + cornerOffsets[corner][1]
                        ? 1.0f
                        : -1.0f;
                    append(
                        ensureTerrainConvexLawnCornerUnderlayObject(
                            *donor,
                            sourceCornerX,
                            sourceCornerZ,
                            quadrantSignX,
                            quadrantSignZ),
                        sourcePlacementMatrix(
                            {sourceCornerX,
                             static_cast<float>(firstNeighborLevel) *
                                 kTerrainElevationStepCm,
                             sourceCornerZ},
                            {0.0f, 0.0f, 0.0f},
                            {1.0f, 1.0f, 1.0f}));
                };
                if (!terrainPatchV2PreviewEnabled) {
                    appendLowCornerSector(firstLowTile);
                    appendLowCornerSector(secondLowTile);
                    appendLowCornerSector(findTile(
                        tile.gridX + firstDirection[0] +
                            secondDirection[0],
                        tile.gridZ + firstDirection[1] +
                            secondDirection[1]));
                }

                // The rounded cliff foot recedes into the high tile's square
                // footprint. That leaves one low-elevation quarter pocket on
                // the high-cell side of the logical corner; the three outer
                // donor sectors above do not own it. Split this last quadrant
                // along its bisector so the first and second low neighbors
                // extend their own material fields to the wall without either
                // neighbor painting the complete turn.
                const float interiorQuadrantSignX =
                    cornerOffsets[corner][0] == 0 ? 1.0f : -1.0f;
                const float interiorQuadrantSignZ =
                    cornerOffsets[corner][1] == 0 ? 1.0f : -1.0f;
                const auto appendLowCornerPocketHalf = [&](
                        const TerrainTileState* donor,
                        std::size_t half) {
                    if (!donor || !hasSurface(*donor) ||
                        donor->elevationLevel != firstNeighborLevel) {
                        return;
                    }
                    append(
                        ensureTerrainConvexLawnCornerPocketRepairObject(
                            *donor,
                            sourceCornerX,
                            sourceCornerZ,
                            interiorQuadrantSignX,
                            interiorQuadrantSignZ,
                            half),
                        sourcePlacementMatrix(
                            {sourceCornerX,
                             static_cast<float>(firstNeighborLevel) *
                                 kTerrainElevationStepCm,
                             sourceCornerZ},
                            {0.0f, 0.0f, 0.0f},
                            {1.0f, 1.0f, 1.0f}));
                };
                if (!terrainPatchV2PreviewEnabled) {
                    appendLowCornerPocketHalf(firstLowTile, 0u);
                    appendLowCornerPocketHalf(secondLowTile, 1u);
                }

                // A convex junction belongs to three independently authored
                // low tiles. Split its hidden ground carrier into their real
                // quadrants so texture fields and projected-shadow policy do
                // not leak across the logical corner. The high carrier uses
                // the same rounded contour as the crown instead of a disk,
                // keeping every safety pixel inside the raised silhouette.
                if (!terrainPatchV2PreviewEnabled) {
                    append(
                        ensureTerrainConvexLawnCapUnderlayObject(
                            tile,
                            corner),
                        sourcePlacementMatrix(
                            {center[0],
                             static_cast<float>(tile.elevationLevel) *
                                 kTerrainElevationStepCm,
                             center[2]},
                            {0.0f, 0.0f, 0.0f},
                            {1.0f, 1.0f, 1.0f}));
                }
            }
        }
    }

    // Concave turns belong to two different high-side cells, unlike convex
    // corners whose two edges share one owner tile. Emit exactly one joined
    // handoff from the selected incoming contour edge. The ordinary strips stop
    // at the logical grid vertex; this connector is therefore the sole wall
    // and fringe carrier through the inside turn rather than a third layer on
    // top of two intersecting planes.
    constexpr std::array<std::array<std::int32_t, 2>, 4>
        endOffsets{{
            {1, 1},
            {1, 0},
            {0, 0},
            {0, 1},
        }};
    for (const auto& incoming : terrainLedgeResolution.edges) {
        const auto* outgoing =
            route1_terrain_ledges::findConcaveSuccessor(
                terrainLedgeResolution, incoming);
        if (!outgoing || incoming.edge >= endOffsets.size()) {
            continue;
        }
        const float cornerX = static_cast<float>(
            incoming.ownerCell.first +
                endOffsets[incoming.edge][0]) *
            kTerrainTileSizeCm;
        const float cornerZ = static_cast<float>(
            incoming.ownerCell.second +
                endOffsets[incoming.edge][1]) *
            kTerrainTileSizeCm;
        append(
            ensureTerrainConcaveCliffCornerObject(
                incoming, *outgoing),
            sourcePlacementMatrix(
                {cornerX,
                 static_cast<float>(
                     incoming.profile.neighborLevels[1u]) *
                     kTerrainElevationStepCm,
                 cornerZ},
                {0.0f, 0.0f, 0.0f},
                {1.0f, 1.0f, 1.0f}));
        append(
            ensureTerrainConcaveFringeCornerObject(
                incoming, *outgoing),
            sourcePlacementMatrix(
                {cornerX,
                 static_cast<float>(
                     incoming.profile.tileLevels[1u]) *
                     kTerrainElevationStepCm,
                 cornerZ},
                {0.0f, 0.0f, 0.0f},
                {1.0f, 1.0f, 1.0f}));
        append(
            ensureTerrainConcaveCrownObject(
                incoming, *outgoing),
            sourcePlacementMatrix(
                {cornerX,
                 static_cast<float>(
                     incoming.profile.tileLevels[1u]) *
                     kTerrainElevationStepCm,
                 cornerZ},
                {0.0f, 0.0f, 0.0f},
                {1.0f, 1.0f, 1.0f}));
        const auto incomingLowDirection = directions[incoming.edge];
        const auto outgoingLowDirection = directions[outgoing->edge];
        const auto* incomingLowTile = findTile(
            incoming.ownerCell.first + incomingLowDirection[0],
            incoming.ownerCell.second + incomingLowDirection[1]);
        const auto* outgoingLowTile = findTile(
            outgoing->ownerCell.first + outgoingLowDirection[0],
            outgoing->ownerCell.second + outgoingLowDirection[1]);
        const bool underlayReceivesProjectedShadow =
            (!incomingLowTile ||
             incomingLowTile->receivesProjectedShadow) &&
            (!outgoingLowTile ||
             outgoingLowTile->receivesProjectedShadow);
        const std::int32_t lowLevel =
            incoming.profile.neighborLevels[1u];
        if (!terrainPatchV2PreviewEnabled) {
            append(
                ensureTerrainLawnCornerUnderlayObject(
                    cornerX,
                    cornerZ,
                    lowLevel,
                    underlayReceivesProjectedShadow),
                sourcePlacementMatrix(
                    {cornerX,
                     static_cast<float>(lowLevel) *
                         kTerrainElevationStepCm,
                     cornerZ},
                    {0.0f, 0.0f, 0.0f},
                    {1.0f, 1.0f, 1.0f}));
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
        const std::string kind = root.at("kind").get<std::string>();
        if ((schemaVersion != 1 &&
             schemaVersion != 2 &&
             schemaVersion != 3 &&
             schemaVersion != 4 &&
             schemaVersion != 5 &&
             schemaVersion != 6) ||
            (kind != "route1_environment_board_layout" &&
             kind != "lgpe_route1_board_layout_delta")) {
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
        const auto registration =
            root.find("board_registration");
        if (registration != root.end()) {
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
        if (schemaVersion <= 5 && registration == root.end()) {
            decoded.terrainGridOrigin = {
                static_cast<std::int32_t>(std::llround(
                    decoded.sourceAnchorCm[0] /
                        kTerrainTileSizeCm -
                    static_cast<float>(decoded.boardCells[0]) *
                        0.5f)),
                static_cast<std::int32_t>(std::llround(
                    decoded.sourceAnchorCm[2] /
                        kTerrainTileSizeCm -
                    static_cast<float>(decoded.boardCells[1]) *
                        0.5f))};
            decoded.terrainElevationLevel =
                static_cast<std::int32_t>(std::llround(
                    decoded.sourceAnchorCm[1] /
                    kTerrainElevationStepCm));
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
        const bool routeProfile =
            isRoute1EnvironmentProfile(decoded.sourceProfileId);
        if (decoded.coordinateSystem !=
                "source_centimetres_xyz_y_up" ||
            !routeProfile ||
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
        {"kind", "route1_environment_board_layout"},
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
        engine::render::route1_field_small_grass;
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

const char *cookedCompositionManifestPath(
    const engine::IAssetStore &mountedSceneStore) noexcept {
    return mountedSceneStore.exists(kCompositionManifestPath)
               ? kCompositionManifestPath
               : kLegacyCookedCompositionManifestPath;
}

const char *cookedCanonicalRoot(
    const engine::IAssetStore &mountedSceneStore) noexcept {
    return mountedSceneStore.exists(
               std::string(kCanonicalRoot) + "/scene.json")
               ? kCanonicalRoot
               : kLegacyCookedCanonicalRoot;
}

const char *cookedBoardLayoutManifestPath(
    const engine::IAssetStore &mountedSceneStore) noexcept {
    return mountedSceneStore.exists(kBoardLayoutManifestPath)
               ? kBoardLayoutManifestPath
               : kLegacyCookedBoardLayoutManifestPath;
}

bool loadCookedEnvironment(
    const engine::IAssetStore& hostStore,
    RuntimeEnvironment& outEnvironment,
    std::size_t* outVirtualFileCount,
    std::string* outError) {
    if (outVirtualFileCount) {
        *outVirtualFileCount = 0u;
    }

    engine::assets::phlosion::SceneArchiveStore sceneStore;
    std::string error;
    if (!sceneStore.load(
            hostStore,
            kCookedSceneArchivePath,
            &error)) {
        return fail(
            outError,
            "Could not mount required cooked Route 1 PHSC " +
                std::string(kCookedSceneArchivePath) + ": " + error);
    }

    RuntimeEnvironment loaded;
    if (!loaded.load(
            sceneStore,
            cookedCanonicalRoot(sceneStore),
            cookedCompositionManifestPath(sceneStore),
            cookedBoardLayoutManifestPath(sceneStore),
            &error)) {
        return fail(
            outError,
            "Could not load Route 1 from cooked PHSC: " + error);
    }

    if (outVirtualFileCount) {
        *outVirtualFileCount = sceneStore.fileCount();
    }
    outEnvironment = std::move(loaded);
    return true;
}

bool RuntimeEnvironment::load(
    const engine::IAssetStore& store,
    const std::string& canonicalRoot,
    const std::string& compositionManifestPath,
    const std::string& boardLayoutManifestPath,
    std::string* outError) {
    auto loaded = std::make_unique<Impl>();
    auto filter = engine::env::get("PAC_ROUTE1_MATERIAL_FILTER");
    if (!filter) {
        // Compatibility for local qualification scripts created before the
        // research/runtime boundary was introduced.
        filter = engine::env::get("PAC_LGPE_ROUTE1_MATERIAL_FILTER");
    }
    if (filter) {
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
    if (!game::assets::published_environment::loadCanonicalScene(
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
    if (!published_environment_scene::prepareCanonicalScene(
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
            std::set<GridCell> sourceCoreTerrainCells;
            for (const auto& cell :
                 record.at("core_cells_source_xz")) {
                const auto localCell =
                    cell.get<std::array<std::int32_t, 2>>();
                const auto terrainCell =
                    route1EncounterGrassCoreTerrainCell(
                        translation, localCell);
                sourceCoreTerrainCells.emplace(
                    terrainCell[0], terrainCell[1]);
            }
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
                        translation,
                    .sourceCoreTerrainCells =
                        std::move(sourceCoreTerrainCells)});
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
            if (!game::assets::published_environment::loadCanonicalScene(
                    store,
                    modelRoot,
                    layer.source,
                    &error) ||
                !published_environment_scene::prepareCanonicalScene(
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
            if (!game::assets::published_environment::loadCanonicalScene(
                    store,
                    model.at("cache_root").get<std::string>(),
                    layer.source,
                    &error) ||
                !published_environment_scene::prepareCanonicalScene(
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

bool RuntimeEnvironment::terrainPatchV2PreviewEnabled() const noexcept {
    return impl_ && impl_->terrainPatchV2PreviewEnabled;
}

bool RuntimeEnvironment::setTerrainPatchV2PreviewEnabled(
    bool enabled,
    std::string* outError) {
    if (!loaded()) {
        return fail(
            outError,
            "Route 1 must be mounted before changing Terrain Patch V2 preview state.");
    }
    if (impl_->terrainPatchV2PreviewEnabled == enabled) {
        if (outError) {
            outError->clear();
        }
        return true;
    }
    impl_->terrainPatchV2PreviewEnabled = enabled;
    std::string error;
    if (!impl_->rebuildLayoutDependentState(&error)) {
        impl_->terrainPatchV2PreviewEnabled = !enabled;
        std::string ignored;
        impl_->rebuildLayoutDependentState(&ignored);
        return fail(
            outError,
            "Terrain Patch V2 preview rebuild failed: " + error);
    }
    if (outError) {
        outError->clear();
    }
    return true;
}

bool RuntimeEnvironment::sampleWorldTerrainHeight(
    float worldX,
    float worldZ,
    float& outWorldY) const noexcept {
    return impl_ && impl_->sampleWorldTerrainHeight(
        worldX, worldZ, outWorldY);
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
        !route1EnvironmentProfilesCompatible(
            layout.sourceProfileId,
            impl_->source.profileId) ||
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
    const std::string_view sceneId =
        route1_scene_variants::editable(
            impl_->authoredScene.sceneId)
        ? std::string_view(impl_->authoredScene.sceneId)
        : route1_scene_variants::kRoute1.sceneId;
    auto authored =
        authoredSceneFromLayout(
            impl_->layout,
            impl_->layoutObjects,
            sceneId);
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
        !route1EnvironmentProfilesCompatible(
            layout.sourceProfileId,
            impl_->source.profileId) ||
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

void RuntimeEnvironment::setEncounterGrassInteractors(
    std::span<const EncounterGrassInteractor> interactors) {
    if (!impl_) {
        return;
    }
    impl_->encounterGrassInteractors.assign(
        interactors.begin(), interactors.end());
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

} // namespace game::runtime::route1_environment
