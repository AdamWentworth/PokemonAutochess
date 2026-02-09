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
    }
}
