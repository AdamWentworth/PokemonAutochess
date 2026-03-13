#include <sstream>
#include <string>
#include <vector>

#include "game/runtime/startup/RuntimeWorldLayerPrewarm.h"

bool test_runtime_world_layer_prewarm_contract(std::string& outFail) {
    using game::runtime::world_layer_prewarm::Callbacks;

    {
        int framesRemaining = 0;
        std::vector<std::string> titles;
        std::vector<float> progressValues;
        std::vector<std::pair<int, int>> renderCalls;
        int requestQuitCalls = 0;
        std::ostringstream logs;

        const Callbacks callbacks{
            .setTitle = [&](const std::string& title) { titles.push_back(title); },
            .renderBootLoading = [&](float progress) { progressValues.push_back(progress); },
            .pumpPreloadEvents = []() { return true; },
            .requestQuit = [&]() { ++requestQuitCalls; },
            .renderWorldLayer =
                [&](int w, int h) {
                    renderCalls.emplace_back(w, h);
                },
        };

        game::runtime::world_layer_prewarm::schedule(framesRemaining, 2, callbacks, logs);
        game::runtime::world_layer_prewarm::drainStartupFrames(
            framesRemaining,
            2,
            1600,
            900,
            callbacks,
            logs);
        game::runtime::world_layer_prewarm::restoreTitleAfterInit(framesRemaining, callbacks);

        if (framesRemaining != 0 ||
            requestQuitCalls != 0 ||
            renderCalls.size() != 2u ||
            renderCalls[0] != std::pair<int, int>(1600, 900) ||
            renderCalls[1] != std::pair<int, int>(1600, 900)) {
            outFail = "schedule and drainStartupFrames should run each world/board prewarm frame once.";
            return false;
        }

        if (titles.size() < 4u ||
            titles.front() != "PokemonAutochess - Loading world/board..." ||
            titles[1].find("Loading world/board 1/2") == std::string::npos ||
            titles[2].find("Loading world/board 2/2") == std::string::npos ||
            titles.back() != "Pokemon Autochess") {
            outFail = "world-layer prewarm should drive schedule, per-frame, and completion titles.";
            return false;
        }

        if (progressValues.size() != 3u ||
            progressValues.front() != 0.98f ||
            progressValues[1] <= 0.98f ||
            progressValues[2] != 1.0f) {
            outFail = "world-layer prewarm should report scheduled and per-frame boot progress.";
            return false;
        }

        const std::string logText = logs.str();
        if (logText.find("World/board prewarm scheduled: frames=2") == std::string::npos ||
            logText.find("World/board prewarm frame 1/2 begin") == std::string::npos ||
            logText.find("World/board prewarm frame 2/2 complete: time=") == std::string::npos) {
            outFail = "world-layer prewarm should preserve scheduling and frame timing logs.";
            return false;
        }
    }

    {
        int framesRemaining = 1;
        std::vector<std::string> titles;
        std::vector<std::pair<int, int>> renderCalls;
        std::ostringstream logs;

        const Callbacks callbacks{
            .setTitle = [&](const std::string& title) { titles.push_back(title); },
            .renderWorldLayer =
                [&](int w, int h) {
                    renderCalls.emplace_back(w, h);
                },
        };

        game::runtime::world_layer_prewarm::maybeRunDeferredFrame(
            framesRemaining,
            2,
            false,
            1280,
            720,
            callbacks,
            logs);

        if (framesRemaining != 0 ||
            renderCalls.size() != 1u ||
            titles.size() < 2u ||
            titles.front().find("Loading world/board 2/2") == std::string::npos ||
            titles.back() != "Pokemon Autochess") {
            outFail = "maybeRunDeferredFrame should complete the remaining world-layer prewarm frame and restore the title.";
            return false;
        }
    }

    {
        int framesRemaining = 2;
        int requestQuitCalls = 0;
        int renderCalls = 0;
        std::ostringstream logs;

        const Callbacks callbacks{
            .pumpPreloadEvents =
                [calls = 0]() mutable {
                    ++calls;
                    return calls < 1;
                },
            .requestQuit = [&]() { ++requestQuitCalls; },
            .renderWorldLayer =
                [&](int, int) {
                    ++renderCalls;
                },
        };

        game::runtime::world_layer_prewarm::drainStartupFrames(
            framesRemaining,
            2,
            1920,
            1080,
            callbacks,
            logs);

        if (framesRemaining != 1 || requestQuitCalls != 1 || renderCalls != 1) {
            outFail = "drainStartupFrames should stop after the current frame when preload event pumping requests quit.";
            return false;
        }
    }

    return true;
}
