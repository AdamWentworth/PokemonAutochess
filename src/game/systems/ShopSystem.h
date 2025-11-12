// ShopSystem.h
#pragma once
#include "../../engine/core/IUpdatable.h"
#include <sol/sol.hpp>
#include <SDL2/SDL.h>
#include <string>
#include <vector>
#include <optional>
#include "../ui/CardFactory.h"
#include "CardSystem.h"
#include "../../engine/events/Event.h"
#include "../../engine/events/EventManager.h"
#include "../../engine/ui/TextRenderer.h"
#include "../../game/GameConfig.h"

struct ShopCard {
    std::string name;
    int cost = 3;
    std::string type; // display only
};

class ShopSystem : public IUpdatable {
public:
    ShopSystem();
    ~ShopSystem() override = default;

    void update(float dt) override;
    void renderUI(int screenW, int screenH);

private:
    // Lua VM for shop only
    sol::state lua;
    bool ok = false;

    // UI
    CardSystem cardSystem;
    std::unique_ptr<TextRenderer> title;
    std::vector<CardData> currentCards;
    bool visible = false;

    // Fake single-player economy (you can later wire to real player data)
    int playerId = 0;
    int gold = 10;
    int level = 3;

    void loadScript();
    void onRoundPhaseChanged(const std::string& prev, const std::string& next);
    void rollShop();
    void handleMouseDown(int x, int y);

    // Helpers to expose into Lua
    void bindHostFunctions();
};
