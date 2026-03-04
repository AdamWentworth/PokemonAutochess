#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

class IRenderBackend {
public:
    struct BackendFrameTimings {
        // CPU time spent blocked in backend-controlled present/wait path.
        float presentWaitMs = 0.0f;
        // Backend-reported GPU frame duration (if available).
        float gpuFrameMs = 0.0f;
        bool gpuFrameValid = false;
    };

    struct BackendFrameStats {
        std::uint32_t drawCalls = 0u;
        std::uint64_t triangles = 0u;
    };

    struct WorldMeshVertex {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float u = 0.0f;
        float v = 0.0f;
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;
        // Optional local-space normal used by material/shading paths.
        float nx = 0.0f;
        float ny = 0.0f;
        float nz = 0.0f;
        // Optional GPU skinning payload used by backend world shaders.
        float joint0 = 0.0f;
        float joint1 = 0.0f;
        float joint2 = 0.0f;
        float joint3 = 0.0f;
        float weight0 = 0.0f;
        float weight1 = 0.0f;
        float weight2 = 0.0f;
        float weight3 = 0.0f;
        // Optional local-space tangent.xyz and handedness in .w.
        // Zero-length tangent means "not provided" and shader should fallback.
        float tx = 0.0f;
        float ty = 0.0f;
        float tz = 0.0f;
        float tw = 1.0f;
    };

    struct WorldTextureData {
        const char* key = nullptr;
        const unsigned char* rgba = nullptr;
        int width = 0;
        int height = 0;
        int wrapS = 10497; // GL_REPEAT
        int wrapT = 10497; // GL_REPEAT
        const char* normalKey = nullptr;
        const unsigned char* normalRgba = nullptr;
        int normalWidth = 0;
        int normalHeight = 0;
        int normalWrapS = 10497; // GL_REPEAT
        int normalWrapT = 10497; // GL_REPEAT
        const char* metallicRoughnessKey = nullptr;
        const unsigned char* metallicRoughnessRgba = nullptr;
        int metallicRoughnessWidth = 0;
        int metallicRoughnessHeight = 0;
        int metallicRoughnessWrapS = 10497; // GL_REPEAT
        int metallicRoughnessWrapT = 10497; // GL_REPEAT
        const char* occlusionKey = nullptr;
        const unsigned char* occlusionRgba = nullptr;
        int occlusionWidth = 0;
        int occlusionHeight = 0;
        int occlusionWrapS = 10497; // GL_REPEAT
        int occlusionWrapT = 10497; // GL_REPEAT
        const char* emissiveKey = nullptr;
        const unsigned char* emissiveRgba = nullptr;
        int emissiveWidth = 0;
        int emissiveHeight = 0;
        int emissiveWrapS = 10497; // GL_REPEAT
        int emissiveWrapT = 10497; // GL_REPEAT
        std::uint8_t alphaMode = 0u; // 0=OPAQUE, 1=MASK, 2=BLEND
        std::uint8_t blendMode = 0u; // 0=Alpha, 1=Additive, 2=Premultiplied
        std::uint8_t materialMode = 0u; // 0=Default, 1=FireTailExact
        float alphaCutoff = 0.5f;
        float normalScale = 1.0f;
        float metallicFactor = 1.0f;
        float roughnessFactor = 1.0f;
        float occlusionStrength = 1.0f;
        float emissiveFactorR = 0.0f;
        float emissiveFactorG = 0.0f;
        float emissiveFactorB = 0.0f;
        // Optional character inking (silhouette edge darkening) toggle for model shading paths.
        std::uint8_t characterInkingEnabled = 0u;
        // Optional camera payload used by world shaded paths.
        // Defaults preserve prior fixed-camera lighting behavior.
        float cameraPosX = 0.0f;
        float cameraPosY = 7.0f;
        float cameraPosZ = 9.0f;
        float cameraForwardX = 0.0f;
        float cameraForwardY = -0.6139406f;
        float cameraForwardZ = -0.7893522f;
        float cameraTargetX = 0.0f;
        float cameraTargetY = -1.0f;
        float cameraTargetZ = 0.0f;

