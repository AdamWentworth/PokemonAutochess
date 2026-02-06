# Changelog

## Unreleased

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
