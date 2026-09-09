#include "game/arena/ArenaMapData.h"
#include "game/arena/AuthoredCombatMap.h"
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
    occupied.assign(64, 0);
    reserveStep(map, {3, 3}, {3, 4}, occupied);
    check(firstStepTowards(map, {1, 0, {3, 5}}, {2, 1, {3, 3}}, occupied, {3, 4}) == Cell{},
          "An incoming enemy provoked a detour instead of waiting at the meeting point.");
    check(firstStepTowards(map, {1, 0, {3, 6}}, {2, 1, {3, 3}}, occupied, {3, 4}) == Cell{3, 5},
          "Pursuit routed around the enemy's step toward its old cell.");
    map.rules = &rules;
    check(firstStepTowards(map, {1, 0, {3, 6}}, {99, 1, {3, 3}}, occupied, {3, 4}) == Cell{},
          "Knowing a reservation bypassed target visibility.");
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
    bad["cells"][0]["ramp"] = 13;
    check(!map.load(bad.dump(), &error), "An unknown corner-ramp shape was accepted.");
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

void cornerRamps() {
    constexpr std::array<std::array<float, 2>, 4> corners{{{0, 0}, {100, 0}, {100, 100}, {0, 100}}};
    for (int direction = 0; direction < 4; ++direction) {
        for (int part = 0; part < 2; ++part) {
            const Tile tile{22, -22, 3, 0, 5 + direction * 2 + part};
            const int high = (direction + 1) % 4;
            for (int i = 0; i < 4; ++i) {
                const bool raised = part ? i != (high + 2) % 4 : i == high;
                check(tile.heightAt(2200 + corners[i][0], -2200 + corners[i][1]) == (raised ? 200 : 150),
                      "Corner-ramp rotation or foot/crest heights changed.");
            }
            check(tile.heightAt(2250, -2150) == (part ? 200 : 150), "Corner ramp lost its diagonal fold.");
        }
    }
    // Recovered diagonal band above South Clearing's enemy bench. Shared
    // edges must meet all the way across, including each intermediate height.
    const Tile west{22, -22, 3, 0, 5}, crest{23, -22, 3, 0, 6}, foot{23, -21, 3, 0, 5}, straight{24, -21, 3, 0, 1};
    for (int i = 0; i <= 10; ++i) {
        const float t = i * 10.0f;
        check(west.heightAt(2300, -2200 + t) == crest.heightAt(2300, -2200 + t) &&
                  crest.heightAt(2300 + t, -2100) == foot.heightAt(2300 + t, -2100) &&
                  foot.heightAt(2400, -2100 + t) == straight.heightAt(2400, -2100 + t),
              "Corner ramp has an internal step at its join.");
    }
    check(west.heightAt(2275, -2175) == 175 && crest.heightAt(2325, -2125) == 175 && foot.heightAt(2375, -2075) == 175,
          "The diagonal slope no longer interpolates halfway between its terraces.");
}

