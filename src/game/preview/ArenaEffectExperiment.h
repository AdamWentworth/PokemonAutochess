#pragma once

#include "game/vfx/SampledEffectClip.h"
#include <string_view>

namespace game::preview::arena_effect_experiment {
bool enabled(std::string_view stateScript);
// Presentation-only sequence. It neither deals damage nor deforms navigation.
void append(std::string_view stateScript, float time, const glm::mat4 &viewProjection,
            glm::vec3 cameraForward, const vfx::SampledEffectClip::TextureLoader &textures,
            std::vector<vfx::SampledEffectClip::Batch> &batches);
} // namespace game::preview::arena_effect_experiment
