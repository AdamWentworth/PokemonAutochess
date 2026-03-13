# Render Path File Map

Date: 2026-03-12

Use this file to find ownership quickly when working on parity/performance tasks.

## Runtime Route Model
- Gameplay rendering routes are shared-path based for active renderers.
- Legacy `opengl_shared` remains a backward-compat token in preference parsing, canonicalized to `opengl` (`src/game/runtime/VideoPreferences.cpp`).
- Preference parsing still recognizes `vulkan`, but the menu no longer exposes it because the backend is not implemented.

## Ownership Map

### 1) App + Frame Loop + Perf Logging
- `src/game/runtime/GameRunner.cpp`
  - Main loop timing (`fixed`, `render`, `swap`)
  - Perf log line emission (`[Perf]`)
- `src/game/runtime/session/GameSession.*`
  - owns the running gameplay session, render delegation, state stack, and session-local caches
  - main remaining large cleanup target after startup extraction
- `src/game/runtime/session/GameUpdateGraph.*`
  - fixed-step update ordering policy inside the gameplay session
  - phase transition side effects between gameplay systems
- `src/game/runtime/session/SessionBackendRenderHelpers.*`
  - backend-model animation lookup fallbacks, card-label formatting, and world-texture prewarm payload generation
  - shared session-side helpers used by startup model prewarm and backend shop card UI
- `src/game/runtime/session/SessionBackendInventoryUi.*`
  - backend inventory panel refresh, paging, selection, and hit/input routing
  - session-side adapter between world inventory state and backend UI controls
- `src/game/runtime/backend_ui/*.h`
  - backend menu, HUD, card, inventory, text, and scaling helpers used by game-side UI presentation
  - game-facing backend presentation utilities, distinct from render API backends under `src/engine/render/*`
- `src/game/runtime/session/SessionBackendUnitHydration.*`
  - backend unit model-path hydration, animation-role cache population, and importer-scale correction
  - flyer default application and per-unit backend animation cache setup
- `src/game/runtime/session/SessionDebugSnapshot.*`
  - debug snapshot path resolution, JSON file IO, and summary formatting
  - session/world snapshot metadata parsing for save/load
- `src/game/runtime/session/SessionRenderConfig.*`
  - session-local env/config gates for backend prewarm, projected rendering, and snapshot render restore
  - backend model triangle limits and GPU clip-skinning policy used by GameSession render flow
- `src/game/runtime/RendererBackendBootstrap.*`
  - Backend creation/fallback
  - Backend capability-to-window API mapping
- `src/game/runtime/RendererStartupDiagnostics.*`
  - GPU adapter inventory logging
  - backend startup summary logging
- `src/game/runtime/RuntimeRendererActivation.*`
  - active renderer/GPU identity resolution after backend creation
  - startup summary emission and discrete-GPU requirement evaluation
- `src/game/runtime/RuntimeRendererRecovery.*`
  - backend creation and OpenGL fallback recovery
  - init-time failure-stage ownership for fallback diagnostics
- `src/game/runtime/RuntimeRendererStartupState.*`
  - renderer identity capture from backend + GL strings
  - startup GPU/service finalization and summary logging
- `src/game/runtime/startup/RuntimeStartupConfig.*`
  - startup preference/env resolution
  - one-shot boot-menu consumption
  - startup video override parsing
- `src/game/runtime/startup/RuntimeStartupPresentation.*`
  - font subsystem startup result handling
  - default camera creation and initial loading-frame handoff
- `src/game/runtime/startup/RuntimeStartupSession.*`
  - preference load/consume flow for startup
  - startup service state population and adapter inventory capture
- `src/game/runtime/startup/RuntimeStartupVideoOverride.*`
  - startup video override application policy
  - startup video override success/failure messaging
- `src/game/runtime/startup/RuntimeStartupAssetPrewarm.*`
  - world shading, tail-fire, UI sprite, card-art, and backend card-UI startup prewarm orchestration
  - backend card-art proxy selection for eager startup warming
- `src/game/runtime/startup/RuntimeBackendCardUiPrewarm.*`
  - one-frame backend shop-card UI warmup used during startup prewarm
  - portrait filtering and debug/UI submission ownership for first-shop-entry hitch avoidance
- `src/game/runtime/startup/RuntimeWorldLayerPrewarm.*`
  - world/board prewarm scheduling, startup drain, and deferred frame completion
  - init title/progress ownership for world-layer warmup
- `src/game/runtime/RuntimeWindowBootstrap.*`
  - initial/fallback window open orchestration
  - startup window GL-context state capture
- `src/game/runtime/RuntimeRestartPolicy.*`
  - launch-time stale restart cleanup
  - post-run restart request consumption
  - restart preference persistence semantics
