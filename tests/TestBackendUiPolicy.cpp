#include "game/state/BackendUiPolicy.h"

#include <string>

bool test_backend_ui_sell_overlay_policy(std::string& outFail) {
    using game::runtime::render::makeRenderRoutes;
    using game::state::backend_ui::computeSellOverlayHitLayout;
    using game::state::backend_ui::computeSellOverlayOuterLayout;
    using game::state::backend_ui::shouldRenderBackendTextMenu;
    using game::state::backend_ui::shouldShowSellOverlay;
    using game::state::backend_ui::shouldUseBackendUi;

    if (!shouldUseBackendUi(makeRenderRoutes(true))) {
        outFail = "backend ui policy should allow backend UI when legacy path is not preferred";
        return false;
    }
    if (shouldUseBackendUi(makeRenderRoutes(true), true)) {
        outFail = "backend ui policy should allow explicit legacy-ui opt-out";
        return false;
    }
    if (shouldUseBackendUi(makeRenderRoutes(false))) {
        outFail = "backend ui policy should require renderer availability";
        return false;
    }
    if (!shouldRenderBackendTextMenu(makeRenderRoutes(true), true)) {
        outFail = "backend text menu policy should allow backend-neutral text menus";
        return false;
    }
    if (shouldRenderBackendTextMenu(makeRenderRoutes(true), false)) {
        outFail = "backend text menu policy should require text-menu mode";
        return false;
    }
    if (shouldRenderBackendTextMenu(makeRenderRoutes(false), true)) {
        outFail = "backend text menu policy should require renderer availability";
        return false;
    }

    if (shouldShowSellOverlay(false, true, true, 3)) {
        outFail = "sell overlay should require shop mode";
        return false;
    }
    if (shouldShowSellOverlay(true, false, true, 3)) {
        outFail = "sell overlay should require world context";
        return false;
    }
    if (shouldShowSellOverlay(true, true, false, 3)) {
        outFail = "sell overlay should require active drag";
        return false;
    }
    if (shouldShowSellOverlay(true, true, true, 0)) {
        outFail = "sell overlay should require positive drop-zone card count";
        return false;
    }
    if (!shouldShowSellOverlay(true, true, true, 2)) {
        outFail = "sell overlay should activate when all conditions are met";
        return false;
    }

    {
        const auto outer = computeSellOverlayOuterLayout(1280, 720, 5, false);
        if (outer.x != 463 || outer.y != 593 || outer.w != 354 || outer.h != 127) {
            outFail = "backend sell-overlay outer layout mismatch";
            return false;
        }
        const auto hit = computeSellOverlayHitLayout(outer);
        if (hit.x != 537 || hit.y != 619 || hit.w != 205 || hit.h != 74) {
            outFail = "backend sell-overlay hit layout mismatch";
            return false;
        }
    }

    {
        const auto outer = computeSellOverlayOuterLayout(1280, 720, 0, false);
        if (outer.w != 0 || outer.h != 0) {
            outFail = "backend sell-overlay outer layout should be empty when card count is zero";
            return false;
        }
        const auto hit = computeSellOverlayHitLayout(outer);
        if (hit.w != 0 || hit.h != 0) {
            outFail = "backend sell-overlay hit layout should be empty when outer layout is empty";
            return false;
        }
    }

    return true;
}
