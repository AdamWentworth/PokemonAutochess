// CardSystem.cpp

#include "CardSystem.h"
#include <iostream>
#include <sstream>
#include <algorithm>

static const char* kShopLua = "scripts/systems/card_shop.lua";

// Default ctor (legacy path): disable telemetry emitter.
CardSystem::CardSystem() : CardSystem(EmitFn{}) {}

CardSystem::CardSystem(EmitFn emit) : emit_(std::move(emit)) {
    // init simple state
    for (int p = 0; p < kMaxPlayers; ++p) {
        gold_[p] = 10;
        level_[p] = 1;
        shops_[p] = {};
        shops_[p].slots.fill(-1);
        shops_[p].reroll_cost = 2;
        shops_[p].free_reroll = false;
    }

    // Lua VM
    lua_.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string);

    // expose deterministic RNG & queries
    lua_.set_function("randi", [this](int n){ return this->randi(n); });
    lua_.set_function("randf", [this](){ return this->randf(); });
    lua_.set_function("get_level", [this](int pid){ return this->getLevel(pid); });
    lua_.set_function("get_gold",  [this](int pid){ return this->getGold(pid); });

    // analytics/telemetry
    lua_.set_function("emit", [this](const std::string& name, const std::string& json){
        if (emit_) emit_(name, json);
    });

    // authoritative pricing
    lua_.set_function("price_for", [this](int card_id, int base_cost, int pid){
        return this->priceFor(card_id, base_cost, pid);
    });

    loadScript();
}

void CardSystem::loadScript() {
    ok_ = false;

    sol::load_result lr = lua_.load_file(kShopLua);
    if (!lr.valid()) {
        std::cerr << "[CardSystem] load error (card_shop.lua)\n";
        return;
    }
    sol::protected_function_result pr = lr();
    if (!pr.valid()) {
        std::cerr << "[CardSystem] exec error (card_shop.lua)\n";
        return;
    }

    if (sol::function init = lua_["shop_init"]; init.valid()) {
        sol::protected_function_result ir = init();
        if (!ir.valid()) {
            std::cerr << "[CardSystem] shop_init error\n";
            return;
        }
    }

    ok_ = true;
}

void CardSystem::onRoundStart(int round_idx) {
    if (!ok_) return;
    if (sol::function f = lua_["on_round_start"]; f.valid()) {
        auto r = f(round_idx);
        if (!r.valid()) std::cerr << "[CardSystem] on_round_start error\n";
    }
}

void CardSystem::onLevelChanged(int player_id, int new_level) {
    setLevel(player_id, new_level);
    if (!ok_) return;
    if (sol::function f = lua_["on_level_changed"]; f.valid()) {
        auto r = f(player_id, new_level);
        if (!r.valid()) std::cerr << "[CardSystem] on_level_changed error\n";
    }
}

PlayerShopState CardSystem::rollShop(int player_id) {
    if (!ok_) return shops_[player_id];
    shops_[player_id] = luaRollInternal(player_id);
    if (shops_[player_id].free_reroll) {
        shops_[player_id].free_reroll = false;
    } else {
        addGold(player_id, -shops_[player_id].reroll_cost);
    }
    if (emit_) emit_("ShopRolled", toJson(shops_[player_id]));
    return shops_[player_id];
}

bool CardSystem::canBuy(int player_id, int slot_index, std::string& out_reason) const {
    if (!ok_) { out_reason = "shop unavailable"; return false; }
    return luaCanBuyInternal(player_id, slot_index, out_reason);
}

bool CardSystem::buy(int player_id, int slot_index) {
    std::string reason;
    if (!canBuy(player_id, slot_index, reason)) {
        if (emit_) emit_("BuyRejected", "{\"reason\":\"" + reason + "\"}");
        return false;
    }
    const int card_id = shops_[player_id].slots[slot_index];
    if (card_id < 0) return false;

    int base_cost = 3; // demo default
    const int price = priceFor(card_id, base_cost, player_id);
    addGold(player_id, -price);

    shops_[player_id].slots[slot_index] = -1;

    if (emit_) emit_("CardBought",
        "{\"player\":" + std::to_string(player_id) +
        ",\"card\":"   + std::to_string(card_id) +
        ",\"price\":"  + std::to_string(price) + "}");
    return true;
}

// -------------------- State / RNG (stubbed) --------------------
int CardSystem::getGold(int player_id) const { return gold_[player_id]; }
void CardSystem::addGold(int player_id, int delta) { gold_[player_id] += delta; if (gold_[player_id] < 0) gold_[player_id] = 0; }
int CardSystem::getLevel(int player_id) const { return level_[player_id]; }
void CardSystem::setLevel(int player_id, int lvl) { level_[player_id] = std::max(1, lvl); }

