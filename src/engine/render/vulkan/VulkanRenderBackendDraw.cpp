#include "engine/render/vulkan/VulkanRenderBackendInternal.h"

#include <algorithm>
#include <array>
#include <cmath>
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

Vec3 add(Vec3 a, Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 multiply(Vec3 value, float scalar) {
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

Vec3 normalize(Vec3 value) {
    const float lengthSq = value.x * value.x + value.y * value.y + value.z * value.z;
    if (lengthSq <= 0.0000001f) return {};
    const float invLength = 1.0f / std::sqrt(lengthSq);
    return multiply(value, invLength);
}

Vec3 transform(const float* matrix, Vec3 value, float w) {
    if (!matrix) return value;
    return {
        matrix[0] * value.x + matrix[4] * value.y + matrix[8] * value.z + matrix[12] * w,
        matrix[1] * value.x + matrix[5] * value.y + matrix[9] * value.z + matrix[13] * w,
        matrix[2] * value.x + matrix[6] * value.y + matrix[10] * value.z + matrix[14] * w,
    };
}

std::array<float, 16> identityMatrix() {
    return {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
}

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

Vec3 skinValue(const IRenderBackend::WorldMeshVertex& vertex,
               const IRenderBackend::WorldTextureData* texture,
               Vec3 value,
               float w) {
    if (!texture || texture->gpuSkinning == 0u || !texture->skinMatrices ||
        texture->skinMatrixCount == 0u) {
        return value;
    }
    const std::array<float, 4> joints{
        vertex.joint0, vertex.joint1, vertex.joint2, vertex.joint3};
    const std::array<float, 4> weights{
        vertex.weight0, vertex.weight1, vertex.weight2, vertex.weight3};
    Vec3 blended{};
    float totalWeight = 0.0f;
    for (std::size_t i = 0u; i < weights.size(); ++i) {
        if (weights[i] <= 0.00001f) continue;
        const int joint = static_cast<int>(joints[i] + 0.5f);
        if (joint < 0 || static_cast<std::uint32_t>(joint) >= texture->skinMatrixCount ||
            joint >= 128) {
            continue;
        }
        const float* primary = texture->skinMatrices + static_cast<std::size_t>(joint) * 16u;
        Vec3 transformed = value;
        if (texture->gpuSkinningMode != 0u) {
            const float* secondary = texture->skinMatrices +
                (static_cast<std::size_t>(texture->skinMatrixCount) +
                 static_cast<std::size_t>(joint)) * 16u;
            transformed = transform(secondary, transformed, w);
        }
        transformed = transform(primary, transformed, w);
        blended = add(blended, multiply(transformed, weights[i]));
        totalWeight += weights[i];
    }
    if (totalWeight <= 0.00001f) return value;
    if (totalWeight < 0.999f) {
        blended = add(blended, multiply(value, 1.0f - totalWeight));
    }
    return blended;
}

VulkanRenderBackendImpl::WorldVertex transformWorldVertex(
    const IRenderBackend::WorldMeshVertex& vertex,
    const IRenderBackend::WorldTextureData* texture) {
    const float* model = texture ? texture->modelMatrix.data() : nullptr;
    Vec3 position = skinValue(vertex, texture, {vertex.x, vertex.y, vertex.z}, 1.0f);
    Vec3 normal = skinValue(vertex, texture, {vertex.nx, vertex.ny, vertex.nz}, 0.0f);
    Vec3 tangent = skinValue(vertex, texture, {vertex.tx, vertex.ty, vertex.tz}, 0.0f);
    position = transform(model, position, 1.0f);
    normal = normalize(transform(model, normal, 0.0f));
    tangent = normalize(transform(model, tangent, 0.0f));

    const float mulR = texture ? texture->vertexColorMulR : 1.0f;
    const float mulG = texture ? texture->vertexColorMulG : 1.0f;
    const float mulB = texture ? texture->vertexColorMulB : 1.0f;
    const float mulA = texture ? texture->vertexColorMulA : 1.0f;
    const Vec3 generatedMin{
        texture ? texture->materialRect0U : 0.0f,
        texture ? texture->materialRect0V : 0.0f,
        texture ? texture->materialRect0W : 1.0f};
    const Vec3 generatedMax{
        texture ? texture->materialRect1U : 0.0f,
        texture ? texture->materialRect1V : 0.0f,
        texture ? texture->materialRect1W : 1.0f};
    const auto generatedComponent = [](float value, float minValue, float maxValue) {
        const float denominator = std::max(maxValue - minValue, 0.00001f);
        return std::clamp((value - minValue) / denominator, 0.0f, 1.0f);
    };
    return {
        position.x,
        position.y,
        position.z,
        vertex.u,
        vertex.v,
        vertex.r * mulR,
        vertex.g * mulG,
        vertex.b * mulB,
        vertex.a * mulA,
        normal.x,
        normal.y,
        normal.z,
        tangent.x,
        tangent.y,
        tangent.z,
        vertex.tw,
        generatedComponent(vertex.x, generatedMin.x, generatedMax.x),
        generatedComponent(vertex.y, generatedMin.y, generatedMax.y),
        generatedComponent(vertex.z, generatedMin.z, generatedMax.z),
    };
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
    VkBuffer viewBuffer = VK_NULL_HANDLE;
    VkDeviceSize viewOffset = 0u;
    const VkDeviceSize alignment = std::max<VkDeviceSize>(
        16u,
        physicalDeviceProperties.limits.minUniformBufferOffsetAlignment);
    VkBuffer specializedMaterialBuffer = VK_NULL_HANDLE;
    VkDeviceSize specializedMaterialOffset = 0u;
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
        viewBuffer != frames[currentFrame].transient.buffer ||
        specializedMaterialBuffer != frames[currentFrame].transient.buffer ||
        viewOffset > std::numeric_limits<std::uint32_t>::max() ||
        specializedMaterialOffset > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }

    const std::array<VkDescriptorSet, 2> descriptorSets{
        materialDescriptorSet,
        frames[currentFrame].worldStateDescriptorSet,
    };
    const std::array<std::uint32_t, 2> dynamicOffsets{
        static_cast<std::uint32_t>(viewOffset),
        static_cast<std::uint32_t>(specializedMaterialOffset),
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
    std::vector<WorldVertex> vertices;
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
                        static_cast<VkDeviceSize>(vertices.size()) * sizeof(WorldVertex),
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
    std::vector<WorldVertex> transformed;
    transformed.reserve(safeVertexCount);
    for (std::size_t i = 0u; i < safeVertexCount; ++i) {
        transformed.push_back(transformWorldVertex(vertices[i], textureData));
    }

    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceSize vertexOffset = 0u;
    if (!writeTransient(transformed.data(),
                        static_cast<VkDeviceSize>(transformed.size()) * sizeof(WorldVertex),
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

    std::vector<WorldVertex> outlineVertices;
    outlineVertices.reserve(safeVertexCount);
    constexpr float kOutlineExtrude = 0.001f;
    for (std::size_t i = 0u; i < safeVertexCount; ++i) {
        IRenderBackend::WorldMeshVertex outlineSource = vertices[i];
        const float normalLengthSq =
            outlineSource.nx * outlineSource.nx +
            outlineSource.ny * outlineSource.ny +
            outlineSource.nz * outlineSource.nz;
        if (normalLengthSq > 1e-10f) {
            const float scale = kOutlineExtrude / std::sqrt(normalLengthSq);
            outlineSource.x += outlineSource.nx * scale;
            outlineSource.y += outlineSource.ny * scale;
            outlineSource.z += outlineSource.nz * scale;
        }
        outlineSource.r = 0.0f;
        outlineSource.g = 0.0f;
        outlineSource.b = 0.0f;
        outlineSource.a = 1.0f;
        outlineVertices.push_back(transformWorldVertex(outlineSource, textureData));
    }

    VkBuffer outlineVertexBuffer = VK_NULL_HANDLE;
    VkDeviceSize outlineVertexOffset = 0u;
    if (!writeTransient(
            outlineVertices.data(),
            static_cast<VkDeviceSize>(outlineVertices.size()) * sizeof(WorldVertex),
            16u,
            outlineVertexBuffer,
            outlineVertexOffset)) {
        return;
    }

    const bool outlineDepthEnabled = textureData->depthTestEnabled != 0u;
    const std::size_t outlinePipelineIndex = outlineDepthEnabled ? 2u : 3u;
    vkCmdBindPipeline(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        worldPipelines[outlinePipelineIndex]);
    vkCmdBindVertexBuffers(
        commandBuffer,
        0u,
        1u,
        &outlineVertexBuffer,
        &outlineVertexOffset);
    push.materialMode = 3.0f;
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
