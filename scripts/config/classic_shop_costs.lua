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
    global_add = 1,

    species_tier = {
        pidgey = 1,
        rattata = 1,
        caterpie = 1,
        weedle = 1,

        bulbasaur = 2,
        charmander = 2,
        squirtle = 2,
        ["nidoran-f"] = 2,
        ["nidoran-m"] = 2,
        spearow = 2,
        mankey = 2,
        pikachu = 2,
        metapod = 2,
        kakuna = 2,

        ivysaur = 3,
        charmeleon = 3,
        wartortle = 3,
        pidgeotto = 3,
        butterfree = 3,
        beedrill = 3
    },

    level_brackets = {
        -- Lv 1-3: commons 1g, mid-tier 2g, rares 3g
        { min = 1, max = 3, by_tier = { [1] = 1, [2] = 2, [3] = 3 } },
        -- Lv 4-5
        { min = 4, max = 5, by_tier = { [1] = 2, [2] = 3, [3] = 4 } },
        -- Lv 6-8
        { min = 6, max = 8, by_tier = { [1] = 3, [2] = 4, [3] = 5 } },
        -- Lv 9-12
        { min = 9, max = 12, by_tier = { [1] = 4, [2] = 5, [3] = 6 } }
    }
}
