-- scripts/states/shared/shop_catalog.lua

local SHOPS = {
    route1_shop = {
        message_label = nil,
        next_combat_route = "route1_5",
        classic = {
            commons = { "pidgey", "rattata" },
            common_weight = 0.50,
            level_min = 2,
            level_max = 5,
            show_income_hint = true
        }
    },
    route22_shop = {
        message_label = "Route 22 Shop",
        next_combat_route = "route22",
        classic = {
            commons = { "nidoran-f", "nidoran-m", "spearow", "mankey", "pidgey", "rattata" },
            common_weight = 0.60,
            level_min = 3,
            level_max = 6
        }
    },
    route2_shop = {
        message_label = "Route 2 Shop",
        next_combat_route = "route2",
        classic = {
            commons = { "caterpie", "weedle", "pikachu", "nidoran-f", "nidoran-m", "spearow", "mankey", "pidgey", "rattata" },
            common_weight = 0.65,
            level_min = 5,
            level_max = 8
        }
    },
    viridian_forest_shop = {
        message_label = "Viridian Forest Shop",
        next_combat_route = "viridian_forest",
        classic = {
            commons = { "caterpie", "weedle", "metapod", "kakuna", "pikachu", "pidgey", "rattata", "nidoran-f", "nidoran-m" },
            common_weight = 0.70,
            level_min = 6,
            level_max = 9
        }
    },
    route3_shop = {
        message_label = "Route 3 Shop",
        next_combat_route = "route3",
        classic = {
            commons = { "metapod", "kakuna", "pikachu", "caterpie", "weedle", "pidgey", "spearow", "mankey" },
            common_weight = 0.70,
            level_min = 8,
            level_max = 11
        }
    }
}

local M = {}

function M.get(shop_id)
    return SHOPS[shop_id]
end

return M
