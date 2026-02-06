# Roadmap (Living)

This document is the **living** tracker: what is done, what is next, and what “exit criteria” still block forward progress.
The target end-state and stable constraints live in `TARGET_ARCHITECTURE.md`.

Last updated: 2026-02-06

---

## Current Status Snapshot

- ✅ Stage 0: architecture rules captured (`ARCHITECTURE.md`, `TARGET_ARCHITECTURE.md`).
- ✅ Stage 1: build split into `engine_core`, `engine_platform`, `engine_render` (with `Engine` umbrella). Game + tests run.
- ✅ Stage 2 (ECS foundation): ECS in `engine_core` is usable and covered by headless tests:
  - `ecs_smoke`
  - `ecs_destroy_cleans_components`
  - `ecs_for_each_join`
  - `ecs_structural_change_deferral`

### Current ECS capabilities

- Entities: id + generation
- Type-erased component stores keyed by `type_index`
- Correctness-first storage (`unordered_map`)
- Scheduler executes ordered systems
- Canonical query API: `World::for_each<Cs...>(fn)` joins by id (driver = first component store)
- Destroy cleanup: `World::destroy()` removes components across all stores
- Structural-change policy (Stage 2.3):
  - `World::add/remove/has/get`
  - Structural changes are deferred while iterating (`each/each2/for_each`)

### Current ECS gaps (non-blocking for Stage 2)

- Query performance/driver selection is naive (acceptable for now).
- The public API still allows bypassing policy via direct store access (`components<T>()`), which is fine during migration but not end-state.

---

## Next Step (Do this next)

### Stage 3 — Remove globals (events + logging first)

Goal: eliminate shared mutable runtime state so tests and headless sims do not collide.

Work items:
- Replace singleton event manager usage with injected `IEventBus` (via composition root / services).
- Replace global/active logger usage with injected `ILogger`.
- Delete fallback/legacy service access patterns.

Exit criteria:
- No `getInstance()`, no `setActive()`, no default/global bus in core gameplay.
- Tests can run in parallel without shared state collisions.

---

## Stage Checklist (High level)

### Stage 0 — Lock target rules ✅
Exit criteria:
- dependency rule enforceable in build graph
- “where does this file go?” has a consistent answer

### Stage 1 — Module boundaries ✅
Exit criteria:
- `engine_core` builds without SDL/GL
- game links engine libs and runs
- tests still pass

### Stage 2 — ECS foundation ✅
Exit criteria:
- structural-change policy enforced (Stage 2.3)
- a clear “preferred API” for gameplay so raw map access stops spreading

### Stage 3 — Remove globals (events + logging first) ⏭️ next
Work items:
- inventory global/singleton usage in game + engine_core
- replace with explicit service wiring (composition root)
- remove fallback global access paths

### Stage 4 — One update graph
Work items:
- unify phases and ordering in one place
- support headless mode with renderer/platform omitted

### Stage 5 — Scripting firewall
Work items:
- ScriptAPI + command buffer + stable event schema
- bind only ScriptAPI to Lua

### Stage 6 — Asset pipeline
Work items:
- cooker + validation
- PackedAssetStore (+ optional DevAssetStore)

### Stage 7 — Determinism hardening
Work items:
- ITimeSource + IRandom injection
- headless battle/content invariants tests
