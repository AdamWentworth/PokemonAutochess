#include "game/runtime/shared/scene/Route1TreeInstances.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <utility>
#include <vector>

namespace game::runtime::route1_tree_instances {
namespace {

bool fail(std::string* outError, std::string message) {
    if (outError) {
        *outError = std::move(message);
    }
    return false;
}

} // namespace

std::uint32_t expectedInstanceCount(
    std::uint32_t sourceMeshIndex) noexcept {
    constexpr std::array<std::uint32_t, 6> kCounts{
        11u, 11u, 12u, 2u, 2u, 9u};
    return sourceMeshIndex >= 10u &&
            sourceMeshIndex <= 15u
        ? kCounts[sourceMeshIndex - 10u]
        : 0u;
}

bool derivePartition(
    const game::assets::published_environment::Mesh& mesh,
    std::uint32_t instanceCount,
    MeshPartition& out,
    std::string* outError) {
    if (instanceCount == 0u) {
        return fail(
            outError,
            "Route 1 tree partition requires at least one instance.");
    }
    const auto trunkGroup = std::find_if(
        mesh.polygonGroups.begin(),
        mesh.polygonGroups.end(),
        [](const auto& group) {
            return group.materialIndex == 2u;
        });
    if (trunkGroup == mesh.polygonGroups.end() ||
        trunkGroup->indices.empty()) {
        return fail(
            outError,
            "Route 1 tree has no trunk material topology.");
    }

    std::vector<std::int32_t> parents(
        mesh.vertices.size(),
        -1);
    const auto findRoot = [&](std::uint32_t value) {
        std::uint32_t root = value;
        while (parents[root] !=
               static_cast<std::int32_t>(root)) {
            root = static_cast<std::uint32_t>(
                parents[root]);
        }
        std::uint32_t current = value;
        while (current != root) {
            const std::uint32_t next =
                static_cast<std::uint32_t>(
                    parents[current]);
            parents[current] =
                static_cast<std::int32_t>(root);
            current = next;
        }
        return root;
    };
    const auto join =
        [&](std::uint32_t left, std::uint32_t right) {
            const std::uint32_t leftRoot =
                findRoot(left);
            const std::uint32_t rightRoot =
                findRoot(right);
            if (leftRoot != rightRoot) {
                parents[rightRoot] =
                    static_cast<std::int32_t>(
                        leftRoot);
            }
        };
    for (const std::uint32_t index :
         trunkGroup->indices) {
        if (index >= parents.size()) {
            return fail(
                outError,
                "Route 1 tree trunk index is out of range.");
        }
        parents[index] =
            static_cast<std::int32_t>(index);
    }
    for (std::size_t index = 0u;
         index + 2u < trunkGroup->indices.size();
         index += 3u) {
        join(
            trunkGroup->indices[index],
            trunkGroup->indices[index + 1u]);
        join(
            trunkGroup->indices[index],
            trunkGroup->indices[index + 2u]);
    }

    struct Component {
        std::uint32_t count = 0u;
        std::array<double, 3> sum{};
    };
    std::map<std::uint32_t, Component> components;
    for (std::uint32_t index = 0u;
         index < parents.size();
         ++index) {
        if (parents[index] < 0) {
            continue;
        }
        auto& component =
            components[findRoot(index)];
        ++component.count;
        for (std::size_t axis = 0u;
             axis < 3u;
             ++axis) {
            component.sum[axis] +=
                mesh.vertices[index].position[axis];
        }
    }
    std::uint32_t largestComponent = 0u;
    for (const auto& [root, component] :
         components) {
        (void)root;
        largestComponent = std::max(
            largestComponent,
            component.count);
    }
    if (largestComponent == 0u) {
        return fail(
            outError,
            "Route 1 tree trunk topology is empty.");
    }

    std::vector<std::array<double, 3>>
        candidateCenters;
    for (const auto& [root, component] :
         components) {
        (void)root;
        if (component.count != largestComponent) {
            continue;
        }
        candidateCenters.push_back({
            component.sum[0] / component.count,
            component.sum[1] / component.count,
            component.sum[2] / component.count});
    }
    std::sort(
        candidateCenters.begin(),
        candidateCenters.end(),
        [](const auto& left, const auto& right) {
            return left[0] != right[0]
                ? left[0] < right[0]
                : left[2] < right[2];
        });

    struct Cluster {
        std::array<double, 3> sum{};
        std::uint32_t count = 0u;
    };
    constexpr double kClusterRadiusCm = 100.0;
    std::vector<Cluster> clusters;
    for (const auto& center : candidateCenters) {
        const auto cluster = std::find_if(
            clusters.begin(),
            clusters.end(),
            [&](const Cluster& candidate) {
                const double x =
                    candidate.sum[0] /
                    candidate.count;
                const double z =
                    candidate.sum[2] /
                    candidate.count;
                const double dx = center[0] - x;
                const double dz = center[2] - z;
                return dx * dx + dz * dz <
                    kClusterRadiusCm *
                        kClusterRadiusCm;
            });
        if (cluster == clusters.end()) {
            clusters.push_back(
                Cluster{center, 1u});
            continue;
        }
        for (std::size_t axis = 0u;
             axis < 3u;
             ++axis) {
            cluster->sum[axis] += center[axis];
        }
        ++cluster->count;
    }
    if (clusters.size() != instanceCount) {
        return fail(
            outError,
            "Route 1 tree connected-instance count changed: "
            "expected " +
                std::to_string(instanceCount) +
                ", found " +
                std::to_string(clusters.size()) +
                ".");
    }

    MeshPartition derived;
    derived.sourceMeshIndex = mesh.sourceIndex;
    derived.largestTrunkComponentVertices =
        largestComponent;
    derived.polygonGroups.reserve(
        mesh.polygonGroups.size());
    for (std::size_t groupIndex = 0u;
         groupIndex < mesh.polygonGroups.size();
         ++groupIndex) {
        const auto& group =
            mesh.polygonGroups[groupIndex];
        if (group.indices.empty()) {
            return fail(
                outError,
                "Route 1 tree polygon group is empty.");
        }
        const auto [minimumIt, maximumIt] =
            std::minmax_element(
                group.indices.begin(),
                group.indices.end());
        const std::uint32_t minimum = *minimumIt;
        const std::uint32_t maximum = *maximumIt;
        if (maximum >= mesh.vertices.size()) {
            return fail(
                outError,
                "Route 1 tree polygon-group index is out of range.");
        }
        const std::uint32_t span =
            maximum - minimum + 1u;
        std::vector<bool> referenced(span, false);
        for (const std::uint32_t index :
             group.indices) {
            referenced[index - minimum] = true;
        }
        if (!std::all_of(
                referenced.begin(),
                referenced.end(),
                [](bool value) { return value; })) {
            return fail(
                outError,
                "Route 1 tree source vertex range is not contiguous.");
        }

        std::vector<std::uint32_t> boundaries{
            minimum};
        if (span % instanceCount == 0u) {
            const std::uint32_t block =
                span / instanceCount;
            for (std::uint32_t instance = 1u;
                 instance < instanceCount;
                 ++instance) {
                boundaries.push_back(
                    minimum +
                    instance * block);
            }
        } else {
            struct Gap {
                double distanceSquared = 0.0;
                std::uint32_t boundary = 0u;
            };
            std::vector<Gap> gaps;
            gaps.reserve(span - 1u);
            for (std::uint32_t index = minimum;
                 index < maximum;
                 ++index) {
                const auto& left =
                    mesh.vertices[index].position;
                const auto& right =
                    mesh.vertices[index + 1u]
                        .position;
                const double dx =
                    static_cast<double>(left[0]) -
                    right[0];
                const double dz =
                    static_cast<double>(left[2]) -
                    right[2];
                gaps.push_back(
                    Gap{
                        dx * dx + dz * dz,
                        index + 1u});
            }
            std::sort(
                gaps.begin(),
                gaps.end(),
                [](const Gap& left,
                   const Gap& right) {
                    return left.distanceSquared !=
                            right.distanceSquared
                        ? left.distanceSquared >
                              right.distanceSquared
                        : left.boundary <
                              right.boundary;
                });
            if (gaps.size() <
                instanceCount - 1u) {
                return fail(
                    outError,
                    "Route 1 tree source range has too few instance boundaries.");
            }
            for (std::uint32_t index = 0u;
                 index + 1u < instanceCount;
                 ++index) {
                boundaries.push_back(
                    gaps[index].boundary);
            }
            std::sort(
                boundaries.begin(),
                boundaries.end());
        }
        boundaries.push_back(maximum + 1u);
        if (boundaries.size() !=
            instanceCount + 1u) {
            return fail(
                outError,
                "Route 1 tree source instance boundary count changed.");
        }

        PolygonGroupPartition groupPartition;
        groupPartition.polygonGroupIndex =
            static_cast<std::uint32_t>(
                groupIndex);
        groupPartition.materialIndex =
            group.materialIndex;
        groupPartition.instances.reserve(
            instanceCount);
        for (std::uint32_t instance = 0u;
             instance < instanceCount;
             ++instance) {
            const std::uint32_t first =
                boundaries[instance];
            const std::uint32_t count =
                boundaries[instance + 1u] -
                first;
            if (count == 0u) {
                return fail(
                    outError,
                    "Route 1 tree source instance range is empty.");
            }
            VertexRange range{first, count, {}};
            for (std::uint32_t index = first;
                 index < first + count;
                 ++index) {
                for (std::size_t axis = 0u;
                     axis < 3u;
                     ++axis) {
                    range.center[axis] +=
                        mesh.vertices[index]
                            .position[axis];
                }
            }
            for (double& value : range.center) {
                value /= count;
            }
            groupPartition.instances.push_back(
                range);
        }
        derived.polygonGroups.push_back(
            std::move(groupPartition));
    }

    const auto trunkPartition = std::find_if(
        derived.polygonGroups.begin(),
        derived.polygonGroups.end(),
        [](const PolygonGroupPartition& group) {
            return group.materialIndex == 2u;
        });
    if (trunkPartition ==
        derived.polygonGroups.end()) {
        return fail(
            outError,
            "Route 1 tree source blocks have no trunk group.");
    }
    for (const auto& group :
         derived.polygonGroups) {
        for (std::size_t instance = 0u;
             instance < group.instances.size();
             ++instance) {
            std::size_t nearest = 0u;
            double nearestDistance =
                std::numeric_limits<double>::max();
            for (std::size_t candidate = 0u;
                 candidate <
                     trunkPartition->instances.size();
                 ++candidate) {
                const double dx =
                    group.instances[instance]
                        .center[0] -
                    trunkPartition
                        ->instances[candidate]
                        .center[0];
                const double dz =
                    group.instances[instance]
                        .center[2] -
                    trunkPartition
                        ->instances[candidate]
                        .center[2];
                const double distance =
                    dx * dx + dz * dz;
                if (distance < nearestDistance) {
                    nearest = candidate;
                    nearestDistance = distance;
                }
            }
            if (nearest != instance) {
                return fail(
                    outError,
                    "Route 1 tree material-instance ordering is inconsistent.");
            }
        }
    }

    derived.sourcePivotsCm.resize(
        instanceCount);
    for (std::uint32_t instance = 0u;
         instance < instanceCount;
         ++instance) {
        float minimumY =
            std::numeric_limits<float>::max();
        for (const auto& group :
             derived.polygonGroups) {
            const auto& range =
                group.instances[instance];
            for (std::uint32_t index =
                     range.first;
                 index <
                     range.first + range.count;
                 ++index) {
                minimumY = std::min(
                    minimumY,
                    mesh.vertices[index]
                        .position[1]);
            }
        }
        derived.sourcePivotsCm[instance] = {
            static_cast<float>(
                trunkPartition
                    ->instances[instance]
                    .center[0]),
            minimumY,
            static_cast<float>(
                trunkPartition
                    ->instances[instance]
                    .center[2])};
    }

    out = std::move(derived);
    if (outError) {
        outError->clear();
    }
    return true;
}

bool selectInstanceTriangles(
    const game::assets::published_environment::Mesh& mesh,
    const PolygonGroupPartition& partition,
    std::uint32_t instanceIndex,
    std::vector<std::uint32_t>& outIndices,
    std::string* outError) {
    if (partition.polygonGroupIndex >=
            mesh.polygonGroups.size() ||
        instanceIndex >=
            partition.instances.size()) {
        return fail(
            outError,
            "Route 1 tree instance selector is out of range.");
    }
    const auto& group =
        mesh.polygonGroups[
            partition.polygonGroupIndex];
    if (group.materialIndex !=
        partition.materialIndex) {
        return fail(
            outError,
            "Route 1 tree material partition changed.");
    }
    const auto& range =
        partition.instances[instanceIndex];
    const std::uint32_t end =
        range.first + range.count;
    outIndices.clear();
    outIndices.reserve(
        group.indices.size() /
            partition.instances.size() +
        3u);
    for (std::size_t index = 0u;
         index + 2u < group.indices.size();
         index += 3u) {
        const std::uint32_t a =
            group.indices[index];
        const std::uint32_t b =
            group.indices[index + 1u];
        const std::uint32_t c =
            group.indices[index + 2u];
        if (a >= mesh.vertices.size() ||
            b >= mesh.vertices.size() ||
            c >= mesh.vertices.size()) {
            return fail(
                outError,
                "Route 1 tree triangle index is out of range.");
        }
        const bool insideA =
            a >= range.first && a < end;
        const bool insideB =
            b >= range.first && b < end;
        const bool insideC =
            c >= range.first && c < end;
        if ((insideA || insideB || insideC) &&
            !(insideA && insideB && insideC)) {
            return fail(
                outError,
                "Route 1 tree triangle crosses source instance blocks.");
        }
        if (insideA) {
            outIndices.push_back(a);
            outIndices.push_back(b);
            outIndices.push_back(c);
        }
    }
    if (outIndices.empty()) {
        return fail(
            outError,
            "Route 1 tree instance has no triangles.");
    }
    if (outError) {
        outError->clear();
    }
    return true;
}

} // namespace game::runtime::route1_tree_instances
