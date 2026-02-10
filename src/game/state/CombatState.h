#pragma once

#include "game/GameState.h"
#include "game/GameServices.h"
#include "game/scripting/LuaScript.h"
#include "game/ui/ShopUiFacade.h"
#include "engine/ui/Card.h"

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
    void ensureShopUi();
    void rebuildShopCards();
    void drawShopHud(int uiW, int uiH, bool showSellOverlay);
    void setCombatActiveFlag(bool active);
    bool shouldDelayPostCombat() const;

    GameStateManager* stateManager = nullptr;
    GameWorld* gameWorld = nullptr;
    GameServices& services;

    LuaScript script;

    std::unique_ptr<TextRenderer> textRenderer;
    std::string combatMessage;

    std::unique_ptr<game::ui::ShopUiFacade> shopUi;
    bool shopUiEnabled = false;
    bool shopUiInitialized = false;
    bool hasShopRerollButton = false;

    bool combatStarted = false;
    bool postCombatHoldActive = false;
    float preCombatCountdownSec = 0.0f;
    float postCombatCountdownSec = 0.0f;

    const std::string& scriptPath() const;
};
