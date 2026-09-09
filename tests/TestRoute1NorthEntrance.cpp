#include "game/runtime/shared/scene/AuthoredArenaBundle.h"
#include "game/runtime/shared/scene/ArenaSceneActivation.h"
#include "game/runtime/shared/scene/Route1RuntimeEnvironment.h"
#include "game/runtime/shared/scene/Route1SceneVariants.h"
#include "engine/core/Paths.h"
#include "game/assets/DevAssetStore.h"
#include "game/GameConfig.h"
#include "game/GameWorld.h"
#include <cmath>
#include <stdexcept>

bool test_route1_north_entrance_contract(std::string &outFail) {
    namespace env = game::runtime::route1_environment;
    namespace variants = game::runtime::route1_scene_variants;
    namespace activation = game::runtime::arena_scene_activation;
    using game::arena::Actor;
    using game::arena::StepKind;
    try {
        const auto check = [](bool ok, const std::string &message) { if (!ok) throw std::runtime_error(message); };
        const auto &variant = variants::kRoute1NorthEntrance;
        check(variants::find(variant.sceneId) == &variant &&
                  &variants::fromStateScriptPath("scripts/states/route1_north_entrance.lua") == &variant,
              "North Entrance must select the fourth authored Route 1 arena.");
        game::assets::DevAssetStore store(engine::paths::dataRoot());
        game::runtime::authored_arena::Bundle bundle;
        env::RuntimeEnvironment environment;
        env::BoardLayoutTransform board;
        check(bundle.load(store, std::string(variant.arenaBundlePath), &outFail), outFail);
        check(env::loadCookedEnvironment(store, environment, nullptr, &outFail), outFail);
        check(env::loadBoardLayoutTransform(bundle.store, bundle.boardPath, board, &outFail), outFail);
        check(board.terrainGridOrigin == std::array<std::int32_t, 2>{17, -36}, "North Entrance must align with the earlier arenas, one tile south and two west of its initial position.");
        check(activation::apply(store, variant, environment, true, true, &outFail), outFail);
        check(environment.terrainTiles().empty() && environment.stats().visibleTriangleCount > 0 && environment.stats().shadowGroundTriangleCount > 0,
              "North Entrance must render and cast shadows from its authored terrain.");
        bool sourceGrassRestored = false;
        for (const auto &object : environment.layoutObjects()) {
            if (object.suppressed || object.targetKind == "gameplay_board_ground_prototype") continue;
            check(object.authored, "Source scenery leaked into North Entrance: " + object.stableId);
            if (object.stableId.ends_with("encounter-grass-enc_grass01-record-4")) {
                sourceGrassRestored = std::abs(object.translationCm[0] - 2550) < .1f && std::abs(object.translationCm[2] + 2350) < .1f;
            }
            if (object.targetKind == "environment_mesh_patch" || object.prefabAssetId.find("encounter_grass") != std::string::npos) continue;
            const bool overlapsColumns = object.boundsMinimumCm[0] < 2500 && object.boundsMaximumCm[0] > 1700;
            const bool overlapsRows = object.boundsMinimumCm[2] < -2700 && object.boundsMaximumCm[2] > -3700;
            const bool solid = object.prefabAssetId.starts_with("route1/tree_") || object.boundsMaximumCm[1] - object.boundsMinimumCm[1] > 65;
            check(!solid || !overlapsColumns || !overlapsRows, "A solid prop overlaps the board/reserves: " + object.stableId);
            for (const int z : {-37, -28}) {
                check(!overlapsColumns || object.boundsMinimumCm[2] >= (z + 1) * 100 || object.boundsMaximumCm[2] <= z * 100,
                      "A decorative plant overlaps a dirt reserve row: " + object.stableId);
            }
        }
        check(sourceGrassRestored, "The backdrop must preserve the original grass bed below this arena.");
        const auto matrix = env::worldFromSourceMatrix(board);
        for (int row = -1; row <= 8; ++row) {
            for (int col = 0; col < 8; ++col) {
                const float x = (17.5f + col) * 100, z = (-35.5f + row) * 100;
                const float expected = row == 8 ? (col <= 3 ? 200.0f : 225.0f) : row <= 3 ? 300.0f
                                                                                          : 250.0f;
                const auto *tile = bundle.map.tileAt(17 + col, -36 + row);
                check(tile && std::abs(tile->heightAt(x, z) - expected) < .01f, "North Entrance must preserve its battlefield terraces and the lower source terrain beneath the friendly bench.");
                if (row == -1 || row == 8) check(tile->surface == 1 && bundle.map.coverAt(x, z).empty(), "Both reserve rows must be dirt without encounter grass.");
                if (row == 8) check(tile->height == 4 && tile->ramp == (col < 3 ? 0 : col == 3 ? 5
                                                                                               : 1),
                                    "The friendly bench must preserve the original ledge and corner ramp instead of extending the battlefield platform.");
                if (row >= 4 && row <= 6 && col >= 1 && col <= 3) check(tile->surface == 0, "The removed spur must blend into the accessible lawn.");
                float actual = -999;
                check(environment.sampleWorldTerrainHeight(matrix[0] * x + matrix[8] * z + matrix[12], matrix[2] * x + matrix[10] * z + matrix[14], actual) &&
                          std::abs(actual - (matrix[5] * expected + matrix[13])) < .001f,
                      "A board/reserve centre does not stand on the visible floor.");
            }
        }
        const auto *backdropIsland = bundle.map.tileAt(21, -25);
        const auto *backdropLawn = bundle.map.tileAt(24, -20);
        check(backdropIsland && backdropIsland->height == 5 && backdropLawn && backdropLawn->surface == 0,
              "North Terraces arena alterations must not leak into the southern backdrop.");
        const auto *oldNorthBank = bundle.map.tileAt(24, -38);
        const auto *oldNorthLane = bundle.map.tileAt(20, -38);
        const auto *oldSouthBench = bundle.map.tileAt(23, -29);
        const auto *easternRamp = bundle.map.tileAt(25, -33);
        check(oldNorthBank && oldNorthBank->height == 7 && oldNorthBank->surface == 2 &&
                  oldNorthLane && oldNorthLane->height == 6 && oldNorthLane->surface == 0 &&
                  oldSouthBench && oldSouthBench->height == 5 && oldSouthBench->surface == 0 && !bundle.map.coverAt(2350, -2850).empty(),
              "The old bench locations must return to their source bank, lane and full grass bed.");
        check(easternRamp && easternRamp->height == 5 && easternRamp->ramp == 1,
              "Recentring the board must preserve the original eastern ramp beside it.");
        GameConfigData config;
        GameWorld gameplay(config);
        check(activation::applyGameplay(store, variant, gameplay, &outFail), outFail);
        const auto map = gameplay.combatMap();
        check(map.stepKind({3, 3}, {3, 4}, {}) == StepKind::LedgeDrop && map.stepKind({3, 4}, {3, 3}, {}) == StepKind::Blocked &&
                  map.stepKind({3, 4}, {3, 3}, {true}) == StepKind::Walk,
              "North Entrance must retain southbound jumps, uphill walls and flying exceptions.");
        check(map.stepKind({0, 4}, {1, 4}, {}) == StepKind::Walk && map.stepKind({7, 3}, {8, 3}, {}) == StepKind::Blocked,
              "The cleared lower lawn must be walkable and the backdrop ramp must stay outside the playable board.");
        Actor open{.id = 1, .team = 0, .cell = {1, 2}}, hidden{.id = 2, .team = 1, .cell = {4, 6}}, samePatch{.id = 3, .team = 0, .cell = {5, 6}};
        check(map.coverGroup(open) < 0 && map.coverGroup(hidden) >= 0 && map.coverGroup(hidden) == map.coverGroup(samePatch) &&
                  !map.canPerceive(open, hidden) && map.canPerceive(hidden, open) && map.canPerceive(samePatch, hidden),
              "The lower grass bed must preserve concealment and sight from within grass.");
        for (const auto *other : {&variants::kRoute1Pilot, &variants::kRoute1SouthClearing, &variants::kRoute1NorthTerraces, &variant}) {
            check(activation::apply(store, *other, environment, true, false, &outFail) && activation::applyGameplay(store, *other, gameplay, &outFail), outFail);
        }
        return true;
    } catch (const std::exception &error) {
        outFail = error.what();
        return false;
    }
}
