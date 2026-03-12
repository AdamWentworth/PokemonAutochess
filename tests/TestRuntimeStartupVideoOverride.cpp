#include <string>

#include "game/runtime/startup/RuntimeStartupConfig.h"
#include "game/runtime/startup/RuntimeStartupVideoOverride.h"

bool test_runtime_startup_video_override_contract(std::string& outFail) {
    using game::runtime::startup_config::StartupVideoOverride;

    {
        int queryCalls = 0;
        int applyCalls = 0;
        const auto result = game::runtime::startup_video_override::apply(
            StartupVideoOverride{},
            [&]() {
                ++queryCalls;
                return GameContext::VideoMode{};
            },
            [&](int, int, bool) {
                ++applyCalls;
                return true;
            });
        if (result.attempted || result.applied || !result.message.empty() || queryCalls != 0 || applyCalls != 0) {
            outFail = "apply should do nothing when no startup video override is present.";
            return false;
        }
    }

    {
        StartupVideoOverride overrideValues;
        overrideValues.hasWidth = true;
        overrideValues.width = 1600;
        overrideValues.hasFullscreen = true;
        overrideValues.fullscreen = true;

        GameContext::VideoMode currentMode;
        currentMode.width = 1280;
        currentMode.height = 720;
        currentMode.fullscreen = false;

        int applyWidth = 0;
        int applyHeight = 0;
        bool applyFullscreen = false;
        const auto result = game::runtime::startup_video_override::apply(
            overrideValues,
            [&]() { return currentMode; },
            [&](int width, int height, bool fullscreen) {
                applyWidth = width;
                applyHeight = height;
                applyFullscreen = fullscreen;
                currentMode.width = 1920;
                currentMode.height = 1080;
                currentMode.fullscreen = fullscreen;
                return true;
            });
        if (!result.attempted ||
            !result.applied ||
            applyWidth != 1600 ||
            applyHeight != 720 ||
            !applyFullscreen ||
            result.message.find("Fullscreen 1920x1080") == std::string::npos) {
            outFail = "apply should resolve missing override fields from the current mode and report the post-apply mode.";
            return false;
        }
    }

    {
        StartupVideoOverride overrideValues;
        overrideValues.hasHeight = true;
        overrideValues.height = 900;

        const auto result = game::runtime::startup_video_override::apply(
            overrideValues,
            []() {
                GameContext::VideoMode mode;
                mode.width = 1280;
                mode.height = 720;
                mode.fullscreen = false;
                return mode;
            },
            [](int, int, bool) { return false; });
        if (!result.attempted ||
            result.applied ||
            result.message.find("Failed to apply startup override video mode") == std::string::npos) {
            outFail = "apply should report a failed startup override attempt.";
            return false;
        }
    }

    return true;
}
