#include "engine/render/vulkan/VulkanWorldIndirectBatch.h"
#include "engine/render/vulkan/VulkanWorldIndirectState.h"

#include <cmath>
#include <string>
#include <vector>

namespace {

bool near(float lhs, float rhs, float epsilon = 0.0001f) {
    return std::fabs(lhs - rhs) <= epsilon;
}

} // namespace

bool test_vulkan_world_indirect_state_contract(std::string& outFail) {
    namespace backend = engine::render::backend;
    namespace vulkan = engine::render::vulkan_backend;

    backend::WorldTextureData texture;
    texture.alphaMode = 1u;
    texture.alphaCutoff = 0.35f;
    texture.materialMode = 2u;
    texture.normalScale = 0.8f;
    texture.metallicFactor = 0.7f;
    texture.roughnessFactor = 0.6f;
    texture.occlusionStrength = 0.5f;
    texture.emissiveFactorR = 0.4f;
    texture.materialTimeSec = 12.0f;
    const auto state = vulkan::makeWorldIndirectDrawState(
        &texture, 23u, 456u);
    if (!near(state.materialParams[0], 1.0f) ||
        !near(state.materialParams[1], 0.35f) ||
        !near(state.materialParams[3], 2.0f) ||
        !near(state.pbrFactors[0], 0.8f) ||
        !near(state.emissiveAndCamera[0], 0.4f) ||
        !near(state.specializedMaterial.timingFlagsAtlas[0], 12.0f) ||
        state.drawParams[0] != 23u || state.drawParams[1] != 456u) {
        outFail = "Vulkan indirect draw state should preserve material and instance addressing.";
        return false;
    }

    const auto outlineState =
        vulkan::makeWorldIndirectOutlineDrawState(state);
    if (!near(outlineState.materialParams[3], 3.0f) ||
        !near(outlineState.shadingParams[2],
              vulkan::kWorldCharacterOutlineExtrude) ||
        outlineState.drawParams != state.drawParams ||
        !near(outlineState.pbrFactors[0], state.pbrFactors[0])) {
        outFail =
            "Vulkan indirect outline state should only override outline shading.";
        return false;
    }

    const std::vector<vulkan::WorldIndirectDrawKey> keys{
        {11u, 0u},
        {11u, 0u},
        {11u, 2u},
        {22u, 2u},
        {22u, 2u},
    };
    const auto runs = vulkan::buildWorldIndirectRuns(keys);
    if (runs.size() != 3u || runs[0].firstDraw != 0u ||
        runs[0].drawCount != 2u || runs[1].firstDraw != 2u ||
        runs[1].drawCount != 1u || runs[2].firstDraw != 3u ||
        runs[2].drawCount != 2u) {
        outFail = "Vulkan indirect runs should merge only contiguous pipeline and arena matches.";
        return false;
    }
    return true;
}
