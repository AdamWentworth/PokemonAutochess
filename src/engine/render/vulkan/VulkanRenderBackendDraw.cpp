#include "engine/render/vulkan/VulkanRenderBackendInternal.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <vector>

#include "engine/render/DebugGeometry.h"

namespace {

constexpr std::size_t kMaxDebugPrimitives = 4096u;
constexpr std::size_t kMaxSpriteQuads = 2048u;
constexpr std::size_t kMaxWorldVertices = 540000u;
constexpr std::size_t kMaxWorldIndices = 900000u;

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

std::size_t worldPipelineIndex(const IRenderBackend::WorldTextureData* texture) {
    const bool depthEnabled = !texture || texture->depthTestEnabled != 0u;
    const bool blended = texture && texture->alphaMode == 2u;
    if (!blended) return depthEnabled ? 0u : 1u;
    const std::uint8_t blendMode = std::min<std::uint8_t>(texture->blendMode, 2u);
    return 2u + static_cast<std::size_t>(blendMode) * 2u + (depthEnabled ? 0u : 1u);
}

void setViewportAndScissor(VkCommandBuffer commandBuffer,
                           VkExtent2D swapchainExtent,
                           int surfaceWidth,
                           int surfaceHeight) {
    const std::uint32_t width = std::min(
        swapchainExtent.width,
        static_cast<std::uint32_t>(std::max(1, surfaceWidth)));
    const std::uint32_t height = std::min(
        swapchainExtent.height,
        static_cast<std::uint32_t>(std::max(1, surfaceHeight)));
    VkViewport viewport{};
    viewport.width = static_cast<float>(width);
    viewport.height = static_cast<float>(height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0u, 1u, &viewport);
    VkRect2D scissor{{0, 0}, {width, height}};
    vkCmdSetScissor(commandBuffer, 0u, 1u, &scissor);
}

} // namespace

bool VulkanRenderBackendImpl::bindWorldDescriptorSets(
    VkCommandBuffer commandBuffer,
    VkDescriptorSet materialDescriptorSet,
    const IRenderBackend::WorldTextureData* texture) {
    const auto viewState =
        engine::render::vulkan_backend::makeWorldViewState(texture);
    const auto specializedMaterialState =
        engine::render::vulkan_backend::makeWorldSpecializedMaterialState(texture);
    const std::size_t skinFloatCount =
        engine::render::vulkan_backend::worldSkinMatrixFloatCount(texture);
    VkBuffer skinBuffer = frames[currentFrame].transient.buffer;
    VkDeviceSize skinOffset = 0u;
    if (skinFloatCount > 0u &&
        (!writeTransient(
             texture->skinMatrices,
             static_cast<VkDeviceSize>(skinFloatCount * sizeof(float)),
             sizeof(float) * 16u,
             skinBuffer,
             skinOffset) ||
         skinBuffer != frames[currentFrame].transient.buffer)) {
        return false;
    }
    const std::uint32_t skinMatrixBaseIndex = static_cast<std::uint32_t>(
        skinOffset / (sizeof(float) * 16u));
    const auto transformState =
        engine::render::vulkan_backend::makeWorldTransformState(
            texture,
            skinMatrixBaseIndex);
    VkBuffer viewBuffer = VK_NULL_HANDLE;
    VkDeviceSize viewOffset = 0u;
    const VkDeviceSize alignment = std::max<VkDeviceSize>(
        16u,
        physicalDeviceProperties.limits.minUniformBufferOffsetAlignment);
    VkBuffer specializedMaterialBuffer = VK_NULL_HANDLE;
    VkDeviceSize specializedMaterialOffset = 0u;
    VkBuffer transformBuffer = VK_NULL_HANDLE;
    VkDeviceSize transformOffset = 0u;
    if (!writeTransient(
            &viewState,
            sizeof(viewState),
            alignment,
            viewBuffer,
            viewOffset) ||
        !writeTransient(
            &specializedMaterialState,
            sizeof(specializedMaterialState),
            alignment,
            specializedMaterialBuffer,
            specializedMaterialOffset) ||
        !writeTransient(
            &transformState,
            sizeof(transformState),
            alignment,
            transformBuffer,
            transformOffset) ||
        viewBuffer != frames[currentFrame].transient.buffer ||
        specializedMaterialBuffer != frames[currentFrame].transient.buffer ||
        transformBuffer != frames[currentFrame].transient.buffer ||
        viewOffset > std::numeric_limits<std::uint32_t>::max() ||
        specializedMaterialOffset > std::numeric_limits<std::uint32_t>::max() ||
        transformOffset > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }

    const std::array<VkDescriptorSet, 2> descriptorSets{
        materialDescriptorSet,
        frames[currentFrame].worldStateDescriptorSet,
    };
    const std::array<std::uint32_t, 3> dynamicOffsets{
        static_cast<std::uint32_t>(viewOffset),
        static_cast<std::uint32_t>(specializedMaterialOffset),
        static_cast<std::uint32_t>(transformOffset),
    };
    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        texturedPipelineLayout,
        0u,
        static_cast<std::uint32_t>(descriptorSets.size()),
        descriptorSets.data(),
        static_cast<std::uint32_t>(dynamicOffsets.size()),
        dynamicOffsets.data());
    return true;
}

