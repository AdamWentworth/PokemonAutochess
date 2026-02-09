-- scripts/states/route1_shop.lua

local SHOP_DURATION = 30.0
local time_left = SHOP_DURATION
local transitioned = false

local commons = { "pidgey", "rattata" }
local starters = { "bulbasaur", "charmander", "squirtle" }

local item_catalog = {
    { id = "pokeball", label = "Pokeball", cost = 200, row = 1, col = 4 },
    { id = "potion", label = "Potion", cost = 300, row = 2, col = 4 },
    { id = "burn_heal", label = "Burn Heal", cost = 300, row = 2, col = 6 },
    { id = "antidote", label = "Antidote", cost = 100, row = 2, col = 5 },
    { id = "paralyze_heal", label = "Paralyze Heal", cost = 200, row = 2, col = 9 }
}

-- Place the provided item atlas here (same grid as the reference image).
local item_image = "assets/images/items_atlas.png"
local ITEM_ATLAS_COLS = 13
local ITEM_ATLAS_ROWS = 14

local ball_types = {
    { id = "pokeball", mult = 1.0 }
}

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

local function atlas_uv(row, col)
    local c = math.max(1, col or 1)
    local r = math.max(1, row or 1)
    local u0 = (c - 1) / ITEM_ATLAS_COLS
    local u1 = c / ITEM_ATLAS_COLS
    -- v=0 is top of texture (no vertical flip), so count rows from top.
    local v0 = (r - 1) / ITEM_ATLAS_ROWS
    local v1 = r / ITEM_ATLAS_ROWS
    return { u0, v0, u1, v1 }
end

local function make_card()
    local name = pick_weighted()
    local level = math.random(2, 5)
    return { name = name, cost = 0, level = level, label = pretty_name(name), type = "Shop" }
end

local function fill_cards(count)
    cards = {}
    for i = 1, count do
        local c = make_card()
        if c then table.insert(cards, c) end
    end
end

local function get_best_ball()
    for _, b in ipairs(ball_types) do
        if get_item_count(b.id) > 0 then
            return b
        end
    end
    return nil
end

local function catch_chance(base_rate, level, ball_mult)
    local lvl = math.max(1, level or 1)
    local lvl_factor = 1.0 / (1.0 + (lvl - 1) * 0.15)
    local chance = (base_rate or 0.0) * (ball_mult or 1.0) * lvl_factor
    if chance < 0.05 then chance = 0.05 end
    if chance > 0.95 then chance = 0.95 end
    return chance
end

local function emit_shakes(count)
    if count >= 1 then emit_catch("The ball shook once!") end
    if count >= 2 then emit_catch("The ball shook twice!") end
    if count >= 3 then emit_catch("The ball shook three times!") end
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
    local money = get_money()
    local balls = get_item_count("pokeball")
    return string.format("Shop - $%d | Pokeball x%d | Time left: %ds",
        money,
        balls,
        math.max(0, math.floor(time_left)))
end

function get_shop_cards()
    return cards
end

function get_shop_items()
    local items = {}
    for _, it in ipairs(item_catalog) do
        table.insert(items, {
            name = it.id,
            cost = it.cost,
            label = it.label .. " $" .. tostring(it.cost),
            image = item_image,
            uv = atlas_uv(it.row, it.col),
            type = "Item"
        })
    end
    return items
end

function on_shop_item_click(item_id)
    if not item_id or item_id == "" then return end
    for _, it in ipairs(item_catalog) do
        if it.id == item_id then
            if spend_money(it.cost) then
                add_item(item_id, 1)
                emit("Shop", "Bought " .. it.label)
            else
                emit("Shop", "Not enough money for " .. it.label)
            end
            return
        end
    end
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
    if selected_index and cards[selected_index] then
        card_level = cards[selected_index].level
    end
    card_level = card_level or math.random(2, 5)

    local ball = get_best_ball()
    if not ball then
        emit("Shop", "You need a Pokeball to catch this.")
        return
    end
    if not consume_item(ball.id, 1) then
        emit("Shop", "Out of Pokeballs.")
        return
    end

    local base_rate = get_pokemon_catch_rate(pokemon)
    local chance = catch_chance(base_rate, card_level, ball.mult)
    emit_catch(string.format("Threw %s at %s (Lv%d)", ball.id, pretty_name(pokemon), card_level))

    local shake_p = math.pow(chance, 1.0 / 3.0)
    local shakes = 0
    for i = 1, 3 do
        if math.random() <= shake_p then
            shakes = shakes + 1
        else
            break
        end
    end
    emit_shakes(shakes)

    if shakes >= 3 then
        spawn_on_bench(pokemon, card_level)
        emit_catch("Gotcha! " .. pretty_name(pokemon) .. " was caught!")
        if selected_index then
            local c = make_card()
            if c then cards[selected_index] = c end
        end
    else
        emit_catch(pretty_name(pokemon) .. " broke free!")
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
