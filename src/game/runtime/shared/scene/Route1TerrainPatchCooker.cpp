#include "game/runtime/shared/scene/Route1TerrainPatchCooker.h"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <queue>
#include <set>

namespace game::runtime::route1_terrain_patch_v2 {
namespace {

using route1_environment::TerrainTileState;

constexpr std::array<std::array<std::int32_t, 2>, 4> kDirections{{
    {0, 1},
    {1, 0},
    {0, -1},
    {-1, 0},
}};

bool activeSurface(const TerrainTileState& tile) {
    return tile.surface != "empty" &&
        (tile.sourceOccupied || tile.authored);
}

bool startsRegionalCook(const TerrainTileState& tile) {
    if (!activeSurface(tile)) {
        return false;
    }
    // Authorship is not geometry authority. Shadow reception, vegetation
    // ownership, and other render metadata serialize a terrain node without
    // changing its imported surface. Treating every authored node as a core
    // cell expanded V2 through untouched LGPE corners and replaced exact
    // source carriers with generated transition grids.
    const bool sourceAppearanceChanged =
        !tile.sourceOccupied ||
        tile.elevationLevel != tile.sourceElevationLevel ||
        tile.surface != tile.sourceSurface ||
        tile.shape != tile.sourceShape ||
        tile.sourceReference.has_value();
    return sourceAppearanceChanged ||
        tile.cleanSuppressedEncounterGrassTint ||
        tile.rebuildContinuousMaterialFields;
}

std::array<GridPoint, 4> edgeStarts(const GridCell& cell) {
    const auto [x, z] = cell;
    return {{{x, z + 1}, {x + 1, z + 1},
             {x + 1, z}, {x, z}}};
}

std::array<GridPoint, 4> edgeEnds(const GridCell& cell) {
    const auto [x, z] = cell;
    return {{{x + 1, z + 1}, {x + 1, z},
             {x, z}, {x, z + 1}}};
}

std::vector<BoundaryLoop> chainBoundary(
    BoundaryKind kind,
    const std::set<GridCell>& inside,
    const std::map<GridCell, const TerrainTileState*>& tileByCell,
    Validation& validation) {
    std::vector<BoundaryEdge> edges;
    std::set<std::pair<GridPoint, GridPoint>> directedEdges;
    for (const auto& cell : inside) {
        const auto tileFound = tileByCell.find(cell);
        if (tileFound == tileByCell.end()) {
            continue;
        }
        const auto starts = edgeStarts(cell);
        const auto ends = edgeEnds(cell);
        for (std::size_t edge = 0u; edge < kDirections.size(); ++edge) {
            const auto& direction = kDirections[edge];
            const GridCell outside{
                cell.first + direction[0],
                cell.second + direction[1]};
            if (inside.contains(outside)) {
                continue;
            }
            if (!directedEdges.emplace(starts[edge], ends[edge]).second) {
                ++validation.duplicateDirectedEdgeCount;
                validation.valid = false;
            }
            const auto outsideFound = tileByCell.find(outside);
            const TerrainTileState* outsideTile =
                outsideFound == tileByCell.end()
                ? nullptr
                : outsideFound->second;
            edges.push_back(BoundaryEdge{
                .ownerCell = cell,
                .outsideCell = outside,
                .edge = edge,
                .start = starts[edge],
                .end = ends[edge],
                .profile = route1_environment::route1TerrainSharedEdgeProfile(
                    *tileFound->second, outsideTile, edge)});
        }
    }

    std::map<GridPoint, std::vector<std::size_t>> edgesByStart;
    for (std::size_t edgeIndex = 0u; edgeIndex < edges.size(); ++edgeIndex) {
        edgesByStart[edges[edgeIndex].start].push_back(edgeIndex);
    }
    for (auto& [point, indices] : edgesByStart) {
        (void)point;
        std::sort(
            indices.begin(),
            indices.end(),
            [&](std::size_t left, std::size_t right) {
                if (edges[left].edge != edges[right].edge) {
                    return edges[left].edge < edges[right].edge;
                }
                return edges[left].ownerCell < edges[right].ownerCell;
            });
    }

    std::vector<BoundaryLoop> loops;
    std::vector<bool> visited(edges.size(), false);
    for (std::size_t first = 0u; first < edges.size(); ++first) {
        if (visited[first]) {
            continue;
        }
        BoundaryLoop loop;
        loop.kind = kind;
        std::size_t current = first;
        while (!visited[current]) {
            visited[current] = true;
            loop.edges.push_back(edges[current]);
            const auto found = edgesByStart.find(edges[current].end);
            if (found == edgesByStart.end()) {
                break;
            }
            std::size_t next = edges.size();
            std::uint32_t bestTurnRank = 4u;
            for (const std::size_t candidate : found->second) {
                if (visited[candidate]) {
                    continue;
                }
                const std::uint32_t turn = static_cast<std::uint32_t>(
                    (edges[candidate].edge + 4u - edges[current].edge) % 4u);
                const std::uint32_t rank =
                    turn == 1u ? 0u :
                    turn == 0u ? 1u :
                    turn == 3u ? 2u : 3u;
                if (rank < bestTurnRank) {
                    bestTurnRank = rank;
                    next = candidate;
                }
            }
            if (next >= edges.size()) {
                break;
            }
            current = next;
        }
        loop.closed = !loop.edges.empty() &&
            loop.edges.back().end == loop.edges.front().start;
        if (!loop.closed) {
            ++validation.openBoundaryCount;
            validation.valid = false;
        }
        for (std::size_t index = 1u; index < loop.edges.size(); ++index) {
            if (loop.edges[index - 1u].profile.tileLevels[1] !=
                loop.edges[index].profile.tileLevels[0]) {
                ++validation.levelHandoffCount;
            }
        }
        if (loop.closed && loop.edges.back().profile.tileLevels[1] !=
            loop.edges.front().profile.tileLevels[0]) {
            ++validation.levelHandoffCount;
        }
        loops.push_back(std::move(loop));
    }

    const auto visitedCount = static_cast<std::size_t>(std::count(
        visited.begin(), visited.end(), true));
    if (visitedCount != edges.size()) {
        validation.strandedBoundaryEdgeCount +=
            static_cast<std::uint32_t>(edges.size() - visitedCount);
        validation.valid = false;
    }
    return loops;
}

} // namespace

Plan cook(
    const std::vector<TerrainTileState>& tiles,
    std::uint32_t sourceTransitionRingCells) {
    Plan plan;
    std::map<GridCell, const TerrainTileState*> tileByCell;
    std::set<GridCell> coreCells;
    for (const auto& tile : tiles) {
        if (!activeSurface(tile)) {
            continue;
        }
        const GridCell cell{tile.gridX, tile.gridZ};
        tileByCell.emplace(cell, &tile);
        if (startsRegionalCook(tile)) {
            coreCells.emplace(cell);
        }
    }
    if (coreCells.empty()) {
        return plan;
    }

    std::set<GridCell> coverage = coreCells;
    const std::int32_t ring = static_cast<std::int32_t>(
        std::min<std::uint32_t>(sourceTransitionRingCells, 8u));
    for (const auto& core : coreCells) {
        for (std::int32_t dx = -ring; dx <= ring; ++dx) {
            for (std::int32_t dz = -ring; dz <= ring; ++dz) {
                const GridCell candidate{
                    core.first + dx, core.second + dz};
                if (tileByCell.contains(candidate)) {
                    coverage.emplace(candidate);
                }
            }
        }
    }

    std::set<GridCell> unassigned = coverage;
    while (!unassigned.empty()) {
        std::set<GridCell> regionCells;
        std::queue<GridCell> pending;
        pending.push(*unassigned.begin());
        unassigned.erase(unassigned.begin());
        while (!pending.empty()) {
            const GridCell cell = pending.front();
            pending.pop();
            regionCells.emplace(cell);
            for (const auto& direction : kDirections) {
                const GridCell neighbor{
                    cell.first + direction[0],
                    cell.second + direction[1]};
                const auto found = unassigned.find(neighbor);
                if (found == unassigned.end()) {
                    continue;
                }
                pending.push(neighbor);
                unassigned.erase(found);
            }
        }

        Region region;
        region.id = static_cast<std::uint32_t>(plan.regions.size() + 1u);
        region.minimumCell = {
            std::numeric_limits<std::int32_t>::max(),
            std::numeric_limits<std::int32_t>::max()};
        region.maximumCell = {
            std::numeric_limits<std::int32_t>::min(),
            std::numeric_limits<std::int32_t>::min()};
        std::set<GridCell> regionCore;
        for (const auto& cell : regionCells) {
            const bool core = coreCells.contains(cell);
            region.cells.push_back(PatchCell{
                .cell = cell,
                .role = core
                    ? CellRole::Core
                    : CellRole::SourceTransition});
            if (core) {
                regionCore.emplace(cell);
                ++plan.coreCellCount;
            } else {
                ++plan.transitionCellCount;
            }
            region.minimumCell.first = std::min(
                region.minimumCell.first, cell.first);
            region.minimumCell.second = std::min(
                region.minimumCell.second, cell.second);
            region.maximumCell.first = std::max(
                region.maximumCell.first, cell.first);
            region.maximumCell.second = std::max(
                region.maximumCell.second, cell.second);
        }

        auto coreBoundaries = chainBoundary(
            BoundaryKind::CoreToTransition,
            regionCore,
            tileByCell,
            plan.validation);
        auto sourceBoundaries = chainBoundary(
            BoundaryKind::PatchToSource,
            regionCells,
            tileByCell,
            plan.validation);
        region.boundaries.reserve(
            coreBoundaries.size() + sourceBoundaries.size());
        for (auto& boundary : coreBoundaries) {
            plan.boundaryEdgeCount += static_cast<std::uint32_t>(
                boundary.edges.size());
            region.boundaries.push_back(std::move(boundary));
        }
        for (auto& boundary : sourceBoundaries) {
            plan.boundaryEdgeCount += static_cast<std::uint32_t>(
                boundary.edges.size());
            region.boundaries.push_back(std::move(boundary));
        }
        plan.boundaryLoopCount += static_cast<std::uint32_t>(
            region.boundaries.size());
        plan.regions.push_back(std::move(region));
    }
    return plan;
}

} // namespace game::runtime::route1_terrain_patch_v2
