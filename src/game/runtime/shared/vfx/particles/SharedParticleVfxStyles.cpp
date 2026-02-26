#include "game/runtime/shared/vfx/particles/SharedParticleVfxStyles.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace {

std::string toLowerCopy(std::string value) {
    std::transform(value.begin(),
                   value.end(),
                   value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

} // namespace

namespace game::runtime::shared_particle_vfx_styles {

ParticleVisualStyle resolveStyle(const ParticleSystem::RenderSnapshot& snapshot,
                                 const ParticleSystem::Particle& particle,
                                 float age01) {
    ParticleVisualStyle style;
    style.texturePath = "__proc:soft_circle";

    const std::string frag = toLowerCopy(snapshot.shaderFragPath);
    const float seed = std::clamp(particle.seed, 0.0f, 1.0f);
    const float fadeInFast = std::clamp(age01 / 0.12f, 0.0f, 1.0f);
    const float fadeOutLate = 1.0f - std::clamp((age01 - 0.70f) / 0.30f, 0.0f, 1.0f);
    const float lifeFade = std::clamp(1.0f - age01, 0.0f, 1.0f);

    if (snapshot.useFlipbook && !snapshot.flipbookPath.empty()) {
        style.texturePath = snapshot.flipbookPath;
        style.color = glm::vec3(1.0f);
        style.alpha = std::clamp((0.18f + lifeFade * 0.95f), 0.0f, 1.0f);
        return style;
    }

    if (frag.find("leaf_impact") != std::string::npos) {
        style.texturePath = "__proc:leaf";
        style.color = glm::mix(glm::vec3(0.12f, 0.45f, 0.15f),
                               glm::vec3(0.50f, 0.88f, 0.36f),
                               0.45f + 0.35f * seed);
        style.alpha = std::pow(lifeFade, 0.65f);
    } else if (frag.find("splat_impact") != std::string::npos) {
        style.texturePath = "__proc:starburst";
        style.color = glm::mix(glm::vec3(1.00f, 0.96f, 0.92f),
                               glm::vec3(0.98f, 0.78f, 0.70f),
                               0.55f + 0.25f * seed);
        const float fadeIn = std::clamp(age01 / 0.06f, 0.0f, 1.0f);
        const float fadeOut = 1.0f - std::clamp((age01 - 0.35f) / 0.65f, 0.0f, 1.0f);
        style.alpha = 0.80f * fadeIn * fadeOut;
    } else if (frag.find("impact_spark") != std::string::npos) {
        style.texturePath = "__proc:dot";
        style.color = glm::mix(glm::vec3(0.96f, 0.72f, 0.66f),
                               glm::vec3(0.98f, 0.92f, 0.88f),
                               seed);
        style.alpha = 0.70f * fadeInFast *
                      (1.0f - std::clamp((age01 - 0.55f) / 0.45f, 0.0f, 1.0f));
    } else if (frag.find("claw_swipe") != std::string::npos) {
        style.texturePath = "__proc:claw";
        const bool metallic = seed >= 0.55f;
        style.color = metallic ? glm::vec3(0.88f, 0.92f, 0.98f)
                               : glm::vec3(0.96f, 0.96f, 0.96f);
        const float outStart = metallic ? 0.60f : 0.78f;
        style.alpha = (metallic ? 0.95f : 1.10f) *
                      std::clamp(age01 / (metallic ? 0.06f : 0.04f), 0.0f, 1.0f) *
                      (1.0f - std::clamp((age01 - outStart) /
                                         std::max(0.01f, 1.0f - outStart),
                                         0.0f,
                                         1.0f));
    } else if (frag.find("aqua_swoosh") != std::string::npos) {
        style.texturePath = "__proc:swoosh";
        if (seed < 0.40f) {
            style.color = glm::vec3(0.58f, 0.92f, 1.00f);
        } else if (seed < 0.75f) {
            style.color = glm::vec3(0.70f, 0.95f, 1.00f);
        } else {
            style.color = glm::vec3(0.52f, 0.82f, 1.00f);
        }
        style.alpha = 0.95f * fadeInFast * fadeOutLate;
    } else if (frag.find("seed_projectile") != std::string::npos) {
        style.texturePath = "__proc:seed";
        style.color = glm::mix(glm::vec3(0.50f, 0.40f, 0.14f),
                               glm::vec3(0.96f, 0.86f, 0.34f),
                               0.50f + 0.20f * seed);
        style.alpha = std::pow(lifeFade, 0.65f);
    } else if (frag.find("leech_drain_dot") != std::string::npos) {
        style.texturePath = "__proc:dot";
        style.color = glm::mix(glm::vec3(0.10f, 0.50f, 0.18f),
                               glm::vec3(0.45f, 0.95f, 0.40f),
                               0.65f);
        style.alpha = fadeInFast * fadeOutLate;
    } else if (frag.find("heal_plus") != std::string::npos) {
        style.texturePath = "__proc:plus";
        style.color = glm::mix(glm::vec3(0.10f, 0.55f, 0.18f),
                               glm::vec3(0.35f, 0.95f, 0.45f),
                               lifeFade);
        style.alpha = std::clamp(age01 / 0.18f, 0.0f, 1.0f) * fadeOutLate;
    } else {
        style.texturePath = "__proc:soft_circle";
        style.color = glm::vec3(1.0f);
        style.alpha = fadeInFast * fadeOutLate;
    }

    style.alpha = std::clamp(style.alpha, 0.0f, 1.0f);
    style.color = glm::clamp(style.color, glm::vec3(0.0f), glm::vec3(1.0f));
    return style;
}

} // namespace game::runtime::shared_particle_vfx_styles
