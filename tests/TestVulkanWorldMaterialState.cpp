#include <cmath>
#include <string>

#include "engine/render/RenderBackendTypes.h"
#include "engine/render/vulkan/VulkanWorldMaterialLayout.h"
#include "engine/render/vulkan/VulkanWorldMaterialState.h"
#include "engine/render/vulkan/VulkanWorldSpecializedMaterialState.h"
#include "engine/render/vulkan/VulkanWorldViewState.h"

namespace {

bool near(float lhs, float rhs, float epsilon = 0.0001f) {
    return std::fabs(lhs - rhs) <= epsilon;
}

} // namespace

bool test_vulkan_world_material_state_contract(std::string& outFail) {
    namespace backend = engine::render::backend;
    namespace vulkan = engine::render::vulkan_backend;

    if (vulkan::kWorldMaterialTextureCount != 6u ||
        static_cast<std::uint32_t>(vulkan::WorldMaterialBinding::Environment) != 5u ||
        !near(vulkan::kNeutralPmremRgbmRange, 16.0f)) {
        outFail = "Vulkan material descriptor and PMREM contracts should remain stable.";
        return false;
    }

    const auto fallbackView = vulkan::makeWorldViewState(nullptr);
    if (!near(fallbackView.cameraPosition[1], 7.0f) ||
        !near(fallbackView.cameraPosition[2], 9.0f) ||
        !near(fallbackView.cameraForward[1], -0.6139406f) ||
        !near(fallbackView.cameraForward[2], -0.7893522f) ||
        !near(fallbackView.cameraTarget[1], -1.0f)) {
        outFail = "Vulkan fallback view state should preserve shared camera defaults.";
        return false;
    }

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
    const auto fallbackSpecialized = vulkan::makeWorldSpecializedMaterialState(nullptr);
    if (!near(fallbackSpecialized.timingFlagsAtlas[0], 0.0f) ||
        !near(fallbackSpecialized.rect0[2], 1.0f) ||
        !near(fallbackSpecialized.rect1[3], 1.0f) ||
        !near(fallbackSpecialized.flipbook0[0], 1.0f) ||
        !near(fallbackSpecialized.flipbook1[2], 1.0f)) {
        outFail = "Vulkan specialized material state should have stable neutral defaults.";
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
    texture.cameraForwardX = 0.1f;
    texture.cameraForwardY = 0.2f;
    texture.cameraForwardZ = 0.3f;
    texture.cameraTargetX = 7.0f;
    texture.cameraTargetY = 8.0f;
    texture.cameraTargetZ = 9.0f;
    texture.materialTimeSec = 12.5f;
    texture.materialFlags = 11.0f;
    texture.materialAtlasWidth = 256.0f;
    texture.materialAtlasHeight = 128.0f;
    texture.materialRect0U = 0.1f;
    texture.materialRect0V = 0.2f;
    texture.materialRect0W = 0.3f;
    texture.materialRect0H = 0.4f;
    texture.materialRect1U = 0.5f;
    texture.materialRect1V = 0.6f;
    texture.materialRect1W = 0.7f;
    texture.materialRect1H = 0.8f;
    texture.materialFlipbook0Cols = 4.0f;
    texture.materialFlipbook0Rows = 5.0f;
    texture.materialFlipbook0Frames = 18.0f;
    texture.materialFlipbook0Fps = 24.0f;
    texture.materialFlipbook1Cols = 2.0f;
    texture.materialFlipbook1Rows = 3.0f;
    texture.materialFlipbook1Frames = 6.0f;
    texture.materialFlipbook1Fps = 12.0f;

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

    const auto view = vulkan::makeWorldViewState(&texture);
    if (!near(view.cameraPosition[0], 4.0f) ||
        !near(view.cameraPosition[1], 5.0f) ||
        !near(view.cameraPosition[2], 6.0f) ||
        !near(view.cameraForward[0], 0.1f) ||
        !near(view.cameraForward[1], 0.2f) ||
        !near(view.cameraForward[2], 0.3f) ||
        !near(view.cameraTarget[0], 7.0f) ||
        !near(view.cameraTarget[1], 8.0f) ||
        !near(view.cameraTarget[2], 9.0f)) {
        outFail = "Vulkan view state should preserve per-draw camera inputs.";
        return false;
    }
    const auto specialized = vulkan::makeWorldSpecializedMaterialState(&texture);
    if (!near(specialized.timingFlagsAtlas[0], 12.5f) ||
        !near(specialized.timingFlagsAtlas[1], 11.0f) ||
        !near(specialized.timingFlagsAtlas[2], 256.0f) ||
        !near(specialized.timingFlagsAtlas[3], 128.0f) ||
        !near(specialized.rect0[0], 0.1f) ||
        !near(specialized.rect0[3], 0.4f) ||
        !near(specialized.rect1[0], 0.5f) ||
        !near(specialized.rect1[3], 0.8f) ||
        !near(specialized.flipbook0[2], 18.0f) ||
        !near(specialized.flipbook0[3], 24.0f) ||
        !near(specialized.flipbook1[2], 6.0f) ||
        !near(specialized.flipbook1[3], 12.0f)) {
        outFail = "Vulkan specialized material state should preserve animated material inputs.";
        return false;
    }
    return true;
}
