# Delete/Replace List (Globals and Singletons)

Last updated: 2026-02-07

Purpose: Make Stage 3 (remove globals) actionable by listing remaining singleton/global call sites and their intended replacements.

## Current Count
- `getInstance()` call sites: 0 (from `python tools/health/count_singletons.py`)

## getInstance() Call Sites
| Count | File |
| --- | --- |
| 0 | (none) |

## Inventory

### EventManager singleton
Files:
None (removed).

Current status:
Legacy wrapper deleted; engine uses injected EventBus via EngineServices/CoreServices.

Replacement:
Use `engine::CoreServices.events` or `EngineServices.events`.

### LogBus active logger fallback
Files:
`src/game/logging/LogBus.h`
`src/game/logging/LogBus.cpp`

Call sites (non-exhaustive):
None (as of 2026-02-07).
`src/game/config/*.cpp`
`src/game/state/*.cpp`
`src/game/scripting/*.cpp`

Replacement:
Pass `LogBus::Logger&` via `GameServices` (or `engine::CoreServices.log`) and call instance methods directly. Remove `LogBus::setActive` and compatibility free functions once call sites are gone.

Status:
As of 2026-02-07, no `LogBus::` call sites remain in `src/game` runtime code. Compatibility functions remain but are unused.

### Config loader singletons
Files:
`src/game/config/PokemonConfigLoader.*`
`src/game/config/MovesConfigLoader.*`
`src/game/config/AttackAnimConfigLoader.*`
`src/game/config/FlyerConfigLoader.*`

Call sites:
None (as of 2026-02-07).

Replacement:
Own loader instances in `GameBootstrap` or `GameSession`, store them in `GameDataDb`, pass `GameDataDb` into preload helpers and Lua bindings, and remove `getInstance()` from loaders. Long-term replacement is `IAssetStore` (Stage 6).

Status:
Completed (loader instances owned by `GameDataDb`; no config loader `getInstance()` call sites remain).

### GameConfig global cache
Files:
`src/game/GameConfig.h`
`src/game/GameConfig.cpp`

Call sites (non-exhaustive):
`src/game/GameSession.cpp`
`src/game/GameWorld.cpp`

Replacement:
Load config once in `GameBootstrap` and store in `GameServices` or `GameContext`, then remove the static cache when all call sites are migrated.

Status:
Completed (GameConfig now loads on demand; no `GameConfig::get()` call sites remain).

## Next Migration Slice
- Stage 4 update graph: unify phases/order and support headless mode without renderer/platform.
