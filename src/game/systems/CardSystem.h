// CardSystem.h

#pragma once
#include <array>
#include <vector>
#include <functional>
#include <string>
#include <optional>
#include <sol/sol.hpp>

static constexpr int kMaxPlayers = 4;

enum class CardType {
    Starter,
    Shop
};

struct CardData {
    int id = -1;                  // Used by shop flow; -1 for placeholder
    CardType type = CardType::Shop;

    // Starter/UI expectations from ScriptedState
    std::string pokemonName;      // display / identifier
    int cost = 0;                 // display cost

    // Shop pricing hook remains (kept for parity with older code)
    int base_cost = 3;
};

// Authoritative shop state per player
struct PlayerShopState {
    std::array<int, 5> slots{};   // -1 = empty
    int  reroll_cost = 2;
    bool free_reroll = false;
};

class CardSystem {
public:
    using EmitFn = std::function<void(const std::string&, const std::string&)>;

    // Lua-driven constructor
    explicit CardSystem(EmitFn emit);

    // Default ctor (legacy paths)
    CardSystem();

    // ---------- Legacy/UI surface consumed by ScriptedState ----------
    void init() {} // retained for older call sites

    // Starter UI: lay out a row of provided cards across UI width at y
    void spawnCardRow(const std::vector<CardData>& list, int ui_width, int y);

    // Hit-test mouse click; returns selected card if any
    std::optional<CardData> handleMouseClick(int mouse_x, int mouse_y);

    // Draw cards (stubbed; safe no-op)
    void render(int ui_width, int ui_height);

    // ---------- Current Lua-first API ----------
    void onRoundStart(int round_idx);
    void onLevelChanged(int player_id, int new_level);

    PlayerShopState rollShop(int player_id);

    bool canBuy(int player_id, int slot_index, std::string& out_reason) const;
    bool buy(int player_id, int slot_index);

    // Deterministic RNG exposed to Lua
    int   randi(int n);
    float randf();

    // Pricing hook
    int priceFor(int card_id, int base_cost, int player_id) const;

    // State accessors
    int  getGold(int player_id) const;
    void addGold(int player_id, int delta);
    int  getLevel(int player_id) const;
    void setLevel(int player_id, int lvl);

    // ---------- Back-compat shims kept from earlier step ----------
    std::vector<CardData> spawnCardRow();             // assume player 0
    std::vector<CardData> spawnCardRow(int player_id);

private:
    // Lua helpers
    PlayerShopState luaRollInternal(int player_id);
    bool luaCanBuyInternal(int player_id, int slot_index, std::string& reason) const;
    void loadScript();
    static std::string toJson(const PlayerShopState& s);

    // state
    sol::state lua_;
    bool ok_ = false;
    uint64_t rng_state_ = 0x9E3779B97F4A7C15ull;

    EmitFn emit_{};

    int gold_[kMaxPlayers]{};
    int level_[kMaxPlayers]{};
    PlayerShopState shops_[kMaxPlayers]{};

    // RNG core
    uint32_t rng();

    // -------- Starter UI state (layout + hit-testing only) --------
    struct UICard {
        CardData data;
        int x = 0, y = 0, w = 0, h = 0; // screen-space rect
    };
    std::vector<UICard> ui_cards_;
    int ui_row_y_ = 0;
    int ui_row_w_ = 0;

    // layout params
    int card_w_ = 220;   // tuned to match earlier starter screens
    int card_h_ = 300;
    int card_gap_ = 24;
};