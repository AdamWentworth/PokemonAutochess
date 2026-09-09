#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace engine {
class IAssetStore;
}
namespace game::runtime::route1_environment {
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
    // Grass prototypes own the bed footprint; this selects its blade model.
    std::string prefabAssetId;
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
    bool receivesProjectedShadow = true;
    bool normalizeSourceTint = false;
    bool suppressOverlappingVegetation = false;
    std::string reason;
};

struct BoardLayoutTransform {
    std::string coordinateSystem;
    std::string sourceProfileId;
    float sourceUnitsToWorld = 0.01f;
    std::array<float, 3> sourceAnchorCm{2100.0f, 0.0f, -1500.0f};
    std::array<float, 3> worldAnchor{0.0f, -0.04f, 0.0f};
    float yawDegrees = 0.0f;
    std::array<std::uint32_t, 2> boardCells{8u, 8u};
    std::array<std::int32_t, 2> terrainGridOrigin{17, -19};
    std::int32_t terrainElevationLevel = 0;
    float boardCellSizeWorld = 1.0f;
    std::uint32_t benchSlots = 8u;
    std::uint32_t benchGapCells = 0u;
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

bool loadBoardLayoutTransform(
    const engine::IAssetStore &store,
    const std::string &virtualPath,
    BoardLayoutTransform &out,
    std::string *outError = nullptr);
void bindBoardLayoutToTerrainGrid(
    BoardLayoutTransform &layout) noexcept;
std::array<std::int32_t, 2> northBenchTerrainGridOrigin(
    const BoardLayoutTransform &layout) noexcept;
std::array<std::int32_t, 2> southBenchTerrainGridOrigin(
    const BoardLayoutTransform &layout) noexcept;
std::string serializeBoardLayoutTransform(
    const BoardLayoutTransform &transform);

namespace board_layout_metrics {
inline constexpr float kTerrainTileSizeCm = 100.0f;
inline constexpr float kTerrainElevationStepCm = 50.0f;
} // namespace board_layout_metrics
bool isRoute1EnvironmentProfile(std::string_view profileId);
bool route1EnvironmentProfilesCompatible(std::string_view left, std::string_view right);
bool boardRegistrationMatchesTerrainGrid(const BoardLayoutTransform &layout);
} // namespace game::runtime::route1_environment
