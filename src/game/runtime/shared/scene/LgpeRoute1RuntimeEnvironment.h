#pragma once

#include "engine/assets/phlosion/PhlosionAuthoredScene.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace engine {
class IAssetStore;
}

namespace game::runtime::lgpe_route1_runtime {

inline constexpr char kCanonicalRoot[] = "cache/lgpe/route1";
inline constexpr char kCompositionManifestPath[] =
    "tools/lgpe_importer/route1.composition.json";
inline constexpr char kBoardLayoutManifestPath[] =
    "config/lgpe/route1_board_layout.json";
inline constexpr char kAuthoredSceneDocumentPath[] =
    "scenes/route1.scene.json";

struct LocalLayoutDelta {
    std::string id;
    std::string targetKind;
    std::string logicalName;
    std::uint32_t recordIndex = 0u;
    std::array<float, 3> expectedSourceTranslationCm{};
    std::array<float, 3> expectedSourceRotationDegrees{};
    std::array<float, 3> expectedSourceScale{1.0f, 1.0f, 1.0f};
    std::array<float, 3> translationCm{};
    std::array<float, 3> rotationDegrees{};
    std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
    bool suppressed = false;
    std::string reason;
};

struct LayoutObjectMetadataOverride {
    std::string stableId;
    std::string displayName;
    std::string categoryPath;
};

struct AuthoredPrefabInstance {
    std::string stableId;
    std::string prototypeStableId;
    std::string displayName;
    std::string categoryPath;
    std::array<float, 3> sourceTranslationCm{};
    std::array<float, 3> sourceRotationDegrees{};
    std::array<float, 3> sourceScale{1.0f, 1.0f, 1.0f};
    std::array<float, 3> translationCm{};
    std::array<float, 3> rotationDegrees{};
    std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
    bool suppressed = false;
    std::string reason;
};

struct AuthoredTerrainTile {
    std::string stableId;
    std::string displayName;
    std::string categoryPath;
    std::string tileSetAssetId;
    std::int32_t gridX = 0;
    std::int32_t gridZ = 0;
    std::int32_t elevationLevel = 0;
    std::string surface = "light_lawn";
    std::string shape = "flat";
    std::string visualVariant = "auto";
    std::optional<std::array<std::int32_t, 2>> sourceReference;
    std::string reason;
};

struct BoardLayoutTransform {
    std::string coordinateSystem;
    std::string sourceProfileId;
    float sourceUnitsToWorld = 0.01f;
    std::array<float, 3> sourceAnchorCm{2200.0f, 0.0f, -1700.0f};
    std::array<float, 3> worldAnchor{0.0f, -0.04f, 0.0f};
    float yawDegrees = 0.0f;
    std::array<std::uint32_t, 2> boardCells{8u, 8u};
    std::array<std::int32_t, 2> terrainGridOrigin{18, -21};
    std::int32_t terrainElevationLevel = 0;
    float boardCellSizeWorld = 1.0f;
    std::uint32_t benchSlots = 8u;
    std::uint32_t benchGapCells = 1u;
    bool northBench = true;
    bool southBench = true;
    // Adapter-only composition state. Schema 6 board manifests serialize the
    // integer terrain-cell registration only; project object authoring
    // persists through AuthoredSceneDocument.
    std::vector<LocalLayoutDelta> localLayoutDeltas;
    std::vector<LayoutObjectMetadataOverride>
        objectMetadataOverrides;
    std::vector<AuthoredPrefabInstance>
        authoredPrefabInstances;
    std::vector<AuthoredTerrainTile> authoredTerrainTiles;
    std::uint32_t declaredLocalDeltaCount = 0u;
};

struct TerrainTileState {
    std::int32_t gridX = 0;
    std::int32_t gridZ = 0;
    std::int32_t sourceElevationLevel = 0;
    std::int32_t elevationLevel = 0;
    std::string sourceSurface = "light_lawn";
    std::string sourceShape = "flat";
    std::string surface = "light_lawn";
    std::string shape = "flat";
    std::string visualVariant = "auto";
    std::optional<std::array<std::int32_t, 2>> sourceReference;
    std::string reason;
    bool sourceOccupied = false;
    bool authored = false;
};

struct TerrainSharedEdgeProfile {
    // Endpoints follow the clockwise edge winding used by Route 1 terrain:
    // south (-X to +X), east (+Z to -Z), north (+X to -X), west (-Z to +Z).
    std::array<std::int32_t, 2> tileLevels{};
    std::array<std::int32_t, 2> neighborLevels{};
};

struct LayoutObject {
    std::string stableId;
    std::string displayName;
    std::string targetKind;
    std::string categoryPath;
    std::string prefabAssetId;
    std::string logicalName;
    std::uint32_t recordIndex = 0u;
    std::array<float, 3> sourceTranslationCm{};
    std::array<float, 3> sourceRotationDegrees{};
    std::array<float, 3> sourceScale{1.0f, 1.0f, 1.0f};
    std::array<float, 3> translationCm{};
    std::array<float, 3> rotationDegrees{};
    std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
    std::array<float, 3> boundsMinimumCm{};
    std::array<float, 3> boundsMaximumCm{};
    bool suppressed = false;
    bool hasOverride = false;
    bool authored = false;
    std::string reason;
};

struct RuntimeStats {
    std::uint32_t sceneCount = 0u;
    std::uint32_t materialCount = 0u;
    std::uint32_t drawClassCount = 0u;
    std::uint32_t encounterGrassInstanceCount = 0u;
    std::uint32_t placedVegetationInstanceCount = 0u;
    std::uint64_t visibleTriangleCount = 0u;
    std::uint64_t shadowTriangleCount = 0u;
};

struct LightProjectionRows {
    std::array<float, 4> u{};
    std::array<float, 4> v{};
};

bool loadBoardLayoutTransform(
    const engine::IAssetStore& store,
    const std::string& virtualPath,
    BoardLayoutTransform& out,
    std::string* outError = nullptr);
void bindBoardLayoutToTerrainGrid(
    BoardLayoutTransform& layout) noexcept;
std::array<std::int32_t, 2> northBenchTerrainGridOrigin(
    const BoardLayoutTransform& layout) noexcept;
std::array<std::int32_t, 2> southBenchTerrainGridOrigin(
    const BoardLayoutTransform& layout) noexcept;
std::string serializeBoardLayoutTransform(
    const BoardLayoutTransform& transform);

std::array<float, 16> worldFromSourceMatrix(
    const BoardLayoutTransform& transform);
std::array<float, 16> sourceFromWorldMatrix(
    const BoardLayoutTransform& transform);
LightProjectionRows route1CloudProjectionRows(
    const BoardLayoutTransform& transform);
std::string route1TerrainTileStableId(
    std::int32_t gridX,
    std::int32_t gridZ);
std::array<float, 4> route1SignRampDirtColor(
    float normalizedHeight,
    float normalizedCrossRamp) noexcept;
std::array<float, 4> route1SignRampAdjacentDirtColor(
    const std::array<float, 4>& normalDirtColor,
    float rampBoundaryWeight,
    float normalizedCrossRamp,
    bool highSide) noexcept;
TerrainSharedEdgeProfile route1TerrainSharedEdgeProfile(
    const TerrainTileState& tile,
    const TerrainTileState* neighbor,
    std::size_t edge) noexcept;

class RuntimeEnvironment {
public:
    RuntimeEnvironment();
    ~RuntimeEnvironment();
    RuntimeEnvironment(const RuntimeEnvironment&) = delete;
    RuntimeEnvironment& operator=(const RuntimeEnvironment&) = delete;
    RuntimeEnvironment(RuntimeEnvironment&&) noexcept;
    RuntimeEnvironment& operator=(RuntimeEnvironment&&) noexcept;

