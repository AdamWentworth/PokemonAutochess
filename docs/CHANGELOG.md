# Changelog

## Unreleased

## 2026-02-07 — Stage 3.4 EventManager singleton removal (engine events)
- Removed the legacy EventManager wrapper and routed engine events through EngineServices only.
- Application no longer configures a global event bus singleton.

## 2026-02-07 — Stage 3.3 GameConfig global cache removal (explicit ownership)
- GameConfig now loads on demand (no static cache); session owns config data.
- GameWorld/Lua bindings read board sizing from injected config rather than GameConfig::get.
- States with legacy constructors load a local config instead of using globals.

## 2026-02-07 — Stage 3.2 config loader singleton removal (GameDataDb ownership)
- GameDataDb now owns config loader instances (Pokemon/Moves/AttackAnim/Flyer) and bootstrap loads them.
- Removed config loader `getInstance()` usage in preload helpers, Lua bindings, and anim set resolution.
- Lua bindings and anim set logic now read config data through the injected GameDataDb.

## 2026-02-07 — Stage 3.1 logging migration (remove LogBus globals)
- Replaced LogBus global call sites with explicit logger injection across gameplay/runtime code.
- Lua scripting binds now log through an injected logger (no global LogBus usage).
- Config loaders accept an optional logger and no longer depend on LogBus globals.
- GameSession/GameWorld use the instance logger directly for runtime messages.


## 2026-02-07 — Stage 3 prep: singleton inventory + roadmap refresh
- Added `DELETE_REPLACE_LIST.md` to track remaining globals/singletons and their replacements.
- Updated roadmap to reflect Stage 2.3 structural-change deferral and set Stage 3.1 logging migration as next step.
- ECS tests now use `World::add` for new component creation (preferred API).

## 2026-02-06 — Stage 3 prep: thread-local legacy fallbacks + runtime CoreServices wiring

- EventManager legacy fallback bus is thread-local; Application routes legacy callers to engine-owned bus.
- LogBus legacy active logger + default are thread-local to avoid cross-test shared state collisions.
- Engine runtime now wires `engine::CoreServices` into `EngineServices` and routes core logging into the in-game feed during a session.

## 2026-02-06 — Stage 2.2 ECS canonical query API (World::for_each)
- Added header-only join/view helper (`ecs/View.h`).
- Added `World::for_each<Cs...>()` as a canonical, correctness-first query API.
- Added new headless test `ecs_for_each_join`.

## 2026-02-06 — Stage 2.1 ECS destroy cleanup + iteration helpers
- `engine_core` ECS: `World::destroy()` now removes components across all stores.
- Added iteration helpers (`World::each`, `World::each2`) to support early systems/tests without exposing raw maps everywhere.
- Added new headless test `ecs_destroy_cleans_components`.

## 2026-02-06 — Stage 2 ECS starter + tests target
- Added ECS starter in `engine_core` (`World`, `Scheduler`, component storage).
- Added `ecs_smoke` headless test and hooked tests into CMake (`PAC_Tests`).
- Moved `main.cpp` under `src/` (updated build accordingly).

## 2026-02-06 — Stage 1 engine target split + core services
- Split build into `engine_core`, `engine_platform`, `engine_render` with `Engine` umbrella target.
- Added `ILogger`/`StdoutLogger`, `IEventBus`/`EventBus`, and `CoreServices` bundle in `engine_core`.
- Added/updated docs (architecture + roadmap).
