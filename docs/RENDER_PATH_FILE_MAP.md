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
- `src/game/runtime/RendererBackendBootstrap.*`
  - Backend creation/fallback
  - Backend capability-to-window API mapping
- `src/game/runtime/RendererStartupDiagnostics.*`
  - GPU adapter inventory logging
  - backend startup summary logging
- `src/game/runtime/RuntimeStartupConfig.*`
  - startup preference/env resolution
  - one-shot boot-menu consumption
  - startup video override parsing
- `src/game/runtime/RuntimeRestartPolicy.*`
  - launch-time stale restart cleanup
  - post-run restart request consumption
  - restart preference persistence semantics

### 2) Runtime Route Policy
- `src/game/runtime/routes/RenderRoutes.h`
- `src/game/runtime/routes/StartupRenderRoutePolicy.h`
- `src/game/runtime/routes/RenderFlowDecisions.h`

### 3) Shared Gameplay Presentation Path
- `src/game/runtime/GameSession.cpp`
- `src/game/runtime/shared/projected/SharedProjected*.*`
- `src/game/runtime/shared/capture/SharedCapture*.*`
- `src/game/runtime/shared/vfx/growl/SharedGrowl*.*`
- `src/game/runtime/shared/vfx/particles/SharedParticle*.*`
- `src/game/runtime/shared/vfx/tail_fire/SharedTailFire*.*`
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
