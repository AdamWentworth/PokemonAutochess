# Target Architecture (Engine-first, Game-owned loop)

This document is intended to be **stable**. It describes the target end-state and the non-negotiable architectural constraints.
Day-to-day progress tracking lives in `ROADMAP.md`.

---

## Product & Ownership Decisions

### Primary product
- **Engine-first**: the engine is the primary product; the current game is one client.
- **Implication**: engine modules must be reusable without inheriting game-specific policies, data formats, or loop constraints.

### Who owns the main loop?
**Game-owned loop** (selected)

- The game (client) owns:
  - lifecycle (`init`, `tick`, `shutdown`)
  - composition root (construct services + wire dependencies)
  - selecting which engine services are used (renderer optional, platform optional in headless)
- The engine provides libraries and services, not a mandated loop model.
- Optional: provide a convenience runner as a *separate* helper (not required by engine clients).

### Who owns the scheduler / update graph?
**Engine provides the mechanism; game provides the configuration** (selected)

- `engine_core` provides:
  - `ISystem` interface(s)
  - `Scheduler` (or equivalent) to execute systems
  - a phase model later (fixed_update / commands / events / render submit)
- `game` provides:
  - system list
  - ordering and phase assignment
  - dependencies between systems (if applicable)

**Constraint**: there must be exactly **one** authoritative update graph.

---

## Non-negotiable Architecture Rules

### 1) Strict layering
- Dependency direction is **only**: `game -> engine_*`
- `engine_*` must never include or depend on game headers/types.
- If `engine_*` needs to talk ?up? to a client, it must do so through **interfaces** defined in `engine_core`.

### 2) Headless-capable core
- `engine_core` must not depend on:
  - SDL, OpenGL, GLAD, windowing, input
- Core simulation must run headless (tests, tools, server mode).

### 3) No mutable globals in runtime
- No `getInstance()`, no `setActive()`, no default singleton ?current? service in:
  - `engine_core`
  - `game` runtime
- Allowed:
  - `constexpr` constants
  - pure/stateless utilities
  - immutable tables
  - process-lifetime objects in tooling (`engine_tools`) only
  - *temporary adapters* during migration (must be deleted before ?done?)

### 4) Lua boundary (stable ScriptAPI)
- Lua interacts through a stable, narrow **ScriptAPI**:
  - commands/events, not raw pointers/references to internal objects
- Lua must not keep ?live? references to engine/game objects.

### 5) Assets pipeline
- Runtime reads cooked packages via `IAssetStore`.
- JSON remains the authoring format; a cooker validates and packs.

---

## Target Module Split

### `engine_core` (headless-capable)
Responsibilities:
- ECS framework
  - `World`, `Entity`, component storage
  - queries/views
  - system interface + scheduler
  - structural-change policy (enforced)
- Eventing
  - `IEventBus` interface + implementation(s)
- Logging
  - `ILogger` interface + sink implementations (console/file/test)
- Determinism hooks
  - `ITimeSource` for fixed-step simulation
  - `IRandom` for seeded RNG (injectable)
- Assets (runtime read-only API)
  - `IAssetStore`
  - deserialization helpers that do **not** assume filesystem
- Shared utilities
  - ids, hashing, string utilities, error types

Hard constraints:
- No SDL/OpenGL/GLAD.
- Avoid filesystem assumptions in core runtime APIs (prefer byte streams / injected stores).

### `engine_platform` (OS abstraction)
Responsibilities:
- window + input backend (SDL or other)
- platform timing source
- filesystem implementation used by dev mode and tools integration
- OS paths/env helpers

### `engine_render` (graphics backend)
Responsibilities:
- renderer interface(s) + implementations
- GPU resource management
- debug draw, GPU timings, capture hooks
- asset loading only if it does not leak platform concerns into `engine_core`
  - preferred: loader outputs intermediate data blobs; core holds only abstract handles or POD data.

### `engine_tools` (offline)
Responsibilities:
- content cooking (JSON ? runtime package)
- schema validation + diagnostics
- reference resolution
- build pipeline helpers (CLI tools)
- optional hot reload watchers (dev-only)

### `game` (client)
Responsibilities:
- main loop and composition root
- gameplay ECS components/systems
- Lua scripts and script event routing
- content sources (JSON) + built packages
- rendering/platform usage specific to this game

---

## ECS Design Intent (Target)

### Required properties
- Headless deterministic stepping (fixed timestep).
- Explicit structural-change policy:
  - either forbid add/remove during iteration with enforcement, or
  - defer via command buffer and flush at safe points.
- Query API must be canonical and replace raw store poking.
- Minimal surface area required for gameplay systems and scripting integration.

### Performance policy
- Correctness + architecture first.
- Only after invariants are enforced and tests are meaningful:
  - consider sparse-set / SoA / archetypes
  - improve driver selection for joins (smallest store)
  - reduce allocations in queries/commands

---

## Lua Scripting Boundary (Target)

Lua sees:
- commands (enqueue):
  - `spawn_pokemon(id)`
  - `start_battle(a, b)`
  - `give_item(entity, item_id)`
- limited queries returning value types (ids, small structs)
- events/callbacks:
  - `on_turn_start`
  - `on_damage`
  - `on_status_applied`

Implementation pattern:
- Lua enqueues commands into a command buffer.
- ECS systems consume commands at a defined phase.
- Gameplay emits stable, versioned events for scripts.

Safety guarantees:
- scripts cannot mutate internal structures directly
- scripts cannot store pointers to engine/game internals
- internals can refactor without breaking scripts if ScriptAPI stays compatible

---

## Content / Assets Pipeline (Target)

Source-of-truth:
- `game/content_src/**/*.json`

Cook step (`engine_tools`):
- schema validation (fail fast with actionable diagnostics)
- reference resolution (ids must exist)
- output runtime package (e.g., `content.pak`)
- optional indexes for fast lookup

Runtime:
- uses `IAssetStore` only:
  - `PackedAssetStore` (release)
  - optional `DevAssetStore` (dev-only JSON + hot reload), implemented outside `engine_core` (filesystem belongs to platform/tools)

Workflow goals:
- small balance changes are easy (edit JSON)
- invalid content fails at cook/build time, not at runtime
- runtime avoids heavy parsing and reflection
