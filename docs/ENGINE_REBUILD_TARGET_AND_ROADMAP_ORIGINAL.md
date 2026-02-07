# Engine Rebuild Target and Roadmap (Iteration 2)

This replaces the original rebuild plan. The target decisions are unchanged, but the roadmap now focuses on the remaining gaps after the initial rebuild phases.

Last updated: 2026-02-07

---

## Target Goals (unchanged)

- Engine-first
- Game-owned loop
- Strict layering (game -> engine only)
- ECS foundation for gameplay
- Headless core simulation and tests
- No mutable globals in runtime
- Narrow ScriptAPI boundary (commands/events)
- Cooked assets with JSON as source-of-truth

---

## Target Architecture Summary

- engine_core is headless, contains ECS, IEventBus, ILogger, IRandom, ITimeSource, IAssetStore, and shared utilities.
- engine_platform contains SDL/window/input/filesystem and platform timing sources.
- engine_render contains graphics backends and render utilities.
- engine_runtime contains the optional Application host loop (SDL/GL convenience runner).
- engine_tools contains offline content cooking and validation.
- game owns the loop, wiring, system ordering, gameplay systems, scripts, and content.

---

## Current Gaps vs Target

- ECS adoption is still partial (only RoundPhase is stored in ECS; most gameplay still lives in GameWorld/legacy systems).
- ScriptAPI still exposes GameWorld/GameStateManager; Lua bindings include render headers.
- UI width/height constants are hard-coded in gameplay states.

---

## Iteration 2 Roadmap (ordered)

### Stage 9 - Dependency hygiene and legacy removal

Actions:
- Require GameServices in all runtime states and systems.
- Remove fallback constructors and services == nullptr branches.
- Remove local fallback config loads in runtime.
- Delete LogBus compatibility functions and thread-local active logger.

Exit criteria:
- No runtime code compiles without explicit services.
- No fallback GameConfig::load(nullptr) in runtime.
- LogBus exposes only the instance Logger API.

---

### Stage 10 - Loop ownership and module placement cleanup

Actions:
- Move Application and Window (SDL/GL) out of engine_core tree.
- Ensure the default main entrypoint can run a game-owned loop without Application.
- Keep Application as an optional convenience runner in engine_platform or engine_runtime.

Exit criteria:
- engine_core tree has no SDL/GL includes.
- Game can run headless without Application.

---

### Stage 11 - Single scheduler and update graph

Actions:
- Make ECS Scheduler the only system update mechanism.
- Add an adapter system for Updatable or convert those systems.
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

---

## Definition of Done (iteration complete)

- Game-owned loop is the default runtime path.
- engine_core is headless in both build and file organization.
- ECS Scheduler is the only update graph.
- Runtime code has no optional service dependencies or fallback constructors.
- ScriptAPI is the only Lua surface and does not expose internal pointers.
- UI sizing uses a single injected context.
- Global compatibility shims are removed.
