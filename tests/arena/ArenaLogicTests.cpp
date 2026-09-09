#include "game/arena/ArenaMapData.h"
#include "game/runtime/shared/scene/AuthoredGroundSurface.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

using namespace game::arena;
namespace {
void check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
struct TestRules : CombatMapRules {
    bool canTraverseCardinal(Cell from, Cell to, TraversalCapabilities capability) const override {
        return capability.ignoresTerrain || !(from.x == 2 && to.x == 3);
    }
    bool canPerceive(const Actor &, const Actor &target) const override { return target.id != 99; }
    bool canEngageMelee(const Actor &a, const Actor &b) const override {
        return !(a.cell.x == 2 && b.cell.x == 3);
    }
};
void navigation() {
    TestRules rules;
    CombatMapView map{8, 8, &rules};
    std::vector<std::uint8_t> occupied(64);
    check(!map.canStep({2, 2}, {3, 3}, {}, occupied), "A diagonal bypassed a directed cardinal restriction.");
    check(map.canStep({3, 3}, {2, 2}, {}, occupied), "Directed restrictions became symmetric.");
    check(map.canStep({2, 2}, {3, 3}, {true}, occupied), "Traversal capabilities were ignored.");
    occupied[map.index({3, 2})] = 1;
    check(!map.canStep({2, 2}, {3, 3}, {true}, occupied), "A flying capability bypassed an occupied corridor.");
    check(!map.canEngageMelee({1, 0, {2, 2}, {}}, {2, 1, {3, 2}, {}}), "Melee targeting ignored map rules.");
    check(firstStepTowards(map, {1, 0, {1, 1}, {}}, {99, 1, {6, 6}, {}}, occupied) == Cell{}, "Planner pursued an imperceptible target.");
    map.rules = nullptr;
    std::mt19937 random(0xA4E1u);
    for (int scenario = 0; scenario < 400; ++scenario) {
        for (auto &value : occupied)
            value = (random() % 7) == 0;
        const Actor actor{1, 0, {0, 0}, {}}, target{2, 1, {7, 7}, {}};
        occupied[0] = occupied[63] = 1;
        const auto step = firstStepTowards(map, actor, target, occupied);
        check(step == firstStepTowards(map, actor, target, occupied), "Planner changed for identical inputs.");
        // Independent flood-fill oracle: a legal route to an attack cell must
        // produce progress, even when greedy pursuit initially needs a detour.
        std::array<bool, 64> seen{};
        std::vector<Cell> reachable{{0, 0}};
        seen[0] = true;
        bool attackReachable = false;
        for (std::size_t i = 0; i < reachable.size(); ++i) {
            const auto cell = reachable[i];
            attackReachable |= cell.x >= 6 && cell.z >= 6;
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx) {
                    const Cell next{cell.x + dx, cell.z + dz};
                    if ((!dx && !dz) || !map.contains(next) || seen[map.index(next)] || occupied[map.index(next)]) continue;
                    if (dx && dz && (occupied[map.index({next.x, cell.z})] || occupied[map.index({cell.x, next.z})])) continue;
                    seen[map.index(next)] = true;
                    reachable.push_back(next);
                }
        }
        check(!attackReachable || step != Cell{}, "Planner stalled despite a reachable attack position.");
        if (step == Cell{}) continue;
        check(map.canStep(actor.cell, step, {}, occupied), "Planner selected an illegal first step.");
        reserveStep(map, actor.cell, step, occupied);
        check(occupied[map.index(actor.cell)] && occupied[map.index(step)], "Reservation lost an endpoint.");
    }
    check(firstStepTowards({0, 8, nullptr}, {}, {}, occupied) == Cell{}, "Invalid dimensions reached planner indexing.");
}
void authoredData() {
    std::ifstream stream(PAC_ARENA_MAP_FIXTURE);
    nlohmann::json document;
    stream >> document;
    ArenaMapData map;
    std::string error;
    check(map.load(document.dump(), &error), error.c_str());
    check(map.edgeHeightDelta({21, -8}, {21, -9}) == std::array<float, 2>{50, 50}, "Known ledge changed.");
    check(map.edgeHeightDelta({17, -8}, {17, -9}) == std::array<float, 2>{0, 0}, "Connected ramp changed.");
    const auto originalCount = map.tiles.size();
    auto bad = document;
    bad["cells"].push_back(bad["cells"][0]);
    check(!map.load(bad.dump(), &error) && map.tiles.size() == originalCount, "Invalid reload replaced good map state.");
    bad = document;
    bad["connections"].erase(0);
    check(!map.load(bad.dump(), &error), "Missing directed connection accepted.");
    bad = document;
    bad["connections"][0]["edge_height_delta_cm"] = {123, 123};
    check(!map.load(bad.dump(), &error), "Stale height connection accepted.");
    bad = document;
    bad["cover_regions"][0]["polygons_source_xz_cm"][0] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
    check(!map.load(bad.dump(), &error), "Degenerate cover footprint accepted.");
    const auto &region = map.cover.front();
    const auto &polygon = region.polygons.front();
    float x = 0, z = 0;
    for (const auto &p : polygon) {
        x += p[0] / 4;
        z += p[1] / 4;
    }
    check(region.contains(x, z) && !region.contains(1.0e9f, 1.0e9f), "Cover point query failed.");
    game::runtime::authored_environment::GroundSurface ground;
    check(!ground.add({glm::vec3{0, 0, 0}, {std::numeric_limits<float>::infinity(), 0, 0}, {0, 0, 1}}), "Non-finite floor accepted.");
    check(!ground.add({glm::vec3{0, 0, 0}, {1.0e7f, 0, 0}, {0, 0, 1.0e7f}}), "Unbounded spatial index accepted.");
}
} // namespace

int main() {
    try {
        navigation();
        authoredData();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "Arena logic passed: directed corners, visibility, 400 seeded layouts, data and spatial limits.\n";
}
