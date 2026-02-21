#include "game/state/BackendUiPolicy.h"

#include <string>

bool test_backend_ui_sell_overlay_policy(std::string& outFail) {
    using game::state::backend_ui::computeSellOverlayHitLayout;
    using game::state::backend_ui::computeSellOverlayOuterLayout;
    using game::state::backend_ui::shouldRenderBackendTextMenu;
    using game::state::backend_ui::shouldShowSellOverlay;
    using game::state::backend_ui::shouldUseBackendUi;

    if (!shouldUseBackendUi(true, "d3d12")) {
        outFail = "backend ui policy should allow d3d12";
        return false;
    }
    if (!shouldUseBackendUi(true, "D3D12")) {
        outFail = "backend ui policy should be case-insensitive";
        return false;
    }
    if (shouldUseBackendUi(true, "d3d12", true)) {
        outFail = "backend ui policy should allow explicit legacy-ui opt-out";
        return false;
    }
    if (shouldUseBackendUi(true, "opengl")) {
        outFail = "backend ui policy should keep opengl on legacy UI path";
        return false;
    }
    if (shouldUseBackendUi(false, "d3d12")) {
        outFail = "backend ui policy should require renderer availability";
        return false;
    }
    if (!shouldRenderBackendTextMenu(true, "d3d12", true)) {
        outFail = "backend text menu policy should allow d3d12 text menus";
        return false;
    }
    if (!shouldRenderBackendTextMenu(true, "opengl", true)) {
        outFail = "backend text menu policy should allow opengl text menus";
        return false;
    }
    if (shouldRenderBackendTextMenu(true, "d3d12", false)) {
        outFail = "backend text menu policy should require text-menu mode";
        return false;
    }
    if (shouldRenderBackendTextMenu(false, "d3d12", true)) {
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
