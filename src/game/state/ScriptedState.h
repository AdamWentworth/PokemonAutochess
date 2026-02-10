#pragma once

#include <memory>
#include <string>
#include <vector>
#include <optional>

#include "game/GameState.h"
#include "game/GameWorld.h"
#include "game/GameServices.h"
#include "game/scripting/LuaScript.h"
#include "game/systems/CardSystem.h"
#include "engine/ui/TextRenderer.h"

class GameStateManager;

// A thin C++ wrapper that forwards state lifecycle to a Lua script.
// Additionally, if the Lua script exposes starter UI helpers
// (get_starter_cards / on_card_click / handle_starter_key),
// this state will build and drive a simple card UI for selection.
class ScriptedState : public GameState {
public:
    ScriptedState(GameStateManager* manager, GameWorld* world, GameServices& services, const std::string& scriptPath);

    ~ScriptedState() override;

    void onEnter() override;
    void onExit() override;
    void handleInput(const InputEvent& event) override;
    void update(float deltaTime) override;
    void render() override;
    bool shouldRenderWorld() const override { return renderWorld; }

private:
    void ensureCardUI();
    void rebuildCardRow();
    void rebuildTextMenu();
    void ensureCurrencyHudResources();
    void releaseCurrencyHudResources();
    void drawCurrencyHud(int uiW, int uiH);
    unsigned int loadCurrencyTexture(const std::string& path) const;

private:
    GameStateManager* stateManager = nullptr;
    GameWorld* gameWorld = nullptr;
    GameServices& services;

    std::string scriptPath;
    LuaScript script;

    CardSystem cardSystem;
    CardSystem itemCardSystem;
    std::unique_ptr<TextRenderer> titleText;
    bool uiInitialized = false;
    enum class CardMode { None, Starter, Shop, TextMenu };
    CardMode cardMode = CardMode::None;
    bool hasShopItems = false;
    bool hasTextMenu = false;
    bool renderWorld = true;

    std::unique_ptr<TextRenderer> currencyText;
    std::string currencyIconPath;
    unsigned int currencyIconTexture = 0;
    unsigned int currencyVAO = 0;
    unsigned int currencyVBO = 0;
    unsigned int currencyEBO = 0;
    int shopCardsX = 0;
    int shopCardsY = 0;
    int shopCardsH = 0;
    bool shopCardsValid = false;

    struct TextMenuEntry {
        std::string id;
        std::string label;
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        float scale = 1.0f;
        bool enabled = true;
        bool bold = false;
        bool underline = false;
        bool hasCustomX = false;
        bool hasCustomY = false;
        float xFrac = 0.5f;
        float yFrac = 0.5f;
        bool anchorCenter = true;
        bool hasColor = false;
        float colorR = 1.0f;
        float colorG = 1.0f;
        float colorB = 1.0f;
    };
    std::vector<TextMenuEntry> textMenuEntries;
    bool hasShopReadyButton = false;
    float shopReadyX = 0.0f;
    float shopReadyY = 0.0f;
    float shopReadyW = 0.0f;
    float shopReadyH = 0.0f;
    bool hasShopRerollButton = false;
    float shopRerollX = 0.0f;
    float shopRerollY = 0.0f;
    float shopRerollW = 0.0f;
    float shopRerollH = 0.0f;
};
