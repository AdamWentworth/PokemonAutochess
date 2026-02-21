#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

class IRenderBackend {
public:
    struct WorldMeshVertex {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;
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
    virtual void shutdown() = 0;
};
