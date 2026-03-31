#include <sstream>
#include <string>

#include "engine/utils/LogSink.h"
#include "game/runtime/session/SessionRenderBridge.h"
#include "game/ui/UIViewport.h"

bool test_session_render_bridge_contract(std::string& outFail) {
    {
        game::ui::UIViewport viewport;
        std::ostringstream info;
        std::ostringstream err;
        engine::log::Sink sink("TEST", &info, &err);
        int framesRemaining = 1;
        int unitW = 0;
        int unitH = 0;
        int worldCalls = 0;
        int stateCalls = 0;
        bool flowRenderWorld = true;
        bool deferredRenderWorld = false;

        game::runtime::session_render_bridge::render(
            1600,
            900,
            {
                .viewport = &viewport,
                .worldLayerPrewarmFramesRemaining = &framesRemaining,
                .worldLayerPrewarmFrameCount = 1,
                .consoleLog = &sink,
                .setUnitScreenSize =
                    [&](unsigned int w, unsigned int h) {
                        unitW = static_cast<int>(w);
                        unitH = static_cast<int>(h);
                    },
                .resolveRenderWorld =
                    [&]() {
                        return false;
                    },
                .currentFrameFlow =
                    [&](bool renderWorld) {
                        flowRenderWorld = renderWorld;
                        return game::runtime::render::FrameRenderFlow{
                            .renderStateLayer = true,
                            .renderWorldLayer = false,
                        };
                    },
                .setTitle = [&](const std::string&) {},
                .renderWorldLayer =
                    [&](int, int, bool renderWorld) {
                        ++worldCalls;
                        deferredRenderWorld = renderWorld;
                    },
                .renderStateLayer =
                    [&]() {
                        ++stateCalls;
                    },
            });

        if (viewport.width != 1600 || viewport.height != 900 ||
            unitW != 1600 || unitH != 900 ||
            flowRenderWorld != false ||
            worldCalls != 1 || stateCalls != 1 ||
            !deferredRenderWorld || framesRemaining != 0) {
            outFail =
                "SessionRenderBridge should update viewport sizing, honor render-world resolution, and run one deferred world-layer prewarm frame when the flow skips world rendering.";
            return false;
        }
    }

    {
        game::ui::UIViewport viewport;
        std::ostringstream info;
        std::ostringstream err;
        engine::log::Sink sink("TEST", &info, &err);
        int framesRemaining = 1;
        int worldCalls = 0;

        game::runtime::session_render_bridge::render(
            1280,
            720,
            {
                .viewport = &viewport,
                .worldLayerPrewarmFramesRemaining = &framesRemaining,
                .worldLayerPrewarmFrameCount = 1,
                .consoleLog = &sink,
                .resolveRenderWorld =
                    [&]() {
                        return true;
                    },
                .currentFrameFlow =
                    [&](bool) {
                        return game::runtime::render::FrameRenderFlow{
                            .renderStateLayer = false,
                            .renderWorldLayer = true,
                        };
                    },
                .setTitle = [&](const std::string&) {},
                .renderWorldLayer =
                    [&](int, int, bool) {
                        ++worldCalls;
                    },
            });

        if (worldCalls != 1 || framesRemaining != 1) {
            outFail =
                "SessionRenderBridge should not consume a deferred world-layer prewarm frame when the active flow already renders the world layer.";
            return false;
        }
    }

    return true;
}
