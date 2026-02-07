# Roadmap (Iteration 2)

This is the active tracker for maintainability hardening after the initial rebuild.
Stable target rules live in ARCHITECTURE.md and TARGET_ARCHITECTURE.md.

Last updated: 2026-02-07

---

## Stable Decisions

- Engine-first
- Game-owned loop
- Strict layering
- ECS foundation
- Headless core
- No mutable globals in runtime
- Narrow ScriptAPI boundary
- Cooked assets plus JSON source

---

## Current State Snapshot

Done:
- Module split into engine_core, engine_platform, engine_render (CMake)
- ECS foundation in engine_core with tests
- IEventBus, ILogger, IRandom, ITimeSource, IAssetStore in place
- ScriptAPI with command queue and ScriptEventBus
- Asset cooker and packed/dev asset stores
- Determinism tests and headless battle invariants tests
- Runtime states/systems require GameServices (no legacy constructors)
- LogBus compatibility globals removed
- Game-owned loop default; Application/Window moved to runtime/platform
- ECS Scheduler drives update graph; SystemRegistry removed from runtime

Partial:
- Script firewall exists, but Lua bindings still reach GameWorld and use render headers
- ECS adoption started (RoundPhase stored in ECS; RoundSystem + ShopSystem run as ECS systems)

Not done:
- Replace hard-coded UI constants with a UI/Viewport service
- Gameplay adoption of ECS for authoritative state and systems

---

## Iteration 2 Plan (ordered)

### Stage 9 - Dependency hygiene and legacy removal

Actions:
- Make GameServices required for runtime states and systems.
- Remove fallback constructors and services == nullptr branches.
- Remove local config loads used as fallbacks in runtime.
- Remove LogBus compatibility functions and thread-local active logger.

Exit criteria:
- No runtime code compiles without explicit services.
- No fallback GameConfig::load(nullptr) in runtime.
- LogBus exposes only the instance Logger API.

---

### Stage 10 - Loop ownership and module placement cleanup

Actions:
- Move Application and Window (SDL/GL) out of engine_core tree.
- Ensure main entrypoint can run a game-owned loop without Application.
- Keep Application as an optional convenience runner in engine_platform or engine_runtime.

Exit criteria:
- engine_core tree has no SDL/GL includes.
- Game can run headless without Application.

---

### Stage 11 - Single scheduler and update graph

Actions:
- Make ECS Scheduler the only system update mechanism.
- Add an adapter system for existing Updatable systems or convert them.
- Remove SystemRegistry from game and engine runtime.
- Rework GameUpdateGraph to drive ECS phases.

Exit criteria:
- SystemRegistry is unused in game runtime.
- Exactly one scheduler defines update ordering.

---

### Stage 12 - ECS adoption in gameplay

Actions:
- Move core gameplay data into ECS components.
- Convert Movement, Combat, Round, and Shop logic into ECS systems.
- Reduce GameWorld to a thin facade or remove it.

Exit criteria:
- Gameplay systems operate on ECS data.
- GameWorld no longer owns authoritative gameplay state.

---

### Stage 13 - Script firewall completion

Actions:
- Remove ScriptAPI accessors that expose GameWorld or GameStateManager.
- Bind Lua only to ScriptAPI methods that return value types.
- Remove engine render headers from Lua bindings.

Exit criteria:
- Lua bindings compile without GameWorld or render includes.
- Scripts cannot access internal pointers.

---

### Stage 14 - UI and viewport context

Actions:
- Introduce UIContext or Viewport service from GameContext or GameServices.
- Replace UI_W, UI_H, and 1280/720 literals in gameplay.

Exit criteria:
- No hard-coded UI dimensions in runtime code.

---

### Stage 15 - Enforcement and cleanup

Actions:
- Add build checks for layering and forbidden includes.
- Remove temporary adapters left from migration.

Exit criteria:
- Architecture rules are enforceable by build tooling.
- No legacy adapters remain in runtime.
