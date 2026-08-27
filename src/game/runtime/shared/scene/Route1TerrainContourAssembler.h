#pragma once

#include "game/runtime/shared/scene/Route1TerrainLedgeResolver.h"
#include "game/runtime/shared/scene/TerrainContourMesher.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace game::runtime::route1_terrain_contours {

using GridCell = route1_terrain_ledges::GridCell;
using terrain_contours::Point;

// One geometric frame is shared by every visible carrier attached to a
// rebuilt ledge: lawn crown, leafy fringe, cliff wall, and low-side foot.
// Materials may remain separate draw batches, but they may no longer invent
// independent straight/corner coordinates.
struct Frame {
    Point position;
    Point outward;
    Point tangent;
    float logicalContourCm = 0.0f;
    float materialContourCm = 0.0f;
};

struct EdgeSpan {
    GridCell ownerCell{};
    std::size_t edge = 0u;
    std::uint32_t contourIndex = 0u;
    route1_terrain_ledges::Join startJoin =
        route1_terrain_ledges::Join::Open;
    route1_terrain_ledges::Join endJoin =
        route1_terrain_ledges::Join::Open;
    std::vector<Frame> frames;
};

struct ConvexTurn {
    GridCell ownerCell{};
    std::size_t corner = 0u;
    std::uint32_t contourIndex = 0u;
    GridCell outgoingOwnerCell{};
    std::size_t outgoingEdge = 0u;
    Point logicalCorner;
    Point center;
    std::vector<Frame> frames;
};

// A renderable contour is assembled once from its ordered straight spans and
// convex turns. Consumers receive one welded frame sequence rather than
// independently triangulating each tile edge and corner.
struct ContourRun {
    std::uint32_t contourIndex = 0u;
    std::vector<Frame> frames;
    bool closed = false;
};

struct Validation {
    bool valid = true;
    std::uint32_t missingEdgeSamples = 0u;
    std::uint32_t missingTurnPartners = 0u;
    std::uint32_t disconnectedTurnEndpoints = 0u;
    std::uint32_t disconnectedCarrierRows = 0u;
    std::uint32_t discontinuousTurnNormals = 0u;
    std::uint32_t duplicateTurnOwners = 0u;
    std::uint32_t disconnectedContourRuns = 0u;
};

struct Assembly {
    std::vector<EdgeSpan> edges;
    std::vector<ConvexTurn> convexTurns;
    std::vector<ContourRun> runs;
    Validation validation;
};

Assembly assemble(
    const route1_terrain_ledges::Resolution& resolution,
    std::uint32_t edgeSegments,
    std::uint32_t cornerSegments);

const EdgeSpan* findEdge(
    const Assembly& assembly,
    GridCell ownerCell,
    std::size_t edge) noexcept;

const ConvexTurn* findConvexTurn(
    const Assembly& assembly,
    GridCell ownerCell,
    std::size_t corner) noexcept;

const ContourRun* findRun(
    const Assembly& assembly,
    std::uint32_t contourIndex) noexcept;

Point offset(const Frame& frame, float outwardCm) noexcept;

} // namespace game::runtime::route1_terrain_contours
