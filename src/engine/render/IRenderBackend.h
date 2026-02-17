#pragma once

#include <cstddef>
#include <string>

class IRenderBackend {
public:
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

    virtual ~IRenderBackend() = default;

    virtual const char* backendId() const = 0;
    virtual void beginFrame(float r, float g, float b, float a) = 0;
    virtual void endFrame() = 0;
    virtual void onResize(int width, int height) = 0;
    virtual bool requiresOpenGLContext() const = 0;
    virtual bool handlesPresentation() const = 0;
    virtual std::string activeGpuName() const { return {}; }
    virtual bool activeGpuIsDiscrete() const { return false; }
    virtual void drawDebugQuads(const DebugQuad* quads,
                                std::size_t quadCount,
                                int surfaceWidth,
                                int surfaceHeight) {
        (void)quads;
        (void)quadCount;
        (void)surfaceWidth;
        (void)surfaceHeight;
    }
    virtual void shutdown() = 0;
};
