# Render Path File Map (Living)

Goal
- Make it obvious which files are:
- always-used game/runtime code
- shared-contract render/runtime code (`opengl_shared` + `d3d12`)
- renderer/backend-specific code (`opengl`, `d3d12`)
- legacy OpenGL gameplay render path code
- mixed coordinator files that dispatch conditionally

Why this exists
- Housework has reduced monoliths, but the top-level folders are now crowded.
- The next maintainability win is not just splitting files, but making ownership and runtime path usage obvious.

How route selection works (source of truth)
- `src/game/runtime/routes/RenderRoutes.h`
  - Canonical per-frame route state (`legacyRenderPath`, `legacyUiPath`)
- `src/game/runtime/routes/RenderFlowDecisions.h`
  - Decides frame layers (`renderWorldLayer`, `renderLegacyHudLayer`)
- `src/game/state/BackendUiPolicy.h`
  - Backend/legacy UI policy helpers (menu/shop/sell overlay behavior)
- `src/game/runtime/GameSession.cpp`
  - Main runtime render coordinator that materializes active routes and dispatches to shared/legacy helpers

## File Ownership by Runtime Role

### 1) Game Core (renderer-agnostic, always used)
These files are gameplay/runtime scaffolding and run regardless of renderer API.

Paths (primary)
- `src/game/runtime/GameApp.*`
- `src/game/runtime/GameRunner.*`
- `src/game/runtime/GameRuntime.*`
- `src/game/runtime/GameBootstrap.*`
- `src/game/runtime/GameUpdateGraph.*`
- `src/game/runtime/VideoPreferences.*`
- `src/game/runtime/routes/RenderRoutes.h`
- `src/game/runtime/routes/RenderFlowDecisions.h`
- `src/game/runtime/routes/GameServiceRenderRoutes.h`
- `src/game/state/CombatState.*`
- `src/game/state/PlacementState.*`
- `src/game/state/BackendUiPolicy.h` (policy helpers; path-agnostic logic)

Notes
- `GameSession.cpp` is in this category as a coordinator, but it is a mixed dispatcher (see section 5).

### 2) Shared-Contract Game Rendering Runtime (used by `opengl_shared` and `d3d12`)
These files implement the shared gameplay presentation path. They are game-side rendering orchestration/helpers, not engine backends.

Strong signal
- Most files prefixed `Shared*` under `src/game/runtime/shared/`

Paths (examples, not exhaustive)
- `src/game/runtime/shared/projected/SharedProjected*.*`
- `src/game/runtime/shared/capture/SharedCapture*.*`
- `src/game/runtime/shared/vfx/growl/SharedGrowl*.*`
- `src/game/runtime/shared/vfx/particles/SharedParticle*.*`
- `src/game/runtime/shared/vfx/tail_fire/SharedTailFire*.*`
- `src/game/runtime/shared/ui/SharedUnitHudBatches.*`
- `src/game/runtime/shared/world/SharedWorldIndexedBatches.*`
- `src/game/runtime/shared/world/SharedBoardGridBatches.*`
- `src/game/runtime/shared/ui/SharedBackendDebugViewOverlay.*`
- `src/game/runtime/shared/backend/SharedBackendPoseEval.*`
- `src/game/runtime/shared/backend/SharedBackendTextureCache.h`

Also shared-path support (not `Shared*`, but used by shared runtime paths)
- `src/game/runtime/backend_model_cache/BackendModelCache*.*` (backend cache load/build/read/write)
- `src/game/runtime/Backend*.*` helper headers (backend UI/HUD/world formatting/model visuals)

Notes
- These are reused across both shared renderers.
- Some contain backend-specific branches internally (e.g., D3D12 fast path or OpenGL-shared direct draw), but ownership is still “shared-contract runtime.”

### 3) D3D12-specific Game Runtime Code (game-side)
These are game/runtime files only relevant when D3D12 is active (or when probing for D3D12).

Paths
- `src/game/runtime/d3d12/D3D12Probe.*`
- `src/game/runtime/shared/capture/SharedCaptureD3d12FastPath.*` (D3D12-only branch extracted from shared capture model bridge)

Notes
- This is game/runtime code, not engine/backend code.
- It exists because shared gameplay presentation sometimes needs a D3D12-specific optimization hook.

