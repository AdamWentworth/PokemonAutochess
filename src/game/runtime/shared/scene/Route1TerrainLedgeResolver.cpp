#include "game/runtime/shared/scene/Route1TerrainLedgeResolver.h"

#include <algorithm>
#include <array>
#include <compare>
#include <limits>
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
    bool rebuilt = false;
    bool propagationCompatible = false;
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
    bool hasOccupiedSourceTerrain = false;
    std::int32_t occupiedMinimumX =
        std::numeric_limits<std::int32_t>::max();
    std::int32_t occupiedMaximumX =
        std::numeric_limits<std::int32_t>::lowest();
    std::int32_t occupiedMinimumZ =
        std::numeric_limits<std::int32_t>::max();
    std::int32_t occupiedMaximumZ =
        std::numeric_limits<std::int32_t>::lowest();
    for (const auto& tile : sourceTiles) {
        if (!hasSurface(tile)) {
            continue;
        }
        hasOccupiedSourceTerrain = true;
        occupiedMinimumX = std::min(occupiedMinimumX, tile.gridX);
        occupiedMaximumX = std::max(occupiedMaximumX, tile.gridX);
        occupiedMinimumZ = std::min(occupiedMinimumZ, tile.gridZ);
        occupiedMaximumZ = std::max(occupiedMaximumZ, tile.gridZ);
    }
    const auto insideOccupiedSourceExtent = [&](GridCell cell) {
        return hasOccupiedSourceTerrain &&
            cell.first >= occupiedMinimumX &&
            cell.first <= occupiedMaximumX &&
            cell.second >= occupiedMinimumZ &&
            cell.second <= occupiedMaximumZ;
    };

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
            const auto* sourceTile = tileAt(
                sourceTileByCell, ownerCell);
            const auto* sourceNeighbor = tileAt(
                sourceTileByCell, neighborCell);
            // The source sampler pads its rectangular grid with unoccupied
            // cells. Route 1's z=0 padding row is therefore present in the
            // lookup even though the occupied location ends at z=-1. Use the
            // occupied source extent—not map membership—to prevent that empty
            // padding from shortening a terminal side ledge into a convex
            // handoff. Authored terrain extending beyond the source extent is
            // still allowed to establish a real continuation.
            if (sourceTile &&
                insideOccupiedSourceExtent(ownerCell) &&
                !insideOccupiedSourceExtent(neighborCell) &&
                (!neighbor || !hasSurface(*neighbor))) {
                continue;
            }
            const bool neighborAffected =
                (neighbor && neighbor->authored) ||
                cleanupCells.contains(neighborCell);
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
            const bool rebuildBoundary =
                ownerAffected || neighborAffected ||
                !sourceBoundaryMatches;

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
                .end = node(1u),
                .rebuilt = rebuildBoundary,
                // Recovered ramp junctions use source-specific diagonal and
                // underside carriers. They can still be rebuilt by a direct
                // ramp edit, but a neighboring flat edit must not promote
                // them as if they were interchangeable flat ledge modules.
                .propagationCompatible =
                    tile.shape == "flat" &&
                    (!neighbor || neighbor->shape == "flat")});
        }
    }
    std::sort(pending.begin(), pending.end(), edgeLess);

    std::map<BoundaryNode, std::vector<std::size_t>> outgoing;
    for (std::size_t index = 0u; index < pending.size(); ++index) {
        outgoing[pending[index].start].push_back(index);
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

    // A grid vertex can expose more than one level-compatible continuation.
    // Keep the raised owner on the right of the directed contour: take a
    // convex turn first, continue straight second, and take a concave turn
    // only when neither of those exists. Selecting this topology once keeps
    // propagation, join metadata, and emitted corner geometry in agreement.
    constexpr std::size_t kNoEdge =
        std::numeric_limits<std::size_t>::max();
    const auto joinRank = [](Join join) {
        switch (join) {
        case Join::Convex:
            return 0u;
        case Join::Straight:
            return 1u;
        case Join::Concave:
            return 2u;
        default:
            return 3u;
        }
    };
    std::vector<std::size_t> selectedSuccessor(
        pending.size(), kNoEdge);
    std::vector<std::vector<std::size_t>> selectedPredecessors(
        pending.size());
    for (std::size_t index = 0u; index < pending.size(); ++index) {
        const auto candidates = outgoing.find(pending[index].end);
        if (candidates == outgoing.end()) {
            continue;
        }
        std::uint32_t bestRank = 3u;
        for (const std::size_t candidate : candidates->second) {
            const std::uint32_t rank = joinRank(
                joinFor(pending[index], pending[candidate]));
            if (rank >= bestRank) {
                continue;
            }
            bestRank = rank;
            selectedSuccessor[index] = candidate;
        }
        if (selectedSuccessor[index] != kNoEdge) {
            selectedPredecessors[selectedSuccessor[index]].push_back(index);
        }
    }

    // A generated carrier and its canonical neighbor are not interchangeable
    // metre pieces: the recovered source rows can terminate on opposite sides
    // of the logical plane and use a different longitudinal material phase.
    // Grow every invalidated edge through the complete compatible boundary
    // component before contour geometry is emitted. This fixed-point pass
    // gives straight, convex, and concave neighbors one geometry owner and
    // prevents repairing a corner from merely moving the crack one tile away.
    std::vector<std::size_t> rebuildQueue;
    rebuildQueue.reserve(pending.size());
    for (std::size_t index = 0u; index < pending.size(); ++index) {
        if (pending[index].rebuilt) {
            rebuildQueue.push_back(index);
        }
    }
    const auto inheritCompatible = [&pending, &rebuildQueue](
                                       std::size_t predecessor,
                                       std::size_t successor,
                                       std::size_t inherited) {
        if (pending[inherited].rebuilt ||
            !pending[predecessor].propagationCompatible ||
            !pending[successor].propagationCompatible ||
            joinFor(
                pending[predecessor], pending[successor]) == Join::Open) {
            return;
        }
        pending[inherited].rebuilt = true;
        pending[inherited].edge.rebuildsJoinedSourceBoundary = true;
        rebuildQueue.push_back(inherited);
    };
    for (std::size_t cursor = 0u;
         cursor < rebuildQueue.size();
         ++cursor) {
        const std::size_t current = rebuildQueue[cursor];
        const std::size_t successor = selectedSuccessor[current];
        if (successor != kNoEdge) {
            inheritCompatible(current, successor, successor);
        }
        for (const std::size_t predecessor :
             selectedPredecessors[current]) {
            inheritCompatible(predecessor, current, predecessor);
        }
    }

    std::vector<bool> visited(pending.size(), false);
    Resolution resolution;
    resolution.edges.reserve(std::count_if(
        pending.begin(),
        pending.end(),
        [](const PendingEdge& edge) { return edge.rebuilt; }));
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

            const std::size_t next = selectedSuccessor[current];
            // Join metadata belongs to the generated contour, not merely to
            // the abstract source topology. A compatible flat rebuild can
            // stop before a ramp-specific source carrier; treating that
            // unbuilt successor as a convex join reserves a corner footprint
            // for geometry that is never emitted and opens a triangular hole.
            if (next == kNoEdge || !pending[next].rebuilt ||
                visited[next]) {
                break;
            }
            current = next;
        }
        const auto applyJoin = [&](std::size_t incomingPosition,
                                   std::size_t outgoingPosition) {
            const std::size_t incoming =
                contourEdges[incomingPosition];
            const std::size_t outgoingEdge =
                contourEdges[outgoingPosition];
            const Join join = joinFor(
                pending[incoming], pending[outgoingEdge]);
            pending[incoming].edge.endJoin = join;
            pending[outgoingEdge].edge.startJoin = join;
        };
        for (std::size_t index = 0u;
             index + 1u < contourEdges.size();
             ++index) {
            applyJoin(index, index + 1u);
        }
        if (contourEdges.size() > 1u) {
            const std::size_t incoming = contourEdges.back();
            const std::size_t outgoingEdge = contourEdges.front();
            const Join join = joinFor(
                pending[incoming], pending[outgoingEdge]);
            if (join != Join::Open) {
                applyJoin(contourEdges.size() - 1u, 0u);
            }
        }
        float materialDistanceCm = 0.0f;
        for (const std::size_t edgeIndex : contourEdges) {
            auto& edge = pending[edgeIndex].edge;
            edge.materialContourStartCm = materialDistanceCm;
            materialDistanceCm += materialStraightLengthCm(
                edge.startJoin,
                edge.endJoin);
            if (edge.endJoin == Join::Convex) {
                materialDistanceCm += kConvexCornerArcLengthCm;
            }
        }
    };

    const auto hasRebuiltPredecessor =
        [&](std::size_t index) {
            return std::any_of(
                selectedPredecessors[index].begin(),
                selectedPredecessors[index].end(),
                [&](std::size_t predecessor) {
                    return pending[predecessor].rebuilt;
                });
        };
    for (std::size_t index = 0u; index < pending.size(); ++index) {
        if (pending[index].rebuilt && !visited[index] &&
            !hasRebuiltPredecessor(index)) {
            traceContour(index);
        }
    }
    for (std::size_t index = 0u; index < pending.size(); ++index) {
        if (pending[index].rebuilt && !visited[index]) {
            traceContour(index);
        }
    }
    for (const auto& edge : pending) {
        if (edge.rebuilt) {
            resolution.edges.push_back(edge.edge);
        }
    }
    const auto resolvedEdgeLess = [](
                                      const RebuiltEdge& left,
                                      const RebuiltEdge& right) {
        return std::tie(
                   left.ownerCell.second,
                   left.ownerCell.first,
                   left.edge) <
            std::tie(
                   right.ownerCell.second,
                   right.ownerCell.first,
                   right.edge);
    };
    std::sort(
        resolution.edges.begin(),
        resolution.edges.end(),
        resolvedEdgeLess);
    return resolution;
}

float endpointAlongCm(
    Join join,
    bool start,
    float outwardCm) noexcept {
    constexpr float kHalfEdgeCm = kTerrainTileSizeCm * 0.5f;
    const float inset = join == Join::Concave
        ? std::clamp(std::abs(outwardCm), 0.0f, kHalfEdgeCm)
        : (join == Join::Convex
            ? kConvexCornerRadiusCm
            : 0.0f);
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

bool formsConvexCorner(
    const RebuiltEdge* incoming,
    const RebuiltEdge* outgoing) noexcept {
    return incoming && outgoing &&
        incoming->contourIndex == outgoing->contourIndex &&
        incoming->endJoin == Join::Convex &&
        outgoing->startJoin == Join::Convex;
}

} // namespace game::runtime::route1_terrain_ledges
