#pragma once

#include "game/assets/environment/PublishedEnvironmentScene.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace game::runtime::route1_terrain_assemblies {

struct PolygonGroupSelection {
    std::uint32_t polygonGroupIndex = 0u;
    std::uint32_t materialIndex = 0u;
    std::vector<std::uint32_t> indices;
};

struct Assembly {
    std::uint32_t assemblyIndex = 0u;
    std::string profileRole;
    std::array<float, 3> sourcePivotCm{};
    std::array<float, 3> boundsMinimum{};
    std::array<float, 3> boundsMaximum{};
    std::vector<PolygonGroupSelection> polygonGroups;
};

struct MeshPartition {
    std::uint32_t sourceMeshIndex = 0u;
    std::vector<Assembly> assemblies;
};

std::uint32_t expectedAssemblyCount(
    std::uint32_t sourceMeshIndex) noexcept;

bool derivePartition(
    const game::assets::published_environment::Mesh& mesh,
    MeshPartition& out,
    std::string* outError = nullptr);

} // namespace game::runtime::route1_terrain_assemblies
