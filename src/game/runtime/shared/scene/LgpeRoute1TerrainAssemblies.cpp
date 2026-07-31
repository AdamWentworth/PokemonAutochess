#include "game/runtime/shared/scene/LgpeRoute1TerrainAssemblies.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

namespace game::runtime::lgpe_route1_terrain_assemblies {
namespace {

bool fail(std::string* outError, std::string message) {
    if (outError) {
        *outError = std::move(message);
    }
    return false;
}

class DisjointSet {
public:
    explicit DisjointSet(std::size_t size)
        : parent_(size), rank_(size, 0u) {
        std::iota(parent_.begin(), parent_.end(), 0u);
    }

    std::size_t find(std::size_t value) {
        if (parent_[value] != value) {
            parent_[value] = find(parent_[value]);
        }
        return parent_[value];
    }

    void unite(std::size_t left, std::size_t right) {
        left = find(left);
        right = find(right);
        if (left == right) {
            return;
        }
        if (rank_[left] < rank_[right]) {
            std::swap(left, right);
        }
        parent_[right] = left;
        if (rank_[left] == rank_[right]) {
            ++rank_[left];
        }
    }

private:
    std::vector<std::size_t> parent_;
    std::vector<std::uint8_t> rank_;
};

using PositionKey =
    std::tuple<std::int32_t, std::int32_t, std::int32_t>;

PositionKey positionKey(
    const engine::assets::lgpe::CanonicalVertex& vertex) {
    // One source millimetre keeps seam duplicates together without joining
    // neighbouring but intentionally disconnected Route 1 pieces.
    constexpr float kQuantization = 1000.0f;
    return {
        static_cast<std::int32_t>(
            std::lround(vertex.position[0] * kQuantization)),
        static_cast<std::int32_t>(
            std::lround(vertex.position[1] * kQuantization)),
        static_cast<std::int32_t>(
            std::lround(vertex.position[2] * kQuantization))};
}

struct Component {
    std::array<float, 3> minimum{
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max()};
    std::array<float, 3> maximum{
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest()};
    std::map<std::uint32_t, std::vector<std::uint32_t>>
        indicesByGroup;
    std::set<std::uint32_t> materials;
};

float center(const Component& component, std::size_t axis) {
    return (component.minimum[axis] +
            component.maximum[axis]) *
        0.5f;
}

float pairingScore(
    const Component& body,
    const Component& cap) {
    const float dx = center(body, 0u) - center(cap, 0u);
    const float dz = center(body, 2u) - center(cap, 2u);
    const float bodyWidth =
        std::max(1.0f, body.maximum[0] - body.minimum[0]);
    const float bodyDepth =
        std::max(1.0f, body.maximum[2] - body.minimum[2]);
    const float capWidth =
        std::max(1.0f, cap.maximum[0] - cap.minimum[0]);
    const float capDepth =
        std::max(1.0f, cap.maximum[2] - cap.minimum[2]);
    const float sizeMismatch =
        std::abs(bodyWidth - capWidth) +
        std::abs(bodyDepth - capDepth);
    return dx * dx + dz * dz +
        sizeMismatch * sizeMismatch * 0.25f;
}

} // namespace

std::uint32_t expectedAssemblyCount(
    std::uint32_t sourceMeshIndex) noexcept {
    constexpr std::array<std::uint32_t, 7> kCounts{
        1u, 1u, 2u, 11u, 2u, 2u, 4u};
    return sourceMeshIndex >= 29u &&
            sourceMeshIndex <= 35u
        ? kCounts[sourceMeshIndex - 29u]
        : 0u;
}

bool derivePartition(
    const engine::assets::lgpe::Mesh& mesh,
    MeshPartition& out,
    std::string* outError) {
    const std::uint32_t expected =
        expectedAssemblyCount(mesh.sourceIndex);
    if (expected == 0u || mesh.vertices.empty()) {
        return fail(
            outError,
            "Route 1 terrain partition requires source mesh 29-35 with geometry.");
    }

    DisjointSet sets(mesh.vertices.size());
    std::map<PositionKey, std::size_t> firstAtPosition;
    for (std::size_t vertexIndex = 0u;
         vertexIndex < mesh.vertices.size();
         ++vertexIndex) {
        const auto [found, inserted] =
            firstAtPosition.emplace(
                positionKey(mesh.vertices[vertexIndex]),
                vertexIndex);
        if (!inserted) {
            sets.unite(vertexIndex, found->second);
        }
    }
    for (const auto& group : mesh.polygonGroups) {
        if (group.indices.size() % 3u != 0u) {
            return fail(
                outError,
                "Route 1 terrain polygon group is not triangulated.");
        }
        for (std::size_t index = 0u;
             index < group.indices.size();
             index += 3u) {
            const std::uint32_t a = group.indices[index];
            const std::uint32_t b = group.indices[index + 1u];
            const std::uint32_t c = group.indices[index + 2u];
            if (a >= mesh.vertices.size() ||
                b >= mesh.vertices.size() ||
                c >= mesh.vertices.size()) {
                return fail(
                    outError,
                    "Route 1 terrain triangle index is out of range.");
            }
            sets.unite(a, b);
            sets.unite(a, c);
        }
    }

    std::map<std::size_t, Component> components;
    for (std::size_t groupIndex = 0u;
         groupIndex < mesh.polygonGroups.size();
         ++groupIndex) {
        const auto& group = mesh.polygonGroups[groupIndex];
        for (std::size_t index = 0u;
             index < group.indices.size();
             index += 3u) {
            const std::size_t root =
                sets.find(group.indices[index]);
            auto& component = components[root];
            component.materials.insert(group.materialIndex);
            auto& selected = component.indicesByGroup[
                static_cast<std::uint32_t>(groupIndex)];
            for (std::size_t corner = 0u; corner < 3u; ++corner) {
                const std::uint32_t vertexIndex =
                    group.indices[index + corner];
                selected.push_back(vertexIndex);
                const auto& position =
                    mesh.vertices[vertexIndex].position;
                for (std::size_t axis = 0u; axis < 3u; ++axis) {
                    component.minimum[axis] = std::min(
                        component.minimum[axis], position[axis]);
                    component.maximum[axis] = std::max(
                        component.maximum[axis], position[axis]);
                }
            }
        }
    }

    std::vector<const Component*> bodies;
    std::vector<const Component*> caps;
    for (const auto& [root, component] : components) {
        (void)root;
        const float verticalSpan =
            component.maximum[1] - component.minimum[1];
        if (verticalSpan >= 30.0f) {
            bodies.push_back(&component);
        } else {
            caps.push_back(&component);
        }
    }
    if (bodies.size() != expected ||
        caps.size() != expected) {
        return fail(
            outError,
            "Route 1 terrain body/cap topology changed for mesh " +
                std::to_string(mesh.sourceIndex) +
                ": expected " + std::to_string(expected) +
                " pairs, found " + std::to_string(bodies.size()) +
                " bodies and " + std::to_string(caps.size()) +
                " caps.");
    }

    struct Pair {
        const Component* body = nullptr;
        const Component* cap = nullptr;
    };
    std::vector<Pair> pairs;
    pairs.reserve(expected);
    std::set<std::size_t> usedCaps;
    for (const Component* body : bodies) {
        std::size_t bestIndex = 0u;
        float bestScore =
            std::numeric_limits<float>::max();
        for (std::size_t index = 0u;
             index < caps.size();
             ++index) {
            if (usedCaps.contains(index)) {
                continue;
            }
            const float score = pairingScore(*body, *caps[index]);
            if (score < bestScore) {
                bestScore = score;
                bestIndex = index;
            }
        }
        usedCaps.insert(bestIndex);
        pairs.push_back({body, caps[bestIndex]});
    }
    std::sort(
        pairs.begin(),
        pairs.end(),
        [](const Pair& left, const Pair& right) {
            const auto key = [](const Pair& pair) {
                return std::array<float, 3>{
                    std::min(
                        pair.body->minimum[0],
                        pair.cap->minimum[0]),
                    std::min(
                        pair.body->minimum[2],
                        pair.cap->minimum[2]),
                    std::min(
                        pair.body->minimum[1],
                        pair.cap->minimum[1])};
            };
            return key(left) < key(right);
        });

    MeshPartition derived;
    derived.sourceMeshIndex = mesh.sourceIndex;
    derived.assemblies.reserve(pairs.size());
    std::vector<std::size_t> selectedIndexCounts(
        mesh.polygonGroups.size(), 0u);
    for (std::size_t pairIndex = 0u;
         pairIndex < pairs.size();
         ++pairIndex) {
        const Pair& pair = pairs[pairIndex];
        Assembly assembly;
        assembly.assemblyIndex =
            static_cast<std::uint32_t>(pairIndex);
        for (std::size_t axis = 0u; axis < 3u; ++axis) {
            assembly.boundsMinimum[axis] = std::min(
                pair.body->minimum[axis],
                pair.cap->minimum[axis]);
            assembly.boundsMaximum[axis] = std::max(
                pair.body->maximum[axis],
                pair.cap->maximum[axis]);
        }
        assembly.sourcePivotCm = {
            (assembly.boundsMinimum[0] +
             assembly.boundsMaximum[0]) * 0.5f,
            assembly.boundsMinimum[1],
            (assembly.boundsMinimum[2] +
             assembly.boundsMaximum[2]) * 0.5f};
        const float capVerticalSpan =
            pair.cap->maximum[1] -
            pair.cap->minimum[1];
        assembly.profileRole =
            capVerticalSpan > 20.0f
            ? "source_ramp"
            : "source_ledge_or_raised_platform";

        std::set<std::uint32_t> groupIndices;
        for (const auto& [groupIndex, indices] :
             pair.body->indicesByGroup) {
            (void)indices;
            groupIndices.insert(groupIndex);
        }
        for (const auto& [groupIndex, indices] :
             pair.cap->indicesByGroup) {
            (void)indices;
            groupIndices.insert(groupIndex);
        }
        for (const std::uint32_t groupIndex : groupIndices) {
            PolygonGroupSelection selection{
                .polygonGroupIndex = groupIndex,
                .materialIndex =
                    mesh.polygonGroups[groupIndex]
                        .materialIndex};
            if (const auto found =
                    pair.body->indicesByGroup.find(groupIndex);
                found != pair.body->indicesByGroup.end()) {
                selection.indices.insert(
                    selection.indices.end(),
                    found->second.begin(),
                    found->second.end());
            }
            if (const auto found =
                    pair.cap->indicesByGroup.find(groupIndex);
                found != pair.cap->indicesByGroup.end()) {
                selection.indices.insert(
                    selection.indices.end(),
                    found->second.begin(),
                    found->second.end());
            }
            selectedIndexCounts[groupIndex] +=
                selection.indices.size();
            assembly.polygonGroups.push_back(
                std::move(selection));
        }
        derived.assemblies.push_back(std::move(assembly));
    }
    for (std::size_t groupIndex = 0u;
         groupIndex < mesh.polygonGroups.size();
         ++groupIndex) {
        if (selectedIndexCounts[groupIndex] !=
            mesh.polygonGroups[groupIndex].indices.size()) {
            return fail(
                outError,
                "Route 1 terrain partition did not preserve polygon group " +
                    std::to_string(groupIndex) + ".");
        }
    }

    out = std::move(derived);
    if (outError) {
        outError->clear();
    }
    return true;
}

} // namespace game::runtime::lgpe_route1_terrain_assemblies