void authoredTraversal() {
    ArenaMapData data;
    std::ifstream stream(PAC_ARENA_MAP_FIXTURE);
    nlohmann::json document;
    stream >> document;
    std::string error;
    check(data.load(document.dump(), &error), "Traversal fixture failed to load.");
    AuthoredCombatMap coverRules(data, {17, -10});
    CombatMapView coverMap{8, 8, &coverRules};
    std::vector<std::uint8_t> coverBlocked(64);
    check(!coverMap.canPerceive({1, 0, {2, 6}}, {2, 1, {7, 4}}),
          "A tiny border-corner contact merged the Grass Test's separate patches.");
    // Rattata's first southward patrol step is still inside the dense border
    // grass. The former core-only footprint exposed this entire visible row.
    const Actor observer{1, 0, {4, 5}}, exposed{2, 1, {7, 6}}, concealed{2, 1, {7, 5}};
    check(!coverMap.canPerceive(observer, concealed), "Rattata was exposed while still in the rendered south edge of the grass.");
    reserveStep(coverMap, exposed.cell, concealed.cell, coverBlocked);
    check(coverMap.canPerceive(observer, exposed) && !coverMap.canPerceive(observer, concealed), "Pursuit cover fixture drifted.");
    check(firstStepTowards(coverMap, observer, exposed, coverBlocked, concealed.cell) != Cell{},
          "A visible target's future grass membership prematurely cancelled pursuit.");
    check(firstStepTowards(coverMap, observer, concealed, coverBlocked, exposed.cell) == Cell{},
          "A concealed target's future open-ground destination leaked into pursuit.");
    data.cover.clear();
    AuthoredCombatMap rules(data, {17, -10});
    CombatMapView map{8, 8, &rules};
    std::vector<std::uint8_t> blocked(64);
    for (int x = 3; x < 8; ++x) {
        check(rules.cardinalStep({x, 1}, {x, 2}, {}) == StepKind::LedgeDrop, "South shelf must be a ledge drop.");
        check(!map.canStep({x, 2}, {x, 1}, {}, blocked), "Ground unit climbed a ledge.");
        check(map.canStep({x, 2}, {x, 1}, {true}, blocked), "Flyer was blocked by ledge height.");
        check(!map.canEngageMelee({1, 0, {x, 1}}, {2, 1, {x, 2}}), "Ground melee reached through cliff.");
    }
    for (int x = 0; x < 3; ++x) {
        check(map.canStep({x, 0}, {x, 1}, {}, blocked) && map.canStep({x, 1}, {x, 0}, {}, blocked), "Upper ramp connection blocked.");
        check(map.canStep({x, 1}, {x, 2}, {}, blocked) && map.canStep({x, 2}, {x, 1}, {}, blocked), "Lower ramp connection blocked.");
    }
    check(!map.canStep({5, 1}, {6, 2}, {}, blocked), "Diagonal shortcut bypassed jump sequence.");
    check(!map.canStep({2, 1}, {3, 1}, {}, blocked), "A ramp side wall was traversable.");
    blocked[map.index({5, 2})] = 1;
    check(!map.canStep({5, 1}, {5, 2}, {}, blocked), "Jump entered occupied landing.");
    check(!map.canStep({5, 1}, {5, 2}, {true}, blocked), "Flyer bypassed landing occupancy.");
    check(canReachMelee(map, {1, 0, {6, 4}}, {2, 1, {6, 0}}), "Planner failed to find uphill ramp detour.");
    // Remove ramps to distinguish reachable enemies from a closer enemy sealed
    // above an uphill wall. A* approach fallback must not masquerade as a route.
    for (int x = 17; x < 20; ++x)
        data.tiles.at({x, -9}) = Tile{x, -9, 1, 0, 0};
    AuthoredCombatMap isolated(data, {17, -10});
    map.rules = &isolated;
    check(!canReachMelee(map, {1, 0, {6, 3}}, {2, 1, {6, 1}}), "An unreachable upper target was accepted.");
    check(canReachMelee(map, {1, 0, {6, 3}}, {3, 1, {0, 6}}), "Reachable lower target was discarded.");
}
CoverRegion rect(const char *id, float x, float z, float w, float h) {
    CoverRegion region;
    region.id = id;
    region.polygons.push_back({{{x,z},{x+w,z},{x+w,z+h},{x,z+h}}});
    return region;
}
void concealment() {
    ArenaMapData data;
    data.cover = {rect("a",100,100,200,100), rect("b",300,100,100,100),
                  rect("corner",400,200,100,100), rect("separate",600,100,100,100)};
    // An authored object may contain disconnected islands; these stay separate.
    data.cover[0].polygons.push_back(rect("island",100,400,100,100).polygons[0]);
    AuthoredCombatMap rules(data,{0,0});
    Actor outside{1,0,{0,1}}, inside{2,1,{1,1}}, same{3,0,{3,1}}, other{4,0,{6,1}};
    check(!rules.canPerceive(outside,inside), "Outside observer saw into cover.");
    check(rules.canPerceive(inside,outside), "Grass blocked sight of open ground.");
    check(rules.canPerceive(same,inside), "Shared-edge polygons did not form one patch.");
    check(!rules.canPerceive(other,inside), "Separate patches shared sight.");
    other.cell={4,2};
    check(!rules.canPerceive(other,inside), "Corner-only contact joined patches.");
    other.cell={1,4};
    check(!rules.canPerceive(other,inside), "Disconnected islands of one prefab shared sight.");
    inside.revealed=true;
    check(rules.canPerceive(outside,inside), "Attack reveal failed.");
    inside.revealed=false;
    inside.grounded=false;
    check(rules.canPerceive(outside,inside), "Airborne unit inherited ground concealment.");
    inside.grounded=true;
    inside.traversingLedge=true;
    check(rules.canPerceive(outside,inside), "Ledge jumper inherited ground concealment.");
    inside.traversingLedge=false;
    inside.team=outside.team;
    check(rules.canPerceive(outside,inside), "Ally hidden from its own side.");
    inside.team=1;
    inside.offsetZ=-0.6f;
    check(rules.canPerceive(outside,inside), "Cover used rounded cell instead of continuous footprint.");
}
void investigation() {
    ArenaMapData data;
    for (int z = 0; z < 8; ++z)
        for (int x = 0; x < 8; ++x) {
            data.tiles[{x, z}] = {x, z, 0, 0, 0};
            data.playableCells.insert({x, z});
        }
    data.cover = {rect("brush", 500, 100, 300, 500)};
    AuthoredCombatMap rules(data, {0, 0});
    CombatMapView map{8, 8, &rules};
    std::vector<std::uint8_t> blocked(64);
    Actor mover{1, 0, {1, 3}};
    TargetMemory memory{{7, 1}, 0, 0, kTargetMemorySeconds};
    // Head to the nearby edge of the remembered patch, not the old unit tile
    // at its far end. Entering the patch is enough to check the whole group.
    for (int x = 2; x <= 5; ++x) {
        const auto next = firstInvestigationStep(map, mover, memory, blocked);
        check(next == Cell{x, 3}, "Investigator did not use the nearest reachable patch entry.");
        mover.cell = next;
    }
    check(firstInvestigationStep(map, mover, memory, blocked) == Cell{} && !memory.active(),
          "An empty checked patch retained an investigation forever.");
    memory = {{7, 1}, 0, 0, kTargetMemorySeconds};
    mover.cell = {1, 3};
    mover.grounded = false;
    check(firstInvestigationStep(map, mover, memory, blocked) == Cell{2, 3}, "Airborne unit could not investigate a remembered area.");
    mover.grounded = true;
    std::fill(blocked.begin(), blocked.end(), 1);
    check(firstInvestigationStep(map, mover, memory, blocked) == Cell{} && memory.active(),
          "A temporary reservation discarded the remembered lead or allowed overlap.");
    memory.remainingSec = 0;
    check(firstInvestigationStep(map, mover, memory, blocked) == Cell{} && !memory.active(),
          "Expired memory remained actionable.");
    std::fill(blocked.begin(), blocked.end(), 0);
    memory = {{3, 3}, 0, 0, kTargetMemorySeconds};
    check(firstInvestigationStep(map, mover, memory, blocked) == Cell{2, 3}, "Open-ground memory was not investigated.");
    mover.cell = {3, 3};
    check(firstInvestigationStep(map, mover, memory, blocked) == Cell{} && !memory.active(), "Checked open-ground memory was not cleared.");
}

