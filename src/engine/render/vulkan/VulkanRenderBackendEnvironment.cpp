#include "engine/render/vulkan/VulkanRenderBackendInternal.h"

#include <cstddef>
#include <cmath>
#include <iostream>
#include <stdexcept>

#include "engine/render/NeutralPmrem.h"

void VulkanRenderBackendImpl::createEnvironmentResources() {
    const auto& atlas = engine::render::neutral_pmrem::getNeutralRoomPmremAtlas();
    const std::size_t expectedBytes = static_cast<std::size_t>(atlas.width) *
                                      static_cast<std::size_t>(atlas.height) * 4u;
    if (atlas.width <= 0 || atlas.height <= 0 || atlas.rgba.size() != expectedBytes) {
        throw std::runtime_error("Vulkan neutral PMREM atlas is invalid.");
    }
    if (std::fabs(
            atlas.rgbmRange - engine::render::vulkan_backend::kNeutralPmremRgbmRange) >
        0.0001f) {
        throw std::runtime_error(
            "Vulkan neutral PMREM RGBM range does not match the shader contract.");
    }

    neutralPmremTexture = createTexture(
        atlas.rgba.data(),
        atlas.width,
        atlas.height,
        false,
        33071,
        33071,
        false);
    std::cout << "[Vulkan][PMREM] Uploaded RGBM8 atlas "
              << atlas.width << "x" << atlas.height
              << " range=" << atlas.rgbmRange << "\n"
              << std::flush;
}

void VulkanRenderBackendImpl::destroyEnvironmentResources() {
    destroyTexture(neutralPmremTexture);
}