void VulkanRenderBackendImpl::drawDebugVertices(const DebugVertex* vertices,
                                                std::size_t vertexCount,
                                                int surfaceWidth,
                                                int surfaceHeight) {
    if (!frameActive || !vertices || vertexCount < 3u || debugPipeline == VK_NULL_HANDLE) return;
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceSize vertexOffset = 0u;
    const VkDeviceSize byteCount = static_cast<VkDeviceSize>(vertexCount) * sizeof(DebugVertex);
    if (!writeTransient(vertices, byteCount, 16u, vertexBuffer, vertexOffset)) return;

    VkCommandBuffer commandBuffer = frames[currentFrame].commandBuffer;
    setViewportAndScissor(commandBuffer, swapchainExtent, surfaceWidth, surfaceHeight);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, debugPipeline);
    vkCmdBindVertexBuffers(commandBuffer, 0u, 1u, &vertexBuffer, &vertexOffset);
    DebugPushConstants push{};
    push.surfaceWidth = static_cast<float>(std::max(1, surfaceWidth));
    push.surfaceHeight = static_cast<float>(std::max(1, surfaceHeight));
    vkCmdPushConstants(commandBuffer,
                       debugPipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT,
                       0u,
                       sizeof(push),
                       &push);
    vkCmdDraw(commandBuffer, static_cast<std::uint32_t>(vertexCount), 1u, 0u, 0u);
    ++frameStats.drawCalls;
    frameStats.triangles += vertexCount / 3u;
}

void VulkanRenderBackendImpl::drawDebugQuads(const IRenderBackend::DebugQuad* quads,
                                             std::size_t quadCount,
                                             int surfaceWidth,
                                             int surfaceHeight) {
    if (!quads || quadCount == 0u) return;
    const std::size_t count = std::min(quadCount, kMaxDebugPrimitives);
    std::vector<DebugVertex> vertices;
    vertices.reserve(count * 6u);
    for (std::size_t i = 0u; i < count; ++i) {
        const auto& q = quads[i];
        const DebugVertex a{q.x, q.y, q.r, q.g, q.b, q.a};
        const DebugVertex b{q.x + q.w, q.y, q.r, q.g, q.b, q.a};
        const DebugVertex c{q.x + q.w, q.y + q.h, q.r, q.g, q.b, q.a};
        const DebugVertex d{q.x, q.y + q.h, q.r, q.g, q.b, q.a};
        vertices.insert(vertices.end(), {a, b, c, a, c, d});
    }
    drawDebugVertices(vertices.data(), vertices.size(), surfaceWidth, surfaceHeight);
}

