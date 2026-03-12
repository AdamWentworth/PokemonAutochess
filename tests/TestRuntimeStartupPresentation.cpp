#include <cmath>
#include <stdexcept>
#include <string>

#include "engine/render/IRenderBackend.h"
#include "game/runtime/RuntimeStartupPresentation.h"

namespace {

class FakeRenderBackend final : public IRenderBackend {
public:
    const char* backendId() const override { return "opengl"; }
    void beginFrame(float, float, float, float) override {}
    void endFrame() override {}
    void onResize(int width, int height) override {
        resizeCalls++;
        lastWidth = width;
        lastHeight = height;
    }
    bool requiresOpenGLContext() const override { return true; }
    bool handlesPresentation() const override { return false; }
    void shutdown() override {}

    int resizeCalls = 0;
    int lastWidth = 0;
    int lastHeight = 0;
};

} // namespace

bool test_runtime_startup_presentation_contract(std::string& outFail) {
    {
        const auto fontResult = game::runtime::startup_presentation::initializeFonts(
            []() { return 0; },
            []() { return std::string("unused"); });
        if (!fontResult.succeeded || !fontResult.error.empty()) {
            outFail = "initializeFonts should treat a successful font init as non-fatal and silent.";
            return false;
        }
    }

    {
        const auto fontResult = game::runtime::startup_presentation::initializeFonts(
            []() { return -1; },
            []() { return std::string("mock ttf error"); });
        if (fontResult.succeeded || fontResult.error != "mock ttf error") {
            outFail = "initializeFonts should surface the SDL_ttf error when font init fails.";
            return false;
        }
    }

    {
        const auto camera = game::runtime::startup_presentation::createDefaultCamera(1280, 720);
        const auto proj = camera->getProjectionMatrix();
        const float aspect = proj[1][1] / proj[0][0];
        if (!camera ||
            std::fabs(aspect - (1280.0f / 720.0f)) > 0.01f) {
            outFail = "createDefaultCamera should create a usable camera with the expected startup aspect ratio.";
            return false;
        }
    }

    {
        int renderBootLoadingCalls = 0;
        if (game::runtime::startup_presentation::primeInitialLoadingFrame(
                nullptr,
                1280,
                720,
                [&](float) { ++renderBootLoadingCalls; }) ||
            renderBootLoadingCalls != 0) {
            outFail = "primeInitialLoadingFrame should no-op when no renderer is present.";
            return false;
        }
    }

    {
        FakeRenderBackend renderer;
        int renderBootLoadingCalls = 0;
        const bool primed = game::runtime::startup_presentation::primeInitialLoadingFrame(
            &renderer,
            1920,
            1080,
            [&](float progress01) {
                ++renderBootLoadingCalls;
                if (std::fabs(progress01) > 0.0001f) {
                    throw std::runtime_error("unexpected loading progress");
                }
            });
        if (!primed ||
            renderer.resizeCalls != 1 ||
            renderer.lastWidth != 1920 ||
            renderer.lastHeight != 1080 ||
            renderBootLoadingCalls != 1) {
            outFail = "primeInitialLoadingFrame should resize the renderer and draw the initial loading frame once.";
            return false;
        }
    }

    return true;
}
