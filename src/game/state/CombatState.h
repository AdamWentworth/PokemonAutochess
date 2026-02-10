#pragma once

#include "game/GameState.h"
#include "game/GameServices.h"
#include "game/scripting/LuaScript.h"
#include "game/systems/CardSystem.h"

#include <memory>
#include <string>
#include <vector>

class GameStateManager;
class GameWorld;

class TextRenderer;

class CombatState : public GameState {
public:
    CombatState(GameStateManager* manager, GameWorld* world, GameServices& services, const std::string& scriptPath);
    ~CombatState() override;

    void onEnter() override;
    void onExit() override;
    void handleInput(const InputEvent& event) override;
    void update(float deltaTime) override;
    void render() override;

private:
    bool buildShopCardList(sol::protected_function fn, std::vector<CardData>& out);
    void ensureShopUi();
    void rebuildShopCards();
    void drawShopHud(int uiW, int uiH);
    void ensureCurrencyHudResources();
    void releaseCurrencyHudResources();
    unsigned int loadCurrencyTexture(const std::string& path) const;

    GameStateManager* stateManager = nullptr;
    GameWorld* gameWorld = nullptr;
    GameServices& services;

    LuaScript script;

    std::unique_ptr<TextRenderer> textRenderer;
    std::unique_ptr<TextRenderer> shopHudText;
    std::string combatMessage;

    CardSystem shopCardSystem;
    bool shopUiEnabled = false;
    bool shopUiInitialized = false;
    bool hasShopRerollButton = false;
    int shopCardsX = 0;
    int shopCardsY = 0;
    int shopCardsH = 0;
    bool shopCardsValid = false;
    float shopRerollX = 0.0f;
    float shopRerollY = 0.0f;
    float shopRerollW = 0.0f;
    float shopRerollH = 0.0f;
    std::string currencyIconPath;
    unsigned int currencyIconTexture = 0;
    unsigned int currencyVAO = 0;
    unsigned int currencyVBO = 0;
    unsigned int currencyEBO = 0;

    const std::string& scriptPath() const;
};
