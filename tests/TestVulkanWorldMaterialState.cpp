#include <cmath>
#include <string>

#include "engine/render/RenderBackendTypes.h"
#include "engine/render/vulkan/VulkanWorldMaterialState.h"

namespace {

bool near(float lhs, float rhs, float epsilon = 0.0001f) {
    return std::fabs(lhs - rhs) <= epsilon;
}

} // namespace

bool test_vulkan_world_material_state_contract(std::string& outFail) {
    namespace backend = engine::render::backend;
    namespace vulkan = engine::render::vulkan_backend;

    const auto fallback = vulkan::makeWorldPushConstants(nullptr);
    if (!near(fallback.alphaMode, 0.0f) ||
        !near(fallback.alphaCutoff, 0.5f) ||
        !near(fallback.alphaWindowMin, 0.0f) ||
        !near(fallback.alphaWindowMax, 1.0f) ||
        !near(fallback.cameraPosX, 0.0f) ||
        !near(fallback.cameraPosY, 7.0f) ||
        !near(fallback.cameraPosZ, 9.0f)) {
        outFail = "Vulkan fallback material constants should preserve the shared defaults.";
        return false;
    }

    backend::WorldTextureData texture;
    texture.alphaMode = 9u;
    texture.alphaCutoff = 2.0f;
    texture.alphaWindowMin = -1.0f;
    texture.alphaWindowMax = 4.0f;
    texture.clipSpaceDepthBias = -0.25f;
    texture.materialMode = 2u;
    texture.normalScale = -3.0f;
    texture.metallicFactor = 2.0f;
    texture.roughnessFactor = -2.0f;
    texture.occlusionStrength = 1.5f;
    texture.emissiveFactorR = -1.0f;
    texture.emissiveFactorG = 2.0f;
    texture.emissiveFactorB = 3.0f;
    texture.cameraPosX = 4.0f;
    texture.cameraPosY = 5.0f;
    texture.cameraPosZ = 6.0f;

    const auto material = vulkan::makeWorldPushConstants(&texture);
    if (!near(material.alphaMode, 2.0f) ||
        !near(material.alphaCutoff, 1.0f) ||
        !near(material.alphaWindowMin, 0.0f) ||
        !near(material.alphaWindowMax, 1.0f) ||
        !near(material.clipSpaceDepthBias, 0.0f) ||
        !near(material.materialMode, 2.0f) ||
        !near(material.normalScale, 0.0f) ||
        !near(material.metallicFactor, 1.0f) ||
        !near(material.roughnessFactor, 0.0f) ||
        !near(material.occlusionStrength, 1.0f) ||
        !near(material.emissiveFactorR, 0.0f) ||
        !near(material.emissiveFactorG, 2.0f) ||
        !near(material.emissiveFactorB, 3.0f) ||
        !near(material.cameraPosX, 4.0f) ||
        !near(material.cameraPosY, 5.0f) ||
        !near(material.cameraPosZ, 6.0f)) {
        outFail = "Vulkan material constants should clamp external material state consistently.";
        return false;
    }
    return true;
}
