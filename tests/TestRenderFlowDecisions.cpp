#include "game/runtime/RenderFlowDecisions.h"

#include <string>

bool test_render_flow_decisions_contract(std::string& outFail) {
    using game::runtime::render::decideFrameRenderFlow;
    using game::runtime::render::makeRenderRoutes;

    {
        const auto flow = decideFrameRenderFlow(makeRenderRoutes(false, true), true);
        if (!flow.renderStateLayer ||
            flow.renderWorldLayer ||
            flow.renderLegacyHudLayer) {
            outFail = "disabled rendering flow mismatch";
            return false;
        }
    }

    {
        const auto flow = decideFrameRenderFlow(makeRenderRoutes(true, true), true);
        if (!flow.renderStateLayer ||
            !flow.renderWorldLayer ||
            !flow.renderLegacyHudLayer) {
            outFail = "legacy render-world flow mismatch";
            return false;
        }
    }

    {
        const auto flow = decideFrameRenderFlow(makeRenderRoutes(true, true), false);
        if (!flow.renderStateLayer ||
            flow.renderWorldLayer ||
            flow.renderLegacyHudLayer) {
            outFail = "legacy menu-only flow mismatch";
            return false;
        }
    }

    {
        const auto flow = decideFrameRenderFlow(makeRenderRoutes(true, false), true);
        if (!flow.renderStateLayer ||
            !flow.renderWorldLayer ||
            flow.renderLegacyHudLayer) {
            outFail = "backend world flow mismatch";
            return false;
        }
    }

    {
        const auto flow = decideFrameRenderFlow(makeRenderRoutes(true, false), false);
        if (!flow.renderStateLayer ||
            flow.renderWorldLayer ||
            flow.renderLegacyHudLayer) {
            outFail = "backend menu-only flow mismatch";
            return false;
        }
    }

    {
        const auto flow = decideFrameRenderFlow(makeRenderRoutes(true, false), false, true);
        if (!flow.renderStateLayer ||
            !flow.renderWorldLayer ||
            flow.renderLegacyHudLayer) {
            outFail = "backend menu-backdrop flow mismatch";
            return false;
        }
    }

    return true;
}
