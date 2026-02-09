-- scripts/config/game.lua
return {
    board = { cols = 8, rows = 8, cellSize = 1.2 },
    bench = { slots = 8 },
    fonts = {
        ui = { path = "assets/fonts/GillSans.ttf", size = 48 }
    },
    -- NEW: simple global leveling model
    -- Stats scale by (1 + per_level_*_boost)^(level - 1)
    leveling = {
        base_level = 1,          -- default level used when not specified
        per_level_boost = 0.08,  -- fallback when per-stat boost is not set
        per_level_hp_boost = 0.22,
        per_level_attack_boost = 0.24,
        per_level_speed_boost = 0.07
    },
    xp = {
        per_faint = 10,
        level_base = 10,
        level_growth = 1.35,
        max_level = 0, -- 0 = no cap
        yield_mult = 1.0
    },
    faint = {
        fade_sec = 0.35,
        block_tile = false
    },
    economy = {
        starting_cash = 3000
    },
    items = {
        potion_heal_pct = 0.30,
        potion_heal_flat = 0
    },
    capture = {
        attempt_sec = 0.75,
        min_chance = 0.05,
        max_chance = 0.95,
        hp_factor_min = 0.40,
        hp_factor_max = 1.00,
        faint_bonus = 1.25,
        ball_scale = 5,
        ball_scale_start = 15.0
    }
}
