#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/render/IRenderBackend.h"
#include "engine/render/vulkan/VulkanWorldMaterialLayout.h"
#include "engine/render/vulkan/VulkanWorldMaterialState.h"
#include "engine/render/vulkan/VulkanWorldSpecializedMaterialState.h"
#include "engine/render/vulkan/VulkanWorldTransformState.h"
#include "engine/render/vulkan/VulkanWorldViewState.h"

struct SDL_Window;

struct VulkanRenderBackendImpl {
    static constexpr std::uint32_t kFramesInFlight = 2u;
    static constexpr VkDeviceSize kTransientBytesPerFrame = 128ull * 1024ull * 1024ull;

    struct Buffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        VkDeviceSize size = 0u;
        VkDeviceSize offset = 0u;
        bool coherent = false;
    };

    struct FrameResources {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkSemaphore renderFinished = VK_NULL_HANDLE;
        VkFence inFlight = VK_NULL_HANDLE;
        VkQueryPool timestampQueries = VK_NULL_HANDLE;
        VkDescriptorSet worldStateDescriptorSet = VK_NULL_HANDLE;
        Buffer transient;
        bool timestampIssued = false;
    };

    struct Texture {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        int width = 0;
        int height = 0;
    };

    struct WorldMaterial {
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    };

    struct DebugVertex {
        float x = 0.0f;
        float y = 0.0f;
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;
    };

    struct SpriteVertex {
        float x = 0.0f;
        float y = 0.0f;
        float u = 0.0f;
        float v = 0.0f;
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;
    };

    struct DebugPushConstants {
        float surfaceWidth = 1.0f;
        float surfaceHeight = 1.0f;
        float unused0 = 0.0f;
        float unused1 = 0.0f;
    };

    using WorldPushConstants = engine::render::vulkan_backend::WorldPushConstants;

    SDL_Window* window = nullptr;
    bool initialized = false;
    bool frameActive = false;
    bool swapchainDirty = false;
    bool vsyncEnabled = true;
    bool transientOverflowLogged = false;
    bool swapchainTransferSourceSupported = false;
    bool screenshotCaptureConfigured = false;
    bool screenshotCaptured = false;
    bool screenshotCopyPending = false;
    int requestedWidth = 1;
    int requestedHeight = 1;
    std::uint32_t currentFrame = 0u;
    std::uint32_t acquiredImage = 0u;
    std::uint64_t frameCounter = 0u;
    std::uint64_t screenshotFrameTarget = 0u;
    std::string screenshotPath;
    Buffer screenshotReadback;
    std::uint32_t screenshotWidth = 0u;
    std::uint32_t screenshotHeight = 0u;

    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties physicalDeviceProperties{};
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    VkDevice device = VK_NULL_HANDLE;
    std::uint32_t graphicsQueueFamily = UINT32_MAX;
    std::uint32_t presentQueueFamily = UINT32_MAX;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR swapchainColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkExtent2D swapchainExtent{};
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkFramebuffer> framebuffers;
    std::vector<VkFence> imagesInFlight;
    VkRenderPass renderPass = VK_NULL_HANDLE;

    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    std::vector<VkImage> depthImages;
    std::vector<VkDeviceMemory> depthMemories;
    std::vector<VkImageView> depthViews;

    VkDescriptorSetLayout textureSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout worldStateSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkPipelineLayout debugPipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout texturedPipelineLayout = VK_NULL_HANDLE;
    VkPipeline debugPipeline = VK_NULL_HANDLE;
    VkPipeline spritePipeline = VK_NULL_HANDLE;
    std::array<VkPipeline, 8> worldPipelines{};

    std::array<FrameResources, kFramesInFlight> frames{};
    std::unordered_map<std::string, Texture> worldTextures;
    std::unordered_map<std::string, WorldMaterial> worldMaterials;
    std::unordered_map<std::string, Texture> spriteTextures;
    Texture fallbackWorldTexture;
    Texture fallbackWorldNormalTexture;
    Texture fallbackWorldLinearTexture;
    Texture fallbackWorldEmissiveTexture;
    Texture neutralPmremTexture;
    Texture fallbackSpriteTexture;
    WorldMaterial fallbackWorldMaterial;

    std::string gpuName;
    bool gpuDiscrete = false;
    bool samplerAnisotropyEnabled = false;
    float maxSamplerAnisotropy = 1.0f;
    float timestampPeriodNs = 0.0f;

    IRenderBackend::BackendFrameTimings lastTimings{};
    IRenderBackend::BackendFrameStats frameStats{};
    IRenderBackend::BackendFrameStats lastStats{};

    void initialize(SDL_Window* sdlWindow,
                    int width,
                    int height,
                    bool enableVsync,
                    const std::string& preferredAdapterName);
    void shutdown();
    void beginFrame(float r, float g, float b, float a);
    void endFrame();
    void requestResize(int width, int height);
    void requestVSync(bool enabled);
    void recordSubmissionStats(const IRenderBackend::WorldIndexedSubmissionStats& stats);

    void createInstance();
    void selectPhysicalDevice(const std::string& preferredAdapterName);
    void createDevice();
    void createCommandResources();
    void createDescriptorResources();
    void createFrameResources();
    void createEnvironmentResources();
    void destroyEnvironmentResources();
    void createSwapchainResources();
    void destroySwapchainResources();
    bool recreateSwapchain();
    void createRenderPass();
    void createDepthResources();
    void createFramebuffers();
    void createPipelines();
    void destroyPipelines();
    void configureScreenshotCapture();
    bool recordScreenshotCopy(VkCommandBuffer commandBuffer);
    void finishScreenshotCapture(VkFence frameFence);

    std::uint32_t findMemoryType(std::uint32_t typeBits,
                                 VkMemoryPropertyFlags required,
                                 VkMemoryPropertyFlags preferred = 0u) const;
    Buffer createBuffer(VkDeviceSize size,
                        VkBufferUsageFlags usage,
                        VkMemoryPropertyFlags required,
                        VkMemoryPropertyFlags preferred = 0u);
    void destroyBuffer(Buffer& buffer);
    bool writeTransient(const void* data,
                        VkDeviceSize size,
                        VkDeviceSize alignment,
                        VkBuffer& outBuffer,
                        VkDeviceSize& outOffset);
    bool bindWorldDescriptorSets(
        VkCommandBuffer commandBuffer,
        VkDescriptorSet materialDescriptorSet,
        const IRenderBackend::WorldTextureData* texture);
    VkCommandBuffer beginOneTimeCommands();
    void endOneTimeCommands(VkCommandBuffer commandBuffer);
    VkShaderModule loadShaderModule(const char* fileName) const;

    Texture createTexture(const unsigned char* rgba,
                          int width,
                          int height,
                          bool srgb,
                          int wrapS,
                          int wrapT,
                          bool createStandaloneDescriptor);
    void destroyTexture(Texture& texture);
    Texture* ensureWorldTextureRaw(const char* key,
                                   const char* cacheKey,
                                   const unsigned char* rgba,
                                   int width,
                                   int height,
                                   int wrapS,
                                   int wrapT,
                                   bool srgb,
                                   std::string* outResolvedKey = nullptr);
    WorldMaterial* ensureWorldMaterial(const IRenderBackend::WorldTextureData* texture);
    WorldMaterial createWorldMaterial(Texture& base,
                                      Texture& normal,
                                      Texture& metallicRoughness,
                                      Texture& occlusion,
                                      Texture& emissive);
    Texture* ensureSpriteTexture(const std::string& texturePath);
    void prewarmWorldTexture(const IRenderBackend::WorldTextureData* texture);
    void prewarmSpriteTexture(const char* texturePath);

    void drawDebugVertices(const DebugVertex* vertices,
                           std::size_t vertexCount,
                           int surfaceWidth,
                           int surfaceHeight);
    void drawDebugQuads(const IRenderBackend::DebugQuad* quads,
                        std::size_t quadCount,
                        int surfaceWidth,
                        int surfaceHeight);
    void drawDebugLines(const IRenderBackend::DebugLine* lines,
                        std::size_t lineCount,
                        int surfaceWidth,
                        int surfaceHeight);
    void drawDebugTriangles(const IRenderBackend::DebugTriangle* triangles,
                            std::size_t triangleCount,
                            int surfaceWidth,
                            int surfaceHeight);
    void drawDebugSprites(const IRenderBackend::DebugSprite* sprites,
                          std::size_t spriteCount,
                          int surfaceWidth,
                          int surfaceHeight);
    void drawWorldTriangles(const IRenderBackend::WorldTriangle* triangles,
                            std::size_t triangleCount,
                            const float* viewProjectionMatrix4x4,
                            int surfaceWidth,
                            int surfaceHeight);
    void drawWorldIndexedMesh(const IRenderBackend::WorldMeshVertex* vertices,
                              std::size_t vertexCount,
                              const std::uint32_t* indices,
                              std::size_t indexCount,
                              const IRenderBackend::WorldTextureData* texture,
                              const float* viewProjectionMatrix4x4,
                              int surfaceWidth,
                              int surfaceHeight);
};
