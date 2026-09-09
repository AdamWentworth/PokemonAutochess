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

bool test_route1_north_terraces_contract(std::string &outFail) {
    namespace env = game::runtime::route1_environment;
    namespace variants = game::runtime::route1_scene_variants;
    namespace activation = game::runtime::arena_scene_activation;
    using game::arena::Actor;
    using game::arena::StepKind;
    try {
        const auto check = [](bool ok, const std::string &message) { if (!ok) throw std::runtime_error(message); };
        const auto &variant = variants::kRoute1NorthTerraces;
        check(variants::find(variant.sceneId) == &variant &&
                  &variants::fromStateScriptPath("scripts/states/route1_north_terraces.lua") == &variant,
              "North Terraces fell back to another Route 1 arena.");
        game::assets::DevAssetStore store(engine::paths::dataRoot());
        game::runtime::authored_arena::Bundle bundle;
        env::RuntimeEnvironment environment;
        env::BoardLayoutTransform board;
        check(bundle.load(store, std::string(variant.arenaBundlePath), &outFail), outFail);
        check(env::loadCookedEnvironment(store, environment, nullptr, &outFail), outFail);
        check(env::loadBoardLayoutTransform(bundle.store, bundle.boardPath, board, &outFail), outFail);
        check(board.terrainGridOrigin == std::array<std::int32_t, 2>{17, -28}, "The third board must advance nine tiles north of South Clearing.");
        check(activation::apply(store, variant, environment, true, true, &outFail), outFail);
        check(environment.terrainTiles().empty() && environment.stats().visibleTriangleCount > 0 && environment.stats().shadowGroundTriangleCount > 0,
              "North Terraces must use its authored terrain and shadows.");
        bool restoredSouthernGrass = false, restoredSouthernShrub = false;
        for (const auto &object : environment.layoutObjects()) {
            if (object.suppressed || object.targetKind == "gameplay_board_ground_prototype") continue;
            check(object.authored, "Original source scenery leaked into North Terraces: " + object.stableId);
            if (object.stableId.ends_with("encounter-grass-enc_grass01-record-3")) {
                restoredSouthernGrass = std::abs(object.translationCm[0] - 2350) < .1f && std::abs(object.translationCm[2] + 1550) < .1f;
            }
            if (object.stableId.ends_with("canonical-tree-tree_006-instance-8")) {
                restoredSouthernShrub = std::abs(object.translationCm[0] - 1965.5194f) < .1f && std::abs(object.translationCm[2] + 1339.3231f) < .1f;
            }
            if (object.targetKind == "environment_mesh_patch" || object.prefabAssetId.find("encounter_grass") != std::string::npos) continue;
            if (!object.prefabAssetId.starts_with("route1/tree_") && object.boundsMaximumCm[1] - object.boundsMinimumCm[1] <= 65) continue;
            check(!(object.boundsMinimumCm[0] < 2500 && object.boundsMaximumCm[0] > 1700 &&
                    object.boundsMinimumCm[2] < -2000 && object.boundsMaximumCm[2] > -2900),
                  "A canopy or solid shrub overlaps the North Terraces board/reserve rows: " + object.stableId);
        }
        check(restoredSouthernGrass && restoredSouthernShrub, "The southern backdrop must use original LGPE prop positions rather than South Clearing's arena adjustments.");
        constexpr float heights[10][8] = {
            {250, 250, 250, 250, 250, 250, 250, 250},
            {200, 200, 200, 200, 225, 225, 225, 225},
            {200, 200, 200, 200, 200, 200, 200, 200},
            {200, 200, 200, 200, 200, 200, 200, 200},
            {200, 200, 200, 200, 200, 200, 200, 200},
            {200, 200, 200, 200, 200, 200, 200, 200},
            {175, 175, 175, 200, 200, 200, 200, 200},
            {150, 150, 150, 150, 150, 150, 200, 200},
            {150, 150, 150, 150, 150, 150, 150, 175},
            {150, 150, 150, 150, 150, 150, 150, 150}};
        const auto matrix = env::worldFromSourceMatrix(board);
        for (int row = -1; row <= 8; ++row)
            for (int col = 0; col < 8; ++col) {
                const float x = (17.5f + col) * 100, z = (-27.5f + row) * 100;
                const auto *tile = bundle.map.tileAt(17 + col, -28 + row);
                const float expected = heights[row + 1][col];
                check(tile && std::abs(tile->heightAt(x, z) - expected) < .01f, "North Terraces lost its open board floor, terraces or corner ramp.");
                if (row == -1 || row == 8) check(tile->surface == 1 && bundle.map.coverAt(x, z).empty(), "Both North Terraces reserve rows must be dirt without encounter grass.");
                if ((row == 3 || row == 4) && col >= 4 && col <= 6) check(tile->surface == 0, "The removed island must blend into the accessible light lawn.");
                float actual = -999;
                check(environment.sampleWorldTerrainHeight(matrix[0] * x + matrix[8] * z + matrix[12], matrix[2] * x + matrix[10] * z + matrix[14], actual) &&
                          std::abs(actual - (matrix[5] * expected + matrix[13])) < .001f,
                      "A North Terraces board/reserve centre does not stand on the rendered floor.");
            }
        for (const auto &expected : {game::arena::Tile{17, -15, 4, 2, 0}, game::arena::Tile{20, -14, 3, 2, 0},
                                     game::arena::Tile{16, -17, 2, 1, 0}, game::arena::Tile{20, -17, 2, 0, 0},
                                     game::arena::Tile{17, -13, 2, 2, 0}, game::arena::Tile{23, -13, 1, 0, 1},
                                     game::arena::Tile{19, -12, 1, 1, 0}, game::arena::Tile{24, -11, 1, 0, 0}}) {
            const auto *tile = bundle.map.tileAt(expected.x, expected.z);
            check(tile && tile->height == expected.height && tile->surface == expected.surface && tile->ramp == expected.ramp,
                  "The southern backdrop lost its source banks, dirt pockets or narrow ramp.");
        }
        GameConfigData config;
        GameWorld gameplay(config);
        check(activation::applyGameplay(store, variant, gameplay, &outFail), outFail);
        const auto map = gameplay.combatMap();
        check(map.stepKind({4, 4}, {4, 5}, {}) == StepKind::Walk && map.stepKind({4, 5}, {4, 4}, {}) == StepKind::Walk,
              "The removed island must allow level movement in both directions.");
        check(map.stepKind({3, 5}, {3, 6}, {}) == StepKind::LedgeDrop && map.stepKind({3, 6}, {3, 5}, {}) == StepKind::Blocked &&
                  map.stepKind({3, 6}, {3, 5}, {true}) == StepKind::Walk,
              "North Terraces must retain southbound terrace jumps, uphill walls and flying exceptions.");
        check(map.stepKind({6, 0}, {6, 1}, {}) == StepKind::Walk && map.stepKind({6, 1}, {6, 0}, {}) == StepKind::Walk &&
                  map.stepKind({1, 5}, {1, 6}, {}) == StepKind::Walk && map.stepKind({1, 6}, {1, 5}, {}) == StepKind::Walk,
              "North Terraces ramps must remain walkable in both directions.");
        Actor open{.id = 1, .team = 0, .cell = {2, 2}}, hidden{.id = 2, .team = 1, .cell = {7, 3}}, samePatch{.id = 3, .team = 0, .cell = {7, 4}};
        check(map.coverGroup(open) < 0 && map.coverGroup(hidden) >= 0 && map.coverGroup(hidden) == map.coverGroup(samePatch) &&
                  !map.canPerceive(open, hidden) && map.canPerceive(hidden, open) && map.canPerceive(samePatch, hidden),
              "The original east grass bed must provide concealment in the new arena.");
        for (const auto *other : {&variants::kRoute1SouthClearing, &variants::kRoute1Pilot, &variant}) {
            check(activation::apply(store, *other, environment, true, false, &outFail) && activation::applyGameplay(store, *other, gameplay, &outFail), outFail);
        }
        return true;
    } catch (const std::exception &error) {
        outFail = error.what();
        return false;
    }
}
