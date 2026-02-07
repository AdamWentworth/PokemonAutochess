# Roadmap (Living)

This document is the **living** tracker: what is done, what is next, and what ÃƒÂ¢Ã¢â€šÂ¬Ã…â€œexit criteriaÃƒÂ¢Ã¢â€šÂ¬Ã‚Â still block forward progress.
The target end-state and stable constraints live in `TARGET_ARCHITECTURE.md`.

Last updated: 2026-02-07

---

## Current Status Snapshot

- ÃƒÂ¢Ã…â€œÃ¢â‚¬Â¦ Stage 0: architecture rules captured (`ARCHITECTURE.md`, `TARGET_ARCHITECTURE.md`).
- ÃƒÂ¢Ã…â€œÃ¢â‚¬Â¦ Stage 1: build split into `engine_core`, `engine_platform`, `engine_render` (with `Engine` umbrella). Game + tests run.
- ÃƒÂ¢Ã…â€œÃ¢â‚¬Â¦ Stage 2: ECS foundation in `engine_core` is usable and covered by headless tests:
  - `ecs_smoke`
  - `ecs_destroy_cleans_components`
  - `ecs_for_each_join`
  - `ecs_structural_change_deferral`
- ÃƒÂ¢Ã…â€œÃ¢â‚¬Â¦ Stage 3.1: logging migration (no LogBus call sites in `src/game` runtime).
- ÃƒÂ¢Ã…â€œÃ¢â‚¬Â¦ Stage 3.2: config loader singletons removed (GameDataDb ownership).
- ÃƒÂ¢Ã…â€œÃ¢â‚¬Â¦ Stage 3.3: GameConfig global cache removed (explicit config ownership).
- ÃƒÂ¢Ã…â€œÃ¢â‚¬Â¦ Stage 3.4: EventManager singleton removed (engine events).
- ÃƒÂ¢Ã…â€œÃ¢â‚¬Â¦ Stage 4: one update graph (explicit ordering + headless-safe).
- ÃƒÂ¢Ã…â€œÃ¢â‚¬Â¦ Stage 5.1: ScriptAPI surface + command queue (Lua binds only to ScriptAPI).
- âœ… Stage 5.2: Script event schema + gameplay event emitters.
- ✅ Stage 6.1: Asset store abstraction + dev store scaffold.

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

### Stage 6.2 — Asset pipeline (cooker + validation)
Goal: validate and package content into a distributable bundle.

Preferred implementation order:
1) Implement schema validation for JSON configs.
2) Implement cooker to pack validated content into a bundle.
3) Implement PackedAssetStore reader for runtime.

Exit criteria:
- Cooker outputs a bundle from current JSON assets.
- Runtime can boot using the bundle via PackedAssetStore.

---

## Stage Checklist (High level)

### Stage 0 ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â Lock target rules ÃƒÂ¢Ã…â€œÃ¢â‚¬Â¦
Exit criteria:
- dependency rule enforceable in build graph
- ÃƒÂ¢Ã¢â€šÂ¬Ã…â€œwhere does this file go?ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â has a consistent answer

### Stage 1 ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â Module boundaries ÃƒÂ¢Ã…â€œÃ¢â‚¬Â¦
Exit criteria:
- `engine_core` builds without SDL/GL
- game links engine libs and runs
- tests still pass

### Stage 2 ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â ECS foundation ÃƒÂ¢Ã…â€œÃ¢â‚¬Â¦
Exit criteria: met (structural-change policy + preferred API adoption in gameplay).
### Stage 3 ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â Remove globals (events + logging first) ÃƒÂ°Ã…Â¸Ã…Â¸Ã‚Â¡ (in progress)
Work items:
- inventory global/singleton usage in game + engine_core
- replace with explicit service wiring (composition root)
- remove fallback global access paths

### Stage 4 ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â One update graph
Work items:
- unify phases and ordering in one place
- support headless mode with renderer/platform omitted

### Stage 5 ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â Scripting firewall
Work items:
- ScriptAPI + command buffer + stable event schema
- bind only ScriptAPI to Lua

### Stage 6 ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â Asset pipeline
Work items:
- cooker + validation
- PackedAssetStore (+ optional DevAssetStore)

### Stage 7 ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â Determinism hardening
Work items:
- ITimeSource + IRandom injection
- headless battle/content invariants tests

---

## Immediate Technical Outputs (to keep momentum)

1) **Delete/replace list** for globals/singletons (prep for Stage 3) ÃƒÂ¢Ã…â€œÃ¢â‚¬Â¦ (`DELETE_REPLACE_LIST.md`)
2) **Core service interfaces** to add next:
   - `ITimeSource`, `IRandom`, `IAssetStore`
3) **ECS policy enforcement** (Stage 2.3) ÃƒÂ¢Ã…â€œÃ¢â‚¬Â¦
4) **Draft phase model** for the update graph (Stage 4)