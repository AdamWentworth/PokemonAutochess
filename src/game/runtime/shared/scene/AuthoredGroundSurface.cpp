#include "game/runtime/shared/scene/AuthoredGroundSurface.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace game::runtime::authored_environment {

void GroundSurface::add(const Triangle &triangle) {
    const auto normal = glm::cross(triangle[1] - triangle[0], triangle[2] - triangle[0]);
    if (std::abs(normal.y) < 1.0e-5f) return;
    const auto minimum = glm::min(triangle[0], glm::min(triangle[1], triangle[2]));
    const auto maximum = glm::max(triangle[0], glm::max(triangle[1], triangle[2]));
    for (int x = static_cast<int>(std::floor(minimum.x / kIndexCellSize));
         x <= static_cast<int>(std::floor(maximum.x / kIndexCellSize)); ++x) {
        for (int z = static_cast<int>(std::floor(minimum.z / kIndexCellSize));
             z <= static_cast<int>(std::floor(maximum.z / kIndexCellSize)); ++z) {
            cells_[{x, z}].push_back(triangle);
        }
    }
}

bool GroundSurface::sample(float sourceX, float sourceZ, float &outHeight) const noexcept {
    if (!std::isfinite(sourceX) || !std::isfinite(sourceZ)) return false;
    const double x = std::floor(static_cast<double>(sourceX) / kIndexCellSize);
    const double z = std::floor(static_cast<double>(sourceZ) / kIndexCellSize);
    if (x < std::numeric_limits<int>::min() || x > std::numeric_limits<int>::max() ||
        z < std::numeric_limits<int>::min() || z > std::numeric_limits<int>::max()) return false;
    const auto cell = cells_.find({static_cast<int>(x), static_cast<int>(z)});
    if (cell == cells_.end()) return false;
    float highest = std::numeric_limits<float>::lowest();
    bool sampled = false;
    for (const auto &triangle : cell->second) {
        const auto &a = triangle[0];
        const auto &b = triangle[1];
        const auto &c = triangle[2];
        const float denominator = (b.z - c.z) * (a.x - c.x) + (c.x - b.x) * (a.z - c.z);
        if (std::abs(denominator) < 1.0e-5f) continue;
        const float wa = ((b.z - c.z) * (sourceX - c.x) + (c.x - b.x) * (sourceZ - c.z)) / denominator;
        const float wb = ((c.z - a.z) * (sourceX - c.x) + (a.x - c.x) * (sourceZ - c.z)) / denominator;
        const float wc = 1.0f - wa - wb;
        if (wa < -1.0e-5f || wb < -1.0e-5f || wc < -1.0e-5f) continue;
        highest = std::max(highest, wa * a.y + wb * b.y + wc * c.y);
        sampled = true;
    }
    if (sampled) outHeight = highest;
    return sampled;
}

} // namespace game::runtime::authored_environment
