#include "game/runtime/RenderFlowDecisions.h"

#include <string>

bool test_render_flow_decisions_contract(std::string& outFail) {
    using game::runtime::render::decideFrameRenderFlow;

    {
        const auto flow = decideFrameRenderFlow(false, true, true);
        if (!flow.renderStateLayer ||
            flow.renderLegacyWorldLayer ||
            flow.renderLegacyHudLayer ||
            flow.renderBackendDebugLayer) {
            outFail = "disabled rendering flow mismatch";
            return false;
        }
    }

    {
        const auto flow = decideFrameRenderFlow(true, true, true);
        if (!flow.renderStateLayer ||
            !flow.renderLegacyWorldLayer ||
            !flow.renderLegacyHudLayer ||
            flow.renderBackendDebugLayer) {
            outFail = "legacy render-world flow mismatch";
            return false;
        }
    }

    {
        const auto flow = decideFrameRenderFlow(true, true, false);
        if (!flow.renderStateLayer ||
            flow.renderLegacyWorldLayer ||
            flow.renderLegacyHudLayer ||
            flow.renderBackendDebugLayer) {
            outFail = "legacy menu-only flow mismatch";
            return false;
        }
    }

    {
        const auto flow = decideFrameRenderFlow(true, false, true);
        if (!flow.renderStateLayer ||
            flow.renderLegacyWorldLayer ||
            flow.renderLegacyHudLayer ||
            !flow.renderBackendDebugLayer) {
            outFail = "backend world flow mismatch";
            return false;
        }
    }

    {
        const auto flow = decideFrameRenderFlow(true, false, false);
        if (!flow.renderStateLayer ||
            flow.renderLegacyWorldLayer ||
            flow.renderLegacyHudLayer ||
            flow.renderBackendDebugLayer) {
            outFail = "backend menu-only flow mismatch";
            return false;
        }
    }

    return true;
}
