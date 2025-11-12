-- scripts/config/game.lua
return {
    board = { cols = 8, rows = 8, cellSize = 1.2 },
    bench = { slots = 8 },
    fonts = {
        ui = { path = "assets/fonts/GillSans.ttf", size = 48 }
    },
    -- NEW: simple global leveling model
    -- All stats scale by (1 + per_level_boost)^(level - 1)
    leveling = {
        base_level = 1,          -- default level used when not specified
        per_level_boost = 0.08   -- 8% per level to HP, Attack, Move Speed
    }
}
