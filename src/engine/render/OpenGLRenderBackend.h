#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "engine/render/IRenderBackend.h"

class Renderer;

class OpenGLRenderBackend final : public IRenderBackend {
public:
    OpenGLRenderBackend();
    ~OpenGLRenderBackend() override;

    const char* backendId() const override { return "opengl"; }
    void beginFrame(float r, float g, float b, float a) override;
    void endFrame() override;
    void onResize(int width, int height) override;
    bool requiresOpenGLContext() const override { return true; }
    bool handlesPresentation() const override { return false; }
    bool getLastFrameTimings(BackendFrameTimings& outTimings) const override;
    bool getLastFrameStats(BackendFrameStats& outStats) const override;
    bool supportsWorldTriangles3D() const override { return true; }
    bool supportsWorldIndexedMeshes() const override { return true; }
    std::string activeGpuName() const override;
    bool activeGpuIsDiscrete() const override;
    void drawWorldTriangles(const WorldTriangle* triangles,
                            std::size_t triangleCount,
                            const float* viewProjectionMatrix4x4,
                            int surfaceWidth,
                            int surfaceHeight) override;
    void drawWorldIndexedMesh(const WorldMeshVertex* vertices,
                              std::size_t vertexCount,
                              const std::uint32_t* indices,
                              std::size_t indexCount,
                              const float* viewProjectionMatrix4x4,
                              int surfaceWidth,
                              int surfaceHeight) override;
    void drawWorldIndexedMeshCached(const char* geometryKey,
                                    const WorldMeshVertex* vertices,
                                    std::size_t vertexCount,
                                    const std::uint32_t* indices,
                                    std::size_t indexCount,
                                    const float* viewProjectionMatrix4x4,
                                    int surfaceWidth,
                                    int surfaceHeight) override;
    void prewarmWorldIndexedMeshCached(const char* geometryKey,
                                       const WorldMeshVertex* vertices,
                                       std::size_t vertexCount,
                                       const std::uint32_t* indices,
                                       std::size_t indexCount) override;
    void drawWorldIndexedMeshTextured(const WorldMeshVertex* vertices,
                                      std::size_t vertexCount,
                                      const std::uint32_t* indices,
                                      std::size_t indexCount,
                                      const WorldTextureData* texture,
                                      const float* viewProjectionMatrix4x4,
                                      int surfaceWidth,
                                      int surfaceHeight) override;
    void drawDebugQuads(const DebugQuad* quads,
                        std::size_t quadCount,
                        int surfaceWidth,
                        int surfaceHeight) override;
    void drawDebugLines(const DebugLine* lines,
                        std::size_t lineCount,
                        int surfaceWidth,
                        int surfaceHeight) override;
    void drawDebugTriangles(const DebugTriangle* triangles,
                            std::size_t triangleCount,
                            int surfaceWidth,
                            int surfaceHeight) override;
    void drawDebugSprites(const DebugSprite* sprites,
                          std::size_t spriteCount,
                          int surfaceWidth,
                          int surfaceHeight) override;
    void prewarmDebugSpriteTexture(const char* texturePath) override;
    void shutdown() override;

private:
    void configureScreenshotCapture();
    void captureScreenshotIfRequested();

    void ensureDebugPipeline();
    void destroyDebugPipeline();
    void ensureWorldPipeline();
    void destroyWorldPipeline();
    void ensureSpritePipeline();
    void destroySpritePipeline();
    unsigned int ensureWorldTexture(const WorldTextureData* textureData);
    unsigned int ensureWorldTextureRaw(const char* key,
                                       const unsigned char* rgba,
                                       int width,
                                       int height,
                                       int wrapS,
                                       int wrapT,
                                       bool srgb);
    unsigned int ensureWorldTextureRawHalfFloat(const char* key,
                                                const std::uint16_t* rgba16f,
                                                int width,
                                                int height,
                                                int wrapS,
                                                int wrapT);
    unsigned int ensureSpriteTexture(const std::string& texturePath);
    void clearTextureCaches();

    std::unique_ptr<Renderer> renderer_;
    unsigned int debugProgram_ = 0;
    unsigned int debugVao_ = 0;
    unsigned int debugVbo_ = 0;
    int debugSurfaceSizeLoc_ = -1;

