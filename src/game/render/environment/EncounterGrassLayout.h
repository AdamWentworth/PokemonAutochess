#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace game::render::encounter_grass_layout {

// A bed's scale sizes its footprint. Its blade modules retain their original
// proportions. Keep this deterministic packing in sync with arena_grass.py.
inline std::vector<std::array<float, 2>> centers(
    const std::vector<std::array<float, 2>> &source, float sx, float sz) {
    if (source.empty() || !std::isfinite(sx) || !std::isfinite(sz) || sx <= 0 || sz <= 0)
        throw std::runtime_error("Grass beds require finite positive dimensions.");
    if (std::abs(sx - 1) < 1e-6f && std::abs(sz - 1) < 1e-6f) return source;
    std::vector<std::array<float, 4>> rectangles;
    for (const auto &p : source)
        rectangles.push_back({(p[0] - 50) * sx, (p[1] - 50) * sz, (p[0] + 50) * sx, (p[1] + 50) * sz});
    const auto axis = [&](int index, float scale) {
        std::set<float> values;
        if (std::abs(scale - 1) < 1e-6f) {
            for (const auto &p : source)
                values.insert(p[index]);
        } else {
            float low = rectangles.front()[index], high = rectangles.front()[index + 2];
            for (const auto &r : rectangles) {
                low = std::min(low, r[index]);
                high = std::max(high, r[index + 2]);
            }
            low += 50;
            high -= 50;
            if (high >= low - 1e-4f) {
                const float steps = std::ceil(std::max(0.0f, high - low) / 50);
                if (steps > 4096) throw std::runtime_error("Grass bed exceeds the clump limit.");
                for (int i = 0; i <= static_cast<int>(steps); ++i)
                    values.insert(low + (high - low) * i / std::max(1.0f, steps));
            }
        }
        return values;
    };
    const auto xs = axis(0, sx), zs = axis(1, sz);
    if (xs.size() * zs.size() > 4096) throw std::runtime_error("Grass bed exceeds the clump limit.");
    std::vector<std::array<float, 2>> result;
    for (float x : xs) {
        std::set<float> splits{x - 50, x + 50};
        for (const auto &r : rectangles)
            for (float edge : {r[0], r[2]})
                if (edge > x - 50 && edge < x + 50) splits.insert(edge);
        for (float z : zs) {
            bool fits = true;
            for (auto a = splits.begin(), b = std::next(a); b != splits.end(); ++a, ++b) {
                const float mid = (*a + *b) * .5f;
                std::vector<std::pair<float, float>> intervals;
                for (const auto &r : rectangles)
                    if (r[0] <= mid && mid <= r[2]) intervals.emplace_back(r[1], r[3]);
                std::sort(intervals.begin(), intervals.end());
                float end = z - 50;
                for (const auto &[low, high] : intervals) {
                    if (low > end + 1e-4f) break;
                    end = std::max(end, high);
                }
                if (end < z + 50 - 1e-4f) {
                    fits = false;
                    break;
                }
            }
            if (fits) result.push_back({x, z});
        }
    }
    if (result.empty()) throw std::runtime_error("Grass bed is too small for a full-size clump.");
    return result;
}
} // namespace game::render::encounter_grass_layout
