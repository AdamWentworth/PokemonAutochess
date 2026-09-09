#pragma once

#include "game/arena/CombatMap.h"
#include <array>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace game::arena {

struct Tile {
    // 0 flat, 1..4 north/east/south/west. 5..12 are corner foot/crest
    // pairs rising NE, SE, SW, NW; height is always the lower elevation.
    static constexpr int kMaximumRampShape = 12;
    int x = 0, z = 0, height = 0, surface = 0, ramp = 0;
    float heightAt(float sourceXcm, float sourceZcm) const noexcept;
};
struct CoverRegion {
    std::string id;
    std::vector<std::array<std::array<float, 2>, 4>> polygons;
    bool contains(float sourceXcm, float sourceZcm) const noexcept;
};

// Renderer-free, validated authored data. Loading it does not enable terrain
// restrictions or concealment; those are explicit CombatMapRules policies.
struct ArenaMapData {
    std::string sceneId;
    std::map<std::pair<int, int>, Tile> tiles;
    std::set<std::pair<int, int>> playableCells;
    std::set<std::pair<int, int>> reserveCells;
    std::vector<CoverRegion> cover;
    bool load(const std::string &json, std::string *error = nullptr);
    const Tile *tileAt(int x, int z) const noexcept;
    std::vector<std::string> coverAt(float sourceXcm, float sourceZcm) const;
    std::array<float, 2> edgeHeightDelta(Cell from, Cell to) const;
};

} // namespace game::arena
