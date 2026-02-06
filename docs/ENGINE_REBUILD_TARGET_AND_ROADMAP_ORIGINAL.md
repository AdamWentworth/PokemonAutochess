# Engine Rebuild Target and Roadmap

This document captures the agreed target architecture and the decision checklist outcomes, plus a staged rebuild plan.

## Goals

- **Engine-first**: Engine is the primary product; the game is a client.
- **Game-owned loop**: The game owns lifecycle and composition; the engine is reusable libraries/services.
- **Strict layering**: `game -> engine` only. Engine never depends on game.
- **ECS**: Gameplay uses ECS (entities/components/systems) with a scheduler.
- **Headless testing**: Core gameplay/simulation runs without window/renderer.
- **No mutable globals**: No singletons/global service state in engine core or game runtime.
- **Lua scripting**: Narrow, stable gameplay API (commands/events), no direct internal pointer access.
- **Assets**: Packaged runtime assets, with JSON source-of-truth and dev-friendly iteration.

---

## Target Architecture

### Dependency rule (strict layering)

- Game code may depend on Engine modules.
- Engine modules **must not** include game headers or depend on game types.
- Platform and render backends are separated so `engine_core` stays headless-capable.

### Module split

#### `engine_core` (headless-capable, no SDL/GL)
- ECS framework:
  - `World`, `Entity`, component storage
  - system interface + scheduler
- Eventing:
  - `IEventBus` interface + implementation(s)
- Logging:
  - `ILogger` interface + sinks (console/file/test sink)
- Determinism hooks:
  - `ITimeSource` / fixed timestep support
  - `IRandom` (injectable seeded RNG)
- Assets (runtime read-only API):
  - `IAssetStore` interface
  - lightweight deserialization utilities (not tied to filesystem)
- Utilities: math, containers, string, etc.

#### `engine_platform` (OS/SDL/etc.)
- SDL window/input (or OS abstraction)
- filesystem access used by dev mode and tools
- platform-specific timing sources

#### `engine_render` (graphics backend)
- renderer interface
- GL/Vulkan implementations
- GPU resources, debug draw

#### `engine_tools` (offline)
- content cooker (JSON -> packaged runtime format)
- schema validation
- reference resolution
- build pipeline helpers

#### `game`
- Main loop + composition root
- System registration/order policy
- Gameplay components/systems
- Lua scripts
- Content sources (JSON) and built packages

---

## Ownership Decisions

### Who owns the main loop?

**Game-owned loop** (selected)

Implications:
- Game constructs engine services and wires them together (composition root).
- Engine is reusable as a library and does not impose a single loop model.
- Engine can optionally provide a helper runtime runner (non-core), but it is not required.

### Who owns the scheduler/update graph?

**Engine provides scheduler; Game owns scheduling configuration** (recommended)

- `engine_core` includes the system scheduler and system interface.
- Game chooses which systems exist and their order/phases.
- Exactly one authoritative update graph.

This avoids duplicate registries and improves reuse across games.

---

## Globals and Singletons Policy

### The rule (selected)
**No mutable global state in `engine_core` or `game` runtime code.**

Allowed:
- compile-time constants (`constexpr`)
- pure stateless utility functions
- limited process-lifetime state in tooling/CLI processes (`engine_tools`)
- temporary adapters during migration (must be removed)

### Why avoid global singletons?
- Hidden dependencies: code uses services without declaring them.
- Testing becomes brittle and order-dependent (shared state across tests).
- Reuse suffers: engine becomes implicitly coupled to global config/state.

### How to keep “centralized logging” without a global logger
- Define `ILogger` in `engine_core`.
- Pass an `ILogger&` via:
  - a `Services` struct owned by the composition root, and/or
  - a context stored in `World` (e.g., `world.services().log`)
- Use convenience macros that still require explicit logger access:
  - `LOG(world.services().log, ...)`

Central configuration remains, but dependencies are explicit and testable.

---

## Lua Scripting Boundary

### Selected approach
**Narrow gameplay API** exposed to Lua, not internal pointers.

Lua should see:
- commands (enqueue):
  - `spawn_pokemon(id)`
  - `start_battle(a,b)`
  - `give_item(entity, item_id)`
- limited queries returning simple data (prefer events for UI)
- callbacks/events:
  - `on_turn_start`
  - `on_damage`
  - `on_status_applied`

Implementation pattern:
- Lua calls enqueue commands into a **command buffer** owned by gameplay.
- ECS systems consume commands at a defined phase.
- Gameplay emits events into a stable, versioned event schema for scripts.

