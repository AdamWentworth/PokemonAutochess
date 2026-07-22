#include <cmath>
#include <string>

#include "engine/render/vulkan/VulkanSpriteInstanceState.h"

namespace {

bool near(float lhs, float rhs, float epsilon = 0.0001f) {
    return std::fabs(lhs - rhs) <= epsilon;
}

} // namespace

bool test_vulkan_sprite_instance_state_contract(std::string& outFail) {
    engine::render::backend::DebugSprite sprite;
    sprite.x = 10.0f;
    sprite.y = 20.0f;
    sprite.w = 30.0f;
    sprite.h = 40.0f;
    sprite.u0 = 0.1f;
    sprite.v0 = 0.2f;
    sprite.u1 = 0.7f;
    sprite.v1 = 0.8f;
    sprite.r = 0.25f;
    sprite.g = 0.5f;
    sprite.b = 0.75f;
    sprite.a = 0.9f;

    const auto state =
        engine::render::vulkan_backend::makeSpriteInstanceState(sprite);
    if (!near(state.rectPx[0], 10.0f) ||
        !near(state.rectPx[1], 20.0f) ||
        !near(state.rectPx[2], 30.0f) ||
        !near(state.rectPx[3], 40.0f) ||
        !near(state.uvRect[0], 0.1f) ||
        !near(state.uvRect[1], 0.2f) ||
        !near(state.uvRect[2], 0.7f) ||
        !near(state.uvRect[3], 0.8f) ||
        !near(state.color[0], 0.25f) ||
        !near(state.color[1], 0.5f) ||
        !near(state.color[2], 0.75f) ||
        !near(state.color[3], 0.9f)) {
        outFail = "Vulkan sprite instance packing should preserve rect, UV, and color data.";
        return false;
    }
    return true;
}
