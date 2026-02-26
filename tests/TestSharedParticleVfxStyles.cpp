#include <cmath>
#include <string>

#include <glm/glm.hpp>

#include "game/runtime/SharedParticleVfxStyles.h"

namespace {

bool expect(bool condition, const std::string& message, std::string& outFail) {
    if (condition) return true;
    outFail = message;
    return false;
}

bool approx(float a, float b, float eps = 0.0001f) {
    return std::fabs(a - b) <= eps;
}

} // namespace

bool test_shared_particle_vfx_styles_contract(std::string& outFail) {
    using namespace game::runtime::shared_particle_vfx_styles;

    ParticleSystem::Particle p;
    p.seed = 0.3f;
    p.lifeSec = 0.5f;
    p.maxLifeSec = 1.0f;

    ParticleSystem::RenderSnapshot flipbook;
    flipbook.useFlipbook = true;
    flipbook.flipbookPath = "assets/vfx/example.png";
    const ParticleVisualStyle flipbookStyle = resolveStyle(flipbook, p, 0.25f);
    if (!expect(flipbookStyle.texturePath == flipbook.flipbookPath,
                "resolveStyle should prefer flipbook texture path when flipbook rendering is enabled.",
                outFail)) {
        return false;
    }
    if (!expect(flipbookStyle.alpha > 0.0f && flipbookStyle.alpha <= 1.0f,
                "resolveStyle flipbook branch should clamp alpha to [0,1].",
                outFail)) {
        return false;
    }

    ParticleSystem::RenderSnapshot leaf;
    leaf.useFlipbook = false;
    leaf.shaderFragPath = "assets/shaders/vfx/leaf_impact.frag";
    const ParticleVisualStyle leafStyle = resolveStyle(leaf, p, 0.4f);
    if (!expect(leafStyle.texturePath == "__proc:leaf",
                "resolveStyle should classify leaf impact shaders to the leaf procedural sprite.",
                outFail)) {
        return false;
    }
    if (!expect(leafStyle.color.g > leafStyle.color.r,
                "resolveStyle leaf style should be green-dominant.",
                outFail)) {
        return false;
    }

    ParticleSystem::RenderSnapshot heal;
    heal.shaderFragPath = "assets/shaders/vfx/heal_plus.frag";
    const ParticleVisualStyle healStyle = resolveStyle(heal, p, 0.5f);
    if (!expect(healStyle.texturePath == "__proc:plus",
                "resolveStyle should classify heal_plus shaders to the plus procedural sprite.",
                outFail)) {
        return false;
    }
    if (!expect(healStyle.alpha >= 0.0f && healStyle.alpha <= 1.0f,
                "resolveStyle should clamp computed heal alpha to [0,1].",
                outFail)) {
        return false;
    }

    ParticleSystem::RenderSnapshot unknown;
    unknown.shaderFragPath = "assets/shaders/vfx/custom_unknown.frag";
    p.seed = 0.9f;
    const ParticleVisualStyle unknownStyle = resolveStyle(unknown, p, 0.2f);
    if (!expect(unknownStyle.texturePath == "__proc:soft_circle",
                "resolveStyle should fall back to soft-circle for unknown particle shaders.",
                outFail)) {
        return false;
    }
    if (!expect(approx(unknownStyle.color.r, 1.0f) &&
                    approx(unknownStyle.color.g, 1.0f) &&
                    approx(unknownStyle.color.b, 1.0f),
                "resolveStyle fallback should keep white tint for unknown particle shaders.",
                outFail)) {
        return false;
    }

    return true;
}