void VulkanRenderBackendImpl::drawDebugLines(const IRenderBackend::DebugLine* lines,
                                             std::size_t lineCount,
                                             int surfaceWidth,
                                             int surfaceHeight) {
    if (!lines || lineCount == 0u) return;
    const std::size_t count = std::min(lineCount, kMaxDebugPrimitives);
    std::vector<DebugVertex> vertices;
    vertices.reserve(count * 6u);
    for (std::size_t i = 0u; i < count; ++i) {
        const auto& line = lines[i];
        if (line.thickness <= 0.0f) continue;
        const float dx = line.x2 - line.x1;
        const float dy = line.y2 - line.y1;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length <= 0.0001f) continue;
        const float scale = line.thickness * 0.5f / length;
        const float nx = -dy * scale;
        const float ny = dx * scale;
        const DebugVertex a{line.x1 + nx, line.y1 + ny, line.r, line.g, line.b, line.a};
        const DebugVertex b{line.x2 + nx, line.y2 + ny, line.r, line.g, line.b, line.a};
        const DebugVertex c{line.x2 - nx, line.y2 - ny, line.r, line.g, line.b, line.a};
        const DebugVertex d{line.x1 - nx, line.y1 - ny, line.r, line.g, line.b, line.a};
        vertices.insert(vertices.end(), {a, b, c, a, c, d});
    }
    drawDebugVertices(vertices.data(), vertices.size(), surfaceWidth, surfaceHeight);
}

void VulkanRenderBackendImpl::drawDebugTriangles(
    const IRenderBackend::DebugTriangle* triangles,
    std::size_t triangleCount,
    int surfaceWidth,
    int surfaceHeight) {
    if (!triangles || triangleCount == 0u) return;
    const std::size_t count = std::min(triangleCount, kMaxDebugPrimitives);
    std::vector<DebugVertex> vertices;
    vertices.reserve(count * 3u);
    for (std::size_t i = 0u; i < count; ++i) {
        const auto& t = triangles[i];
        vertices.push_back({t.x1, t.y1, t.r, t.g, t.b, t.a});
        vertices.push_back({t.x2, t.y2, t.r, t.g, t.b, t.a});
        vertices.push_back({t.x3, t.y3, t.r, t.g, t.b, t.a});
    }
    drawDebugVertices(vertices.data(), vertices.size(), surfaceWidth, surfaceHeight);
}

void VulkanRenderBackendImpl::drawDebugSprites(const IRenderBackend::DebugSprite* sprites,
                                               std::size_t spriteCount,
                                               int surfaceWidth,
                                               int surfaceHeight) {
    if (!frameActive || !sprites || spriteCount == 0u ||
        spritePipeline == VK_NULL_HANDLE) {
        return;
    }
    const std::size_t count = std::min(spriteCount, kMaxSpriteQuads);
    VkCommandBuffer commandBuffer = frames[currentFrame].commandBuffer;
    setViewportAndScissor(commandBuffer, swapchainExtent, surfaceWidth, surfaceHeight);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, spritePipeline);
    DebugPushConstants push{};
    push.surfaceWidth = static_cast<float>(std::max(1, surfaceWidth));
    push.surfaceHeight = static_cast<float>(std::max(1, surfaceHeight));
    vkCmdPushConstants(commandBuffer,
                       texturedPipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT,
                       0u,
                       sizeof(push),
                       &push);

    for (std::size_t i = 0u; i < count; ++i) {
        const auto& sprite = sprites[i];
        if (sprite.w <= 0.0f || sprite.h <= 0.0f) continue;
        Texture* texture = ensureSpriteTexture(sprite.texturePath);
        if (!texture || texture->descriptorSet == VK_NULL_HANDLE) continue;
        const float x0 = sprite.x;
        const float y0 = sprite.y;
        const float x1 = sprite.x + sprite.w;
        const float y1 = sprite.y + sprite.h;
        const SpriteVertex a{x0, y0, sprite.u0, sprite.v0, sprite.r, sprite.g, sprite.b, sprite.a};
        const SpriteVertex b{x1, y0, sprite.u1, sprite.v0, sprite.r, sprite.g, sprite.b, sprite.a};
        const SpriteVertex c{x1, y1, sprite.u1, sprite.v1, sprite.r, sprite.g, sprite.b, sprite.a};
        const SpriteVertex d{x0, y1, sprite.u0, sprite.v1, sprite.r, sprite.g, sprite.b, sprite.a};
        const std::array<SpriteVertex, 6> vertices{a, b, c, a, c, d};

        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceSize vertexOffset = 0u;
        if (!writeTransient(vertices.data(),
                            sizeof(vertices),
                            16u,
                            vertexBuffer,
                            vertexOffset)) {
            return;
        }
        vkCmdBindDescriptorSets(commandBuffer,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                texturedPipelineLayout,
                                0u,
                                1u,
                                &texture->descriptorSet,
                                0u,
                                nullptr);
        vkCmdBindVertexBuffers(commandBuffer, 0u, 1u, &vertexBuffer, &vertexOffset);
        vkCmdDraw(commandBuffer, 6u, 1u, 0u, 0u);
        ++frameStats.drawCalls;
        frameStats.triangles += 2u;
    }
}

