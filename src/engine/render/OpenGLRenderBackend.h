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
    void shutdown() override;

private:
    std::unique_ptr<Renderer> renderer_;
};
