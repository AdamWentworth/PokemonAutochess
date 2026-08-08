#include <cmath>
#include <string>

#include "engine/render/RenderBackendTypes.h"
#include "engine/render/RendererParityContract.h"
#include "engine/render/vulkan/VulkanEnvironmentParity.h"
#include "engine/render/vulkan/VulkanWorldInstanceState.h"
#include "engine/render/vulkan/VulkanWorldMaterialLayout.h"
#include "engine/render/vulkan/VulkanWorldMaterialState.h"
#include "engine/render/vulkan/VulkanWorldSpecializedMaterialState.h"
#include "engine/render/vulkan/VulkanWorldTransformState.h"
#include "engine/render/vulkan/VulkanWorldViewState.h"

namespace {

bool near(float lhs, float rhs, float epsilon = 0.0001f) {
    return std::fabs(lhs - rhs) <= epsilon;
}

} // namespace

bool test_vulkan_world_material_state_contract(std::string& outFail) {
    namespace backend = engine::render::backend;
    namespace vulkan = engine::render::vulkan_backend;

    if (vulkan::kWorldMaterialTextureCount != 8u ||
        static_cast<std::uint32_t>(vulkan::WorldMaterialBinding::Environment) != 5u ||
        static_cast<std::uint32_t>(
            vulkan::WorldMaterialBinding::LightProjection) != 6u ||
        static_cast<std::uint32_t>(
            vulkan::WorldMaterialBinding::ProjectedShadow) != 7u) {
        outFail = "Vulkan material descriptor bindings should remain stable.";
        return false;
    }

    auto environmentContract =
        engine::render::parity_contract::makeBaselineConfig();
    vulkan::applyNeutralPmremParityContract(
        VK_FORMAT_R16G16B16A16_SFLOAT,
        environmentContract);
    if (!engine::render::parity_contract::validate(environmentContract).ok) {
        outFail = "Vulkan RGBA16F environment resources should satisfy the parity contract.";
        return false;
    }
    vulkan::applyNeutralPmremParityContract(
        VK_FORMAT_R8G8B8A8_UNORM,
        environmentContract);
    if (engine::render::parity_contract::validate(environmentContract).ok) {
        outFail = "Vulkan RGBM8 environment resources should fail the RGBA16F parity contract.";
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
        !near(fallback.outlineExtrude, 0.0f)) {
        outFail = "Vulkan fallback material constants should preserve the shared defaults.";
        return false;
    }
    const auto fallbackTransform = vulkan::makeWorldTransformState(nullptr, 0u);
    if (!near(fallbackTransform.modelMatrix[0], 1.0f) ||
        !near(fallbackTransform.modelMatrix[5], 1.0f) ||
        !near(fallbackTransform.modelMatrix[10], 1.0f) ||
        !near(fallbackTransform.modelMatrix[15], 1.0f) ||
        !near(fallbackTransform.vertexColorMultiplier[0], 1.0f) ||
        !near(fallbackTransform.skinningParams[0], 0.0f) ||
        !near(fallbackTransform.instanceParams[0], 0.0f)) {
        outFail = "Vulkan transform state should have stable identity defaults.";
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
    const unsigned char projectedShadowPixel[4]{255u, 255u, 255u, 255u};
    texture.projectedShadowRgba = projectedShadowPixel;
    texture.projectedShadowWidth = 1;
    texture.projectedShadowHeight = 1;
    texture.projectedShadowEnabled = 1u;
    texture.projectedShadowSamplingScale = 1.5f;
    texture.projectedShadowBias = 0.9f;
    texture.projectedShadowMatrix[12] = -0.25f;
    texture.lightProjectionUvRowU = {0.11f, 0.12f, 0.13f, 0.14f};
    texture.lightProjectionUvRowV = {0.21f, 0.22f, 0.23f, 0.24f};
    texture.modelMatrix[12] = 10.0f;
    texture.vertexColorMulR = 0.25f;
    texture.vertexColorMulG = 0.5f;
    texture.vertexColorMulB = 0.75f;
    texture.vertexColorMulA = 0.8f;
    float skinMatrixMarker = 1.0f;
    texture.gpuSkinning = 1u;
    texture.gpuSkinningMode = 1u;
    texture.skinMatrixCount = 200u;
    texture.skinMatrices = &skinMatrixMarker;

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
        !near(material.outlineExtrude, 0.0f)) {
        outFail = "Vulkan material constants should clamp external material state consistently.";
        return false;
    }
    const auto transform = vulkan::makeWorldTransformState(&texture, 42u);
    if (!near(transform.modelMatrix[12], 10.0f) ||
        !near(transform.vertexColorMultiplier[0], 0.25f) ||
        !near(transform.vertexColorMultiplier[1], 0.5f) ||
        !near(transform.vertexColorMultiplier[2], 0.75f) ||
        !near(transform.vertexColorMultiplier[3], 0.8f) ||
        !near(transform.skinningParams[0], 1.0f) ||
        !near(transform.skinningParams[1], 1.0f) ||
        !near(transform.skinningParams[2], 128.0f) ||
        !near(transform.skinningParams[3], 42.0f) ||
        vulkan::worldSkinMatrixFloatCount(&texture) != 4096u) {
        outFail = "Vulkan transform state should clamp and preserve GPU skinning inputs.";
        return false;
    }
    const auto instancedTransform =
        vulkan::makeWorldTransformState(&texture, 42u, true, 256u);
    if (!near(instancedTransform.instanceParams[0], 1.0f) ||
        !near(instancedTransform.instanceParams[1], 256.0f)) {
        outFail = "Vulkan transform state should identify the instance-word range.";
        return false;
    }

    backend::WorldMeshInstance instance;
    instance.modelMatrix[13] = 3.0f;
    instance.vertexColorMulR = 0.2f;
    instance.vertexColorMulG = 0.4f;
    instance.vertexColorMulB = 0.6f;
    instance.vertexColorMulA = 0.8f;
    instance.gpuSkinning = 1u;
    instance.gpuSkinningMode = 1u;
    instance.skinMatrixCount = 200u;
    instance.skinMatrices = &skinMatrixMarker;
    const auto instanceState = vulkan::makeWorldInstanceState(instance, 17u);
    if (!near(instanceState.modelMatrix[13], 3.0f) ||
        !near(instanceState.vertexColorMultiplier[0], 0.2f) ||
        !near(instanceState.vertexColorMultiplier[3], 0.8f) ||
        !near(instanceState.skinningParams[0], 1.0f) ||
        !near(instanceState.skinningParams[1], 1.0f) ||
        !near(instanceState.skinningParams[2], 128.0f) ||
        !near(instanceState.skinningParams[3], 17.0f) ||
        vulkan::worldInstanceSkinMatrixFloatCount(instance) != 4096u) {
        outFail = "Vulkan instance state should preserve transforms, colors, and clamped palettes.";
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
        !near(specialized.flipbook1[3], 12.0f) ||
        !near(specialized.projectedShadowMatrix[12], -0.25f) ||
        !near(specialized.projectedShadowParams[0], 1.0f) ||
        !near(specialized.projectedShadowParams[1], 1.5f) ||
        !near(specialized.projectedShadowParams[2], 0.9f) ||
        !near(specialized.projectedShadowParams[3], 0.9f) ||
        !near(specialized.lightProjectionUvRowU[0], 0.11f) ||
        !near(specialized.lightProjectionUvRowU[3], 0.14f) ||
        !near(specialized.lightProjectionUvRowV[0], 0.21f) ||
        !near(specialized.lightProjectionUvRowV[3], 0.24f)) {
        outFail = "Vulkan specialized material state should preserve animated material inputs.";
        return false;
    }
    backend::WorldTextureData routeTexture = texture;
    routeTexture.materialMode = 7u;
    routeTexture.projectedShadowBias = 0.004f;
    const auto routeSpecialized =
        vulkan::makeWorldSpecializedMaterialState(&routeTexture);
    if (!near(routeSpecialized.projectedShadowParams[2], 0.004f) ||
        !near(routeSpecialized.projectedShadowParams[3], 0.0f)) {
        outFail =
            "Route 1 materials must keep projected-shadow bias separate from model texture-detail bias.";
        return false;
    }
    backend::WorldTextureData nativeTexture = texture;
    nativeTexture.materialMode = 27u;
    const auto nativeSpecialized =
        vulkan::makeWorldSpecializedMaterialState(&nativeTexture);
    if (!near(nativeSpecialized.projectedShadowParams[3], 0.0f)) {
        outFail =
            "Native packed model materials must bypass generic texture-detail scaling.";
        return false;
    }
    return true;
}
