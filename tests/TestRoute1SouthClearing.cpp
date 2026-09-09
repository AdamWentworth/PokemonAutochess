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

bool test_route1_south_clearing_contract(std::string &outFail) {
    namespace env = game::runtime::route1_environment;
    namespace variants = game::runtime::route1_scene_variants;
    namespace activation = game::runtime::arena_scene_activation;
    using game::arena::Actor;
    using game::arena::StepKind;
    try {
        const auto check = [](bool ok, const std::string &message) { if (!ok) throw std::runtime_error(message); };
        const auto &variant = variants::kRoute1SouthClearing;
        check(variants::find(variant.sceneId) == &variant &&
                  &variants::fromStateScriptPath("scripts/states/route1_south_clearing.lua") == &variant,
              "South Clearing must select its own authored arena, not a legacy/entrance fallback.");
        game::assets::DevAssetStore store(engine::paths::dataRoot());
        game::runtime::authored_arena::Bundle bundle;
        env::RuntimeEnvironment environment;
        env::BoardLayoutTransform board;
        check(bundle.load(store, std::string(variant.arenaBundlePath), &outFail), outFail);
        check(env::loadCookedEnvironment(store, environment, nullptr, &outFail), outFail);
        check(env::loadBoardLayoutTransform(bundle.store, bundle.boardPath, board, &outFail), outFail);
        check(board.terrainGridOrigin[0] == 17 && board.terrainGridOrigin[1] == -19,
              "South Clearing lost its original board registration.");
        check(activation::apply(store, variant, environment, true, true, &outFail), outFail);
        check(environment.terrainTiles().empty() && environment.stats().visibleTriangleCount > 0 &&
                  environment.stats().shadowGroundTriangleCount > 0,
              "The clearing must render and cast shadows from its authored mesh, without legacy tile repairs.");
        for (const auto &object : environment.layoutObjects()) {
            if (object.suppressed || object.targetKind == "gameplay_board_ground_prototype") continue;
            check(object.authored, "An original source object leaked into the authored clearing: " + object.stableId);
            if (object.targetKind == "environment_mesh_patch" || object.prefabAssetId.find("encounter_grass") != std::string::npos) continue;
            check(!(object.boundsMinimumCm[0] < 2500 && object.boundsMaximumCm[0] > 1700 &&
                    object.boundsMinimumCm[2] < -1100 && object.boundsMaximumCm[2] > -2000),
                  "A solid prop or canopy intrudes into the clearing board/reserve rows: " + object.stableId);
        }
        const auto matrix = env::worldFromSourceMatrix(board);
        for (int row = -1; row <= 8; ++row) {
            for (int col = 0; col < 8; ++col) {
                const float x = (17.5f + col) * 100, z = (-18.5f + row) * 100;
                const float expected = row < 0 ? 150.0f : row == 0 ? (col < 4 ? 125.0f : 150.0f)
                                                      : row <= 5   ? 100.0f
                                                      : row == 6   ? 75.0f
                                                                   : 50.0f;
                const auto *tile = bundle.map.tileAt(17 + col, -19 + row);
                check(tile && std::abs(tile->heightAt(x, z) - expected) < .01f, "The clearing's terraces or ramps changed height.");
                float actual = -999;
                check(environment.sampleWorldTerrainHeight(matrix[0] * x + matrix[8] * z + matrix[12],
                                                           matrix[2] * x + matrix[10] * z + matrix[14], actual) &&
                          std::abs(actual - (matrix[5] * expected + matrix[13])) < .001f,
                      "A clearing board/reserve centre does not stand on the rendered floor.");
            }
        }
        GameConfigData config;
        GameWorld gameplay(config);
        check(activation::applyGameplay(store, variant, gameplay, &outFail), outFail);
        const auto map = gameplay.combatMap();
        check(map.stepKind({5, 0}, {5, 1}, {}) == StepKind::LedgeDrop && map.stepKind({5, 1}, {5, 0}, {}) == StepKind::Blocked,
              "The northern ledge must allow southbound drops and block uphill movement.");
        check(map.stepKind({1, 0}, {1, 1}, {}) == StepKind::Walk && map.stepKind({1, 1}, {1, 0}, {}) == StepKind::Walk &&
                  map.stepKind({5, 5}, {5, 6}, {}) == StepKind::Walk && map.stepKind({5, 6}, {5, 5}, {}) == StepKind::Walk &&
                  map.stepKind({5, 6}, {5, 7}, {}) == StepKind::Walk,
              "The clearing's western and southern ramps must remain walkable in both directions.");
        check(map.stepKind({5, 1}, {5, 0}, {true}) == StepKind::Walk, "Flying units lost their uphill exception.");
        Actor dirt{.id = 1, .team = 0, .cell = {2, 3}}, grass{.id = 2, .team = 1, .cell = {6, 3}}, samePatch{.id = 3, .team = 0, .cell = {5, 3}};
        check(map.coverGroup(dirt) < 0 && map.coverGroup(grass) >= 0 && map.coverGroup(grass) == map.coverGroup(samePatch) &&
                  !map.canPerceive(dirt, grass) && map.canPerceive(grass, dirt) && map.canPerceive(samePatch, grass),
              "The original left dirt/right encounter-grass arrangement must drive concealment.");
        // Loading a second arena must also restore the entrance cleanly.
        check(activation::apply(store, variants::kRoute1Pilot, environment, true, false, &outFail) &&
                  activation::applyGameplay(store, variants::kRoute1Pilot, gameplay, &outFail),
              outFail);
        check(gameplay.combatMap().stepKind({5, 1}, {5, 2}, {}) == StepKind::LedgeDrop,
              "Returning to South Entrance retained the clearing's movement map.");
        check(activation::apply(store, variant, environment, true, false, &outFail), outFail);
        return true;
    } catch (const std::exception &error) {
        outFail = error.what();
        return false;
    }
}
