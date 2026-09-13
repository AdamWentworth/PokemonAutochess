#include "game/runtime/shared/scene/ArenaSceneActivation.h"
#include "game/runtime/shared/scene/AuthoredArenaBundle.h"
#include "game/runtime/shared/scene/Route1RuntimeEnvironment.h"
#include "game/arena/AuthoredCombatMap.h"
#include "game/world/GameWorld.h"
#include "game/GameConfig.h"
#include <cmath>

namespace game::runtime::arena_scene_activation {
bool applyGameplay(const engine::IAssetStore &host, const route1_scene_variants::Variant &variant,
                   GameWorld &world, std::string *error) {
    if (variant.arenaBundlePath.empty()) {
        world.setCombatMapRules({});
        world.clearGroundHeightResolver();
        world.setBenchGapCells(world.getConfig().benchGapCells);
        return true;
    }
    authored_arena::Bundle bundle;
    route1_environment::BoardLayoutTransform layout;
    if (!bundle.load(host, std::string(variant.arenaBundlePath), error) ||
        !route1_environment::loadBoardLayoutTransform(bundle.store, bundle.boardPath, layout, error)) return false;
    if (bundle.scene.sceneId != variant.sceneId || static_cast<int>(layout.boardCells[0]) != world.getConfig().cols ||
        static_cast<int>(layout.boardCells[1]) != world.getConfig().rows || !route1_environment::boardRegistrationMatchesTerrainGrid(layout)) {
        if (error) *error = "Arena gameplay registration does not match the active board.";
        return false;
    }
    auto map = std::make_shared<game::arena::AuthoredCombatMap>(std::move(bundle.map),
                                                                game::arena::Cell{layout.terrainGridOrigin[0], layout.terrainGridOrigin[1]});
    world.setCombatMapRules(map);
    world.bindGroundHeightResolver(map.get(), [map, layout, &world](float x, float z, float &y) {
        const float cellSize = world.getBoardCellSize();
        const float sourceX = (x / cellSize + layout.boardCells[0] * 0.5f + layout.terrainGridOrigin[0]) * 100.0f;
        const float sourceZ = (z / cellSize + layout.boardCells[1] * 0.5f + layout.terrainGridOrigin[1]) * 100.0f;
        const auto *tile = map->data().tileAt(static_cast<int>(std::floor(sourceX / 100)), static_cast<int>(std::floor(sourceZ / 100)));
        if (!tile) return false;
        y = (tile->heightAt(sourceX, sourceZ) - layout.sourceAnchorCm[1]) * layout.sourceUnitsToWorld + layout.worldAnchor[1];
        return true;
    });
    world.setBenchGapCells(static_cast<int>(layout.benchGapCells));
    return true;
}

bool apply(const engine::IAssetStore &host, const route1_scene_variants::Variant &variant,
           route1_environment::RuntimeEnvironment &candidate, bool editorPreview,
           bool terrainV2, std::string *error) {
    authored_arena::Bundle bundle;
    const engine::IAssetStore *inputs = &host;
    std::string boardPath(variant.boardLayoutManifestPath);
    engine::assets::phlosion::AuthoredSceneDocument scene;
    const bool bundled = !variant.arenaBundlePath.empty();
    if (bundled) {
        if (!bundle.load(host, std::string(variant.arenaBundlePath), error)) return false;
        if (bundle.scene.sceneId != variant.sceneId) {
            if (error) *error = "Arena archive belongs to a different scene.";
            return false;
        }
        inputs = &bundle.store;
        boardPath = bundle.boardPath;
        scene = bundle.scene;
    }
    if (bundled || editorPreview || inputs->exists(boardPath)) {
        route1_environment::BoardLayoutTransform layout;
        if (!route1_environment::loadBoardLayoutTransform(*inputs, boardPath, layout, error)) return false;
        if (editorPreview ? !candidate.previewBoardLayout(layout, error) : !candidate.applyBoardLayout(layout, error)) return false;
    }
    if (!bundled && (editorPreview || inputs->exists(std::string(variant.authoredSceneDocumentPath)))) {
        if (!engine::assets::phlosion::loadAuthoredSceneDocument(*inputs, std::string(variant.authoredSceneDocumentPath), scene, error)) return false;
    }
    if (bundled || !scene.sceneId.empty()) return candidate.applyAuthoredScene(scene, *inputs, terrainV2, error);
    return true;
}
} // namespace game::runtime::arena_scene_activation
