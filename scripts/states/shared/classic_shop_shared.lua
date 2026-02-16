-- scripts/states/shared/classic_shop_shared.lua

local SHOP_DURATION = 30.0
local REROLL_COST = 2
local CARD_COUNT = 5
local DEFAULT_STARTERS = { "bulbasaur", "charmander", "squirtle" }

local cost_cfg = dofile("scripts/config/classic_shop_costs.lua")

local seeded = false
local showed_income_formula = false

local function pretty_name(name)
    local s = tostring(name or "")
    s = s:gsub("_", " ")
    s = s:gsub("(%a)([%w']*)", function(a, b)
        return string.upper(a) .. string.lower(b)
    end)
    return s
end

local function get_card_cost(name, level)
    local cfg = cost_cfg or {}
    local tier_map = cfg.species_tier or {}
    local tier = tier_map[name] or cfg.default_tier or 1
    local lvl = math.max(1, level or 1)

    for _, bracket in ipairs(cfg.level_brackets or {}) do
        local min_lv = math.max(1, bracket.min or 1)
        local max_lv = math.max(min_lv, bracket.max or min_lv)
        if lvl >= min_lv and lvl <= max_lv then
            local by_tier = bracket.by_tier or {}
            local cost = by_tier[tier]
            if cost ~= nil then
                return math.max(0, cost + (cfg.global_add or 0))
            end
            if bracket.default ~= nil then
                return math.max(0, bracket.default + (cfg.global_add or 0))
            end
        end
    end

    return math.max(0, (cfg.default_cost or 1) + (cfg.global_add or 0))
end

local function make_message(label, time_left)
    local seconds = math.max(0, math.floor(time_left))
    if label and label ~= "" then
        return string.format("%s - Time left: %ds", label, seconds)
    end
    return string.format("Time left: %ds", seconds)
end

local M = {}

function M.install(cfg, target)
    cfg = cfg or {}
    target = target or _ENV

    local commons = cfg.commons or {}
    local starters = cfg.starters or DEFAULT_STARTERS
    local common_weight = tonumber(cfg.common_weight) or 0.5
    local level_min = math.max(1, math.floor(tonumber(cfg.level_min) or 1))
    local level_max = math.max(level_min, math.floor(tonumber(cfg.level_max) or level_min))
    local card_count = math.max(1, math.floor(tonumber(cfg.card_count) or CARD_COUNT))
    local message_label = cfg.message_label
    local next_combat_path = cfg.next_combat_path
    local show_income_hint = cfg.show_income_hint == true

    common_weight = math.max(0.0, math.min(1.0, common_weight))

    if not next_combat_path or next_combat_path == "" then
        error("classic_shop_shared.install requires next_combat_path")
    end

    local time_left = SHOP_DURATION
    local transitioned = false
    local cards = {}

    local function pick_weighted()
        local choose_common = (#commons > 0) and (math.random() < common_weight or #starters == 0)
        local pool = choose_common and commons or starters
        if #pool == 0 then
            pool = commons
        end
        if #pool == 0 then
            return "pidgey"
        end
        return pool[math.random(#pool)]
    end

    local function make_card()
        local name = pick_weighted()
        local level = math.random(level_min, level_max)
        local cost = get_card_cost(name, level)
        return {
            name = name,
            level = level,
            cost = cost,
            label = pretty_name(name) .. " [" .. tostring(cost) .. "g]",
            type = "Shop"
        }
    end

    local function refill_cards(count)
        cards = {}
        for i = 1, count do
            cards[i] = make_card()
        end
    end

    local function persist_cards()
        if classic_shop_set_cards then
            classic_shop_set_cards(cards)
        end
    end

    target.on_enter = function()
        if not seeded then
            if os and os.time then
                math.randomseed(os.time())
            end
            seeded = true
        end

        time_left = SHOP_DURATION
        transitioned = false
        refill_cards(card_count)
        persist_cards()

        if show_income_hint and (not showed_income_formula) then
            emit_gold("+1g per 10 saved (up to +5g).")
            showed_income_formula = true
        end
    end

    target.get_message = function()
        return make_message(message_label, time_left)
    end

    target.get_shop_cards = function()
        return cards
    end

    target.on_shop_card_click = function(pokemon, level)
        if not pokemon or pokemon == "" then
            return
        end

        local selected_index = nil
        for i = 1, #cards do
            if cards[i].name == pokemon and (not level or cards[i].level == level) then
                selected_index = i
                break
            end
        end

        local card_level = level
        local card_cost = 1
        if selected_index and cards[selected_index] then
            card_level = cards[selected_index].level
            card_cost = cards[selected_index].cost or card_cost
        end
        card_level = card_level or math.random(level_min, level_max)

        if not spend_money(card_cost) then
            emit("Shop", "Not enough gold to buy " .. pretty_name(pokemon) .. " (" .. tostring(card_cost) .. "g)")
            emit_gold("Spend failed: " .. pretty_name(pokemon) .. " costs " .. tostring(card_cost) .. "g")
            return
        end

        spawn_on_bench(pokemon, card_level)
        emit("Shop", "Bought " .. pretty_name(pokemon) .. " Lv" .. tostring(card_level) ..
            " for " .. tostring(card_cost) .. "g")
        emit_gold("Spent -" .. tostring(card_cost) .. "g: " .. pretty_name(pokemon) ..
            " Lv" .. tostring(card_level) .. ".")

        if selected_index then
            cards[selected_index] = make_card()
        end
        persist_cards()
    end

    target.on_update = function(dt)
        if transitioned then
            return
        end

        time_left = time_left - (dt or 0.016)
        if time_left <= 0.0 then
            transitioned = true
            persist_cards()
            pop_state()
            push_combat_state(next_combat_path)
        end
    end

    target.on_shop_ready_click = function()
        if transitioned then
            return
        end
        time_left = 0.0
        emit("Shop", "Ready. Starting next round...")
    end

    target.on_shop_reroll_click = function()
        if transitioned then
            return
        end

        if not spend_money(REROLL_COST) then
            emit("Shop", "Not enough gold to reroll (" .. tostring(REROLL_COST) .. "g)")
            emit_gold("Spend failed: reroll costs " .. tostring(REROLL_COST) .. "g.")
            return
        end

        refill_cards(card_count)
        emit("Shop", "Shop rerolled for " .. tostring(REROLL_COST) .. "g")
        emit_gold("Spent -" .. tostring(REROLL_COST) .. "g: shop reroll.")
        persist_cards()
    end
end

return M
