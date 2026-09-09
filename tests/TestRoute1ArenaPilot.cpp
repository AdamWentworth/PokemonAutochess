#include "game/runtime/shared/scene/AuthoredArenaBundle.h"
#include "game/runtime/shared/scene/ArenaSceneActivation.h"
#include "engine/assets/phlosion/PhlosionAuthoredScene.h"
#include "engine/assets/phlosion/PhlosionEnvironmentPatch.h"
#include "engine/core/Paths.h"
#include "game/assets/DevAssetStore.h"
#include "game/runtime/shared/scene/Route1RuntimeEnvironment.h"
#include "game/runtime/shared/scene/Route1SceneVariants.h"
#include <cmath>
#include <limits>
#include <string>

bool test_route1_arena_pilot_contract(std::string& outFail) {
    namespace env = game::runtime::route1_environment;
    namespace variants = game::runtime::route1_scene_variants;
    namespace ph = engine::assets::phlosion;
    game::assets::DevAssetStore store(engine::paths::dataRoot());
    env::RuntimeEnvironment environment;
    env::BoardLayoutTransform board;
    game::runtime::authored_arena::Bundle arena;
    if (!arena.load(store, std::string(variants::kRoute1Pilot.arenaBundlePath), &outFail) ||
        !env::loadCookedEnvironment(store, environment, nullptr, &outFail) ||
        !env::loadBoardLayoutTransform(arena.store, arena.boardPath, board, &outFail) ||
        !game::runtime::arena_scene_activation::apply(store, variants::kRoute1Pilot, environment, false, true, &outFail)) return false;
    auto scene = arena.scene;
    if (!environment.terrainTiles().empty() || environment.stats().visibleTriangleCount == 0) {
        outFail = "Mesh-authored arena must render without regenerated source terrain, including Terrain Patch V2 preview.";
        return false;
    }
    std::size_t playableBrushBeds = 0;
    for (const auto& object : environment.layoutObjects()) {
        if (object.suppressed || object.targetKind == "gameplay_board_ground_prototype") continue;
        if (!object.authored) {
            outFail = "Original layout leaked into the arena: " + object.stableId;
            return false;
        }
        if (object.targetKind != "environment_mesh_patch" &&
            object.boundsMinimumCm[0] < 2500 && object.boundsMaximumCm[0] > 1700 &&
            object.boundsMinimumCm[2] < -100 && object.boundsMaximumCm[2] > -1100) {
            if (object.prefabAssetId == "route1/encounter_grass_02") {
                ++playableBrushBeds;
            } else if (object.prefabAssetId != "route1/source_mesh_037") {
                outFail = "A canopy or unrelated prop intrudes into the board footprint: " + object.stableId;
                return false;
            }
        }
    }
    if (playableBrushBeds < 2) { outFail = "The arena needs playable encounter-grass pockets."; return false; }
    ph::EnvironmentPatchDocument patch;
    if (!ph::loadEnvironmentPatchDocument(arena.store, "content/phlosion/environment/arena-pilot/terrain.phpatch", patch, &outFail)) return false;
    std::size_t footprintVertices = 0;
    std::size_t groundTriangles = 0;
    for (const auto& mesh : patch.meshes) {
        for (const auto& group : mesh.materialGroups) {
            if (group.materialIndex == 19u) groundTriangles += group.indices.size() / 3;
        }
        if (mesh.id.find("railing/") != std::string::npos) {
            outFail = "The removed entrance fences returned.";
            return false;
        }
        for (const auto& vertex : mesh.vertices) {
            const auto& p = vertex.position;
            if (p[0] >= 1700 && p[0] <= 2500 && p[2] >= -1100 && p[2] <= -100) {
                ++footprintVertices;
            }
        }
    }
    if (!footprintVertices) { outFail = "Authored terrain does not cover the board."; return false; }
    if (!groundTriangles || environment.stats().shadowGroundTriangleCount < groundTriangles) {
        outFail = "The rebuilt terrain is missing from the projected shadow atlas.";
        return false;
    }
    const auto world = env::worldFromSourceMatrix(board);
    const auto sample = [&](float x, float z, float sourceHeight) {
        const float wx = world[0] * x + world[8] * z + world[12];
        const float wz = world[2] * x + world[10] * z + world[14];
        const float expected = world[1] * x + world[5] * sourceHeight + world[9] * z + world[13];
        float actual = -999;
        if (!environment.sampleWorldTerrainHeight(wx, wz, actual) || std::abs(actual - expected) > .001f) {
            outFail = "Authored floor sampling failed at source X/Z " + std::to_string(x) + "/" + std::to_string(z) +
                ": expected " + std::to_string(expected) + ", got " + std::to_string(actual);
            return false;
        }
        return true;
    };
    // Every board and bench centre must resolve onto the visible floor, even
    // underneath brush or where a raised cap overlaps the base ground.
    for (int row = 0; row < 10; ++row) {
        for (int col = 0; col < 8; ++col) {
            const float x = -3.5f + col;
            const float north = 4.5f - row;
            // The old entrance's first landing starts at source north=8 m.
            // Its four-cell western ramp occupies north=8..9 m.
            const float y = 6 + north;
            const float height = y >= 9 || (y >= 8 && x >= -1) ? 50.0f : y > 8 && x < -1 ? (y-8)*50 : 0.0f;
            if (!sample(2100 + x * 100, -600 - north * 100, height)) return false;
        }
    }
    // The western ramp interpolates; the southern ledge retains a sharp
    // height change. This checks geometry only, not movement permissions.
    if (!sample(1750, -825, 12.5f) || !sample(2100, -770, 0) || !sample(2100, -830, 50) ||
        !sample(1750, -875, 37.5f)) return false;
    // Behind the board, the grass shelf must separate two half-metre rises.
    if (!sample(1750, -1250, 100) || !sample(1750, -1350, 150) ||
        !sample(1750, -1450, 200)) return false;
    float unchanged = 123;
    if (environment.sampleWorldTerrainHeight(1.0e5f, 1.0e5f, unchanged) || unchanged != 123 ||
        environment.sampleWorldTerrainHeight(std::numeric_limits<float>::quiet_NaN(), 0, unchanged)) {
        outFail = "Missing authored ground must not return an invented surface.";
        return false;
    }
    auto lifted = scene;
    for (auto& node : lifted.nodes) {
        if (node.meshPatch) node.transform->translation[1] += 25;
    }
    if (!environment.applyAuthoredScene(lifted, arena.store, false, &outFail) ||
        !sample(2100, -770, 25) || !sample(2100, -830, 75) ||
        !environment.applyAuthoredScene(scene, arena.store, true, &outFail) ||
        !sample(2100, -830, 50)) return false;
    // Switching back must restore source terrain after the pilot cleared it.
    if (!ph::loadAuthoredSceneDocument(store, std::string(variants::kRoute1.authoredSceneDocumentPath), scene, &outFail) ||
        !environment.applyAuthoredScene(scene, store, false, &outFail)) return false;
    if (environment.terrainTiles().empty()) {
        outFail = "Leaving the pilot failed to restore the original route terrain.";
        return false;
    }
    return true;
}
