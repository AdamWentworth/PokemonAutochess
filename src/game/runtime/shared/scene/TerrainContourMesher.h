#pragma once

#include <cstdint>
#include <vector>

namespace game::runtime::terrain_contours {

struct Point {
    float x = 0.0f;
    float z = 0.0f;
};

struct StripSample {
    Point outer;
    Point inner;
};

struct Mesh {
    std::vector<Point> vertices;
    std::vector<std::uint32_t> indices;
};

struct Validation {
    bool valid = true;
    std::uint32_t invalidIndexCount = 0u;
    std::uint32_t degenerateTriangleCount = 0u;
    std::uint32_t inconsistentWindingCount = 0u;
};

// Triangulates a sampled contour strip without inventing a centre vertex.
// Straight ledges and their turns use this same primitive so both sides of a
// join can submit the exact same two endpoint vertices.
Mesh makeStrip(const std::vector<StripSample>& samples);

// Closes the inside of a curved strip with an explicit, bounded fan. This is
// used only when the surface behind the contour was deliberately retired; the
// straight strips still meet the fan at a positive-radius shared endpoint and
// therefore cannot collapse into a long spike.
Mesh makeCappedStrip(
    const std::vector<StripSample>& samples,
    Point capCenter);

// Connects one explicit owner point to an already sampled open boundary.
// The caller owns the boundary geometry; this helper only triangulates it and
// therefore cannot subtly resample a ledge corner differently from its wall.
Mesh makeFan(
    Point owner,
    const std::vector<Point>& boundary);

// Builds the low-ground pocket behind a rounded convex ledge foot. The first
// vertex is the logical grid corner and the remaining vertices follow the
// wall-owned arc. This fills only the curved triangular void; it cannot grow
// into the broad rectangular safety slabs used by the legacy tile repair.
Mesh makeConvexFootPocket(
    Point logicalCorner,
    Point center,
    Point startOutward,
    Point endOutward,
    float radiusCm,
    std::uint32_t segments);

Validation validate(const Mesh& mesh) noexcept;

} // namespace game::runtime::terrain_contours
