#include "game/runtime/shared/scene/Route1TerrainContourAssembler.h"

#include <array>
#include <cmath>
#include <string>

namespace {

bool near(
    game::runtime::terrain_contours::Point first,
    game::runtime::terrain_contours::Point second,
    float tolerance = 0.001f) {
    return std::abs(first.x - second.x) <= tolerance &&
        std::abs(first.z - second.z) <= tolerance;
}

} // namespace

bool test_route1_terrain_contour_assembler_contract(
    std::string& outFail) {
    namespace ledges = game::runtime::route1_terrain_ledges;
    namespace contours = game::runtime::route1_terrain_contours;

    ledges::Resolution resolution;
    resolution.contourCount = 1u;
    resolution.edges.push_back(ledges::RebuiltEdge{
        .ownerCell = {25, -4},
        .edge = 0u,
        .contourIndex = 0u,
        .contourStartCm = 0.0f,
        .materialContourStartCm = 0.0f,
        .startJoin = ledges::Join::Open,
        .endJoin = ledges::Join::Convex});
    const float secondMaterialStart =
        ledges::materialStraightLengthCm(
            ledges::Join::Open, ledges::Join::Convex) +
        ledges::kConvexCornerArcLengthCm;
    resolution.edges.push_back(ledges::RebuiltEdge{
        .ownerCell = {25, -4},
        .edge = 1u,
        .contourIndex = 0u,
        .contourStartCm = 100.0f,
        .materialContourStartCm = secondMaterialStart,
        .startJoin = ledges::Join::Convex,
        .endJoin = ledges::Join::Open});

    const auto assembly = contours::assemble(resolution, 8u, 8u);
    if (!assembly.validation.valid || assembly.edges.size() != 2u ||
        assembly.convexTurns.size() != 1u) {
        outFail =
            "A resolved outside turn must produce one validated shared contour frame.";
        return false;
    }
    const auto* incoming = contours::findEdge(
        assembly, {25, -4}, 0u);
    const auto* outgoing = contours::findEdge(
        assembly, {25, -4}, 1u);
    const auto* turn = contours::findConvexTurn(
        assembly, {25, -4}, 0u);
    if (!incoming || !outgoing || !turn ||
        incoming->frames.size() != 9u ||
        outgoing->frames.size() != 9u ||
        turn->frames.size() != 9u) {
        outFail =
            "Every carrier must be able to address the same sampled straight and turn spans.";
        return false;
    }

    // These offsets cover the inner/outer lawn gasket, the cliff rows, the
    // leafy crown, and the low-side foot. If every offset closes, separate
    // material batches cannot reopen a positional crack at the turn.
    constexpr std::array<float, 9> carrierOffsets{
        -29.0f, -27.0f, -22.0f, -5.0f, 0.0f,
        3.0f, 10.0f, 20.0f, 25.0f};
    for (const float offsetCm : carrierOffsets) {
        if (!near(
                contours::offset(incoming->frames.back(), offsetCm),
                contours::offset(turn->frames.front(), offsetCm)) ||
            !near(
                contours::offset(turn->frames.back(), offsetCm),
                contours::offset(outgoing->frames.front(), offsetCm))) {
            outFail =
                "Lawn, wall, foliage, and foot rows must share exact turn endpoints at every profile offset.";
            return false;
        }
    }
    if (std::abs(
            turn->frames.back().materialContourCm -
            outgoing->frames.front().materialContourCm) > 0.001f) {
        outFail =
            "The source material field must advance continuously across a convex turn.";
        return false;
    }

    ledges::Resolution broken = resolution;
    broken.edges.pop_back();
    const auto brokenAssembly = contours::assemble(broken, 8u, 8u);
    if (brokenAssembly.validation.valid ||
        brokenAssembly.validation.missingTurnPartners != 1u) {
        outFail =
            "A missing corner partner must fail topology validation instead of emitting a partial turn.";
        return false;
    }
    return true;
}