        // Optional exact material payload used by world textured draws.
        // Currently interpreted when materialMode == 1 (FireTailExact).
        float materialTimeSec = 0.0f;
        float materialFlags = 0.0f; // bit0=useFlipbook, bit1=hasFlipbook2
        float materialAtlasWidth = 0.0f;
        float materialAtlasHeight = 0.0f;

        float materialRect0U = 0.0f;
        float materialRect0V = 0.0f;
        float materialRect0W = 1.0f;
        float materialRect0H = 1.0f;
        float materialRect1U = 0.0f;
        float materialRect1V = 0.0f;
        float materialRect1W = 1.0f;
        float materialRect1H = 1.0f;

        float materialFlipbook0Cols = 1.0f;
        float materialFlipbook0Rows = 1.0f;
        float materialFlipbook0Frames = 1.0f;
        float materialFlipbook0Fps = 0.0f;
        float materialFlipbook1Cols = 1.0f;
        float materialFlipbook1Rows = 1.0f;
        float materialFlipbook1Frames = 1.0f;
        float materialFlipbook1Fps = 0.0f;

        // Optional model transform for world indexed mesh vertices.
        // Vertices are interpreted in model space when this is non-identity.
        std::array<float, 16> modelMatrix{
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f};

        // Optional GPU skinning payload for world indexed mesh draws.
        // Backend should ignore when gpuSkinning == 0.
        std::uint8_t gpuSkinning = 0u;
        std::uint32_t skinMatrixCount = 0u;
        const float* skinMatrices = nullptr; // 16 * skinMatrixCount floats (column-major mat4)
    };

    struct WorldTriangle {
        float x1 = 0.0f;
        float y1 = 0.0f;
        float z1 = 0.0f;
        float x2 = 0.0f;
        float y2 = 0.0f;
        float z2 = 0.0f;
        float x3 = 0.0f;
        float y3 = 0.0f;
        float z3 = 0.0f;
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;
        float r1 = -1.0f;
        float g1 = -1.0f;
        float b1 = -1.0f;
        float a1 = -1.0f;
        float r2 = -1.0f;
        float g2 = -1.0f;
        float b2 = -1.0f;
        float a2 = -1.0f;
        float r3 = -1.0f;
        float g3 = -1.0f;
        float b3 = -1.0f;
        float a3 = -1.0f;
    };

    struct DebugQuad {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;
    };

    struct DebugLine {
        float x1 = 0.0f;
        float y1 = 0.0f;
        float x2 = 0.0f;
        float y2 = 0.0f;
        float thickness = 1.0f;
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;
    };

    struct DebugTriangle {
        float x1 = 0.0f;
        float y1 = 0.0f;
        float x2 = 0.0f;
        float y2 = 0.0f;
        float x3 = 0.0f;
        float y3 = 0.0f;
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;
    };

    struct DebugSprite {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        float u0 = 0.0f;
        float v0 = 0.0f;
        float u1 = 1.0f;
        float v1 = 1.0f;
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;
        std::string texturePath;
    };

    virtual ~IRenderBackend() = default;

