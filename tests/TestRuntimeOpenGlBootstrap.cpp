#include <stdexcept>
#include <string>

#include "game/runtime/RuntimeOpenGlBootstrap.h"

bool test_runtime_opengl_bootstrap_contract(std::string& outFail) {
    {
        int loaderCalls = 0;
        std::string error;
        if (!game::runtime::opengl_bootstrap::initializeOpenGlFunctions(
                [&](std::string* outError) {
                    ++loaderCalls;
                    if (outError) *outError = "";
                    return true;
                },
                &error) ||
            loaderCalls != 1 ||
            !error.empty()) {
            outFail = "initializeOpenGlFunctions should delegate to the supplied GL-loader callback.";
            return false;
        }
    }

    {
        int preloadPumpCalls = 0;
        int loaderCalls = 0;
        const auto result = game::runtime::opengl_bootstrap::bootstrapLoadingPresentation(
            false,
            game::runtime::opengl_bootstrap::PreloadCallbacks{
                [&](std::string*) {
                    ++loaderCalls;
                    return true;
                },
                []() {},
                [](std::string_view) {},
                [](float, float, float, float) {},
                [&]() {
                    ++preloadPumpCalls;
                    return true;
                }});
        if (!result.success ||
            result.glFunctionsReady ||
            !result.preloadEventsOk ||
            loaderCalls != 0 ||
            preloadPumpCalls != 1) {
            outFail = "bootstrapLoadingPresentation should skip GL bootstrap when no OpenGL context exists and still pump preload events once.";
            return false;
        }
    }

    {
        int loaderCalls = 0;
        int initViewCalls = 0;
        int titleCalls = 0;
        int clearCalls = 0;
        int preloadPumpCalls = 0;
        const auto result = game::runtime::opengl_bootstrap::bootstrapLoadingPresentation(
            true,
            game::runtime::opengl_bootstrap::PreloadCallbacks{
                [&](std::string* outError) {
                    ++loaderCalls;
                    if (outError) *outError = "";
                    return true;
                },
                [&]() { ++initViewCalls; },
                [&](std::string_view title) {
                    ++titleCalls;
                    if (title != "PokemonAutochess - Loading...") {
                        throw std::runtime_error("unexpected title");
                    }
                },
                [&](float r, float g, float b, float a) {
                    ++clearCalls;
                    if (r != 0.05f || g != 0.05f || b != 0.07f || a != 1.0f) {
                        throw std::runtime_error("unexpected clear color");
                    }
                },
                [&]() {
                    ++preloadPumpCalls;
                    return false;
                }});
        if (!result.success ||
            !result.glFunctionsReady ||
            result.preloadEventsOk ||
            loaderCalls != 1 ||
            initViewCalls != 1 ||
            titleCalls != 1 ||
            clearCalls != 1 ||
            preloadPumpCalls != 1) {
            outFail = "bootstrapLoadingPresentation should initialize GL, prime the loading frame, and report the preload pump result.";
            return false;
        }
    }

    {
        const auto result = game::runtime::opengl_bootstrap::bootstrapLoadingPresentation(
            true,
            game::runtime::opengl_bootstrap::PreloadCallbacks{
                [](std::string* outError) {
                    if (outError) *outError = "mock glad failure";
                    return false;
                },
                []() {},
                [](std::string_view) {},
                [](float, float, float, float) {},
                []() { return true; }});
        if (result.success ||
            result.error != "mock glad failure" ||
            result.glFunctionsReady) {
            outFail = "bootstrapLoadingPresentation should stop on GL-loader failure and surface the loader error.";
            return false;
        }
    }

    return true;
}