    bool load(
        const engine::IAssetStore& store,
        const std::string& canonicalRoot = kCanonicalRoot,
        const std::string& compositionManifestPath =
            kCompositionManifestPath,
        const std::string& boardLayoutManifestPath =
            kBoardLayoutManifestPath,
        std::string* outError = nullptr);

    bool loaded() const noexcept;
    const BoardLayoutTransform& layout() const noexcept;
    const engine::assets::phlosion::AuthoredSceneDocument&
        authoredScene() const noexcept;
    const std::vector<LayoutObject>& layoutObjects() const noexcept;
    const std::vector<TerrainTileState>& terrainTiles() const noexcept;
    const RuntimeStats& stats() const noexcept;

    // Applies a project-owned layout manifest to the already mounted
    // canonical scene. The canonical source records remain unchanged.
    bool applyBoardLayout(
        const BoardLayoutTransform& layout,
        std::string* outError = nullptr);
    // Updates only the global source-to-world board registration used by
    // rendering. This is the cheap editor drag path; applyBoardLayout()
    // performs the expensive canonical/layout/shadow rebuild on release.
    bool previewBoardLayout(
        const BoardLayoutTransform& layout,
        std::string* outError = nullptr);
    bool applyAuthoredScene(
        const engine::assets::phlosion::AuthoredSceneDocument& document,
        std::string* outError = nullptr);
    bool setLayoutObjectOverride(
        const std::string& stableId,
        const std::array<float, 3>& translationCm,
        const std::array<float, 3>& rotationDegrees,
        const std::array<float, 3>& scale,
        bool suppressed,
        const std::string& reason,
        std::string* outError = nullptr);
    // Applies only the lightweight instance/frame update required while an
    // editor gizmo is moving. Expensive projected-shadow/material/stat
    // rebuilds are deferred until applyBoardLayout() commits the edit.
    bool previewLayoutObjectOverride(
        const std::string& stableId,
        const std::array<float, 3>& translationCm,
        const std::array<float, 3>& rotationDegrees,
        const std::array<float, 3>& scale,
        bool suppressed,
        const std::string& reason,
        std::string* outError = nullptr);
    bool resetLayoutObjectOverride(
        const std::string& stableId,
        std::string* outError = nullptr);
    bool duplicateLayoutObject(
        const std::string& stableId,
        std::string& outCreatedStableId,
        std::string* outError = nullptr);
    bool deleteLayoutObject(
        const std::string& stableId,
        std::string* outError = nullptr);
    bool renameLayoutObject(
        const std::string& stableId,
        const std::string& displayName,
        std::string* outError = nullptr);
    bool reparentLayoutObject(
        const std::string& stableId,
        const std::string& categoryPath,
        std::string* outError = nullptr);

    // Updates persistent source-local skin palettes in place so cached
    // gameplay batches keep valid pointers while their wind pose advances.
    void updateAnimation(float simulationSeconds);

    // Appends the complete source-backed Route 1 composition. All source
    // model matrices remain untouched until the single manifest-owned
    // world-from-source matrix is applied here.
    void appendIndexedBatches(
        float simulationSeconds,
        std::vector<shared_world_batches::WorldIndexedBatch>& out);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace game::runtime::lgpe_route1_runtime