void patrol() {
    CombatMapView map{8, 8, nullptr};
    std::vector<std::uint8_t> blocked(64);
    for (bool north : {true,false}) {
        Actor mover{1,0,{3,4}};
        PatrolState state;
        std::set<std::pair<int,int>> visited;
        const auto first=firstPatrolStep(map,mover,state,blocked,north);
        check(first == Cell{3,north ? 3 : 5}, "Patrol did not start toward enemy end.");
        for (int tick=0;tick<180;++tick) {
            visited.insert({mover.cell.x,mover.cell.z});
            auto copy=state;
            const auto step=firstPatrolStep(map,mover,state,blocked,north);
            check(step==firstPatrolStep(map,mover,copy,blocked,north), "Search was not deterministic.");
            check(map.canStep(mover.cell,step,{},blocked), "Patrol took illegal step.");
            mover.cell=step;
        }
        check(visited.size()==64, "Search repeated a lane while missing reachable cells.");
    }
    blocked[map.index({3,3})]=1;
    Actor mover{1,0,{3,4}};
    PatrolState state;
    auto step=firstPatrolStep(map,mover,state,blocked,true);
    check(map.canStep(mover.cell,step,{},blocked), "Occupied waypoint stalled or bypassed collision.");
    TestRules wall;
    map.rules=&wall;
    mover.cell={1,4};state={};
    for (int i=0;i<150;++i) {
        const auto next=firstPatrolStep(map,mover,state,blocked,true);
        check(map.canStep(mover.cell,next,{},blocked) && next.x<3, "Search crossed one-way wall.");
        mover.cell=next;
    }
}

} // namespace

int main() {
    try {
        navigation();
        concealment();
        investigation();
        patrol();
        authoredData();
        cornerRamps();
        authoredTraversal();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "Arena logic passed: directed corners, visibility, 400 seeded layouts, data and spatial limits.\n";
}
