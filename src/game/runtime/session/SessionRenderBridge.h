#pragma once

#include "game/runtime/routes/RenderFlowDecisions.h"

#include <functional>
#include <string>

namespace engine::log {
class Sink;
}

namespace game::ui {
struct UIViewport;
}

namespace game::runtime::session_render_bridge {

struct Context {
    game::ui::UIViewport* viewport = nullptr;
    int* worldLayerPrewarmFramesRemaining = nullptr;
    int worldLayerPrewarmFrameCount = 0;
    engine::log::Sink* consoleLog = nullptr;
    std::function<void(unsigned int, unsigned int)> setUnitScreenSize;
    std::function<bool()> resolveRenderWorld;
    std::function<render::FrameRenderFlow(bool)> currentFrameFlow;
    std::function<void(const std::string&)> setTitle;
    std::function<void(int, int, bool)> renderWorldLayer;
    std::function<void()> renderStateLayer;
};

void render(int drawableW, int drawableH, const Context& context);

} // namespace game::runtime::session_render_bridge
