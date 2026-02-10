#include "game/ui/ShopUiFacade.h"

#include "game/ui/ShopLayout.h"
#include "engine/ui/TextRenderer.h"

#include <algorithm>
#include <cmath>

namespace game::ui {

void ShopUiFacade::init(const std::string& fontPath, int hudFontSize, int overlayFontSize) {
    if (initialized_) return;

    cardSystem_.init();
    cardSystem_.initOverlayText(fontPath, overlayFontSize);

    overlayText_ = std::make_unique<TextRenderer>(fontPath, overlayFontSize);
    hud_ = std::make_unique<ClassicShopHud>();
    hud_->init(fontPath, hudFontSize);

    initialized_ = true;
}

void ShopUiFacade::shutdown() {
    clear();
    if (hud_) {
        hud_->shutdown();
    }
    hud_.reset();
    overlayText_.reset();
    initialized_ = false;
}

void ShopUiFacade::clear() {
    cards_.clear();
    cardSystem_.clearCards();
    hasCards_ = false;
    cardsX_ = 0;
    cardsY_ = 0;
    cardsH_ = 0;
    rerollX_ = 0.0f;
    rerollY_ = 0.0f;
    rerollW_ = 0.0f;
    rerollH_ = 0.0f;
}

void ShopUiFacade::setCards(const std::vector<CardData>& cards, int uiW, int uiH) {
    cards_ = cards;
    cardSystem_.clearCards();
    hasCards_ = !cards_.empty();
    if (!hasCards_) {
        cardsX_ = 0;
        cardsY_ = 0;
        cardsH_ = 0;
        return;
    }

    bool allItems = true;
    for (const auto& card : cards_) {
        if (card.type != CardType::Item) {
            allItems = false;
            break;
        }
    }

    const ShopRowLayout layout = computeShopRowLayout(uiW, uiH, allItems);
    const ShopRowPlacement place = computeShopRowPlacement(uiW, uiH, static_cast<int>(cards_.size()), layout);
    cardsX_ = place.startX;
    cardsY_ = place.y;
    cardsH_ = layout.cardH;
    cardSystem_.spawnCardRowLayout(cards_, uiW, cardsY_, layout.cardW, layout.cardH, layout.spacing);
}

void ShopUiFacade::onResize(int uiW, int uiH) {
    if (!initialized_) return;
    setCards(cards_, uiW, uiH);
}

bool ShopUiFacade::isRerollHit(int mouseX, int mouseY) const {
    if (rerollW_ <= 0.0f || rerollH_ <= 0.0f) return false;
    const float mx = static_cast<float>(mouseX);
    const float my = static_cast<float>(mouseY);
    return (mx >= rerollX_ && mx <= (rerollX_ + rerollW_) &&
            my >= rerollY_ && my <= (rerollY_ + rerollH_));
}

ShopUiClickResult ShopUiFacade::handleMouseDown(int mouseX, int mouseY) {
    ShopUiClickResult out;
    if (!initialized_ || !hasCards_) return out;

    if (isRerollHit(mouseX, mouseY)) {
        out.rerollClicked = true;
        return out;
    }

    out.cardClicked = cardSystem_.handleMouseClick(mouseX, mouseY);
    return out;
}

void ShopUiFacade::render(const ShopUiRenderInput& in) {
    rerollX_ = 0.0f;
    rerollY_ = 0.0f;
    rerollW_ = 0.0f;
    rerollH_ = 0.0f;
    if (!initialized_ || !hasCards_) return;

    if (in.showSellOverlay && overlayText_) {
        const std::string sellLabel = "[ DROP HERE TO SELL ]";
        constexpr float kSellScale = 1.0f;
        const float labelW = overlayText_->measureTextWidth(sellLabel, kSellScale);
        const float x = std::round((static_cast<float>(in.uiW) - labelW) * 0.5f);
        const float y = std::round(static_cast<float>(cardsY_) + static_cast<float>(cardsH_) * 0.5f);
        overlayText_->renderText(sellLabel, x, y, glm::vec3(1.0f, 0.35f, 0.35f), kSellScale);
    } else {
        cardSystem_.render(in.uiW, in.uiH);
    }

    if (!hud_) return;

    ClassicShopHudInput hudIn;
    hudIn.uiW = in.uiW;
    hudIn.uiH = in.uiH;
    hudIn.shopCardsX = cardsX_;
    hudIn.shopCardsY = cardsY_;
    hudIn.shopCardsH = cardsH_;
    hudIn.money = in.money;
    hudIn.showReroll = in.showReroll;
    hudIn.gameMode = in.gameMode;
    hudIn.moneyScale = in.moneyScale;
    hudIn.rerollScale = in.rerollScale;
    hudIn.rerollLabel = in.rerollLabel;

    const ClassicShopHudOutput hudOut = hud_->draw(hudIn);
    rerollX_ = hudOut.rerollX;
    rerollY_ = hudOut.rerollY;
    rerollW_ = hudOut.rerollW;
    rerollH_ = hudOut.rerollH;
}

} // namespace game::ui