    unsigned int worldProgram_ = 0;
    unsigned int worldVao_ = 0;
    unsigned int worldVbo_ = 0;
    unsigned int worldIbo_ = 0;
    int worldViewProjLoc_ = -1;
    int worldModelLoc_ = -1;
    int worldUseTextureLoc_ = -1;
    int worldTextureSamplerLoc_ = -1;
    int worldUseNormalTextureLoc_ = -1;
    int worldUseMetallicRoughnessTextureLoc_ = -1;
    int worldUseOcclusionTextureLoc_ = -1;
    int worldUseEmissiveTextureLoc_ = -1;
    int worldNormalTextureSamplerLoc_ = -1;
    int worldMetallicRoughnessTextureSamplerLoc_ = -1;
    int worldOcclusionTextureSamplerLoc_ = -1;
    int worldEmissiveTextureSamplerLoc_ = -1;
    int worldEnvTextureSamplerLoc_ = -1;
    int worldEnvTexelSizeLoc_ = -1;
    int worldEnvMaxMipLoc_ = -1;
    int worldEnvRgbmRangeLoc_ = -1;
    int worldWrapSLoc_ = -1;
    int worldWrapTLoc_ = -1;
    int worldVertexColorMulLoc_ = -1;
    int worldAlphaModeLoc_ = -1;
    int worldAlphaCutoffLoc_ = -1;
    int worldCameraPosLoc_ = -1;
    int worldCameraForwardLoc_ = -1;
    int worldCameraTargetLoc_ = -1;
    int worldNormalScaleLoc_ = -1;
    int worldMetallicFactorLoc_ = -1;
    int worldRoughnessFactorLoc_ = -1;
    int worldOcclusionStrengthLoc_ = -1;
    int worldEmissiveFactorLoc_ = -1;
    int worldCharacterInkingEnabledLoc_ = -1;
    int worldMaterialModeLoc_ = -1;
    int worldMaterialTimeLoc_ = -1;
    int worldMaterialFlagsLoc_ = -1;
    int worldMaterialAtlasSizeLoc_ = -1;
    int worldMaterialRect0Loc_ = -1;
    int worldMaterialRect1Loc_ = -1;
    int worldMaterialFlipbook0Loc_ = -1;
    int worldMaterialFlipbook1Loc_ = -1;
    int worldSkinningEnabledLoc_ = -1;
    int worldSkinMatrixCountLoc_ = -1;
    int worldSkinMatricesLoc_ = -1;

    unsigned int spriteProgram_ = 0;
    unsigned int spriteVao_ = 0;
    unsigned int spriteVbo_ = 0;
    int spriteSurfaceSizeLoc_ = -1;
    int spriteSamplerLoc_ = -1;

    struct TextureCacheEntry {
        unsigned int textureId = 0;
        int width = 0;
        int height = 0;
        int wrapS = 10497;
        int wrapT = 10497;
    };

    std::unordered_map<std::string, TextureCacheEntry> worldTextures_;
    std::unordered_map<std::string, unsigned int> spriteTextures_;
    unsigned int worldFallbackTexture_ = 0;
    unsigned int spriteFallbackTexture_ = 0;

    std::uint32_t frameDrawCalls_ = 0u;
    std::uint64_t frameTriangles_ = 0u;
    std::uint32_t lastFrameDrawCalls_ = 0u;
    std::uint64_t lastFrameTriangles_ = 0u;
    bool gpuTimingSupported_ = false;
    std::array<unsigned int, 2> gpuTimerQueries_{0u, 0u};
    std::array<bool, 2> gpuTimerIssued_{false, false};
    std::uint8_t gpuTimerWriteIndex_ = 0u;
    float lastGpuFrameMs_ = 0.0f;
    bool lastGpuFrameValid_ = false;

    bool screenshotCaptureConfigured_ = false;
    bool screenshotCaptured_ = false;
    std::uint64_t screenshotFrameTarget_ = 0u;
    std::uint64_t frameCounter_ = 0u;
    std::string screenshotPath_;
};
