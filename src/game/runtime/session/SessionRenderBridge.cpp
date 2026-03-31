#include "game/runtime/session/SessionRenderBridge.h"

#include "engine/utils/LogSink.h"
#include "game/runtime/startup/RuntimeWorldLayerPrewarm.h"
#include "game/ui/UIViewport.h"

#include <algorithm>

namespace game::runtime::session_render_bridge {

namespace {

void renderFrameFromFlow(const render::FrameRenderFlow& flow,
                         int drawableW,
                         int drawableH,
                         bool renderWorld,
                         const Context& context) {
    if (flow.renderWorldLayer && context.renderWorldLayer) {
        context.renderWorldLayer(drawableW, drawableH, renderWorld);
    }
    if (flow.renderStateLayer && context.renderStateLayer) {
        context.renderStateLayer();
    }
}

} // namespace

void render(int drawableW, int drawableH, const Context& context) {
    if (context.viewport) {
        context.viewport->set(drawableW, drawableH);
    }
    if (context.setUnitScreenSize) {
        context.setUnitScreenSize(
            static_cast<unsigned int>(std::max(1, drawableW)),
            static_cast<unsigned int>(std::max(1, drawableH)));
    }

    const bool renderWorld = context.resolveRenderWorld ? context.resolveRenderWorld() : true;
    const render::FrameRenderFlow flow =
        context.currentFrameFlow ? context.currentFrameFlow(renderWorld)
                                 : render::FrameRenderFlow{};

    if (context.worldLayerPrewarmFramesRemaining && context.consoleLog) {
        world_layer_prewarm::maybeRunDeferredFrame(
            *context.worldLayerPrewarmFramesRemaining,
            context.worldLayerPrewarmFrameCount,
            flow.renderWorldLayer,
            drawableW,
            drawableH,
            world_layer_prewarm::Callbacks{
                .setTitle = context.setTitle,
                .renderWorldLayer =
                    [&](int prewarmW, int prewarmH) {
                        if (context.renderWorldLayer) {
                            context.renderWorldLayer(prewarmW, prewarmH, true);
                        }
                    },
            },
            *context.consoleLog);
    }

    renderFrameFromFlow(flow, drawableW, drawableH, renderWorld, context);
}

} // namespace game::runtime::session_render_bridge
