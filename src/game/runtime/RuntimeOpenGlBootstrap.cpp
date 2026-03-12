#include "game/runtime/RuntimeOpenGlBootstrap.h"

#include <exception>

namespace game::runtime::opengl_bootstrap {

bool initializeOpenGlFunctions(const std::function<bool(std::string*)>& loadGlFunctions,
                               std::string* outError) {
    if (!loadGlFunctions) return false;
    return loadGlFunctions(outError);
}

PreloadResult bootstrapLoadingPresentation(bool hasOpenGlContext,
                                           const PreloadCallbacks& callbacks) {
    PreloadResult out;
    if (!hasOpenGlContext) {
        out.preloadEventsOk = callbacks.pumpPreloadEvents ? callbacks.pumpPreloadEvents() : true;
        return out;
    }

    if (!initializeOpenGlFunctions(callbacks.loadGlFunctions, &out.error)) {
        out.success = false;
        return out;
    }
    out.glFunctionsReady = true;

    try {
        if (callbacks.initBootLoadingView) {
            callbacks.initBootLoadingView();
        }
        if (callbacks.setTitle) {
            callbacks.setTitle("PokemonAutochess - Loading...");
        }
        if (callbacks.clearAndPresentFrame) {
            callbacks.clearAndPresentFrame(0.05f, 0.05f, 0.07f, 1.0f);
        }
        out.preloadEventsOk = callbacks.pumpPreloadEvents ? callbacks.pumpPreloadEvents() : true;
    } catch (const std::exception& ex) {
        out.success = false;
        out.error = ex.what();
    }

    return out;
}

} // namespace game::runtime::opengl_bootstrap
