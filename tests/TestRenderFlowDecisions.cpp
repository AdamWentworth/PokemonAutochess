#include "game/runtime/routes/RenderFlowDecisions.h"

#include <string>

bool test_render_flow_decisions_contract(std::string& outFail) {
    using game::runtime::render::decideFrameRenderFlow;
    using game::runtime::render::makeRenderRoutes;

    {
        const auto flow = decideFrameRenderFlow(makeRenderRoutes(false), true);
        if (!flow.renderStateLayer ||
            flow.renderWorldLayer) {
            outFail = "disabled rendering flow mismatch";
            return false;
        }
    }

    {
        const auto flow = decideFrameRenderFlow(makeRenderRoutes(true), true);
        if (!flow.renderStateLayer ||
            !flow.renderWorldLayer) {
            outFail = "render-world flow mismatch";
            return false;
        }
    }

    {
        const auto flow = decideFrameRenderFlow(makeRenderRoutes(true), false);
        if (!flow.renderStateLayer ||
            flow.renderWorldLayer) {
            outFail = "menu-only flow mismatch";
            return false;
        }
    }

    {
        const auto flow = decideFrameRenderFlow(makeRenderRoutes(true), false);
        if (!flow.renderStateLayer ||
            flow.renderWorldLayer) {
            outFail = "backend menu-only flow mismatch";
            return false;
        }
    }

    {
        const auto flow = decideFrameRenderFlow(makeRenderRoutes(true), false, true);
        if (!flow.renderStateLayer ||
            !flow.renderWorldLayer) {
            outFail = "backend menu-backdrop flow mismatch";
            return false;
        }
    }

    return true;
}
