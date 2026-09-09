#pragma once

#include "game/runtime/shared/scene/Route1SceneVariants.h"
#include <string>

namespace engine {
class IAssetStore;
}
namespace game::runtime::route1_environment {
class RuntimeEnvironment;
}

namespace game::runtime::arena_scene_activation {
// Apply to a candidate environment before swapping it into the live scene.
// Bundled arenas never fall back to the independently editable loose mirrors.
bool apply(const engine::IAssetStore &host, const route1_scene_variants::Variant &variant,
           route1_environment::RuntimeEnvironment &candidate, bool editorPreview,
           bool terrainV2, std::string *error);
} // namespace game::runtime::arena_scene_activation
