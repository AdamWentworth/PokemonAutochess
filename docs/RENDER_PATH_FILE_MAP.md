# Render Path File Map

Date: 2026-03-12

Use this file to find ownership quickly when working on parity/performance tasks.

## Runtime Route Model
- Gameplay rendering routes are shared-path based for active renderers.
- Legacy `opengl_shared` remains a backward-compat token in preference parsing, canonicalized to `opengl` (`src/game/runtime/video/VideoPreferences.cpp`).
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
- `src/game/runtime/session/SessionCoreBootstrapRuntime.*`
  - core session bootstrap extracted from `GameSession`
  - asset-store selection, RNG seed setup, services/world/state creation, scheduler wiring, and update-graph configuration
- `src/game/runtime/session/SessionBackendInventoryUi.*`
  - backend inventory panel refresh, paging, selection, and hit/input routing
  - session-side adapter between world inventory state and backend UI controls
- `src/game/runtime/ui/*.h`
  - backend menu, HUD, card, inventory, text, and scaling helpers used by game-side UI presentation
  - examples: `CardRenderer.h`, `CardVisuals.h`, `DebugText.h`, `InventoryPanel.h`, `StatusText.h`, `UiScale.h`
  - game-facing backend presentation utilities, distinct from render API backends under `src/engine/render/*`
- `src/game/runtime/render_prep/*.h`
  - API-agnostic render-prep helpers shared by `GameSession`, render model cache decode, and the shared projected path
  - examples: `MaterialShading.h`, `ProceduralPose.h`, `UnitVisuals.h`, `WorldProjection.h`, `WorldProxyGeometry.h`, `MeshNormals.h`
  - material shading math, procedural pose/deformation, world proxy geometry, board projection, mesh-normal generation, and portrait/tint decisions
- `src/game/runtime/session/SessionBackendUnitHydration.*`
  - backend unit model-path hydration, animation-role cache population, and importer-scale correction
  - flyer default application and per-unit backend animation cache setup
- `src/game/runtime/session/SessionDebugSnapshot.*`
  - debug snapshot path resolution, JSON file IO, and summary formatting
  - session/world snapshot metadata parsing for save/load
- `src/game/runtime/session/SessionFrameMetrics.*`
  - session-side publication of per-frame projected/world metrics into `EngineServices`
  - render build breakdown writeback before the shared overlay submit path
- `src/game/runtime/session/SessionLegacyWorldView.*`
  - legacy non-projected board/bench/unit composition used when backend world projection is unavailable
  - board grid, bench panel, fallback portrait sprites, and legacy unit HUD overlay assembly
- `src/game/runtime/session/SessionLoopRuntime.*`
  - session input/dev-pause/fixed-step orchestration extracted from `GameSession`
  - routes resize, hotkeys, backend inventory UI input, and fixed-step backend hydrate timing
- `src/game/runtime/session/SessionProjectedWorldView.*`
  - projected board/bench world composition for the backend world path
  - camera/view-proj setup, projected unit draw dispatch, capture-model bridge, VFX bridge, and depth flush timing
- `src/game/runtime/session/SessionRenderScratch.*`
  - frame-local render scratch ownership for backend debug/world rendering
  - projected-backdrop cache keys, scratch-capacity policy, and per-frame scratch reset/reuse
- `src/game/runtime/session/SessionWorldBackdrop.*`
  - projected board/bench backdrop composition for the backend world view
  - cache-aware backdrop rebuild and reuse policy for projected board geometry
- `src/game/runtime/session/SessionSnapshotRuntime.*`
  - live session save/load orchestration for debug snapshots
  - state-stack restore, runtime flag restore, inventory refresh, and restore-time indexed-world prewarm
- `src/game/runtime/session/SessionStartupRuntime.*`
  - post-bootstrap session startup orchestration extracted from `GameSession`
  - render-model preload, startup asset prewarm, world-layer warmup scheduling/drain, initial menu push, and final init logging
- `src/game/runtime/session/SessionTailFirePrewarm.*`
  - session-side startup prewarm for legacy and authored tail-fire texture assets
  - resolves combined/premultiplied atlases and authored mesh flipbook uploads before first use
- `src/game/runtime/session/SessionTextureCache.*`
  - session-local runtime texture cache population for world/UI/debug paths
  - white fallback texture, procedural sprite atlases, and on-demand image decode/caching
- `src/game/runtime/session/SessionRenderConfig.*`
  - session-local env/config gates for backend prewarm, projected rendering, and snapshot render restore
  - backend model triangle limits and GPU clip-skinning policy used by GameSession render flow
- `src/game/runtime/session/SessionRenderLayout.*`
  - viewport-derived board/UI layout used by `GameSession` render routing
  - board bounds, cell sizing, UI padding, and shared HUD config packaging
- `src/game/runtime/session/SessionWorldRenderRuntime.*`
  - session-side backend world render orchestration extracted from `GameSession`
  - projected vs legacy world path selection, prewarm-only indexed-layer path, frame-metric publication, and shared overlay submit wiring
- `src/game/runtime/renderer/RendererBackendBootstrap.*`
  - Backend creation/fallback
  - Backend capability-to-window API mapping
- `src/game/runtime/renderer/RendererStartupDiagnostics.*`
  - GPU adapter inventory logging
  - backend startup summary logging
- `src/game/runtime/renderer/RuntimeRendererActivation.*`
  - active renderer/GPU identity resolution after backend creation
  - startup summary emission and discrete-GPU requirement evaluation
