-- scripts/config/combat_tuning.lua
-- Optional overrides for scripts/systems/combat.lua
return {
  -- Damage model:
  -- final_damage = round( (power * DAMAGE_POWER_MULT) + (attack * DAMAGE_ATK_SCALE) )
  DAMAGE_POWER_MULT = 1.0,
  DAMAGE_ATK_SCALE = 0.50,
  DAMAGE_MIN = 1,

  -- Base attack cadence (higher = slower)
  FAST_CD_MULT = 2.25,
  CHARGED_CD_MULT = 2.25,
  MIN_FAST_REQUEST_SEC = 1.00,
  MIN_CHARGED_REQUEST_SEC = 1.00,
  -- Keep speed-stat ordering, but slow everyone down globally.
  ATTACK_SPEED_SCALE = 0.65,

  -- Energy gain tuning:
  -- gain = round(base_gain * ENERGY_GAIN_MULT)
  ENERGY_GAIN_MULT = 0.75,
  -- energy awarded to the target when hit by a fast move
  ENERGY_GAIN_ON_HIT = 6,
  ENERGY_GAIN_ON_HIT_MULT = 1.0
}
