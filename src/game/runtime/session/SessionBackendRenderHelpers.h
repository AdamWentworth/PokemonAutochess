#pragma once

#include <string>
#include <vector>

#include "engine/render/ModelAnimationTypes.h"
#include "game/runtime/render_model_cache/RenderModelCache.h"

class IRenderBackend;

namespace game::runtime::session_backend_render_helpers {

std::string toLowerCopy(std::string s);
std::string stripSuffix(const std::string& s, const std::string& suffix);
std::string makeBackendCardPrewarmLabel(const std::string& texturePath);

int resolveBackendAnimIndexByName(const std::vector<engine::render::model_types::AnimationClip>& animations,
                                  const std::string& requestedName);
int findBackendAnimIndexBySubstring(const std::vector<engine::render::model_types::AnimationClip>& animations,
                                    const std::vector<std::string>& needles);

std::size_t prewarmBackendWorldTexturesForMesh(
    IRenderBackend* renderer,
    const game::runtime::render_model::MeshData* mesh);

} // namespace game::runtime::session_backend_render_helpers
