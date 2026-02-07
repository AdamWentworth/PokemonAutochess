# Roadmap (Living)

This document is the **living** tracker: what is done, what is next, and what “exit criteria” still block forward progress.
The target end-state and stable constraints live in `TARGET_ARCHITECTURE.md`.

Last updated: 2026-02-07

---

## Current Status Snapshot

- ✅ Stage 0: architecture rules captured (`ARCHITECTURE.md`, `TARGET_ARCHITECTURE.md`).
- ✅ Stage 1: build split into `engine_core`, `engine_platform`, `engine_render` (with `Engine` umbrella). Game + tests run.
- ✅ Stage 2: ECS foundation in `engine_core` is usable and covered by headless tests:
  - `ecs_smoke`
  - `ecs_destroy_cleans_components`
  - `ecs_for_each_join`
  - `ecs_structural_change_deferral`
- ✅ Stage 3.1: logging migration (no LogBus call sites in `src/game` runtime).
- ✅ Stage 3.2: config loader singletons removed (GameDataDb ownership).

### Current ECS capabilities
- Entities: id + generation
- Type-erased component stores keyed by `type_index`
- Correctness-first storage (`unordered_map`)
- Scheduler executes ordered systems
- Canonical query API: `World::for_each<Cs...>(fn)` joins by id (driver = first component store)
- Destroy cleanup: `World::destroy()` removes components across all stores
- Structural-change deferral policy for add/remove/destroy during iteration (deferred until outermost iteration completes)
- Preferred API: `World::add/remove/has/get` (use in new gameplay code)

### Current ECS notes (non-blocking)
- Preferred API adoption: reduce new usage of `components<T>()` outside ECS internals/tests.
- Query performance/driver selection is naive (acceptable for now).
- The public API still allows bypassing policy via direct store access (`components<T>()`), which is fine during migration but not end-state.

---

## Next Step (Do this next)

### Stage 3.3 — GameConfig global cache removal (explicit config ownership)
Goal: remove `GameConfig::get()` usage and thread `GameConfigData` explicitly.

Preferred implementation order:
1) Load config once in GameBootstrap or GameSession.
2) Store `GameConfigData` in `GameServices` (or GameContext) and pass by reference.
3) Delete the `GameConfig::get()` global cache once call sites are migrated.

Exit criteria:
- No `GameConfig::get()` call sites in `src/game` runtime.

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
Exit criteria: met (structural-change policy + preferred API adoption in gameplay).
### Stage 3 — Remove globals (events + logging first) 🟡 (in progress)
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

---

## Immediate Technical Outputs (to keep momentum)

1) **Delete/replace list** for globals/singletons (prep for Stage 3) ✅ (`DELETE_REPLACE_LIST.md`)
2) **Core service interfaces** to add next:
   - `ITimeSource`, `IRandom`, `IAssetStore`
3) **ECS policy enforcement** (Stage 2.3) ✅
4) **Draft phase model** for the update graph (Stage 4)
