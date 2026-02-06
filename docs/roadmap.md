# Engine Rebuild Target and Roadmap

This document is the single source of truth for the agreed target architecture and the staged rebuild plan. It is intentionally detailed so future changes can be evaluated against the same constraints.

---

## Current Status

- ✅ Stage 0: rules documented (see `ARCHITECTURE.md`).
- ✅ Stage 1: build split into `engine_core`, `engine_platform`, `engine_render` with `Engine` umbrella target; game + tests still run.
- ✅ Stage 2 (initial): ECS starter exists in `engine_core` and is covered by `ecs_smoke` headless test.

**Known gaps in current Stage 2 ECS starter (intentional for now):**
- `World::destroy()` does not yet remove components across all stores.
- Storage is `unordered_map`-based (correctness-first, not performance-first).
- No query/view API yet (systems iterate storage raw maps).

---

## Goals

- **Engine-first**: the engine is the primary product; the game is a client.
- **Game-owned loop**: the game owns lifecycle and composition; engine is reusable libraries/services.
- **Strict layering**: dependency direction is `game -> engine_*` only. Engine never depends on game.
- **ECS**: gameplay state lives in ECS (entities/components/systems) with a scheduler.
- **Headless testing**: core simulation runs without window/renderer.
- **No mutable globals**: no global singletons or “active” services in runtime.
- **Lua scripting**: stable, narrow gameplay API (commands/events); no direct internal pointer access.
- **Assets**: packaged runtime assets; JSON remains the authoring format; strong validation.

---

## Non-Negotiable Architecture Rules

### 1) Dependency Rule (Strict Layering)

- `game` may depend on `engine_*`.
- `engine_*` must never include or depend on `game` code.
- `engine_core` must remain headless-capable:
  - **No SDL**, **no OpenGL/GLAD**, **no window/input**.
- Platform/renderer concerns are separated so `engine_core` stays pure.

**Enforcement mechanisms**
- CMake target graph reflects allowed dependencies.
- Headers follow “include visibility” discipline:
  - `engine_core` public headers must not include platform/render headers.
- If a dependency is needed, it must be expressed as an interface in `engine_core` (e.g., `IRenderer`, `IFileSystem`) and implemented in `engine_render`/`engine_platform`.

---

## Target Module Split

### `engine_core` (headless-capable)
Responsibilities:
- ECS framework
  - `World`, `Entity`, component storage
  - system interface + scheduler
  - queries/views (eventually)
- Eventing
  - `IEventBus` interface + implementation(s)
- Logging
  - `ILogger` interface + sink implementations (console/file/test)
- Determinism hooks
  - `ITimeSource` (fixed timestep support)
  - `IRandom` (injectable seeded RNG)
- Assets (runtime read-only API)
  - `IAssetStore`
  - deserialization helpers (not tied to filesystem)
- Shared utilities
  - math, containers, string, hashing, ids, error types

**Hard constraints**
- No SDL/OpenGL/GLAD.
- No filesystem assumptions in core runtime APIs (prefer byte streams / injected stores).

### `engine_platform` (OS abstraction)
Responsibilities:
- window + input (SDL or other backend)
- platform timing source (high-res timer)
- filesystem implementation for dev mode and tooling integration
- OS-specific services (paths, env vars, etc.)

### `engine_render` (graphics backend)
Responsibilities:
- rendering interfaces + implementations
- GPU resource management
- renderer debug tools (debug draw, GPU timings, etc.)
- model/texture loading *if and only if* it does not leak platform concerns into `engine_core`
  - ideally: loader outputs intermediate data formats; core uses only abstract handles or data blobs

### `engine_tools` (offline)
Responsibilities:
- content cooking pipeline (JSON → runtime package)
- schema validation + diagnostics
- reference resolution
- build pipeline helpers (CLI tools)
- optional dev utilities (asset hot reload watchers)

### `game` (client)
Responsibilities:
- main loop (game-owned)
- composition root (constructs engine services + wiring)
- gameplay ECS systems + components
- Lua scripts
- content sources (JSON) + built packages
- game-specific platform/render usage

---

## Ownership Decisions

### Who owns the main loop?
**Game-owned loop** (selected)

Implications:
- game constructs and wires engine services (composition root)
- engine remains usable in other games or tools without inheriting a fixed loop model
- engine may provide optional helper runners (non-core), but clients should not depend on them

