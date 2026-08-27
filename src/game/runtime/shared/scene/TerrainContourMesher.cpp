#include "game/runtime/shared/scene/TerrainContourMesher.h"

#include <algorithm>
#include <cmath>

namespace game::runtime::terrain_contours {
namespace {

constexpr float kMinimumAreaTwiceCm2 = 0.0001f;

float signedAreaTwice(
    const Point& first,
    const Point& second,
    const Point& third) noexcept {
    return (second.x - first.x) * (third.z - first.z) -
        (second.z - first.z) * (third.x - first.x);
}

void appendTriangle(
    Mesh& mesh,
    std::uint32_t first,
    std::uint32_t second,
    std::uint32_t third) {
    const float area = signedAreaTwice(
        mesh.vertices[first],
        mesh.vertices[second],
        mesh.vertices[third]);
    if (std::abs(area) <= kMinimumAreaTwiceCm2) {
        return;
    }
    if (area < 0.0f) {
        std::swap(second, third);
    }
    mesh.indices.insert(mesh.indices.end(), {first, second, third});
}

Point normalizedBlend(
    Point first,
    Point second,
    float angle) noexcept {
    Point result{
        first.x * std::cos(angle) + second.x * std::sin(angle),
        first.z * std::cos(angle) + second.z * std::sin(angle)};
    const float length = std::sqrt(
        result.x * result.x + result.z * result.z);
    if (length > 0.000001f) {
        result.x /= length;
        result.z /= length;
    }
    return result;
}

} // namespace

Mesh makeStrip(const std::vector<StripSample>& samples) {
    Mesh mesh;
    if (samples.size() < 2u) {
        return mesh;
    }
    mesh.vertices.reserve(samples.size() * 2u);
    mesh.indices.reserve((samples.size() - 1u) * 6u);
    for (const auto& sample : samples) {
        mesh.vertices.push_back(sample.outer);
    }
    for (const auto& sample : samples) {
        mesh.vertices.push_back(sample.inner);
    }
    const auto rowWidth = static_cast<std::uint32_t>(samples.size());
    for (std::uint32_t sample = 0u;
         sample + 1u < rowWidth;
         ++sample) {
        const std::uint32_t outerLeft = sample;
        const std::uint32_t outerRight = sample + 1u;
        const std::uint32_t innerLeft = rowWidth + sample;
        const std::uint32_t innerRight = innerLeft + 1u;
        appendTriangle(
            mesh, outerLeft, outerRight, innerRight);
        appendTriangle(
            mesh, outerLeft, innerRight, innerLeft);
    }
    return mesh;
}

Mesh makeCappedStrip(
    const std::vector<StripSample>& samples,
    Point capCenter) {
    Mesh mesh = makeStrip(samples);
    if (samples.size() < 2u) {
        return mesh;
    }
    const auto rowWidth = static_cast<std::uint32_t>(samples.size());
    const auto centerIndex = static_cast<std::uint32_t>(
        mesh.vertices.size());
    mesh.vertices.push_back(capCenter);
    mesh.indices.reserve(
        mesh.indices.size() + (samples.size() - 1u) * 3u);
    for (std::uint32_t sample = 0u;
         sample + 1u < rowWidth;
         ++sample) {
        appendTriangle(
            mesh,
            centerIndex,
            rowWidth + sample,
            rowWidth + sample + 1u);
    }
    return mesh;
}

Mesh makeConvexFootPocket(
    Point logicalCorner,
    Point center,
    Point startOutward,
    Point endOutward,
    float radiusCm,
    std::uint32_t segments) {
    Mesh mesh;
    if (radiusCm <= 0.0f || segments == 0u) {
        return mesh;
    }
    constexpr float kHalfPi = 1.57079632679489661923f;
    mesh.vertices.reserve(static_cast<std::size_t>(segments) + 2u);
    mesh.indices.reserve(static_cast<std::size_t>(segments) * 3u);
    mesh.vertices.push_back(logicalCorner);
    for (std::uint32_t sample = 0u; sample <= segments; ++sample) {
        const float phase = static_cast<float>(sample) /
            static_cast<float>(segments);
        const Point outward = normalizedBlend(
            startOutward, endOutward, phase * kHalfPi);
        mesh.vertices.push_back({
            center.x + outward.x * radiusCm,
            center.z + outward.z * radiusCm});
    }
    for (std::uint32_t sample = 0u; sample < segments; ++sample) {
        appendTriangle(mesh, 0u, sample + 1u, sample + 2u);
    }
    return mesh;
}

Validation validate(const Mesh& mesh) noexcept {
    Validation result;
    if (mesh.indices.size() % 3u != 0u) {
        result.valid = false;
        ++result.invalidIndexCount;
    }
    for (std::size_t index = 0u;
         index + 2u < mesh.indices.size();
         index += 3u) {
        const std::uint32_t first = mesh.indices[index];
        const std::uint32_t second = mesh.indices[index + 1u];
        const std::uint32_t third = mesh.indices[index + 2u];
        if (first >= mesh.vertices.size() ||
            second >= mesh.vertices.size() ||
            third >= mesh.vertices.size()) {
            result.valid = false;
            ++result.invalidIndexCount;
            continue;
        }
        const float area = signedAreaTwice(
            mesh.vertices[first],
            mesh.vertices[second],
            mesh.vertices[third]);
        if (std::abs(area) <= kMinimumAreaTwiceCm2) {
            result.valid = false;
            ++result.degenerateTriangleCount;
        } else if (area < 0.0f) {
            result.valid = false;
            ++result.inconsistentWindingCount;
        }
    }
    return result;
}

} // namespace game::runtime::terrain_contours