    virtual const char* backendId() const = 0;
    virtual void beginFrame(float r, float g, float b, float a) = 0;
    virtual void endFrame() = 0;
    virtual void onResize(int width, int height) = 0;
    virtual bool requiresOpenGLContext() const = 0;
    virtual bool handlesPresentation() const = 0;
    virtual bool getLastFrameTimings(BackendFrameTimings& outTimings) const {
        outTimings = BackendFrameTimings{};
        return false;
    }
    virtual bool getLastFrameStats(BackendFrameStats& outStats) const {
        outStats = BackendFrameStats{};
        return false;
    }
    virtual std::string activeGpuName() const { return {}; }
    virtual bool activeGpuIsDiscrete() const { return false; }
    virtual bool supportsWorldTriangles3D() const { return false; }
    virtual bool supportsWorldIndexedMeshes() const { return false; }
    virtual void drawWorldTriangles(const WorldTriangle* triangles,
                                    std::size_t triangleCount,
                                    const float* viewProjectionMatrix4x4,
                                    int surfaceWidth,
                                    int surfaceHeight) {
        (void)triangles;
        (void)triangleCount;
        (void)viewProjectionMatrix4x4;
        (void)surfaceWidth;
        (void)surfaceHeight;
    }
    virtual void drawWorldIndexedMesh(const WorldMeshVertex* vertices,
                                      std::size_t vertexCount,
                                      const std::uint32_t* indices,
                                      std::size_t indexCount,
                                      const float* viewProjectionMatrix4x4,
                                      int surfaceWidth,
                                      int surfaceHeight) {
        (void)vertices;
        (void)vertexCount;
        (void)indices;
        (void)indexCount;
        (void)viewProjectionMatrix4x4;
        (void)surfaceWidth;
        (void)surfaceHeight;
    }
    virtual void drawWorldIndexedMeshCached(const char* geometryKey,
                                            const WorldMeshVertex* vertices,
                                            std::size_t vertexCount,
                                            const std::uint32_t* indices,
                                            std::size_t indexCount,
                                            const float* viewProjectionMatrix4x4,
                                            int surfaceWidth,
                                            int surfaceHeight) {
        (void)geometryKey;
        drawWorldIndexedMesh(
            vertices,
            vertexCount,
            indices,
            indexCount,
            viewProjectionMatrix4x4,
            surfaceWidth,
            surfaceHeight);
    }
    virtual void prewarmWorldIndexedMeshCached(const char* geometryKey,
                                               const WorldMeshVertex* vertices,
                                               std::size_t vertexCount,
                                               const std::uint32_t* indices,
                                               std::size_t indexCount) {
        (void)geometryKey;
        (void)vertices;
        (void)vertexCount;
        (void)indices;
        (void)indexCount;
    }
    virtual void drawWorldIndexedMeshTextured(const WorldMeshVertex* vertices,
                                              std::size_t vertexCount,
                                              const std::uint32_t* indices,
                                              std::size_t indexCount,
                                              const WorldTextureData* texture,
                                              const float* viewProjectionMatrix4x4,
                                              int surfaceWidth,
                                              int surfaceHeight) {
        (void)vertices;
        (void)vertexCount;
        (void)indices;
        (void)indexCount;
        (void)texture;
        (void)viewProjectionMatrix4x4;
        (void)surfaceWidth;
        (void)surfaceHeight;
    }
    virtual void drawDebugQuads(const DebugQuad* quads,
                                std::size_t quadCount,
                                int surfaceWidth,
                                int surfaceHeight) {
        (void)quads;
        (void)quadCount;
        (void)surfaceWidth;
        (void)surfaceHeight;
    }
    virtual void drawDebugLines(const DebugLine* lines,
                                std::size_t lineCount,
                                int surfaceWidth,
                                int surfaceHeight) {
        (void)lines;
        (void)lineCount;
        (void)surfaceWidth;
        (void)surfaceHeight;
    }
    virtual void drawDebugTriangles(const DebugTriangle* triangles,
                                    std::size_t triangleCount,
                                    int surfaceWidth,
                                    int surfaceHeight) {
        (void)triangles;
        (void)triangleCount;
        (void)surfaceWidth;
        (void)surfaceHeight;
    }
    virtual void drawDebugSprites(const DebugSprite* sprites,
                                  std::size_t spriteCount,
                                  int surfaceWidth,
                                  int surfaceHeight) {
        (void)sprites;
        (void)spriteCount;
        (void)surfaceWidth;
        (void)surfaceHeight;
    }
    virtual void prewarmDebugSpriteTexture(const char* texturePath) {
        (void)texturePath;
    }
    virtual void shutdown() = 0;
};
