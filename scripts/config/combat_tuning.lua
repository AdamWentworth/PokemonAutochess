-- scripts/config/combat_tuning.lua
-- Optional overrides for scripts/systems/combat.lua
return {
  -- Damage model:
  -- final_damage = round( (power * DAMAGE_POWER_MULT) + (attack * DAMAGE_ATK_SCALE) )
  DAMAGE_POWER_MULT = 1.0,
  DAMAGE_ATK_SCALE = 0.50,
  DAMAGE_MIN = 1
}
