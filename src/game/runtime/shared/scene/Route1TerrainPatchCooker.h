#pragma once

#include "game/runtime/shared/scene/Route1RuntimeEnvironment.h"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace game::runtime::route1_terrain_patch_v2 {

using GridCell = std::pair<std::int32_t, std::int32_t>;
using GridPoint = std::pair<std::int32_t, std::int32_t>;

enum class CellRole : std::uint8_t {
    Core,
    SourceTransition,
};

enum class BoundaryKind : std::uint8_t {
    CoreToTransition,
    PatchToSource,
};

struct PatchCell {
    GridCell cell{};
    CellRole role = CellRole::SourceTransition;
};

struct BoundaryEdge {
    GridCell ownerCell{};
    GridCell outsideCell{};
    std::size_t edge = 0u;
    GridPoint start{};
    GridPoint end{};
    route1_environment::TerrainSharedEdgeProfile profile;
};

struct BoundaryLoop {
    BoundaryKind kind = BoundaryKind::PatchToSource;
    std::vector<BoundaryEdge> edges;
    bool closed = false;
};

struct Region {
    std::uint32_t id = 0u;
    std::vector<PatchCell> cells;
    std::vector<BoundaryLoop> boundaries;
    GridCell minimumCell{};
    GridCell maximumCell{};
};

struct Validation {
    bool valid = true;
    std::uint32_t openBoundaryCount = 0u;
    std::uint32_t duplicateDirectedEdgeCount = 0u;
    std::uint32_t strandedBoundaryEdgeCount = 0u;
    // A non-zero handoff count is legal for ramp/corner profiles, but is
    // exposed so the eventual regional mesher knows where it must insert a
    // vertical or sloped stitch instead of welding unlike heights.
    std::uint32_t levelHandoffCount = 0u;
};

struct Plan {
    std::vector<Region> regions;
    Validation validation;
    std::uint32_t coreCellCount = 0u;
    std::uint32_t transitionCellCount = 0u;
    std::uint32_t boundaryLoopCount = 0u;
    std::uint32_t boundaryEdgeCount = 0u;
};

// Converts cell-addressed authoring edits into connected regional cook
// domains. Cells are retained as selection and material metadata only. The
// generated plan owns both the edit perimeter and a source-derived transition
// perimeter so the renderer can build one surface with shared vertices rather
// than four independent tile edges.
Plan cook(
    const std::vector<route1_environment::TerrainTileState>& tiles,
    std::uint32_t sourceTransitionRingCells = 1u);

} // namespace game::runtime::route1_terrain_patch_v2
