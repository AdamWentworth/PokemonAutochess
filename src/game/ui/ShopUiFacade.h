#pragma once

#include "game/systems/CardSystem.h"
#include "game/ui/ClassicShopHud.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

class TextRenderer;

namespace game::ui {

struct ShopUiRenderInput {
    int uiW = 1280;
    int uiH = 720;
    int money = 0;
    bool showReroll = false;
    std::string gameMode = "classic";
    float moneyScale = 1.35f;
    float rerollScale = 0.90f;
    std::string rerollLabel = "[Reroll 2g]";
    bool showSellOverlay = false;
};

struct ShopUiClickResult {
    bool rerollClicked = false;
    std::optional<CardData> cardClicked;
};

class ShopUiFacade {
public:
    void init(const std::string& fontPath, int hudFontSize, int overlayFontSize);
    void shutdown();

    void setCards(const std::vector<CardData>& cards, int uiW, int uiH);
    void onResize(int uiW, int uiH);
    void clear();

    ShopUiClickResult handleMouseDown(int mouseX, int mouseY);
    void render(const ShopUiRenderInput& in);

    bool hasCards() const { return hasCards_; }
    int cardsX() const { return cardsX_; }
    int cardsY() const { return cardsY_; }
    int cardsH() const { return cardsH_; }
    int sellZoneX() const { return sellZoneX_; }
    int sellZoneY() const { return sellZoneY_; }
    int sellZoneW() const { return sellZoneW_; }
    int sellZoneH() const { return sellZoneH_; }

private:
    bool isRerollHit(int mouseX, int mouseY) const;

    bool initialized_ = false;
    bool hasCards_ = false;
    std::vector<CardData> cards_;
    CardSystem cardSystem_;
    std::unique_ptr<TextRenderer> overlayText_;
    std::unique_ptr<ClassicShopHud> hud_;

    int cardsX_ = 0;
    int cardsY_ = 0;
    int cardsH_ = 0;
    int sellZoneX_ = 0;
    int sellZoneY_ = 0;
    int sellZoneW_ = 0;
    int sellZoneH_ = 0;

    float rerollX_ = 0.0f;
    float rerollY_ = 0.0f;
    float rerollW_ = 0.0f;
    float rerollH_ = 0.0f;
};

} // namespace game::ui
