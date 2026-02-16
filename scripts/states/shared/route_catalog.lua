-- scripts/states/shared/route_catalog.lua

local ROUTES = {
    route1 = {
        message = "Route 1 - Wild Pokemon Appeared!",
        clear_message = "Route 1 cleared!",
        next_shop = "route1_shop",
        enemies = {
            { name = "pidgey",  gridCol = 2, gridRow = 1, level = 2 },
            { name = "rattata", gridCol = 5, gridRow = 1, level = 3 },
        },
        combat_balance = {
            playerDamageMult = 1.40,
            enemyDamageMult = 0.80,
            playerDamageTakenMult = 0.85,
            enemyDamageTakenMult = 1.00
        }
    },
    route1_5 = {
        message = "Route 1.5 - Another wave!",
        clear_message = "Route 1.5 cleared!",
        next_shop = "route22_shop",
        enemies = {
            { name = "pidgey",  gridCol = 1, gridRow = 1, level = 3 },
            { name = "rattata", gridCol = 4, gridRow = 1, level = 4 },
            { name = "pidgey",  gridCol = 6, gridRow = 1, level = 3 },
        },
        combat_balance = {
            playerDamageMult = 1.20,
            enemyDamageMult = 0.90,
            playerDamageTakenMult = 0.90,
            enemyDamageTakenMult = 1.00
        }
    },
    route22 = {
        message = "Route 22 - New challengers!",
        clear_message = "Route 22 cleared!",
        next_shop = "route2_shop",
        enemies = {
            { name = "nidoran-f", gridCol = 1, gridRow = 1, level = 4 },
            { name = "spearow",   gridCol = 3, gridRow = 1, level = 5 },
            { name = "nidoran-m", gridCol = 5, gridRow = 1, level = 4 },
            { name = "mankey",    gridCol = 6, gridRow = 1, level = 4 },
        },
        combat_balance = {
            playerDamageMult = 1.10,
            enemyDamageMult = 1.00,
            playerDamageTakenMult = 0.95,
            enemyDamageTakenMult = 1.00
        }
    },
    route2 = {
        message = "Route 2 - Edge of Viridian Forest!",
        clear_message = "Route 2 cleared!",
        next_shop = "viridian_forest_shop",
        enemies = {
            { name = "caterpie", gridCol = 1, gridRow = 1, level = 5 },
            { name = "weedle",   gridCol = 2, gridRow = 1, level = 5 },
            { name = "pidgey",   gridCol = 4, gridRow = 1, level = 6 },
            { name = "caterpie", gridCol = 5, gridRow = 1, level = 5 },
            { name = "weedle",   gridCol = 6, gridRow = 1, level = 5 }
        },
        combat_balance = {
            playerDamageMult = 1.05,
            enemyDamageMult = 1.00,
            playerDamageTakenMult = 0.97,
            enemyDamageTakenMult = 1.00
        }
    },
    viridian_forest = {
        message = "Viridian Forest - The bugs are swarming!",
        clear_message = "Viridian Forest cleared!",
        next_shop = "route3_shop",
        enemies = {
            { name = "metapod",  gridCol = 1, gridRow = 1, level = 7 },
            { name = "kakuna",   gridCol = 2, gridRow = 1, level = 7 },
            { name = "pikachu",  gridCol = 4, gridRow = 1, level = 6 },
            { name = "caterpie", gridCol = 5, gridRow = 1, level = 6 },
            { name = "weedle",   gridCol = 6, gridRow = 1, level = 6 }
        },
        combat_balance = {
            playerDamageMult = 1.00,
            enemyDamageMult = 1.00,
            playerDamageTakenMult = 1.00,
            enemyDamageTakenMult = 1.00
        }
    },
    route3 = {
        message = "Route 3 - Stronger wilds ahead!",
        clear_message = "Route 3 cleared!",
        next_shop = "route3_shop",
        enemies = {
            { name = "metapod", gridCol = 1, gridRow = 1, level = 8 },
            { name = "kakuna",  gridCol = 2, gridRow = 1, level = 8 },
            { name = "pidgey",  gridCol = 4, gridRow = 1, level = 9 },
            { name = "kakuna",  gridCol = 5, gridRow = 1, level = 8 },
            { name = "metapod", gridCol = 6, gridRow = 1, level = 8 }
        },
        combat_balance = {
            playerDamageMult = 0.96,
            enemyDamageMult = 1.04,
            playerDamageTakenMult = 1.04,
            enemyDamageTakenMult = 0.98
        }
    }
}

local function copy_table(src)
    if type(src) ~= "table" then
        return src
    end

    local out = {}
    for k, v in pairs(src) do
        out[k] = copy_table(v)
    end
    return out
end

local M = {}

function M.get(route_id)
    return ROUTES[route_id]
end

function M.copy(route_id)
    local route = ROUTES[route_id]
    if not route then
        return nil
    end
    return copy_table(route)
end

return M
