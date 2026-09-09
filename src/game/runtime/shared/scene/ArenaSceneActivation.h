#pragma once

#include "game/runtime/shared/scene/Route1SceneVariants.h"
#include <string>

namespace engine {
class IAssetStore;
}
class GameWorld;
namespace game::runtime::route1_environment {
class RuntimeEnvironment;
}

namespace game::runtime::arena_scene_activation {
// Activate simulation before the first tick, including renderer-free sessions.
// The same validated archive supplies logical floor height until a visual floor
// resolver is available. Failure leaves the previous map intact.
bool applyGameplay(const engine::IAssetStore &host, const route1_scene_variants::Variant &variant,
                   GameWorld &world, std::string *error);
// Apply to a candidate environment before swapping it into the live scene.
// Bundled arenas never fall back to the independently editable loose mirrors.
bool apply(const engine::IAssetStore &host, const route1_scene_variants::Variant &variant,
           route1_environment::RuntimeEnvironment &candidate, bool editorPreview,
           bool terrainV2, std::string *error);
} // namespace game::runtime::arena_scene_activation
