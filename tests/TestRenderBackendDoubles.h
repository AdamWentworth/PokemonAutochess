#pragma once

#include "engine/render/IRenderBackend.h"

#include <string>

namespace test::render_doubles {

struct FakeRenderBackendConfig {
    std::string backendId = "test";
    bool requiresOpenGlContext = false;
    bool handlesPresentation = false;
    bool supportsWorldTriangles3D = false;
    bool supportsWorldIndexedMeshes = false;
    std::string activeGpuName;
    bool activeGpuIsDiscrete = false;
};

class ConfigurableFakeRenderBackend final : public IRenderBackend {
public:
    explicit ConfigurableFakeRenderBackend(FakeRenderBackendConfig config = {})
        : config_(std::move(config)) {}

    const char* backendId() const override { return config_.backendId.c_str(); }
    void beginFrame(float, float, float, float) override {}
    void endFrame() override {}
    void onResize(int width, int height) override {
        ++resizeCalls;
        lastWidth = width;
        lastHeight = height;
    }
    bool requiresOpenGLContext() const override { return config_.requiresOpenGlContext; }
    bool handlesPresentation() const override { return config_.handlesPresentation; }
    bool supportsWorldTriangles3D() const override { return config_.supportsWorldTriangles3D; }
    bool supportsWorldIndexedMeshes() const override { return config_.supportsWorldIndexedMeshes; }
    std::string activeGpuName() const override { return config_.activeGpuName; }
    bool activeGpuIsDiscrete() const override { return config_.activeGpuIsDiscrete; }
    void shutdown() override {}

    int resizeCalls = 0;
    int lastWidth = 0;
    int lastHeight = 0;

private:
    FakeRenderBackendConfig config_;
};

} // namespace test::render_doubles
