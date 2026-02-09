-- scripts/states/route1_shop.lua

local SHOP_DURATION = 30.0
local time_left = SHOP_DURATION
local transitioned = false

local commons = { "pidgey", "rattata" }
local starters = { "bulbasaur", "charmander", "squirtle" }

local cards = {}
local seeded = false

local function pick_weighted()
    if math.random() < 0.5 then
        return commons[math.random(#commons)]
    end
    return starters[math.random(#starters)]
end

local function make_card()
    local name = pick_weighted()
    return { name = name, cost = 1, type = "Shop" }
end

local function fill_cards(count)
    cards = {}
    for i = 1, count do
        local c = make_card()
        if c then table.insert(cards, c) end
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
    if #cards == 0 then
        fill_cards(5)
    end
end

function get_message()
    return string.format("Shop - Time left: %ds", math.max(0, math.floor(time_left)))
end

function get_shop_cards()
    return cards
end

function on_shop_card_click(pokemon)
    if not pokemon or pokemon == "" then return end
    local level = math.random(2, 5)
    spawn_on_bench(pokemon, level)

    for i = 1, #cards do
        if cards[i].name == pokemon then
            local c = make_card()
            if c then cards[i] = c end
            break
        end
    end
end

function on_update(dt)
    if transitioned then return end
    time_left = time_left - (dt or 0.016)
    if time_left <= 0.0 then
        transitioned = true
        pop_state()
        push_combat_state("scripts/states/route1_5.lua")
    end
end
