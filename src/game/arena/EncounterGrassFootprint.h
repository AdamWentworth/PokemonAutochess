#pragma once

#include <algorithm>
#include <array>
#include <set>
#include <utility>
#include <vector>

namespace game::arena {

// Nominal 100 cm grass clumps, including the dense half-cell border ring.
// Rendering and archive validation share these centres. Cover uses a 50 cm
// square around each centre; individual swaying blade tips do not change sight.
inline std::vector<std::array<float, 2>> encounterGrassCenters(
    const std::set<std::pair<int, int>> &core) {
    std::set<std::pair<int, int>> expanded;
    for (const auto &[x, z] : core)
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
                expanded.emplace(x + dx, z + dz);
    std::vector<std::array<float, 2>> centers;
    centers.reserve(expanded.size());
    for (const auto &cell : expanded) {
        float x = static_cast<float>(cell.first), z = static_cast<float>(cell.second);
        if (!core.contains(cell)) {
            const auto nearest = std::min_element(core.begin(), core.end(), [&](const auto &a, const auto &b) {
                const auto rank = [&](const auto &p) {
                    const auto dx = static_cast<long long>(p.first) - cell.first;
                    const auto dz = static_cast<long long>(p.second) - cell.second;
                    return std::array<long long, 3>{dx * dx + dz * dz, p.first, p.second};
                };
                return rank(a) < rank(b);
            });
            x = (x + nearest->first) * 0.5f;
            z = (z + nearest->second) * 0.5f;
        }
        centers.push_back({(x + 0.5f) * 100.0f, (z + 0.5f) * 100.0f});
    }
    return centers;
}

} // namespace game::arena
