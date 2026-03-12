#include <stdexcept>
#include <string>

#include "engine/platform/Window.h"
#include "game/runtime/RuntimeWindowBootstrap.h"

bool test_runtime_window_bootstrap_contract(std::string& outFail) {
    using game::runtime::window_bootstrap::OpenRequest;

    {
        Window::GraphicsApi seenApi = Window::GraphicsApi::Native;
        bool seenVsync = false;
        bool queriedContext = false;
        const auto result = game::runtime::window_bootstrap::openWindow(
            OpenRequest{Window::GraphicsApi::OpenGL, true},
            [&](Window::GraphicsApi graphicsApi, bool vsyncEnabled) {
                seenApi = graphicsApi;
                seenVsync = vsyncEnabled;
            },
            [&]() {
                queriedContext = true;
                return true;
            });
        if (!result.success ||
            result.error.size() != 0 ||
            !result.hasOpenGlContext ||
            result.glFunctionsReady ||
            seenApi != Window::GraphicsApi::OpenGL ||
            !seenVsync ||
            !queriedContext) {
            outFail = "openWindow should create the requested graphics API window, query its GL-context state, and leave GL functions uninitialized.";
            return false;
        }
    }

    {
        bool queriedContext = false;
        const auto result = game::runtime::window_bootstrap::openWindow(
            OpenRequest{Window::GraphicsApi::Native, false},
            [](Window::GraphicsApi, bool) {
                throw std::runtime_error("mock window failure");
            },
            [&]() {
                queriedContext = true;
                return false;
            });
        if (result.success ||
            result.error != "mock window failure" ||
            queriedContext) {
            outFail = "openWindow should surface window-construction exceptions and skip context inspection on failure.";
            return false;
        }
    }

    return true;
}
