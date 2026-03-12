#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace game::runtime::opengl_bootstrap {

struct PreloadCallbacks {
    std::function<bool(std::string*)> loadGlFunctions;
    std::function<void()> initBootLoadingView;
    std::function<void(std::string_view)> setTitle;
    std::function<void(float, float, float, float)> clearAndPresentFrame;
    std::function<bool()> pumpPreloadEvents;
};

struct PreloadResult {
    bool success = true;
    bool glFunctionsReady = false;
    bool preloadEventsOk = true;
    std::string error;
};

bool initializeOpenGlFunctions(const std::function<bool(std::string*)>& loadGlFunctions,
                               std::string* outError);

PreloadResult bootstrapLoadingPresentation(bool hasOpenGlContext,
                                           const PreloadCallbacks& callbacks);

} // namespace game::runtime::opengl_bootstrap