- `src/game/runtime/renderer/RuntimeRendererRecovery.*`
  - backend creation and OpenGL fallback recovery
  - init-time failure-stage ownership for fallback diagnostics
- `src/game/runtime/renderer/RuntimeRendererStartupState.*`
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
- `src/game/runtime/startup/RuntimeUiCardPrewarm.*`
  - one-frame backend shop-card UI warmup used during startup prewarm
  - portrait filtering and debug/UI submission ownership for first-shop-entry hitch avoidance
- `src/game/runtime/startup/RuntimeWorldLayerPrewarm.*`
  - world/board prewarm scheduling, startup drain, and deferred frame completion
  - init title/progress ownership for world-layer warmup
- `src/game/runtime/video/RuntimeWindowBootstrap.*`
  - initial/fallback window open orchestration
  - startup window GL-context state capture
- `src/game/runtime/RuntimeRestartPolicy.*`
  - launch-time stale restart cleanup
  - post-run restart request consumption
  - restart preference persistence semantics
- `src/game/runtime/loop/RuntimeLoopConfig.*`
  - fixed tick budget env parsing
  - frame-delta clamping and dropped-tick policy
- `src/game/runtime/loop/RuntimeFixedStepPhase.*`
  - fixed-step execution and dropped-tick enforcement
  - per-frame fixed-update breakdown capture
- `src/game/runtime/loop/RuntimeFrameObservation.*`
  - engine-service frame snapshot capture
  - perf sample assembly from frame timings and service counters
- `src/game/runtime/loop/RuntimeFramePerfCapture.*`
  - backend timing/stat interpretation
  - per-frame render/perf metric derivation
- `src/game/runtime/loop/RuntimeLoopControl.*`
  - stop-reason ownership for the main loop
  - SDL quit and auto-quit bookkeeping
- `src/game/runtime/RuntimeOpenGlBootstrap.*`
  - OpenGL function bootstrap for loading/preload paths
  - initial loading-frame presentation and preload pump sequencing
- `src/game/runtime/video/RuntimeSdlEventDispatch.*`
  - SDL event dispatch orchestration
  - resize sync and translated input delivery policy
- `src/game/runtime/loop/RuntimePerfAccumulator.*`
  - rolling perf-window accumulation and averaging
- `src/game/runtime/loop/RuntimePerfLogging.*`
  - `[Perf]` and `[PerfJSON]` formatting
- `src/game/runtime/RuntimeRelaunchLoop.*`
  - outer relaunch loop around restart-on-exit preferences
  - session re-entry ownership outside `GameRunner`
- `src/game/runtime/RuntimeBootLoading.*`
  - preload-abort event policy
  - fallback loading-screen quad layout for backend boot frames
- `src/game/runtime/video/RuntimeSdlInput.*`
  - SDL event translation into `InputEvent`
  - resize-event detection and mouse-coordinate scaling
- `src/game/runtime/video/RuntimeSdlVideoMode.*`
  - SDL fullscreen/windowed transition policy
  - video mode sanitization and current-mode snapshots
  - SDL error/fallback handling for window mode changes
- `src/game/runtime/video/GpuAdapters.*`
  - SDL/DXGI adapter inventory helpers and discrete/integrated classification support
  - preferred GPU matching utilities shared by startup and settings
- `src/game/runtime/video/D3D12Probe.*`
  - startup/settings D3D12 device probe against the selected DXGI adapter
  - preflight capability check used before committing to a D3D12 launch path
- `src/game/runtime/video/VideoPreferences.*`
  - persistent display/backend preference parsing and serialization
  - restart-required display settings contract used by startup and the menu
- `src/game/runtime/video/VideoInitGuards.h`
  - SDL/TTF/window lifecycle guard helpers for video/bootstrap code

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
- `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshSupport.*`
  - reusable projected mesh batching support extracted from the renderer body
  - fast-textured mesh template cache, GPU skin batch state, and fire-mesh override helpers
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
- `src/game/runtime/shared/ui/SharedBackendDebugViewSupport.*`
  - retained overlay cache/hash helpers and item-atlas lookup support for the shared debug overlay
  - debug line trimming, icon UV lookup, and reusable retained-region support types
- `src/game/runtime/shared/world/Shared*.*`
- `src/game/runtime/render_model_cache/RenderModelCache*.*`

### 4) Backend Implementations
OpenGL:
- `src/engine/render/OpenGLRenderBackend.*`
- `src/engine/render/opengl/OpenGLRenderBackend*.cpp`

D3D12:
- `src/engine/render/D3D12RenderBackend.*`
- `src/engine/render/d3d12/D3D12RenderBackend*.cpp`

### 5) Display Settings and Backend Selection UX
- `scripts/states/main_menu.lua`
- `src/game/runtime/video/VideoPreferences.*`
- `config/user/video_settings.json` (runtime preference file)

## High-Impact Touch Points (Current Program)
1. Instrumentation work:
- `GameRunner.cpp`
- backend timestamp support in render backends

2. D3D12 frame pacing work:
- `src/engine/render/d3d12/D3D12RenderBackendLifecycle.cpp`

3. Settings clarity work:
- `scripts/states/main_menu.lua`
- `src/game/runtime/video/VideoPreferences.*`

4. Parity/perf test wiring:
- `tests/TestMain.cpp`
- `CMakeLists.txt` runtime smoke section

## Rule of Thumb
- Shared rendering behavior changes should start in shared runtime modules, not backend-specific branches.
- Backend files should only own API-specific resource/pipeline/submission mechanics.


