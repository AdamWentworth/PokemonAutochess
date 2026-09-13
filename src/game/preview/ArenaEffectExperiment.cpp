#include "game/preview/ArenaEffectExperiment.h"
#include "engine/core/Paths.h"
#include "game/assets/DevAssetStore.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace game::preview::arena_effect_experiment {
bool enabled(std::string_view script) {
    return script == "scripts/states/route1_flat_experiment_earthquake.lua" ||
           script == "scripts/states/route1_pilot_earthquake.lua";
}
void append(std::string_view script, float time, const glm::mat4 &vp, glm::vec3 forward,
            const vfx::SampledEffectClip::TextureLoader &textures, std::vector<vfx::SampledEffectClip::Batch> &batches) {
    if (!enabled(script)) return;
    struct Resources {
        vfx::SampledEffectClip clip;
        float scale = 1, verticalScale = 1, groundY = -.035f;
        Resources() {
            assets::DevAssetStore store(engine::paths::dataRoot());
            std::string text, error;
            if (!store.readText("config/vfx/earthquake_experiment.json", text, &error)) throw std::runtime_error(error);
            const auto config = nlohmann::json::parse(text);
            if (!clip.load(store, config.at("clip_directory").get<std::string>(), error)) throw std::runtime_error(error);
            const auto span = clip.groundMaximum() - clip.groundMinimum();
            scale = config.at("footprint_metres").get<float>() / std::max(span.x, span.z);
            verticalScale = config.at("vertical_scale").get<float>();
            groundY = config.at("ground_y").get<float>();
            if (!std::isfinite(scale) || scale <= 0 || !std::isfinite(verticalScale) || verticalScale <= 0 || !std::isfinite(groundY))
                throw std::runtime_error("Invalid Earthquake experiment transform");
            std::cout << "[ArenaEffectExperiment] loaded meshes=" << clip.meshCount() << " cards=" << clip.cardCount()
                      << " seconds=" << clip.duration() << " scale=" << scale << '\n';
        }
    };
    static thread_local Resources resources;
    resources.clip.prewarmTextures(textures);
    const auto horizontal = glm::cross(forward, glm::vec3(0, 1, 0));
    const auto right = glm::dot(horizontal, horizontal) > 1e-8f ? glm::normalize(horizontal) : glm::vec3(1, 0, 0);
    const auto up = glm::normalize(glm::cross(right, forward));
    const float t = std::fmod(std::max(time, 0.0f), 12.0f);
    const glm::vec3 scale(resources.scale, resources.scale * resources.verticalScale, resources.scale);
    resources.clip.append(t - 2.0f, {0, resources.groundY, 0}, scale, vp, right, up, textures, batches);
    resources.clip.append(t - 8.0f, {-1.25f, resources.groundY, -.4f}, scale * .72f, vp, right, up, textures, batches);
    resources.clip.append(t - 8.35f, {1.25f, resources.groundY, .4f}, scale * .72f, vp, right, up, textures, batches);
}
} // namespace game::preview::arena_effect_experiment
