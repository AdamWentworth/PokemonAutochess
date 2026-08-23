#include "game/runtime/shared/scene/Route1TerrainLedgeResolver.h"

#include <algorithm>
#include <array>
#include <compare>
#include <map>
#include <tuple>

namespace game::runtime::route1_terrain_ledges {
namespace {

using route1_environment::TerrainSharedEdgeProfile;
using route1_environment::TerrainTileState;

constexpr float kTerrainTileSizeCm = 100.0f;
constexpr std::array<std::array<std::int32_t, 2>, 4> kDirections{{
    {0, 1},
    {1, 0},
    {0, -1},
    {-1, 0},
}};
constexpr std::array<
    std::array<std::array<std::int32_t, 2>, 2>,
    4> kEndpointOffsets{{
    {{{0, 1}, {1, 1}}},
    {{{1, 1}, {1, 0}}},
    {{{1, 0}, {0, 0}}},
    {{{0, 0}, {0, 1}}},
}};

struct BoundaryNode {
    std::int32_t gridX = 0;
    std::int32_t gridZ = 0;
    std::int32_t ownerLevel = 0;
    std::int32_t neighborLevel = 0;

    auto operator<=>(const BoundaryNode&) const = default;
};

struct PendingEdge {
    RebuiltEdge edge;
    BoundaryNode start;
    BoundaryNode end;
};

bool hasSurface(const TerrainTileState& tile) {
    return tile.surface != "empty" &&
        (tile.sourceOccupied || tile.authored);
}

const TerrainTileState* tileAt(
    const std::map<GridCell, const TerrainTileState*>& tiles,
    GridCell cell) {
    const auto found = tiles.find(cell);
    return found == tiles.end() ? nullptr : found->second;
}

bool edgeLess(const PendingEdge& left, const PendingEdge& right) {
    return std::tie(
               left.edge.ownerCell.second,
               left.edge.ownerCell.first,
               left.edge.edge) <
        std::tie(
               right.edge.ownerCell.second,
               right.edge.ownerCell.first,
               right.edge.edge);
}

Join joinFor(const PendingEdge& incoming, const PendingEdge& outgoing) {
    if (incoming.end != outgoing.start) {
        return Join::Open;
    }
    const std::size_t turn =
        (outgoing.edge.edge + 4u - incoming.edge.edge) % 4u;
    if (turn == 0u) {
        return Join::Straight;
    }
    if (turn == 1u) {
        return Join::Convex;
    }
    if (turn == 3u) {
        return Join::Concave;
    }
    return Join::Open;
}

} // namespace

Resolution resolve(
    const std::vector<TerrainTileState>& tiles,
    const std::vector<TerrainTileState>& sourceTiles,
    const std::set<GridCell>& cleanupCells) {
    std::map<GridCell, const TerrainTileState*> tileByCell;
    std::map<GridCell, const TerrainTileState*> sourceTileByCell;
    for (const auto& tile : tiles) {
        tileByCell.emplace(GridCell{tile.gridX, tile.gridZ}, &tile);
    }
    for (const auto& tile : sourceTiles) {
        sourceTileByCell.emplace(
            GridCell{tile.gridX, tile.gridZ}, &tile);
    }

    std::vector<PendingEdge> pending;
    for (const auto& tile : tiles) {
        if (!hasSurface(tile) || tile.sourceReference) {
            continue;
        }
        const GridCell ownerCell{tile.gridX, tile.gridZ};
        const bool ownerAffected =
            tile.authored || cleanupCells.contains(ownerCell);
        for (std::size_t edge = 0u; edge < kDirections.size(); ++edge) {
            const auto direction = kDirections[edge];
            const GridCell neighborCell{
                tile.gridX + direction[0],
                tile.gridZ + direction[1]};
            const auto* neighbor = tileAt(tileByCell, neighborCell);
            if (neighbor && neighbor->sourceReference) {
                continue;
            }
            const bool neighborAffected =
                (neighbor && neighbor->authored) ||
                cleanupCells.contains(neighborCell);
            if (!ownerAffected && !neighborAffected) {
                continue;
            }
            const auto profile =
                route1_environment::route1TerrainSharedEdgeProfile(
                    tile,
                    neighbor && hasSurface(*neighbor) ? neighbor : nullptr,
                    edge);
            if (profile.tileLevels[0] <= profile.neighborLevels[0] &&
                profile.tileLevels[1] <= profile.neighborLevels[1]) {
                continue;
            }

            TerrainSharedEdgeProfile sourceProfile;
            const auto* sourceTile = tileAt(
                sourceTileByCell, ownerCell);
            const auto* sourceNeighbor = tileAt(
                sourceTileByCell, neighborCell);
            if (sourceTile && hasSurface(*sourceTile)) {
                sourceProfile =
                    route1_environment::route1TerrainSharedEdgeProfile(
                        *sourceTile,
                        sourceNeighbor && hasSurface(*sourceNeighbor)
                            ? sourceNeighbor
                            : nullptr,
                        edge);
            }
            const bool sourceHasDrop =
                sourceProfile.tileLevels[0] >
                    sourceProfile.neighborLevels[0] ||
                sourceProfile.tileLevels[1] >
                    sourceProfile.neighborLevels[1];
            const bool sourceBoundaryMatches =
                sourceHasDrop &&
                profile.tileLevels == sourceProfile.tileLevels &&
                profile.neighborLevels == sourceProfile.neighborLevels;
            if (!cleanupCells.contains(ownerCell) &&
                !cleanupCells.contains(neighborCell) &&
                sourceBoundaryMatches) {
                continue;
            }

            const auto node = [&](std::size_t endpoint) {
                return BoundaryNode{
                    .gridX = tile.gridX +
                        kEndpointOffsets[edge][endpoint][0],
                    .gridZ = tile.gridZ +
                        kEndpointOffsets[edge][endpoint][1],
                    .ownerLevel = profile.tileLevels[endpoint],
                    .neighborLevel = profile.neighborLevels[endpoint]};
            };
            pending.push_back(PendingEdge{
                .edge = RebuiltEdge{
                    .ownerCell = ownerCell,
                    .edge = edge,
                    .profile = profile},
                .start = node(0u),
                .end = node(1u)});
        }
    }
    std::sort(pending.begin(), pending.end(), edgeLess);

    std::map<BoundaryNode, std::vector<std::size_t>> outgoing;
    std::map<BoundaryNode, std::size_t> incomingCount;
    for (std::size_t index = 0u; index < pending.size(); ++index) {
        outgoing[pending[index].start].push_back(index);
        ++incomingCount[pending[index].end];
    }
    for (auto& [node, indices] : outgoing) {
        (void)node;
        std::sort(
            indices.begin(),
            indices.end(),
            [&](std::size_t left, std::size_t right) {
                return edgeLess(pending[left], pending[right]);
            });
    }

    std::vector<bool> visited(pending.size(), false);
    Resolution resolution;
    resolution.edges.reserve(pending.size());
    const auto traceContour = [&](std::size_t firstIndex) {
        std::size_t current = firstIndex;
        float distanceCm = 0.0f;
        const std::uint32_t contourIndex = resolution.contourCount++;
        std::vector<std::size_t> contourEdges;
        while (!visited[current]) {
            visited[current] = true;
            contourEdges.push_back(current);
            pending[current].edge.contourIndex = contourIndex;
            pending[current].edge.contourStartCm = distanceCm;
            distanceCm += kTerrainTileSizeCm;

            const auto candidates = outgoing.find(pending[current].end);
            if (candidates == outgoing.end()) {
                break;
            }
            const auto next = std::find_if(
                candidates->second.begin(),
                candidates->second.end(),
                [&](std::size_t index) {
                    return !visited[index];
                });
            if (next == candidates->second.end()) {
                break;
            }
            current = *next;
        }
        for (std::size_t index = 0u;
             index + 1u < contourEdges.size();
             ++index) {
            const std::size_t incoming = contourEdges[index];
            const std::size_t outgoingEdge = contourEdges[index + 1u];
            const Join join = joinFor(
                pending[incoming], pending[outgoingEdge]);
            pending[incoming].edge.endJoin = join;
            pending[outgoingEdge].edge.startJoin = join;
        }
        if (contourEdges.size() > 1u) {
            const std::size_t incoming = contourEdges.back();
            const std::size_t outgoingEdge = contourEdges.front();
            const Join join = joinFor(
                pending[incoming], pending[outgoingEdge]);
            if (join != Join::Open) {
                pending[incoming].edge.endJoin = join;
                pending[outgoingEdge].edge.startJoin = join;
            }
        }
    };

    for (std::size_t index = 0u; index < pending.size(); ++index) {
        if (!visited[index] && incomingCount[pending[index].start] == 0u) {
            traceContour(index);
        }
    }
    for (std::size_t index = 0u; index < pending.size(); ++index) {
        if (!visited[index]) {
            traceContour(index);
        }
    }
    for (auto& edge : pending) {
        resolution.edges.push_back(std::move(edge.edge));
    }
    std::sort(
        resolution.edges.begin(),
        resolution.edges.end(),
        [](const RebuiltEdge& left, const RebuiltEdge& right) {
            return std::tie(
                       left.ownerCell.second,
                       left.ownerCell.first,
                       left.edge) <
                std::tie(
                       right.ownerCell.second,
                       right.ownerCell.first,
                       right.edge);
        });
    return resolution;
}

float endpointAlongCm(
    Join join,
    bool start,
    float outwardCm) noexcept {
    constexpr float kHalfEdgeCm = kTerrainTileSizeCm * 0.5f;
    const float inset = join == Join::Concave
        ? std::clamp(outwardCm, 0.0f, kHalfEdgeCm)
        : 0.0f;
    return start
        ? -kHalfEdgeCm + inset
        : kHalfEdgeCm - inset;
}

const RebuiltEdge* find(
    const Resolution& resolution,
    GridCell ownerCell,
    std::size_t edge) noexcept {
    const auto found = std::find_if(
        resolution.edges.begin(),
        resolution.edges.end(),
        [&](const RebuiltEdge& candidate) {
            return candidate.ownerCell == ownerCell &&
                candidate.edge == edge;
        });
    return found == resolution.edges.end() ? nullptr : &*found;
}

} // namespace game::runtime::route1_terrain_ledges
