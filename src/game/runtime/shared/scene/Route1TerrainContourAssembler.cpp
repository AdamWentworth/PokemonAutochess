#include "game/runtime/shared/scene/Route1TerrainContourAssembler.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <tuple>

namespace game::runtime::route1_terrain_contours {
namespace {

constexpr float kTileSizeCm = 100.0f;
constexpr float kHalfTileCm = kTileSizeCm * 0.5f;
constexpr float kHalfPi = 1.57079632679489661923f;
constexpr float kJoinToleranceCm = 0.001f;

constexpr std::array<Point, 4> kOutward{{
    {0.0f, 1.0f},
    {1.0f, 0.0f},
    {0.0f, -1.0f},
    {-1.0f, 0.0f},
}};

constexpr std::array<Point, 4> kTangent{{
    {1.0f, 0.0f},
    {0.0f, -1.0f},
    {-1.0f, 0.0f},
    {0.0f, 1.0f},
}};

constexpr std::array<std::array<std::array<std::int32_t, 2>, 2>, 4>
    kEndpointOffsets{{
        {{{0, 1}, {1, 1}}},
        {{{1, 1}, {1, 0}}},
        {{{1, 0}, {0, 0}}},
        {{{0, 0}, {0, 1}}},
    }};

Point add(Point first, Point second) noexcept {
    return {first.x + second.x, first.z + second.z};
}

Point multiply(Point point, float scalar) noexcept {
    return {point.x * scalar, point.z * scalar};
}

float distance(Point first, Point second) noexcept {
    const float x = second.x - first.x;
    const float z = second.z - first.z;
    return std::sqrt(x * x + z * z);
}

Point endpointNode(
    const route1_terrain_ledges::RebuiltEdge& edge,
    std::size_t endpoint) noexcept {
    return {
        static_cast<float>(
            edge.ownerCell.first +
            kEndpointOffsets[edge.edge][endpoint][0]) *
            kTileSizeCm,
        static_cast<float>(
            edge.ownerCell.second +
            kEndpointOffsets[edge.edge][endpoint][1]) *
            kTileSizeCm};
}

bool sameNode(
    const route1_terrain_ledges::RebuiltEdge& incoming,
    const route1_terrain_ledges::RebuiltEdge& outgoing) noexcept {
    const Point end = endpointNode(incoming, 1u);
    const Point start = endpointNode(outgoing, 0u);
    return distance(end, start) <= kJoinToleranceCm;
}

const route1_terrain_ledges::RebuiltEdge* convexSuccessor(
    const route1_terrain_ledges::Resolution& resolution,
    const route1_terrain_ledges::RebuiltEdge& incoming) noexcept {
    const auto found = std::find_if(
        resolution.edges.begin(),
        resolution.edges.end(),
        [&](const auto& candidate) {
            return route1_terrain_ledges::formsConvexCorner(
                       &incoming, &candidate) &&
                sameNode(incoming, candidate);
        });
    return found == resolution.edges.end() ? nullptr : &*found;
}

bool near(Point first, Point second) noexcept {
    return distance(first, second) <= kJoinToleranceCm;
}

} // namespace

Assembly assemble(
    const route1_terrain_ledges::Resolution& resolution,
    std::uint32_t edgeSegments,
    std::uint32_t cornerSegments) {
    Assembly result;
    if (edgeSegments == 0u || cornerSegments == 0u) {
        result.validation.valid = false;
        result.validation.missingEdgeSamples =
            static_cast<std::uint32_t>(resolution.edges.size());
        return result;
    }

    result.edges.reserve(resolution.edges.size());
    for (const auto& edge : resolution.edges) {
        if (edge.edge >= kOutward.size()) {
            result.validation.valid = false;
            ++result.validation.missingEdgeSamples;
            continue;
        }
        EdgeSpan span{
            .ownerCell = edge.ownerCell,
            .edge = edge.edge,
            .contourIndex = edge.contourIndex,
            .startJoin = edge.startJoin,
            .endJoin = edge.endJoin};
        span.frames.reserve(edgeSegments + 1u);
        const Point outward = kOutward[edge.edge];
        const Point tangent = kTangent[edge.edge];
        const Point tileCenter{
            (static_cast<float>(edge.ownerCell.first) + 0.5f) *
                kTileSizeCm,
            (static_cast<float>(edge.ownerCell.second) + 0.5f) *
                kTileSizeCm};
        const Point boundaryCenter = add(
            tileCenter, multiply(outward, kHalfTileCm));
        const float startAlong =
            route1_terrain_ledges::endpointAlongCm(
                edge.startJoin, true, 0.0f);
        const float endAlong =
            route1_terrain_ledges::endpointAlongCm(
                edge.endJoin, false, 0.0f);
        const float materialLength =
            route1_terrain_ledges::materialStraightLengthCm(
                edge.startJoin, edge.endJoin);
        for (std::uint32_t sample = 0u;
             sample <= edgeSegments;
             ++sample) {
            const float phase = static_cast<float>(sample) /
                static_cast<float>(edgeSegments);
            span.frames.push_back(Frame{
                .position = add(
                    boundaryCenter,
                    multiply(
                        tangent,
                        std::lerp(startAlong, endAlong, phase))),
                .outward = outward,
                .tangent = tangent,
                .logicalContourCm = edge.contourStartCm +
                    phase * kTileSizeCm,
                .materialContourCm = edge.materialContourStartCm +
                    phase * materialLength});
        }
        result.edges.push_back(std::move(span));
    }

    std::set<std::tuple<std::int32_t, std::int32_t, std::size_t>>
        turnOwners;
    for (const auto& incoming : resolution.edges) {
        if (incoming.edge >= kOutward.size() ||
            incoming.endJoin !=
                route1_terrain_ledges::Join::Convex) {
            continue;
        }
        const auto* outgoing = convexSuccessor(resolution, incoming);
        if (!outgoing || outgoing->edge >= kOutward.size()) {
            result.validation.valid = false;
            ++result.validation.missingTurnPartners;
            continue;
        }
        const auto owner = std::tuple{
            incoming.ownerCell.first,
            incoming.ownerCell.second,
            incoming.edge};
        if (!turnOwners.emplace(owner).second) {
            result.validation.valid = false;
            ++result.validation.duplicateTurnOwners;
            continue;
        }

        const Point logicalCorner = endpointNode(incoming, 1u);
        const Point incomingOutward = kOutward[incoming.edge];
        const Point outgoingOutward = kOutward[outgoing->edge];
        const Point center = add(
            logicalCorner,
            multiply(
                add(incomingOutward, outgoingOutward),
                -route1_terrain_ledges::kConvexCornerRadiusCm));
        ConvexTurn turn{
            .ownerCell = incoming.ownerCell,
            .corner = incoming.edge,
            .contourIndex = incoming.contourIndex,
            .outgoingOwnerCell = outgoing->ownerCell,
            .outgoingEdge = outgoing->edge,
            .logicalCorner = logicalCorner,
            .center = center};
        turn.frames.reserve(cornerSegments + 1u);
        const float materialStart = incoming.materialContourStartCm +
            route1_terrain_ledges::materialStraightLengthCm(
                incoming.startJoin, incoming.endJoin);
        for (std::uint32_t sample = 0u;
             sample <= cornerSegments;
             ++sample) {
            const float phase = static_cast<float>(sample) /
                static_cast<float>(cornerSegments);
            const float angle = phase * kHalfPi;
            Point outward{
                incomingOutward.x * std::cos(angle) +
                    outgoingOutward.x * std::sin(angle),
                incomingOutward.z * std::cos(angle) +
                    outgoingOutward.z * std::sin(angle)};
            const float length = std::sqrt(
                outward.x * outward.x + outward.z * outward.z);
            if (length > 0.000001f) {
                outward.x /= length;
                outward.z /= length;
            }
            const Point tangent{outward.z, -outward.x};
            turn.frames.push_back(Frame{
                .position = add(
                    center,
                    multiply(
                        outward,
                        route1_terrain_ledges::
                            kConvexCornerRadiusCm)),
                .outward = outward,
                .tangent = tangent,
                .logicalContourCm = incoming.contourStartCm +
                    kTileSizeCm +
                    phase * route1_terrain_ledges::
                        kConvexCornerArcLengthCm,
                .materialContourCm = materialStart +
                    phase * route1_terrain_ledges::
                        kConvexCornerArcLengthCm});
        }

        const auto* incomingSpan = findEdge(result, incoming.ownerCell,
                                             incoming.edge);
        const auto* outgoingSpan = findEdge(result, outgoing->ownerCell,
                                             outgoing->edge);
        if (!incomingSpan || !outgoingSpan ||
            incomingSpan->frames.empty() ||
            outgoingSpan->frames.empty() || turn.frames.empty() ||
            !near(incomingSpan->frames.back().position,
                  turn.frames.front().position) ||
            !near(turn.frames.back().position,
                  outgoingSpan->frames.front().position)) {
            result.validation.valid = false;
            ++result.validation.disconnectedTurnEndpoints;
        }
        constexpr std::array<float, 9> kCarrierOffsetsCm{
            -29.0f, -27.0f, -22.0f, -5.0f, 0.0f,
            3.0f, 10.0f, 20.0f, 25.0f};
        if (incomingSpan && outgoingSpan &&
            !incomingSpan->frames.empty() &&
            !outgoingSpan->frames.empty() && !turn.frames.empty()) {
            for (const float carrierOffsetCm : kCarrierOffsetsCm) {
                if (!near(
                        offset(
                            incomingSpan->frames.back(),
                            carrierOffsetCm),
                        offset(
                            turn.frames.front(),
                            carrierOffsetCm)) ||
                    !near(
                        offset(
                            turn.frames.back(),
                            carrierOffsetCm),
                        offset(
                            outgoingSpan->frames.front(),
                            carrierOffsetCm))) {
                    result.validation.valid = false;
                    ++result.validation.disconnectedCarrierRows;
                }
            }
        }
        if (turn.frames.empty() ||
            !near(turn.frames.front().outward, incomingOutward) ||
            !near(turn.frames.back().outward, outgoingOutward)) {
            result.validation.valid = false;
            ++result.validation.discontinuousTurnNormals;
        }
        result.convexTurns.push_back(std::move(turn));
    }
    return result;
}

const EdgeSpan* findEdge(
    const Assembly& assembly,
    GridCell ownerCell,
    std::size_t edge) noexcept {
    const auto found = std::find_if(
        assembly.edges.begin(),
        assembly.edges.end(),
        [&](const EdgeSpan& candidate) {
            return candidate.ownerCell == ownerCell &&
                candidate.edge == edge;
        });
    return found == assembly.edges.end() ? nullptr : &*found;
}

const ConvexTurn* findConvexTurn(
    const Assembly& assembly,
    GridCell ownerCell,
    std::size_t corner) noexcept {
    const auto found = std::find_if(
        assembly.convexTurns.begin(),
        assembly.convexTurns.end(),
        [&](const ConvexTurn& candidate) {
            return candidate.ownerCell == ownerCell &&
                candidate.corner == corner;
        });
    return found == assembly.convexTurns.end() ? nullptr : &*found;
}

Point offset(const Frame& frame, float outwardCm) noexcept {
    return {
        frame.position.x + frame.outward.x * outwardCm,
        frame.position.z + frame.outward.z * outwardCm};
}

} // namespace game::runtime::route1_terrain_contours
