# Delete/Replace List (Globals and Singletons)

Last updated: 2026-02-07

Purpose: Make Stage 3 (remove globals) actionable by listing remaining singleton/global call sites and their intended replacements.

## Current Count
- `getInstance()` call sites: 29 (from `python tools/health/count_singletons.py`)

## getInstance() Call Sites
| Count | File |
| --- | --- |
| 8 | `src/game/GameBootstrap.cpp` |
| 4 | `src/game/scripting/LuaBindings_World.cpp` |
| 4 | `src/engine/events/EventManager.h` |
| 2 | `src/game/config/AnimSetLoader.cpp` |
| 2 | `src/game/GamePreload.cpp` |
| 1 | `src/game/scripting/LuaBindings_UnitMove.cpp` |
| 1 | `src/game/config/PokemonConfigLoader.h` |
| 1 | `src/game/config/PokemonConfigLoader.cpp` |
| 1 | `src/game/config/MovesConfigLoader.h` |
| 1 | `src/game/config/MovesConfigLoader.cpp` |
| 1 | `src/game/config/FlyerConfigLoader.h` |
| 1 | `src/game/config/FlyerConfigLoader.cpp` |
| 1 | `src/game/config/AttackAnimConfigLoader.h` |
| 1 | `src/game/config/AttackAnimConfigLoader.cpp` |

## Inventory

### EventManager singleton
Files:
`src/engine/events/EventManager.h`

Current status:
Legacy wrapper only; no active call sites in `src/` as of 2026-02-07.

Replacement:
Use `engine::CoreServices.events` or `EngineServices.events` and delete `EventManager` once all call sites are removed.

### LogBus active logger fallback
Files:
`src/game/logging/LogBus.h`
`src/game/logging/LogBus.cpp`

Call sites (non-exhaustive):
`src/game/GameSession.cpp`
`src/game/GameWorld.cpp`
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
`src/game/GameBootstrap.cpp`
`src/game/GamePreload.cpp`
`src/game/config/AnimSetLoader.cpp`
`src/game/scripting/LuaBindings_World.cpp`
`src/game/scripting/LuaBindings_UnitMove.cpp`

Replacement:
Own loader instances in `GameBootstrap` or `GameSession`, store them in `GameDataDb`, pass `GameDataDb` into preload helpers and Lua bindings, and remove `getInstance()` from loaders. Long-term replacement is `IAssetStore` (Stage 6).

### GameConfig global cache
Files:
`src/game/GameConfig.h`
`src/game/GameConfig.cpp`

Call sites (non-exhaustive):
`src/game/GameSession.cpp`
`src/game/GameWorld.cpp`

Replacement:
Load config once in `GameBootstrap` and store in `GameServices` or `GameContext`, then remove the static cache when all call sites are migrated.

## Next Migration Slice
- Config loader singletons: instantiate loaders in bootstrap/session and remove `getInstance()` call sites.
