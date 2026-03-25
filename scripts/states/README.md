# State Scripts

State routing is now shared across modes.

- Combat entry scripts:
  - `route1.lua`
  - `route1_5.lua`
  - `route22.lua`
  - `route2.lua`
  - `viridian_forest.lua`
  - `route3.lua`
- Shop entry scripts:
  - `route1_shop.lua`
  - `route22_shop.lua`
  - `route2_shop.lua`
  - `viridian_forest_shop.lua`
  - `route3_shop.lua`

Mode-specific behavior is selected at runtime via `get_game_mode()`:

- `classic`: pokemon shop + round economy
- `adventure`: item shop

Shared implementation lives in `scripts/states/shared/`:

- `route_catalog.lua`: route data (message, enemies, balance, next shop)
  - also carries the intended arena theme progression for route backdrops
- `shop_catalog.lua`: shop data (next route + classic pool tuning)
- `combat_route_shared.lua`: combat state callbacks
- `shop_state_shared.lua`: shop state callbacks
- `classic_shop_shared.lua`: classic shop behavior
- `item_shop_shared.lua`: adventure shop behavior
- `mode_utils.lua`: mode normalization helper
- `round_economy.lua`: classic round payout/transition helper
