#pragma once

#include "game/runtime/shared/scene/Route1RuntimeEnvironment.h"

#include <cstddef>
#include <cstdint>
#include <set>
#include <utility>
#include <vector>

namespace game::runtime::route1_terrain_ledges {

using GridCell = std::pair<std::int32_t, std::int32_t>;

// The crown is roughly 27 cm inside the logical source boundary. Reserve a
// larger turn than that inset so every profile row follows a positive-radius
// quarter arc instead of folding back through the corner.
inline constexpr float kConvexCornerRadiusCm = 32.0f;
inline constexpr float kConvexCornerArcLengthCm =
    kConvexCornerRadiusCm * 1.57079632679489661923f;
// The decoded inside-turn crown advances by roughly one 27 cm quarter-turn.
// Keep that physical source-scale distance in the contour field even though
// the actual mesh is the source's asymmetric four-sample handoff.
inline constexpr float kConcaveCornerMaterialLengthCm =
    27.0f * 1.57079632679489661923f;

enum class Join : std::uint8_t {
    Open,
    Straight,
    Convex,
    Concave,
};

inline constexpr float materialStraightLengthCm(
    Join startJoin,
    Join endJoin) noexcept {
    const float startInset = startJoin == Join::Convex
        ? kConvexCornerRadiusCm
        : 0.0f;
    const float endInset = endJoin == Join::Convex
        ? kConvexCornerRadiusCm
        : 0.0f;
    return 100.0f - startInset - endInset;
}

struct RebuiltEdge {
    GridCell ownerCell{};
    std::size_t edge = 0u;
    route1_environment::TerrainSharedEdgeProfile profile;
    std::uint32_t contourIndex = 0u;
    // Logical source-grid distance drives the per-metre organic wobble.
    float contourStartCm = 0.0f;
    // Physical carrier distance drives tangential material coordinates. It
    // excludes the straight portions reserved by convex joins and includes
    // the intervening quarter-arc, preventing a single texture slice from
    // being stretched around an entire corner.
    float materialContourStartCm = 0.0f;
    Join startJoin = Join::Open;
    Join endJoin = Join::Open;
    // A neighboring edit can invalidate one member of a compatible boundary
    // component. Mark every source edge inherited by the fixed-point rebuild
    // so its old cap and carrier can be retired with the generated contour.
    bool rebuildsJoinedSourceBoundary = false;
};

struct Resolution {
    std::vector<RebuiltEdge> edges;
    std::uint32_t contourCount = 0u;
};

// Derives every cliff/fringe edge invalidated by authored terrain and chains
// compatible directed edges into deterministic contours. Render geometry uses
// contourStartCm instead of restarting or mirroring its texture field per tile.
Resolution resolve(
    const std::vector<route1_environment::TerrainTileState>& tiles,
    const std::vector<route1_environment::TerrainTileState>& sourceTiles,
    const std::set<GridCell>& cleanupCells);

const RebuiltEdge* find(
    const Resolution& resolution,
    GridCell ownerCell,
    std::size_t edge) noexcept;

// A rounded corner carrier is valid only when both participating edges were
// resolved as the two halves of the same convex handoff. An open map-boundary
// endpoint deliberately has no corner carrier.
bool formsConvexCorner(
    const RebuiltEdge* incoming,
    const RebuiltEdge* outgoing) noexcept;

// Finds the selected outgoing half of a resolved inside turn. The contour
// resolver deliberately owns this topology; render code must not guess a
// neighboring edge merely from cell adjacency at a multi-edge grid node.
const RebuiltEdge* findConcaveSuccessor(
    const Resolution& resolution,
    const RebuiltEdge& incoming) noexcept;

// Returns the generic longitudinal endpoint for an edge carrier. The renderer
// replaces concave endpoints with the decoded source-profile reservations;
// extending perpendicular strips to row-specific plane intersections makes
// them overlap into a tapered brown spear and stacked alpha-tested leaf sheets.
// Outside/convex turns reserve the recovered source-scale corner radius.
float endpointAlongCm(
    Join join,
    bool start,
    float outwardCm) noexcept;

} // namespace game::runtime::route1_terrain_ledges