void VulkanRenderBackendImpl::drawWorldTriangles(
    const IRenderBackend::WorldTriangle* triangles,
    std::size_t triangleCount,
    const float* viewProjectionMatrix4x4,
    int surfaceWidth,
    int surfaceHeight) {
    if (!frameActive || !triangles || triangleCount == 0u || !viewProjectionMatrix4x4) return;
    const std::size_t count = std::min(triangleCount, kMaxWorldIndices / 3u);
    std::vector<IRenderBackend::WorldMeshVertex> vertices;
    vertices.reserve(count * 3u);
    for (std::size_t i = 0u; i < count; ++i) {
        const auto& triangle = triangles[i];
        const Vec3 p0{triangle.x1, triangle.y1, triangle.z1};
        const Vec3 p1{triangle.x2, triangle.y2, triangle.z2};
        const Vec3 p2{triangle.x3, triangle.y3, triangle.z3};
        const auto color = [&](float r, float g, float b, float a) {
            return std::array<float, 4>{
                r >= 0.0f ? r : triangle.r,
                g >= 0.0f ? g : triangle.g,
                b >= 0.0f ? b : triangle.b,
                a >= 0.0f ? a : triangle.a,
            };
        };
        const auto c0 = color(triangle.r1, triangle.g1, triangle.b1, triangle.a1);
        const auto c1 = color(triangle.r2, triangle.g2, triangle.b2, triangle.a2);
        const auto c2 = color(triangle.r3, triangle.g3, triangle.b3, triangle.a3);
        vertices.push_back({p0.x, p0.y, p0.z, 0.0f, 0.0f,
                            c0[0], c0[1], c0[2], c0[3], 0.0f, 0.0f, 0.0f});
        vertices.push_back({p1.x, p1.y, p1.z, 0.0f, 0.0f,
                            c1[0], c1[1], c1[2], c1[3], 0.0f, 0.0f, 0.0f});
        vertices.push_back({p2.x, p2.y, p2.z, 0.0f, 0.0f,
                            c2[0], c2[1], c2[2], c2[3], 0.0f, 0.0f, 0.0f});
    }

    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceSize vertexOffset = 0u;
    if (!writeTransient(vertices.data(),
                        static_cast<VkDeviceSize>(vertices.size()) *
                            sizeof(IRenderBackend::WorldMeshVertex),
                        16u,
                        vertexBuffer,
                        vertexOffset)) {
        return;
    }
    VkCommandBuffer commandBuffer = frames[currentFrame].commandBuffer;
    setViewportAndScissor(commandBuffer, swapchainExtent, surfaceWidth, surfaceHeight);
    const VkPipeline pipeline = worldPipelines[0u];
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    if (!bindWorldDescriptorSets(
            commandBuffer,
            fallbackWorldMaterial.descriptorSet,
            nullptr)) {
        return;
    }
    vkCmdBindVertexBuffers(commandBuffer, 0u, 1u, &vertexBuffer, &vertexOffset);
    WorldPushConstants push = engine::render::vulkan_backend::makeWorldPushConstants(nullptr);
    std::memcpy(push.viewProjection.data(), viewProjectionMatrix4x4, sizeof(float) * 16u);
    vkCmdPushConstants(commandBuffer,
                       texturedPipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0u,
                       sizeof(push),
                       &push);
    vkCmdDraw(commandBuffer, static_cast<std::uint32_t>(vertices.size()), 1u, 0u, 0u);
    ++frameStats.drawCalls;
    frameStats.triangles += vertices.size() / 3u;
}