uint32_t CardSystem::rng() {
    uint64_t x = rng_state_;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    rng_state_ = x;
    return (uint32_t)((x * 0x2545F4914F6CDD1Dull) >> 32);
}
int CardSystem::randi(int n) { return n <= 1 ? 0 : (int)(rng() % (uint32_t)n); }
float CardSystem::randf() { return (rng() / 4294967296.0f); }

// -------------------- Lua helpers --------------------
PlayerShopState CardSystem::luaRollInternal(int player_id) {
    PlayerShopState out = shops_[player_id];
    if (sol::function f = lua_["shop_roll"]; f.valid()) {
        sol::table t = f(player_id);
        if (t.valid()) {
            sol::table arr = t["slots"];
            if (arr.valid()) {
                for (int i = 1; i <= 5; ++i) {
                    sol::optional<int> v = arr[i];
                    out.slots[i - 1] = v.value_or(-1);
                }
            }
            if (sol::optional<int> rc = t["reroll_cost"]) {
                out.reroll_cost = rc.value();
            }
            sol::optional<bool> fr = t["free_reroll"];
            out.free_reroll = fr.value_or(false);
        }
    }
    return out;
}

bool CardSystem::luaCanBuyInternal(int player_id, int slot_index, std::string& reason) const {
    if (sol::function f = lua_["can_buy"]; f.valid()) {
        auto r = f(player_id, slot_index + 1 /* Lua is 1-based */);
        if (r.valid()) {
            bool ok = r.get<bool>();
            if (!ok) {
                if (r.return_count() >= 2) {
                    reason = r.get<std::string>(2);
                } else {
                    reason = "blocked";
                }
            }
            return ok;
        }
    }
    reason = "rule error";
    return false;
}

int CardSystem::priceFor(int, int base_cost, int) const {
    return base_cost;
}

std::string CardSystem::toJson(const PlayerShopState& s) {
    std::ostringstream oss;
    oss << "{\"slots\":[";
    for (size_t i = 0; i < s.slots.size(); ++i) {
        if (i) oss << ",";
        oss << s.slots[i];
    }
    oss << "],\"reroll_cost\":" << s.reroll_cost
        << ",\"free_reroll\":" << (s.free_reroll ? "true" : "false") << "}";
    return oss.str();
}

// -------------------- Legacy shims kept from earlier step --------------------
std::vector<CardData> CardSystem::spawnCardRow() {
    return spawnCardRow(0);
}

std::vector<CardData> CardSystem::spawnCardRow(int player_id) {
    std::vector<CardData> out;
    const PlayerShopState& s = shops_[player_id];
    out.reserve(s.slots.size());
    for (int id : s.slots) {
        CardData cd;
        cd.id = id;
        cd.type = CardType::Shop;
        cd.base_cost = 3;
        cd.cost = cd.base_cost;
        // pokemonName left empty in shop path unless you map ids to names elsewhere
        out.push_back(cd);
    }
    return out;
}

// -------------------- Starter UI (layout + hit-test only) --------------------
void CardSystem::spawnCardRow(const std::vector<CardData>& list, int ui_width, int y) {
    ui_cards_.clear();
    ui_row_w_ = std::max(0, ui_width);
    ui_row_y_ = y;

    if (list.empty() || ui_row_w_ <= 0) return;

    const int n = static_cast<int>(list.size());
    // Compute total width and centered start X
    const int total_w = n * card_w_ + (n - 1) * card_gap_;
    int start_x = (ui_row_w_ - total_w) / 2;
    if (start_x < 0) start_x = 0;

    ui_cards_.reserve(n);
    int x = start_x;
    for (const auto& c : list) {
        UICard uc;
        uc.data = c;
        uc.x = x; uc.y = y; uc.w = card_w_; uc.h = card_h_;
        ui_cards_.push_back(uc);
        x += card_w_ + card_gap_;
    }
}

std::optional<CardData> CardSystem::handleMouseClick(int mouse_x, int mouse_y) {
    for (const auto& uc : ui_cards_) {
        const bool inside =
            mouse_x >= uc.x && mouse_x <= (uc.x + uc.w) &&
            mouse_y >= uc.y && mouse_y <= (uc.y + uc.h);
        if (inside) {
            return uc.data;
        }
    }
    return std::nullopt;
}

void CardSystem::render(int /*ui_width*/, int /*ui_height*/) {
    // Intentionally a no-op stub.
    // If you later wire your renderer here, iterate ui_cards_ and draw frames/text.
}