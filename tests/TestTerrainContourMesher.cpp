#include "game/runtime/shared/scene/TerrainContourMesher.h"

#include <cmath>
#include <string>
#include <vector>

bool test_terrain_contour_mesher_contract(std::string& outFail) {
    namespace contours = game::runtime::terrain_contours;
    const std::vector<contours::StripSample> straight{
        {{0.0f, 10.0f}, {0.0f, 3.0f}},
        {{10.0f, 10.0f}, {10.0f, 3.0f}},
        {{20.0f, 10.0f}, {20.0f, 3.0f}}};
    const auto strip = contours::makeStrip(straight);
    const auto stripValidation = contours::validate(strip);
    if (!stripValidation.valid || strip.vertices.size() != 6u ||
        strip.indices.size() != 12u) {
        outFail =
            "Contour strips must triangulate adjacent samples without invalid or degenerate faces.";
        return false;
    }
    const auto cappedStrip = contours::makeCappedStrip(
        straight, {10.0f, 0.0f});
    const auto cappedValidation = contours::validate(cappedStrip);
    if (!cappedValidation.valid || cappedStrip.vertices.size() != 7u ||
        cappedStrip.indices.size() != 18u) {
        outFail =
            "A capped contour turn must close its bounded inner arc without collapsing the adjoining straight strip.";
        return false;
    }

    const auto pocket = contours::makeConvexFootPocket(
        {-50.0f, -50.0f},
        {-18.0f, -18.0f},
        {0.0f, -1.0f},
        {-1.0f, 0.0f},
        27.0f,
        8u);
    const auto pocketValidation = contours::validate(pocket);
    if (!pocketValidation.valid || pocket.vertices.size() != 10u ||
        pocket.indices.size() != 24u) {
        outFail =
            "Convex ledge feet must own one bounded, valid curved pocket.";
        return false;
    }
    for (std::size_t index = 1u; index < pocket.vertices.size(); ++index) {
        const float deltaX = pocket.vertices[index].x + 18.0f;
        const float deltaZ = pocket.vertices[index].z + 18.0f;
        const float radius = std::sqrt(
            deltaX * deltaX + deltaZ * deltaZ);
        if (std::abs(radius - 27.0f) > 0.001f) {
            outFail =
                "Every convex-foot boundary sample must remain on the wall-owned arc.";
            return false;
        }
    }
    return true;
}
