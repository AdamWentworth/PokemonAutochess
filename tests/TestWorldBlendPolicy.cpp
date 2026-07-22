#include "engine/render/WorldBlendPolicy.h"
#include "engine/render/vulkan/VulkanWorldPipelinePolicy.h"

#include <string>

bool test_world_blend_policy_contract(std::string& outFail) {
    using engine::render::world_blend::Mode;
    using engine::render::world_blend::resolve;

    const auto opaque = resolve(0u, 1u, true, true, true);
    if (opaque.enabled || opaque.dualSourceEnabled || opaque.mode != Mode::Additive) {
        outFail = "opaque materials must not enable blending or dual-source output";
        return false;
    }

    const auto unsupported = resolve(2u, 1u, false, true, false);
    if (!unsupported.enabled || unsupported.depthTestEnabled ||
        unsupported.dualSourceEnabled || unsupported.mode != Mode::Additive) {
        outFail = "unsupported dual-source requests must preserve the standard blend fallback";
        return false;
    }

    const auto dualAdditive = resolve(2u, 1u, true, true, true);
    if (!dualAdditive.dualSourceEnabled || dualAdditive.mode != Mode::Additive) {
        outFail = "supported dual-source additive requests must keep their authored equation";
        return false;
    }

    const auto dualPremultiplied = resolve(2u, 2u, true, true, true);
    if (!dualPremultiplied.dualSourceEnabled || dualPremultiplied.mode != Mode::Alpha) {
        outFail = "dual-source premultiplied requests must resolve to the authored alpha equation";
        return false;
    }

    const auto sanitized = resolve(2u, 255u, true, false, true);
    if (sanitized.dualSourceEnabled || sanitized.mode != Mode::Alpha) {
        outFail = "unknown blend modes must resolve to standard alpha blending";
        return false;
    }

    engine::render::backend::WorldTextureData texture{};
    texture.alphaMode = 2u;
    texture.blendMode = 1u;
    texture.dualSourceBlendEnabled = 1u;
    texture.depthTestEnabled = 0u;
    const std::size_t fallbackIndex =
        engine::render::vulkan_backend::worldPipelineIndex(&texture, false);
    const std::size_t nativeIndex =
        engine::render::vulkan_backend::worldPipelineIndex(&texture, true);
    if (fallbackIndex != 5u || nativeIndex != 11u) {
        outFail = "Vulkan must route dual-source requests to native or standard pipelines by capability";
        return false;
    }

    return true;
}
