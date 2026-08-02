#pragma once

#include "game/GameState.h"
#include "game/GameServices.h"
#include "game/state/BackendShopSnapshot.h"
#include "game/state/BackendCardLayoutModel.h"
#include "game/scripting/LuaScript.h"
#include "game/systems/RoundPhase.h"
#include "game/ui/ShopUiFacade.h"
#include "engine/input/InputEvent.h"
#include "game/ui/legacy/Card.h"

#include <optional>
#include <memory>
#include <string>
#include <vector>

class GameStateManager;
class GameWorld;

class TextRenderer;

class CombatState : public GameState {
public:
    CombatState(GameStateManager* manager,
                GameWorld* world,
                GameServices& services,
                const std::string& scriptPath,
                bool resumeFromSnapshot = false);
    ~CombatState() override;

    void onEnter() override;
    void onExit() override;
    void handleInput(const InputEvent& event) override;
    void update(float deltaTime) override;
    void render() override;
    const std::string& debugScriptPath() const { return loadedScriptPath; }
    void configureEditorPreviewPhase(RoundPhase phase);

private:
    bool shouldUseBackendShopUi() const;
    void rebuildBackendShopUi(const std::vector<CardData>& cards, int uiW, int uiH);
    void refreshBackendShopSnapshot();
    bool invokeBackendShopEntry(const game::state::backend_shop::Entry& entry);
    bool tryHandleBackendShopKey(InputEvent::Key keyId);
    bool handleBackendShopMouseClick(int mouseX, int mouseY);
    void renderBackendShopUi(int uiW, int uiH, bool showSellOverlay, const std::string& header);
    void ensureShopUi();
    void rebuildShopCards();
    void clearBackendShopUiCache();
    void drawShopHud(int uiW, int uiH, bool showSellOverlay);
    void setCombatActiveFlag(bool active);
    bool shouldDelayPostCombat() const;
    void refreshNativeRouteFlowMetadata();
    bool tryFinishNativeRouteFlow();
    void emitScriptStyleLog(const std::string& tagOrMsg, const std::optional<std::string>& payload);
    void emitGoldLog(const std::string& msg);

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
    std::vector<game::state::backend_cards::Button> backendShopButtons;
    std::vector<game::state::backend_shop::Entry> backendShopSnapshot;
    float backendRerollX = 0.0f;
    float backendRerollY = 0.0f;
    float backendRerollW = 0.0f;
    float backendRerollH = 0.0f;

    bool combatStarted = false;
    bool editorPlanningPreview = false;
    bool postCombatHoldActive = false;
    float preCombatCountdownSec = 0.0f;
    float postCombatCountdownSec = 0.0f;
    bool nativeRouteFlowEnabled = false;
    bool nativeRouteUsesClassicMode = false;
    bool nativeRouteTransitionQueued = false;
    std::string nativeRouteNextShopScriptPath;
    std::string nativeRouteClearMessage;

    std::string loadedScriptPath;
    bool resumeFromSnapshot = false;
};