- `src/game/runtime/RuntimeLoopConfig.*`
  - fixed tick budget env parsing
  - frame-delta clamping and dropped-tick policy
- `src/game/runtime/RuntimeFixedStepPhase.*`
  - fixed-step execution and dropped-tick enforcement
  - per-frame fixed-update breakdown capture
- `src/game/runtime/RuntimeFrameObservation.*`
  - engine-service frame snapshot capture
  - perf sample assembly from frame timings and service counters
- `src/game/runtime/RuntimeFramePerfCapture.*`
  - backend timing/stat interpretation
  - per-frame render/perf metric derivation
- `src/game/runtime/RuntimeLoopControl.*`
  - stop-reason ownership for the main loop
  - SDL quit and auto-quit bookkeeping
- `src/game/runtime/RuntimeOpenGlBootstrap.*`
  - OpenGL function bootstrap for loading/preload paths
  - initial loading-frame presentation and preload pump sequencing
- `src/game/runtime/RuntimeSdlEventDispatch.*`
  - SDL event dispatch orchestration
  - resize sync and translated input delivery policy
- `src/game/runtime/RuntimePerfAccumulator.*`
  - rolling perf-window accumulation and averaging
- `src/game/runtime/RuntimePerfLogging.*`
  - `[Perf]` and `[PerfJSON]` formatting
- `src/game/runtime/RuntimeRelaunchLoop.*`
  - outer relaunch loop around restart-on-exit preferences
  - session re-entry ownership outside `GameRunner`
- `src/game/runtime/RuntimeBootLoading.*`
  - preload-abort event policy
  - fallback loading-screen quad layout for backend boot frames
- `src/game/runtime/RuntimeSdlInput.*`
  - SDL event translation into `InputEvent`
  - resize-event detection and mouse-coordinate scaling
- `src/game/runtime/RuntimeSdlVideoMode.*`
  - SDL fullscreen/windowed transition policy
  - video mode sanitization and current-mode snapshots
  - SDL error/fallback handling for window mode changes

### 2) Runtime Route Policy
- `src/game/runtime/routes/RenderRoutes.h`
- `src/game/runtime/routes/StartupRenderRoutePolicy.h`
- `src/game/runtime/routes/RenderFlowDecisions.h`

### 3) Shared Gameplay Presentation Path
- `src/game/runtime/session/GameSession.cpp`
- `src/game/runtime/shared/projected/SharedProjected*.*`
- `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshRenderer.cpp`
  - authored fire-mesh UV flipbook override for the Charmander line
  - per-unit fire mesh batch texture/material rewrites
- `src/game/runtime/shared/capture/SharedCapture*.*`
- `src/game/runtime/shared/vfx/growl/SharedGrowl*.*`
- `src/game/runtime/shared/vfx/particles/SharedParticle*.*`
- `src/game/runtime/shared/vfx/tail_fire/SharedTailFireMeshPlayback*.*`
  - authored fire-mesh flipbook spec selection
  - cached fire-submesh detection and startup prewarm inputs
- `src/game/runtime/shared/vfx/tail_fire/SharedTailFire*.*`
- `src/game/world/GameWorldVfx.cpp`
  - legacy tail-fire emitter config and current species filter
- `src/game/runtime/shared/ui/Shared*.*`
- `src/game/runtime/shared/world/Shared*.*`
- `src/game/runtime/backend_model_cache/BackendModelCache*.*`

### 4) Backend Implementations
OpenGL:
- `src/engine/render/OpenGLRenderBackend.*`
- `src/engine/render/opengl/OpenGLRenderBackend*.cpp`

D3D12:
- `src/engine/render/D3D12RenderBackend.*`
- `src/engine/render/d3d12/D3D12RenderBackend*.cpp`

### 5) Display Settings and Backend Selection UX
- `scripts/states/main_menu.lua`
- `src/game/runtime/VideoPreferences.*`
- `config/user/video_settings.json` (runtime preference file)

## High-Impact Touch Points (Current Program)
1. Instrumentation work:
- `GameRunner.cpp`
- backend timestamp support in render backends

2. D3D12 frame pacing work:
- `src/engine/render/d3d12/D3D12RenderBackendLifecycle.cpp`

3. Settings clarity work:
- `scripts/states/main_menu.lua`
- `VideoPreferences.*`

4. Parity/perf test wiring:
- `tests/TestMain.cpp`
- `CMakeLists.txt` runtime smoke section

## Rule of Thumb
- Shared rendering behavior changes should start in shared runtime modules, not backend-specific branches.
- Backend files should only own API-specific resource/pipeline/submission mechanics.