### Who owns the scheduler/update graph?
**Engine provides the mechanism; game provides the configuration** (selected)

- `engine_core` provides:
  - `Scheduler`
  - system interfaces
  - phase execution model (later)
- `game` provides:
  - list of systems
  - ordering and phase assignment
  - configuration and dependencies between systems

**Goal:** exactly one authoritative update graph. No duplicate “registries” doing the same job.

---

## Globals and Singletons Policy

### Rule
**No mutable global state in runtime (`engine_core` or `game`).**

Allowed:
- `constexpr` constants
- pure stateless utility functions
- immutable lookup tables (compile-time or read-only at runtime)
- process-lifetime state in tooling (`engine_tools`) only
- *temporary adapters during migration* (must be deleted before “done”)

### Why avoid globals/singletons
- hidden dependencies (harder to reason about call paths)
- test order dependence (shared state leaks between tests)
- poor reuse (engine becomes coupled to implicit global configuration)
- makes headless deterministic simulation harder

### How to get “central logging” without a global logger
- define `ILogger` in `engine_core`
- wire it in composition root and pass it explicitly:
  - via a `CoreServices` bundle owned by the game (composition root), and/or
  - stored in `World` as a pointer/reference to services

Convenience macros are fine **only if they still route through explicit references**, e.g.
- `LOG(*world.services()->log, ...)` (not `LOG(...)` that implicitly calls a global singleton)

---

## ECS Design Intent (Detailed)

### Stage 2 ECS starter (today)
- `Entity`: id + generation
- `World`:
  - creates/destroys entities
  - stores type-erased component storages keyed by `type_index`
- `ComponentStorage<T>`:
  - correctness-first (`unordered_map<entity_id, T>`)
- `Scheduler`:
  - ordered list of systems; `tick(world, dt)` calls each system in order

### Next ECS expansions (Stage 2.1 / Stage 2.2)
- automatic component cleanup on `World::destroy(Entity)`
- query/view API
  - e.g., iterate entities that have `(A, B)` components
- component lifetime rules and “structural changes” policy
  - e.g., forbid adding/removing components while iterating, or defer via command buffer
- performance upgrades (later, only after correctness + architecture)
  - sparse sets, SoA storage, stable handles, archetypes (optional)

### Determinism requirements
- fixed timestep for simulation
- no frame-time-driven logic in gameplay systems
- injectable `IRandom` and `ITimeSource` into simulation layer

---

## Lua Scripting Boundary

### Selected approach
**Narrow gameplay API** exposed to Lua, not internal pointers.

Lua gets:
- commands (enqueue):
  - `spawn_pokemon(id)`
  - `start_battle(a, b)`
  - `give_item(entity, item_id)`
- limited queries returning simple value types (avoid returning references/handles)
- events/callbacks:
  - `on_turn_start`
  - `on_damage`
  - `on_status_applied`

### Implementation pattern
- Lua calls enqueue commands into a **command buffer**
- ECS systems consume commands at a defined phase
- gameplay emits events into a stable, versioned event schema

### Script safety guarantees
- scripts cannot mutate internal data structures directly
- scripts cannot keep “live references” to engine objects
- engine/game can refactor internals without breaking scripts, as long as the ScriptAPI contract remains

---

## Content / Assets Pipeline (Detailed)

### Selected approach
**JSON sources + cooked packages**

- Source of truth: `game/content_src/**/*.json`
- Offline cooker (`engine_tools`):
  - schema validation (fail fast with clear diagnostics)
  - reference resolution (e.g., moves refer to ids, ids must exist)
  - output packaged runtime format (e.g., `content.pak`)
  - optional: generate indexes for fast lookup (e.g., `pokemon_by_id`)

Runtime reads via `IAssetStore` only:
- `PackedAssetStore` (release)
- optional `DevAssetStore` (dev-only): reads JSON directly, supports hot reload
  - must not leak filesystem usage into `engine_core` (filesystem is `engine_platform`)

### Data editing workflow goals
- changing Pokémon balance numbers should be easy (JSON edit)
- invalid data must produce actionable errors before runtime (tooling validation)
- runtime should avoid heavy parsing and dynamic discovery

---

