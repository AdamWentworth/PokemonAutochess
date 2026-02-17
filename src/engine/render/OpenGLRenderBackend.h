#pragma once

#include <memory>

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
    std::string activeGpuName() const override;
    bool activeGpuIsDiscrete() const override;
    void drawDebugQuads(const DebugQuad* quads,
                        std::size_t quadCount,
                        int surfaceWidth,
                        int surfaceHeight) override;
    void shutdown() override;

private:
    std::unique_ptr<Renderer> renderer_;
};
