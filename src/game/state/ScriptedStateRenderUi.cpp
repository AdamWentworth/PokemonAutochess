#include "ScriptedState.h"

#include "game/scripting/LuaScriptHelpers.h"
#include "game/runtime/GameServiceRenderRoutes.h"
#include "game/state/BackendUiPolicy.h"
#include "game/ui/UIViewport.h"

#include <sol/sol.hpp>
#include <algorithm>
#include <cmath>

void ScriptedState::render() {
    script.call("onRender");

    if (!uiInitialized) return;

    sol::table S = script.getScriptTable();
    const auto* viewport = services.viewport;
    const int uiW = viewport ? viewport->width : 1280;
    const int uiH = viewport ? viewport->height : 720;
    const auto routes = game::runtime::render::routesFromServices(services);
    const bool renderBackendTextMenuPath = game::state::backend_ui::shouldRenderBackendTextMenu(
        routes,
        cardMode == CardMode::TextMenu);

    if (titleText && !renderBackendTextMenuPath) {
        const auto msgOpt = game::scripting::callStringFunction(S, {"get_message"});
        if (msgOpt) {
            const std::string& msg = *msgOpt;
            float w = titleText->measureTextWidth(msg, 1.0f);
            float x = (static_cast<float>(uiW) - w) * 0.5f;
            constexpr float kHeaderY = 58.0f;
            const glm::vec3 msgColor = (cardMode == CardMode::TextMenu)
                ? glm::vec3(1.0f, 1.0f, 1.0f)
                : glm::vec3(1.0f, 1.0f, 0.0f);
            titleText->renderText(msg, x, kHeaderY, msgColor, 1.0f);
        }
    }

    if (cardMode == CardMode::Shop && hasShopReadyButton && titleText) {
        const std::string readyLabel = "[ Ready ]";
        const float readyScale = 0.95f;
        shopReadyW = titleText->measureTextWidth(readyLabel, readyScale);
        shopReadyH = static_cast<float>(services.config.fontSize) * readyScale;
        shopReadyX = static_cast<float>(uiW) - shopReadyW - 36.0f;
        shopReadyY = 62.0f;
        titleText->renderText(readyLabel, shopReadyX, shopReadyY,
                              glm::vec3(1.0f, 1.0f, 1.0f), readyScale);
    } else {
        shopReadyW = 0.0f;
        shopReadyH = 0.0f;
    }

    if (renderBackendTextMenuPath) {
        renderBackendTextMenu(uiW, uiH);
        return;
    }

    if (cardMode == CardMode::TextMenu && titleText) {
        float autoY = 220.0f;
        for (auto& entry : textMenuEntries) {
            const float scale = std::max(0.1f, entry.scale);
            const float textH = static_cast<float>(services.config.fontSize) * scale;
            entry.w = titleText->measureTextWidth(entry.label, scale);
            entry.h = textH;

            if (entry.hasCustomX) {
                const float anchorX = static_cast<float>(uiW) * entry.xFrac;
                entry.x = entry.anchorCenter ? (anchorX - entry.w * 0.5f) : anchorX;
            } else {
                entry.x = (static_cast<float>(uiW) - entry.w) * 0.5f;
            }

            if (entry.hasCustomY) {
                entry.y = static_cast<float>(uiH) * entry.yFrac;
            } else {
                entry.y = autoY;
                autoY += textH + 16.0f;
            }

            glm::vec3 color(1.0f, 1.0f, 1.0f);
            if (!entry.enabled) {
                color = glm::vec3(0.55f, 0.55f, 0.60f);
            } else if (entry.hasColor) {
                color = glm::vec3(entry.colorR, entry.colorG, entry.colorB);
            }

            if (entry.bold) {
                titleText->renderText(entry.label, entry.x + 0.75f, entry.y, color, scale);
            }
            titleText->renderText(entry.label, entry.x, entry.y, color, scale);

            if (entry.underline) {
                const int underCount = std::max(4, static_cast<int>(std::round(entry.w / std::max(4.0f, 10.0f * scale))));
                const std::string under(static_cast<size_t>(underCount), '_');
                titleText->renderText(under, entry.x, entry.y + textH * 0.68f, color, scale);
            }
        }
        return;
    }

    if (shouldUseBackendCardUi() &&
        (cardMode == CardMode::Shop || cardMode == CardMode::Starter)) {
        renderBackendCardUi(uiW, uiH);
        return;
    }

    const bool showSellOverlay = game::state::backend_ui::shouldShowSellOverlay(
        cardMode == CardMode::Shop,
        gameWorld != nullptr,
        gameWorld && gameWorld->isUnitDragActive(),
        gameWorld ? gameWorld->getUnitDropZoneCardCount() : 0);

    if (cardMode == CardMode::Shop) {
        if (hasShopItems && !showSellOverlay) {
            itemCardSystem.render(uiW, uiH);
        }
        drawShopHud(uiW, uiH);
    } else {
        cardSystem.render(uiW, uiH);
    }
}

void ScriptedState::drawShopHud(int uiW, int uiH) {
    if (!shopUi || !shopUi->hasCards()) return;

    game::ui::ShopUiRenderInput in;
    in.uiW = uiW;
    in.uiH = uiH;
    in.money = gameWorld ? gameWorld->getMoney() : 0;
    in.showReroll = hasShopRerollButton;
    in.gameMode = services.gameMode;
    in.moneyScale = 1.35f;
    in.rerollScale = 0.90f;
    in.rerollLabel = "[Reroll 2g]";
    in.showSellOverlay = game::state::backend_ui::shouldShowSellOverlay(
        cardMode == CardMode::Shop,
        gameWorld != nullptr,
        gameWorld && gameWorld->isUnitDragActive(),
        gameWorld ? gameWorld->getUnitDropZoneCardCount() : 0);
    in.sellOverlayPaysMoney = gameWorld ? gameWorld->isUnitSellRewardsEnabled() : true;
    shopUi->render(in);
}