## Rebuild Strategy

### Branch strategy (selected)
- do the hard break work on a dedicated branch
- if the branch becomes irrecoverably broken:
  - return to stable main
  - redo using incremental adapters/refactor approach

### “Keep the game runnable” rule
Even in hard-break mode, prefer:
- additive scaffolding + gradual migration
- frequent green builds + tests
- avoid mixing many refactors with gameplay feature additions in one commit

---

## Staged Rebuild Roadmap

Order matters. Each stage has explicit exit criteria.

### Stage 0 — Lock target rules (done)
Actions:
- create/confirm architecture rules doc
- define module boundaries + dependency rules
- define no-globals policy, headless core requirement, Lua boundary rule
- ensure build system can enforce layering

Exit criteria:
- team can answer “where does this file go?” consistently
- dependency rule is enforceable in build graph

### Stage 1 — Create clean module boundaries (done)
Actions:
- split build into targets:
  - `engine_core`, `engine_platform`, `engine_render`, `engine_tools` (later), `game`
- move or reassign compilation units into correct targets
- ensure `engine_core` builds without SDL/GL headers

Exit criteria:
- `engine_core` builds independently as a library
- game links engine libs and still runs
- tests still pass

### Stage 2 — ECS foundation in `engine_core` (done, initial)
Actions:
- add ECS starter:
  - `World`, `Entity`, component storage
  - `ISystem` + `Scheduler`
- add headless ECS test (`ecs_smoke`)
- wire `PAC_Tests` target in CMake

Exit criteria:
- headless test creates a world, registers a system, steps N ticks, asserts state
- game still runs unchanged

**Stage 2 follow-ups (not done yet):**
- component cleanup on destroy
- query/view API
- structural change rules

### Stage 3 — Remove globals (events + logging first)
Actions:
- replace singleton event manager with injected `IEventBus`
- replace global active logger with injected `ILogger`
- remove or quarantine global access patterns behind temporary adapters
- introduce a single “services bundle” that is created in the game composition root and passed down

Exit criteria:
- no `getInstance()`, no `setActive()`, no default/global bus/logger in gameplay runtime
- tests can run in parallel without shared-state collisions

### Stage 4 — One update graph
Actions:
- remove duplicate registries/schedulers
- unify update phases (example):
  - input sample (platform)
  - fixed simulation tick (core)
  - command processing (gameplay)
  - event dispatch (core)
  - render submit (render; optional in headless)
- ensure headless mode skips render/platform cleanly

Exit criteria:
- exactly one place defines system order and phases
- game can run without renderer attached (headless simulation)

### Stage 5 — Scripting firewall
Actions:
- implement `ScriptAPI` interface
- implement command buffer + stable event schema for scripts
- bind only `ScriptAPI` to Lua
- remove direct `World`/state pointers from Lua bindings

Exit criteria:
- scripts interact via commands/events only
- engine/game internals can refactor without breaking scripts

### Stage 6 — Asset pipeline
Actions:
- implement cooker + schema validation tool
- implement `PackedAssetStore`
- optional: dev store with hot reload
- implement stable IDs and reference resolution rules

Exit criteria:
- runtime boots using packaged content
- invalid content fails during build/cook with actionable diagnostics

### Stage 7 — Testing + determinism hardening
Actions:
- add headless tests for:
  - ECS scheduling invariants
  - battle rules invariants
  - content integrity (schema + references)
- inject time/random:
  - `ITimeSource`, `IRandom` seeded RNG
- add replay/regression harness (optional but recommended)

Exit criteria:
- tests run without window/renderer
- deterministic results across runs and machines (within reason)

---

## Immediate Next Technical Outputs

When continuing implementation, prioritize producing these artifacts in order:

1) **Explicit “delete/replace list”** for globals/singletons to remove (Stage 3)
   - where they live
   - what replaces them (service injection / world services / composition root)

2) **Core service interfaces (minimal set)**
   - already: `ILogger`, `IEventBus`, `CoreServices`
   - next: `ITimeSource`, `IRandom`, `IAssetStore`

3) **ECS expansion tasks**
   - destroy cleanup across stores
   - view/query API
   - structural-change policy (immediate vs deferred)

4) **Phase model draft for update graph**
   - define phases and what is allowed in each phase
   - define how headless mode participates

