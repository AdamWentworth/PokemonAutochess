#pragma once

#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
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

struct BoardLayoutTransform {
    std::string coordinateSystem;
    std::string sourceProfileId;
    float sourceUnitsToWorld = 0.01f;
    std::array<float, 3> sourceAnchorCm{2200.0f, 0.0f, -1700.0f};
    std::array<float, 3> worldAnchor{0.0f, -0.04f, 0.0f};
    float yawDegrees = 0.0f;
    std::uint32_t declaredLocalDeltaCount = 0u;
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

std::array<float, 16> worldFromSourceMatrix(
    const BoardLayoutTransform& transform);
std::array<float, 16> sourceFromWorldMatrix(
    const BoardLayoutTransform& transform);
LightProjectionRows route1CloudProjectionRows(
    const BoardLayoutTransform& transform);

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
    const RuntimeStats& stats() const noexcept;

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
