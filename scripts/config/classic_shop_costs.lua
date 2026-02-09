-- scripts/config/classic_shop_costs.lua
--
-- Pricing model:
-- 1) Species maps to a "tier".
-- 2) Level bracket + tier resolves final gold cost.
--
-- Extend this as the roster grows (new species tiers, more level brackets).
return {
    default_tier = 1,
    default_cost = 1,

    species_tier = {
        pidgey = 1,
        rattata = 1,

        bulbasaur = 2,
        charmander = 2,
        squirtle = 2
    },

    level_brackets = {
        -- Lv 1-3: commons 1g, starters 2g
        { min = 1, max = 3, by_tier = { [1] = 1, [2] = 2 } },
        -- Lv 4-5: commons 2g, starters 3g
        { min = 4, max = 5, by_tier = { [1] = 2, [2] = 3 } }
    }
}

