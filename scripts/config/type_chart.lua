-- scripts/config/type_chart.lua
-- Type effectiveness chart + damage multipliers.
-- Missing pairs default to neutral (1.0).

return {
  multipliers = {
    charged = {
      best = 4.0,      -- 4x (double super effective)
      great = 2.0,     -- 2x (super effective)
      neutral = 1.0,
      bad = 0.5,       -- 1/2x (not very effective)
      terrible = 0.25, -- 1/4x (double resist)
      worst = 0.0      -- no effect
    },
    fast = {
      best = 2.56,     -- 2.56x (double super effective)
      great = 1.6,     -- 1.6x (super effective)
      neutral = 1.0,
      bad = 0.625,     -- 0.625x (not very effective)
      terrible = 0.391,-- 0.391x (double resist)
      worst = 0.244    -- "no effect" for fast moves
    }
  },

  chart = {
    normal = {
      rock = 0.5,
      ghost = 0.0,
      steel = 0.5
    },
    fire = {
      grass = 2.0,
      ice = 2.0,
      bug = 2.0,
      steel = 2.0,
      fire = 0.5,
      water = 0.5,
      rock = 0.5,
      dragon = 0.5
    },
    water = {
      fire = 2.0,
      ground = 2.0,
      rock = 2.0,
      water = 0.5,
      grass = 0.5,
      dragon = 0.5
    },
    electric = {
      water = 2.0,
      flying = 2.0,
      electric = 0.5,
      grass = 0.5,
      dragon = 0.5,
      ground = 0.0
    },
    grass = {
      water = 2.0,
      ground = 2.0,
      rock = 2.0,
      fire = 0.5,
      grass = 0.5,
      poison = 0.5,
      flying = 0.5,
      bug = 0.5,
      dragon = 0.5,
      steel = 0.5
    },
    ice = {
      grass = 2.0,
      ground = 2.0,
      flying = 2.0,
      dragon = 2.0,
      fire = 0.5,
      water = 0.5,
      ice = 0.5,
      steel = 0.5
    },
    fighting = {
      normal = 2.0,
      ice = 2.0,
      rock = 2.0,
      dark = 2.0,
      steel = 2.0,
      poison = 0.5,
      flying = 0.5,
      psychic = 0.5,
      bug = 0.5,
      fairy = 0.5,
      ghost = 0.0
    },
    poison = {
      grass = 2.0,
      fairy = 2.0,
      poison = 0.5,
      ground = 0.5,
      rock = 0.5,
      ghost = 0.5,
      steel = 0.0
    },
    ground = {
      fire = 2.0,
      electric = 2.0,
      poison = 2.0,
      rock = 2.0,
      steel = 2.0,
      grass = 0.5,
      bug = 0.5,
      flying = 0.0
    },
    flying = {
      grass = 2.0,
      fighting = 2.0,
      bug = 2.0,
      electric = 0.5,
      rock = 0.5,
      steel = 0.5
    },
    psychic = {
      fighting = 2.0,
      poison = 2.0,
      psychic = 0.5,
      steel = 0.5,
      dark = 0.0
    },
    bug = {
      grass = 2.0,
      psychic = 2.0,
      dark = 2.0,
      fire = 0.5,
      fighting = 0.5,
      poison = 0.5,
      flying = 0.5,
      ghost = 0.5,
      steel = 0.5,
      fairy = 0.5
    },
    rock = {
      fire = 2.0,
      ice = 2.0,
      flying = 2.0,
      bug = 2.0,
      fighting = 0.5,
      ground = 0.5,
      steel = 0.5
    },
    ghost = {
      psychic = 2.0,
      ghost = 2.0,
      dark = 0.5,
      normal = 0.0
    },
    dragon = {
      dragon = 2.0,
      steel = 0.5,
      fairy = 0.0
    },
    dark = {
      psychic = 2.0,
      ghost = 2.0,
      fighting = 0.5,
      dark = 0.5,
      fairy = 0.5
    },
    steel = {
      ice = 2.0,
      rock = 2.0,
      fairy = 2.0,
      fire = 0.5,
      water = 0.5,
      electric = 0.5,
      steel = 0.5
    },
    fairy = {
      fighting = 2.0,
      dragon = 2.0,
      dark = 2.0,
      fire = 0.5,
      poison = 0.5,
      steel = 0.5
    }
  }
}
