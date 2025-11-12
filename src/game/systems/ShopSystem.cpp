// ShopSystem.cpp
#include "ShopSystem.h"
#include "../../engine/events/RoundEvents.h"
#include <iostream>

ShopSystem::ShopSystem() {
    // UI bootstrap
    cardSystem.init();
    const auto& cfg = GameConfig::get();
    title = std::make_unique<TextRenderer>(cfg.fontPath, cfg.fontSize);

    // Lua VM
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string);

    bindHostFunctions();
    loadScript();

    // Subscribe to round phase transitions
    EventManager::getInstance().subscribe(EventType::RoundPhaseChanged,
        [this](const Event& e){
            const auto& re = static_cast<const RoundPhaseChangedEvent&>(e);
            onRoundPhaseChanged(re.previousPhase, re.nextPhase);
        });

    // Subscribe to mouse clicks for card selection
    EventManager::getInstance().subscribe(EventType::MouseButtonDown,
        [this](const Event& e){
            const auto& me = static_cast<const MouseButtonDownEvent&>(e);
            if (visible) handleMouseDown(me.getX(), me.getY());
        });
}

void ShopSystem::bindHostFunctions() {
    // Random utilities for Lua shop script
    lua.set_function("randf", [](){ return (double)rand() / (double)RAND_MAX; });
    lua.set_function("randi", [](int max){ if (max <= 0) return 0; return rand() % max; });

    // Minimal "economy" bindings (replace with real data later)
    lua.set_function("get_gold", [this](int /*player*/){ return this->gold; });
    lua.set_function("get_level",[this](int /*player*/){ return this->level; });

    // Allow Lua to compute final price
    lua.set_function("price_for", [this](sol::object /*cardId*/, int baseCost, int /*player*/){
        // Placeholder modifier: high levels add +1 cost every 3 levels
        int modifier = (this->level >= 4) ? (this->level / 3) : 0;
        return baseCost + modifier;
    });

    // Simple logger/event
    lua.set_function("emit", [](const std::string& evt, const std::string& payload){
        std::cout << "[Shop Lua Emit] " << evt << " " << payload << "\n";
    });
}

void ShopSystem::loadScript() {
    sol::load_result chunk = lua.load_file("scripts/systems/card_shop.lua");
    if (!chunk.valid()) {
        sol::error e = chunk;
        std::cerr << "[ShopSystem] load error: " << e.what() << "\n";
        ok = false; return;
    }
    sol::protected_function_result r = chunk();
    if (!r.valid()) {
        sol::error e = r;
        std::cerr << "[ShopSystem] exec error: " << e.what() << "\n";
        ok = false; return;
    }
    if (sol::function init = lua["shop_init"]; init.valid()) {
        sol::protected_function_result ir = init();
        if (!ir.valid()) {
            sol::error e = ir;
            std::cerr << "[ShopSystem] shop_init error: " << e.what() << "\n";
            ok = false; return;
        }
    }
    ok = true;
}

void ShopSystem::onRoundPhaseChanged(const std::string& prev, const std::string& next) {
    (void)prev;
    if (!ok) return;
    if (next == "Planning") {
        visible = true;
        rollShop();
    } else {
        visible = false;
        currentCards.clear();
        cardSystem.clearCards();
    }
}

void ShopSystem::rollShop() {
    if (!ok) return;
    sol::function roll = lua["shop_roll"];
    if (!roll.valid()) {
        std::cerr << "[ShopSystem] shop_roll missing in Lua.\n";
        return;
    }
    sol::protected_function_result r = roll(playerId);
    if (!r.valid() || r.get_type()!=sol::type::table) {
        std::cerr << "[ShopSystem] shop_roll returned invalid value.\n";
        return;
    }

    // Expect t.slots = { {name="...", cost=?, type="..."}, ... }
    sol::table t = r;
    sol::table slots = t["slots"];
    currentCards.clear();
    std::vector<CardData> dataList;
    for (auto&& kv : slots) {
        sol::table row = kv.second.as<sol::table>();
        CardData cd;
        auto nameOpt = row.get<sol::optional<std::string>>("name");
        auto costOpt = row.get<sol::optional<int>>("cost");
        auto typeOpt = row.get<sol::optional<std::string>>("type");
        cd.pokemonName = nameOpt.value_or(std::string("rattata"));
        cd.cost = costOpt.value_or(3);
        cd.type = (typeOpt.value_or(std::string("Shop")) == "Starter") ? CardType::Starter : CardType::Shop;
        dataList.push_back(cd);
    }

    // Build visible cards row at the bottom
    cardSystem.spawnCardRow(dataList, /*screenWidth*/1280, /*y*/ 520);
    currentCards = std::move(dataList);
}

void ShopSystem::handleMouseDown(int x, int y) {
    if (!visible) return;
    auto hit = cardSystem.handleMouseClick(x, y);
    if (!hit) return;

    const CardData& cd = *hit;

    // Query Lua if purchasable
    sol::function can_buy = lua["can_buy"];
    bool allowed = true;
    std::string reason;
    if (can_buy.valid()) {
        sol::protected_function_result cr = can_buy(playerId, 1); // slot index is ignored by our Lua impl
        if (cr.valid() && cr.get_type()==sol::type::boolean) {
            allowed = cr.get<bool>();
        }
    }
    if (!allowed) {
        std::cout << "[ShopSystem] Cannot buy " << cd.pokemonName << "\n";
        return;
    }

    int finalPrice = 3;
    // Let Lua compute price again so UI & logic stay in sync
    sol::function pf = lua["price_for"];
    if (pf.valid()) {
        sol::protected_function_result pr = pf(sol::object(), cd.cost, playerId);
        if (pr.valid() && pr.get_type()==sol::type::number) {
            finalPrice = pr.get<int>();
        } else {
            finalPrice = cd.cost;
        }
    } else {
        finalPrice = cd.cost;
    }

    if (gold < finalPrice) {
        std::cout << "[ShopSystem] Not enough gold (" << gold << " / " << finalPrice << ")\n";
        return;
    }

    gold -= finalPrice;
    std::cout << "[ShopSystem] Bought " << cd.pokemonName << " for " << finalPrice << " gold. Remaining: " << gold << "\n";
    // NOTE: Spawning the purchased unit onto the bench/board can be done here
    // by calling into GameWorld once you wire ShopSystem to it.
}

void ShopSystem::update(float dt) {
    (void)dt;
    // No per-frame logic required; input/event-driven
}

void ShopSystem::renderUI(int screenW, int screenH) {
    if (!visible) return;

    // Header text
    const std::string msg = "Shop  (Gold: " + std::to_string(gold) + ", Lvl: " + std::to_string(level) + ")";
    float textWidth = title->measureTextWidth(msg, 1.0f);
    float centeredX = std::round((screenW - textWidth) / 2.0f);
    title->renderText(msg, centeredX, 470.0f, glm::vec3(1.0f), 1.0f);

    // Cards
    cardSystem.render(screenW, screenH);
}
