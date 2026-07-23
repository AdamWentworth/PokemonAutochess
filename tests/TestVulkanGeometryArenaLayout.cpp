#include "engine/render/vulkan/VulkanGeometryArenaLayout.h"

#include <cstdint>
#include <limits>
#include <string>

bool test_vulkan_geometry_arena_layout_contract(std::string& outFail) {
    using engine::render::vulkan_backend::GeometryArenaAllocation;
    using engine::render::vulkan_backend::planGeometryArenaAllocation;

    GeometryArenaAllocation first;
    if (!planGeometryArenaAllocation(4096u, 0u, 80u, 160u, 12u, first) ||
        first.vertexByteOffset != 0u || first.indexByteOffset != 160u ||
        first.endByteOffset != 172u || first.firstIndex != 40u ||
        first.baseVertex != 0) {
        outFail = "Vulkan geometry arena should place the first mesh at draw-compatible offsets.";
        return false;
    }

    GeometryArenaAllocation second;
    if (!planGeometryArenaAllocation(
            4096u, first.endByteOffset, 80u, 160u, 12u, second) ||
        second.vertexByteOffset != 240u || second.indexByteOffset != 400u ||
        second.endByteOffset != 412u || second.firstIndex != 100u ||
        second.baseVertex != 3) {
        outFail = "Vulkan geometry arena should align later meshes for indexed base-vertex draws.";
        return false;
    }

    GeometryArenaAllocation rejected;
    if (planGeometryArenaAllocation(400u, 172u, 80u, 160u, 12u, rejected) ||
        planGeometryArenaAllocation(
            (std::numeric_limits<std::uint64_t>::max)(),
            (std::numeric_limits<std::uint64_t>::max)() - 8u,
            80u,
            160u,
            12u,
            rejected)) {
        outFail = "Vulkan geometry arena should reject page exhaustion and arithmetic overflow.";
        return false;
    }

    return true;
}
