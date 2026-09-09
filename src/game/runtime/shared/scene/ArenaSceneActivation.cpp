#include "game/runtime/shared/scene/ArenaSceneActivation.h"
#include "game/runtime/shared/scene/AuthoredArenaBundle.h"
#include "game/runtime/shared/scene/Route1RuntimeEnvironment.h"

namespace game::runtime::arena_scene_activation {
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