### 4) OpenGL Legacy Gameplay Render Path (game-side)
These files implement legacy gameplay rendering behavior (the old source-of-truth path).

Primary legacy gameplay renderer
- `src/game/world/GameWorldRender.cpp`

Related legacy presentation behavior (mixed usage)
- `src/game/vfx/*`
  - Important nuance: many VFX classes are not strictly “legacy only” anymore.
  - Shared paths now bridge/consume snapshots and simulation data from several of these systems for parity.

Notes
- Legacy gameplay world/HUD rendering is still kept as a fallback path.
- The engine OpenGL backend itself is not “legacy gameplay”; it serves both `opengl` and `opengl_shared`.

### 5) Mixed Coordinators / Conditional Dispatchers (high-context files)
These files are not a single render path; they dispatch based on route/backend.

Paths
- `src/game/runtime/GameSession.cpp`
  - now a much smaller coordinator, but still the main path dispatcher
- `src/game/state/scripted/ScriptedState*.cpp`
  - backend/shared UI and menu/shop/adventure UI composition/input, route-aware behavior
- `src/game/runtime/GameRunner.cpp`
  - backend selection, boot/runtime orchestration, renderer startup

Notes
- These are the right place for route decisions.
- They should remain thin and delegate implementation to path-owned modules.

### 6) Engine Render Backend: OpenGL (used by both `opengl` and `opengl_shared`)
These are engine-side backend implementation files. They do not define gameplay presentation policy.

Primary backend class
- `src/engine/render/OpenGLRenderBackend.*`

OpenGL backend subsystem TUs
- `src/engine/render/opengl/OpenGLRenderBackend*.cpp`
  - pipelines
  - world draw
  - textures
  - debug draw
  - world pipeline/shader utils

Supporting engine OpenGL/GLTF/model systems (shared by OpenGL usage)
- `src/engine/render/Model*.*`
- `src/engine/render/ModelCache.cpp`
- `src/engine/render/gltf/*`

Notes
- `opengl` and `opengl_shared` both use these engine backend files.
- The difference between those modes is game/runtime route selection, not backend implementation.

### 7) Engine Render Backend: D3D12 (used by `d3d12`)
Engine-side backend implementation for D3D12.

Primary backend class
- `src/engine/render/D3D12RenderBackend.*`

D3D12 backend subsystem TUs
- `src/engine/render/d3d12/D3D12RenderBackend*.cpp`
- `src/engine/render/d3d12/D3D12TextureUpload.*`
- `src/engine/render/d3d12/D3D12RenderBackendInternal.h`

Notes
- D3D12 shared gameplay behavior depends on these files plus the shared-contract game runtime modules.

## Quick Route Matrix (What runs where)

`opengl` (legacy)
- Game core/runtime: yes
- Legacy gameplay render path (`GameWorldRender.cpp`): yes
- Shared-contract runtime modules: some may be present for other features, but not primary world/UI path
- OpenGL engine backend: yes
- D3D12 engine backend: no

`opengl_shared`
- Game core/runtime: yes
- Shared-contract runtime modules: yes (primary path)
- Legacy gameplay render path: fallback/reference only, not primary
- OpenGL engine backend: yes
- D3D12 engine backend: no

`d3d12`
- Game core/runtime: yes
- Shared-contract runtime modules: yes (primary path)
- Legacy gameplay render path: no (except legacy data/simulation sources bridged in some parity cases)
- OpenGL engine backend: no
- D3D12 engine backend: yes

## Proposed Folder Organization (phased, low-risk)

This is a recommendation for future moves. Do not move everything at once.

### A) `src/game/runtime/` (most crowded)
Proposed structure
- `src/game/runtime/core/`
  - `GameApp.*`
  - `GameRunner.*`
  - `GameRuntime.*`
  - `GameBootstrap.*`
  - `GamePreload.*`
  - `GameUpdateGraph.*`
  - `VideoPreferences.*`
- `src/game/runtime/routes/`
  - `RenderRoutes.h`
  - `RenderFlowDecisions.h`
  - `GameServiceRenderRoutes.h`
  - `BackendRenderPolicy.h`