void VulkanRenderBackendImpl::drawWorldIndexedMesh(
    const IRenderBackend::WorldMeshVertex* vertices,
    std::size_t vertexCount,
    const std::uint32_t* indices,
    std::size_t indexCount,
    const IRenderBackend::WorldTextureData* textureData,
    const float* viewProjectionMatrix4x4,
    int surfaceWidth,
    int surfaceHeight) {
    if (!frameActive || !vertices || !indices || !viewProjectionMatrix4x4 ||
        vertexCount == 0u || indexCount < 3u) {
        return;
    }
    const std::size_t safeVertexCount = std::min(vertexCount, kMaxWorldVertices);
    const std::size_t safeIndexCount = std::min(indexCount, kMaxWorldIndices);
    for (std::size_t i = 0u; i < safeIndexCount; ++i) {
        if (indices[i] >= safeVertexCount) return;
    }

    WorldMaterial* material = ensureWorldMaterial(textureData);
    if (!material || material->descriptorSet == VK_NULL_HANDLE) return;
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceSize vertexOffset = 0u;
    if (!writeTransient(vertices,
                        static_cast<VkDeviceSize>(safeVertexCount) *
                            sizeof(IRenderBackend::WorldMeshVertex),
                        16u,
                        vertexBuffer,
                        vertexOffset)) {
        return;
    }
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceSize indexOffset = 0u;
    if (!writeTransient(indices,
                        static_cast<VkDeviceSize>(safeIndexCount) * sizeof(std::uint32_t),
                        4u,
                        indexBuffer,
                        indexOffset)) {
        return;
    }

    VkCommandBuffer commandBuffer = frames[currentFrame].commandBuffer;
    setViewportAndScissor(commandBuffer, swapchainExtent, surfaceWidth, surfaceHeight);
    const std::size_t pipelineIndex = worldPipelineIndex(textureData);
    vkCmdBindPipeline(
        commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, worldPipelines[pipelineIndex]);
    if (!bindWorldDescriptorSets(
            commandBuffer,
            material->descriptorSet,
            textureData)) {
        return;
    }
    vkCmdBindVertexBuffers(commandBuffer, 0u, 1u, &vertexBuffer, &vertexOffset);
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer, indexOffset, VK_INDEX_TYPE_UINT32);
    WorldPushConstants push = engine::render::vulkan_backend::makeWorldPushConstants(textureData);
    std::memcpy(push.viewProjection.data(), viewProjectionMatrix4x4, sizeof(float) * 16u);
    vkCmdPushConstants(commandBuffer,
                       texturedPipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0u,
                       sizeof(push),
                       &push);
    vkCmdDrawIndexed(
        commandBuffer, static_cast<std::uint32_t>(safeIndexCount), 1u, 0u, 0, 0u);
    ++frameStats.drawCalls;
    frameStats.triangles += safeIndexCount / 3u;

    const bool drawCharacterOutline =
        textureData &&
        textureData->characterInkingEnabled != 0u &&
        textureData->materialMode >= 2u;
    if (!drawCharacterOutline) return;

    const bool outlineDepthEnabled = textureData->depthTestEnabled != 0u;
    const std::size_t outlinePipelineIndex = outlineDepthEnabled ? 2u : 3u;
    vkCmdBindPipeline(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        worldPipelines[outlinePipelineIndex]);
    push.materialMode = 3.0f;
    push.outlineExtrude = 0.001f;
    vkCmdPushConstants(commandBuffer,
                       texturedPipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0u,
                       sizeof(push),
                       &push);
    vkCmdDrawIndexed(
        commandBuffer, static_cast<std::uint32_t>(safeIndexCount), 1u, 0u, 0, 0u);
    ++frameStats.drawCalls;
    frameStats.triangles += safeIndexCount / 3u;
}
