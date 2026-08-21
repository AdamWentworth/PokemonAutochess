#pragma once

#include "game/assets/environment/PublishedEnvironmentScene.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace game::runtime::route1_tree_instances {

struct VertexRange {
    std::uint32_t first = 0u;
    std::uint32_t count = 0u;
    std::array<double, 3> center{};
};

struct PolygonGroupPartition {
    std::uint32_t polygonGroupIndex = 0u;
    std::uint32_t materialIndex = 0u;
    std::vector<VertexRange> instances;
};

struct MeshPartition {
    std::uint32_t sourceMeshIndex = 0u;
    std::uint32_t largestTrunkComponentVertices = 0u;
    std::vector<std::array<float, 3>> sourcePivotsCm;
    std::vector<PolygonGroupPartition> polygonGroups;
};

std::uint32_t expectedInstanceCount(
    std::uint32_t sourceMeshIndex) noexcept;

bool derivePartition(
    const game::assets::published_environment::Mesh& mesh,
    std::uint32_t instanceCount,
    MeshPartition& out,
    std::string* outError = nullptr);

bool selectInstanceTriangles(
    const game::assets::published_environment::Mesh& mesh,
    const PolygonGroupPartition& partition,
    std::uint32_t instanceIndex,
    std::vector<std::uint32_t>& outIndices,
    std::string* outError = nullptr);

} // namespace game::runtime::route1_tree_instances