- `src/game/runtime/backend_model_cache/`
  - `BackendModelCache*.{h,cpp}`
- `src/game/runtime/shared/`
  - `projected/` (`SharedProjected*`)
  - `capture/` (`SharedCapture*`)
  - `vfx/growl/` (`SharedGrowl*`)
  - `vfx/particles/` (`SharedParticle*`)
  - `vfx/tail_fire/` (`SharedTailFire*`)
  - `ui/` (`SharedUnitHudBatches`, `SharedBackendDebugViewOverlay`, etc.)
  - `world/` (`SharedWorldIndexedBatches`, `SharedBoardGridBatches`)
  - `backend/` (`SharedBackendPoseEval`, `SharedBackendTextureCache`)
- `src/game/runtime/platform/d3d12/`
  - keep `d3d12/D3D12Probe.*` (already mostly aligned)

Pragmatic note
- `GameSession.*` can stay at `src/game/runtime/` root as the coordinator entrypoint (easy to find), even if helpers move into subfolders.

### B) `src/game/state/`
Proposed structure
- `src/game/state/scripted/`
  - `ScriptedState.cpp`
  - `ScriptedStateBackendUi.cpp`
  - `ScriptedStateCardUi.cpp`
  - `ScriptedStateInputUi.cpp`
  - `ScriptedStateRenderUi.cpp`

Why
- `ScriptedState*` is already a family and now split cleanly.
- Moving together reduces root-level clutter.

### C) `src/engine/render/`
Current state is already improving
- `src/engine/render/opengl/*` and `src/engine/render/d3d12/*` are good subsystem homes.

Low-risk next organization move
- Keep `OpenGLRenderBackend.*` and `D3D12RenderBackend.*` at top-level as public backend entrypoints
- Keep subsystem implementation in `opengl/` and `d3d12/`
- Avoid deeper nesting unless there is a clear subsystem boundary (e.g. `pipelines/`, `textures/`) with multiple files

## Recommended Reorg Order (to avoid churn)

1. Document ownership (this file) and agree on naming/folder conventions.
2. [x] Move `ScriptedState*` files into `src/game/state/scripted/` (completed).
3. [x] Move `BackendModelCache*` family into `src/game/runtime/backend_model_cache/` (completed).
4. [x] Move `Shared*` runtime modules into `src/game/runtime/shared/` (flat first pass complete).
5. [x] Move `Shared*` runtime modules from `src/game/runtime/shared/` into deeper subsystem folders by subsystem (`projected`, `capture`, `vfx/*`, `ui`, `world`, `backend`) and update includes/CMake/tests.
6. Continue normalizing special cases over time (for example, decide whether D3D12-only shared capture helpers stay under `shared/capture/` or move under `shared/capture/d3d12/`).
7. Update includes incrementally, build + `ctest` after each batch, plus one manual smoke per batch.

## What still counts as meaningful housework (not just line-count work)

High-value remaining work
- Route/file ownership clarity (this doc + folder moves)
- Further OpenGL backend subsystem splits (active runtime path)
- Regression guardrails per slice (targeted manual smoke + `ctest`)
- Thin coordinators (`GameSession`, `ScriptedState`, backend entrypoints) that mostly dispatch

Lower-value (for now)
- More micro-splitting of niche debug-only or rarely-used code paths unless there is a concrete bug/perf/debugging need

## Current Large Files Worth Watching (snapshot)

Approx. top files in `src/game/runtime`, `src/game/state`, `src/engine/render`:
- `src/game/runtime/GameSession.cpp` (~2059) — coordinator, acceptable but still large
- `src/engine/render/d3d12/D3D12RenderBackendPipelines.cpp` (~929)
- `src/game/state/scripted/ScriptedStateBackendUi.cpp` (~775)
- `src/game/runtime/GameRunner.cpp` (~766)
- `src/game/state/CombatState.cpp` (~689)
- `src/game/runtime/shared/ui/SharedBackendDebugViewOverlay.cpp` (~630)
- `src/game/runtime/shared/vfx/tail_fire/SharedTailFireExactCpuTileBake.cpp` (~613)
- `src/game/runtime/backend_model_cache/BackendModelCacheReadDecode.cpp` (~512)

This list is for prioritization, not a mandate to split everything immediately.
