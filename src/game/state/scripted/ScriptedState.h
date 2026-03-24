#pragma once

#include <memory>
#include <string>
#include <vector>
#include <optional>

#include "game/GameState.h"
#include "game/GameWorld.h"
#include "game/GameServices.h"
#include "game/scripting/LuaScript.h"
#include "game/state/BackendShopSnapshot.h"
#include "game/state/BackendCardLayoutModel.h"
#include "game/systems/CardSystem.h"
#include "game/ui/ShopUiFacade.h"
#include "engine/ui/TextRenderer.h"
#include "engine/input/InputEvent.h"

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
    const std::string& debugScriptPath() const { return scriptPath; }

private:
    void ensureCardUI();
    void rebuildCardRow();
    void rebuildTextMenu();
    void drawShopHud(int uiW, int uiH);
    void layoutBackendTextMenu(int uiW, int uiH);
    void renderBackendTextMenu(int uiW, int uiH);
    bool shouldUseBackendCardUi() const;
    void rebuildBackendCardUi(const std::vector<CardData>& cards, int uiW, int uiH, bool isItemRow);
    void clearBackendShopUiCache();
    void resetBackendShopActionRects();
    void refreshBackendShopSnapshot();
    bool invokeBackendShopEntry(const game::state::backend_shop::Entry& entry);
    void renderBackendCardUi(int uiW, int uiH);
    bool tryHandleBackendCardKey(InputEvent::Key keyId);
    bool handleBackendCardMouseClick(int mouseX, int mouseY);
    bool tryHandleHeadlessTextMenuKey(InputEvent::Key keyId);
    void logHeadlessTextMenuHints() const;

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

    std::unique_ptr<game::ui::ShopUiFacade> shopUi;

    struct TextMenuEntry {
        std::string id;
        std::string label;
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        float labelW = 0.0f;
        float labelH = 0.0f;
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
        bool hasSlider = false;
        float sliderMin = 0.0f;
        float sliderMax = 1.0f;
        float sliderValue = 0.0f;
        float sliderStep = 0.0f;
        float sliderWidthFrac = 0.2f;
        std::string sliderValueLabel;
        float sliderX = 0.0f;
        float sliderY = 0.0f;
        float sliderW = 0.0f;
        float sliderH = 0.0f;
    };
    std::vector<TextMenuEntry> textMenuEntries;
    bool hasShopReadyButton = false;
    float shopReadyX = 0.0f;
    float shopReadyY = 0.0f;
    float shopReadyW = 0.0f;
    float shopReadyH = 0.0f;
    bool hasShopRerollButton = false;
    float backendTextMenuScale = 1.0f;

    std::vector<game::state::backend_cards::Button> backendMainButtons;
    std::vector<game::state::backend_cards::Button> backendItemButtons;
    std::vector<game::state::backend_shop::Entry> backendShopSnapshot;
    float backendRerollX = 0.0f;
    float backendRerollY = 0.0f;
    float backendRerollW = 0.0f;
    float backendRerollH = 0.0f;
};
