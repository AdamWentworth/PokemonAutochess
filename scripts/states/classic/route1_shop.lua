-- scripts/states/classic/route1_shop.lua

local SHOP_DURATION = 30.0
local time_left = SHOP_DURATION
local transitioned = false

local commons = { "pidgey", "rattata" }
local starters = { "bulbasaur", "charmander", "squirtle" }
local cost_cfg = dofile("scripts/config/classic_shop_costs.lua")

local cards = {}
local seeded = false

local function pretty_name(name)
    local s = tostring(name or "")
    s = s:gsub("_", " ")
    s = s:gsub("(%a)([%w']*)", function(a, b)
        return string.upper(a) .. string.lower(b)
    end)
    return s
end

local function pick_weighted()
    if math.random() < 0.5 then
        return commons[math.random(#commons)]
    end
    return starters[math.random(#starters)]
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
                return math.max(0, cost)
            end
            if bracket.default ~= nil then
                return math.max(0, bracket.default)
            end
        end
    end
    return math.max(0, cfg.default_cost or 1)
end

local function make_card()
    local name = pick_weighted()
    local level = math.random(2, 5)
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

function on_enter()
    if not seeded then
        if os and os.time then
            math.randomseed(os.time())
        end
        seeded = true
    end
    time_left = SHOP_DURATION
    transitioned = false
    refill_cards(5)
end

function get_message()
    return string.format("Time left: %ds", math.max(0, math.floor(time_left)))
end

function get_shop_cards()
    return cards
end

function on_shop_card_click(pokemon, level)
    if not pokemon or pokemon == "" then return end

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
    card_level = card_level or math.random(2, 5)

    if not spend_money(card_cost) then
        emit("Shop", "Not enough gold to buy " .. pretty_name(pokemon) .. " (" .. tostring(card_cost) .. "g)")
        return
    end

    spawn_on_bench(pokemon, card_level)
    emit("Shop", "Bought " .. pretty_name(pokemon) .. " Lv" .. tostring(card_level) ..
        " for " .. tostring(card_cost) .. "g")

    if selected_index then
        cards[selected_index] = make_card()
    end
end

function on_update(dt)
    if transitioned then return end
    time_left = time_left - (dt or 0.016)
    if time_left <= 0.0 then
        transitioned = true
        pop_state()
        push_combat_state("scripts/states/classic/route1_5.lua")
    end
end

function on_shop_ready_click()
    if transitioned then return end
    time_left = 0.0
    emit("Shop", "Ready. Starting next round...")
end
