#pragma once

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
    void endFrame() override {}
    void onResize(int width, int height) override;
    bool requiresOpenGLContext() const override { return true; }
    bool handlesPresentation() const override { return false; }
    bool prefersLegacyGameRenderPath() const override { return true; }
    bool prefersLegacyGameUiPath() const override { return true; }
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
    void shutdown() override;

private:
    void ensureDebugPipeline();
    void destroyDebugPipeline();
    void ensureWorldPipeline();
    void destroyWorldPipeline();
    void ensureSpritePipeline();
    void destroySpritePipeline();
    unsigned int ensureWorldTexture(const WorldTextureData* textureData);
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
    int worldUseTextureLoc_ = -1;
    int worldTextureSamplerLoc_ = -1;
    int worldWrapSLoc_ = -1;
    int worldWrapTLoc_ = -1;
    int worldAlphaModeLoc_ = -1;
    int worldAlphaCutoffLoc_ = -1;

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
};