Benefits:
- Refactors do not constantly break scripts.
- Deterministic headless tests remain possible.
- Scripts cannot violate invariants by mutating internals directly.

---

## Content / Assets Pipeline

### Selected approach
**JSON sources + cooked packages**

- Source of truth: `game/content_src/**/*.json`
- Offline cooker (`engine_tools`) does:
  - schema validation
  - reference resolution
  - output packaged runtime format: `game/content_pak/content.pak` (or similar)

Runtime uses `IAssetStore` only:
- `PackedAssetStore` for release
- optional `DevAssetStore` for development (reads JSON directly and can hot reload)

This keeps runtime simple and stable while still allowing easy iteration on Pokémon data.

---

## Rebuild Strategy

### Branch strategy (selected)
- Create a “hard break” branch for the rebuild.
- If the branch becomes irrecoverably broken, return to stable main and do an incremental refactor branch.

---

## Staged Rebuild Roadmap (hard break)

Order matters. Each stage has exit criteria.

### Stage 0 — Lock target rules
Actions:
- Add `ARCHITECTURE.md` capturing:
  - module list
  - dependency rules
  - no-globals policy
  - ECS + headless requirement
  - Lua boundary rule
- Add build discipline (CMake targets, include visibility).

Exit criteria:
- Everyone can answer “where does this file go?” consistently.
- The dependency rule is enforceable in the build graph.

---

### Stage 1 — Create clean module boundaries (CMake + folders)
Actions:
- Create targets: `engine_core`, `engine_platform`, `engine_render`, `engine_tools`, `game`.
- Move code into correct modules.
- Ensure `engine_core` has **zero SDL/GL** includes.

Exit criteria:
- `engine_core` builds independently as a library.
- `game` links engine libs.

---

### Stage 2 — ECS foundation in `engine_core`
Actions:
- Implement or port ECS:
  - `World`, `Entity`, component storage, queries
  - system interface + scheduler
- Decide timestep:
  - fixed-step update for determinism
  - optional render interpolation

Exit criteria:
- Headless test can:
  - create a world
  - register a system
  - step N ticks
  - assert expected state

---

### Stage 3 — Remove globals (events + logging first)
Actions:
- Replace singleton event manager with injected `IEventBus`.
- Replace global active logger with injected `ILogger`.
- Delete fallback/legacy service access patterns (no `cfgOrLegacy()`).

Exit criteria:
- No `getInstance()`, no `setActive()`, no default/global bus in core gameplay.
- Tests can run in parallel without shared state collisions.

---

### Stage 4 — One update graph
Actions:
- Remove duplicate registries/schedulers.
- Game defines system ordering; engine provides scheduler implementation.
- Establish phases:
  - input sampling (if present)
  - fixed_update
  - command processing
  - event dispatch
  - render submit (optional, absent in headless)

Exit criteria:
- Exactly one place defines system order and phases.
- Game can run with renderer omitted.

---

### Stage 5 — Scripting firewall
Actions:
- Implement `ScriptAPI` interface, command buffer, and stable event schema.
- Bind only `ScriptAPI` to Lua.
- Remove direct access to `World`/state manager pointers from scripts.

Exit criteria:
- Core internals can refactor without breaking Lua scripts.
- Scripts are deterministic participants (commands/events).

---

### Stage 6 — Asset pipeline
Actions:
- Implement cooker + schema validation.
- Implement `PackedAssetStore`.
- Implement `DevAssetStore` (optional) for JSON iteration and hot reload.

Exit criteria:
- Runtime boots using packaged content.
- Dev mode can iterate on JSON without rewriting runtime logic.

---

### Stage 7 — Testing + determinism hardening
Actions:
- Add headless tests for:
  - ECS scheduling invariants
  - battle rules invariants
  - content validation (schema + reference integrity)
- Inject time/random:
  - `ITimeSource`, `IRandom` with seeded RNG

Exit criteria:
- Tests run without window/renderer.
- Tests produce consistent results across runs.

---

## Immediate Next Technical Outputs (when implementing)
Once you start executing this plan, the next concrete artifacts to produce are:
- folder tree + target graph (CMake)
- interface definitions:
  - `ILogger`, `IEventBus`, `IAssetStore`, `IRandom`, `ITimeSource`
- “delete/replace list”:
  - all singletons/globals to remove
  - all duplicate registries/schedulers to unify
  - scripting bindings to replace with `ScriptAPI`

