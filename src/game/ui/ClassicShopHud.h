#pragma once

#include <memory>
#include <string>

class TextRenderer;

namespace game::ui {

struct ClassicShopHudInput {
    int uiW = 1280;
    int uiH = 720;
    int shopCardsX = 0;
    int shopCardsY = 0;
    int shopCardsH = 0;
    int money = 0;
    bool showReroll = false;
    std::string gameMode = "classic";
    float moneyScale = 1.35f;
    float rerollScale = 0.90f;
    std::string rerollLabel = "[Reroll 2g]";
};

struct ClassicShopHudOutput {
    float rerollX = 0.0f;
    float rerollY = 0.0f;
    float rerollW = 0.0f;
    float rerollH = 0.0f;
};

class ClassicShopHud {
public:
    ClassicShopHud() = default;
    ~ClassicShopHud();

    void init(const std::string& fontPath, int fontSize);
    void shutdown();

    ClassicShopHudOutput draw(const ClassicShopHudInput& input);

private:
    void ensureResources(const std::string& gameMode);
    void releaseResources();
    unsigned int loadCurrencyTexture(const std::string& path) const;

    std::unique_ptr<TextRenderer> text_;
    std::string iconPath_;
    unsigned int iconTexture_ = 0;
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    unsigned int ebo_ = 0;
};

} // namespace game::ui
