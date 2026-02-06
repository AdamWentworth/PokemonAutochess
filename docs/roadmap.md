# Roadmap (Living)

This document is the **living** tracker: what is done, what is next, and what “exit criteria” still block forward progress.
The target end-state and stable constraints live in `TARGET_ARCHITECTURE.md`.

Last updated: 2026-02-06

---

## Current Status Snapshot

- ✅ Stage 0: architecture rules captured (`ARCHITECTURE.md`, `TARGET_ARCHITECTURE.md`).
- ✅ Stage 1: build split into `engine_core`, `engine_platform`, `engine_render` (with `Engine` umbrella). Game + tests run.
- ✅ Stage 2 (in progress): ECS foundation in `engine_core` is usable and covered by headless tests:
  - `ecs_smoke`
  - `ecs_destroy_cleans_components`
  - `ecs_for_each_join`

### Current ECS capabilities
- Entities: id + generation
- Type-erased component stores keyed by `type_index`
- Correctness-first storage (`unordered_map`)
- Scheduler executes ordered systems
- Canonical query API: `World::for_each<Cs...>(fn)` joins by id (driver = first component store)
- Destroy cleanup: `World::destroy()` removes components across all stores

### Current ECS gaps (still blocking “Stage 2 complete”)
- Structural-change policy is not yet enforced (add/remove during iteration is unsafe unless we implement a rule).
- Query performance/driver selection is naive (acceptable for now).
- The public API still allows bypassing policy via direct store access (`components<T>()`), which is fine during migration but not end-state.

---

## Next Step (Do this next)

### Stage 2.3 — Structural-change policy (enforceable)
Goal: make “add/remove components during iteration” defined and safe.

Preferred implementation order:
1) Add `World::add/remove/has/get` and use them in new code
2) Introduce an iteration guard that defers structural changes until a safe point (end of outermost iteration)
3) Add headless test proving deferral works (e.g., add a component during `for_each`, verify after)

Exit criteria:
- A test proves the chosen policy works and stays stable.
- Iteration helpers (`each/each2/for_each`) participate in the same policy.

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

### Stage 2 — ECS foundation 🟡 (in progress)
Remaining exit criteria (blocking):
- structural-change policy enforced (Stage 2.3)
- a clear “preferred API” for gameplay so raw map access stops spreading

### Stage 3 — Remove globals (events + logging first) ⏭️ next after Stage 2
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

1) **Delete/replace list** for globals/singletons (prep for Stage 3)
2) **Core service interfaces** to add next:
   - `ITimeSource`, `IRandom`, `IAssetStore`
3) **ECS policy enforcement** (Stage 2.3)
4) **Draft phase model** for the update graph (Stage 4)
