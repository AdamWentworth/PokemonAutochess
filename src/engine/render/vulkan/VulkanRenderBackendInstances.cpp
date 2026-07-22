#include "engine/render/vulkan/VulkanRenderBackendInternal.h"

#include <limits>
#include <vector>

bool VulkanRenderBackendImpl::prepareWorldInstances(
    const IRenderBackend::WorldMeshInstance* instances,
    std::size_t instanceCount,
    std::uint32_t& outInstanceCount,
    std::uint32_t& outInstanceBaseWordIndex) {
    outInstanceCount = 0u;
    outInstanceBaseWordIndex = 0u;
    if (!frameActive || !instances || instanceCount == 0u ||
        instanceCount > static_cast<std::size_t>(
            (std::numeric_limits<std::uint32_t>::max)())) {
        return false;
    }

    namespace vulkan = engine::render::vulkan_backend;
    static thread_local std::vector<float> skinMatrixData;
    static thread_local std::vector<std::size_t> skinFloatOffsets;
    static thread_local std::vector<vulkan::WorldInstanceState> instanceStates;

    skinMatrixData.clear();
    skinFloatOffsets.assign(instanceCount, 0u);
    instanceStates.clear();
    instanceStates.reserve(instanceCount);

    std::size_t totalSkinFloatCount = 0u;
    for (std::size_t i = 0u; i < instanceCount; ++i) {
        skinFloatOffsets[i] = totalSkinFloatCount;
        const std::size_t floatCount =
            vulkan::worldInstanceSkinMatrixFloatCount(instances[i]);
        if (floatCount > (std::numeric_limits<std::size_t>::max)() -
                             totalSkinFloatCount) {
            return false;
        }
        totalSkinFloatCount += floatCount;
    }
    if (totalSkinFloatCount >
        static_cast<std::size_t>(kTransientBytesPerFrame / sizeof(float))) {
        return false;
    }

    skinMatrixData.reserve(totalSkinFloatCount);
    for (std::size_t i = 0u; i < instanceCount; ++i) {
        const std::size_t floatCount =
            vulkan::worldInstanceSkinMatrixFloatCount(instances[i]);
        if (floatCount == 0u) continue;
        const float* begin = instances[i].skinMatrices;
        skinMatrixData.insert(skinMatrixData.end(), begin, begin + floatCount);
    }

    std::uint32_t firstSkinMatrixIndex = 0u;
    if (!skinMatrixData.empty()) {
        VkBuffer skinBuffer = VK_NULL_HANDLE;
        VkDeviceSize skinOffset = 0u;
        if (!writeTransient(
                skinMatrixData.data(),
                static_cast<VkDeviceSize>(skinMatrixData.size() * sizeof(float)),
                sizeof(float) * 16u,
                skinBuffer,
                skinOffset) ||
            skinBuffer != frames[currentFrame].transient.buffer) {
            return false;
        }
        firstSkinMatrixIndex = static_cast<std::uint32_t>(
            skinOffset / (sizeof(float) * 16u));
    }

    for (std::size_t i = 0u; i < instanceCount; ++i) {
        const std::uint32_t skinMatrixBaseIndex =
            firstSkinMatrixIndex + static_cast<std::uint32_t>(
                skinFloatOffsets[i] / 16u);
        instanceStates.push_back(
            vulkan::makeWorldInstanceState(instances[i], skinMatrixBaseIndex));
    }

    VkBuffer instanceBuffer = VK_NULL_HANDLE;
    VkDeviceSize instanceOffset = 0u;
    if (!writeTransient(
            instanceStates.data(),
            static_cast<VkDeviceSize>(
                instanceStates.size() * sizeof(vulkan::WorldInstanceState)),
            sizeof(float) * 4u,
            instanceBuffer,
            instanceOffset) ||
        instanceBuffer != frames[currentFrame].transient.buffer) {
        return false;
    }

    const VkDeviceSize baseWordIndex = instanceOffset / (sizeof(float) * 4u);
    // A float in the transform UBO carries this index. The fixed 128 MiB
    // transient buffer keeps it within the exact integer range of float32.
    if (baseWordIndex > 16777216u) return false;
    outInstanceCount = static_cast<std::uint32_t>(instanceCount);
    outInstanceBaseWordIndex = static_cast<std::uint32_t>(baseWordIndex);
    return true;
}
