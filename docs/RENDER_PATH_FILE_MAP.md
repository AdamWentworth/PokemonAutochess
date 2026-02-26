# Render Path File Map

## Status
- Legacy OpenGL gameplay render path is retired.
- Gameplay presentation now runs through the shared-contract path for active renderers.
- In-game renderer API options are:
  - `opengl`
  - `d3d12`
  - `vulkan` (placeholder/not implemented yet)

Compatibility note:
- `opengl_shared` is still accepted as a backward-compatible token in `VideoPreferences` parsing, but it canonicalizes to `opengl`.

## Runtime Ownership

### 1) Game Core (always used)
- `src/game/runtime/GameApp.*`
- `src/game/runtime/GameRunner.*`
- `src/game/runtime/GameRuntime.*`
- `src/game/runtime/GameBootstrap.*`
- `src/game/runtime/GameUpdateGraph.*`
- `src/game/runtime/VideoPreferences.*`

### 2) Shared Gameplay Presentation (OpenGL + D3D12)
- `src/game/runtime/shared/projected/SharedProjected*.*`
- `src/game/runtime/shared/capture/SharedCapture*.*`
- `src/game/runtime/shared/vfx/particles/SharedParticle*.*`
- `src/game/runtime/shared/vfx/growl/SharedGrowl*.*`
- `src/game/runtime/shared/vfx/tail_fire/SharedTailFire*.*`
- `src/game/runtime/shared/ui/Shared*.*`
- `src/game/runtime/shared/world/Shared*.*`
- `src/game/runtime/backend_model_cache/BackendModelCache*.*`
- `src/game/runtime/Backend*.*` helper contracts

### 3) Game Runtime Coordinator
- `src/game/runtime/GameSession.cpp`
  - Route dispatch and frame flow coordinator.
  - No legacy world/HUD branch.

### 4) Game World Simulation + Shared Snapshots
- `src/game/world/GameWorld*.cpp`
- `src/game/world/GameWorldSharedSnapshots.cpp`
  - Shared snapshot builders used by shared capture/VFX render bridges.

### 5) Engine Backend Implementations
- OpenGL backend:
  - `src/engine/render/OpenGLRenderBackend.*`
  - `src/engine/render/opengl/OpenGLRenderBackend*.cpp`
- D3D12 backend:
  - `src/engine/render/D3D12RenderBackend.*`
  - `src/engine/render/d3d12/D3D12RenderBackend*.cpp`

## Removed Legacy Path Artifacts
- Removed from build:
  - `src/game/world/GameWorldRender.cpp`
- Removed route model fields/policies:
  - `legacyRenderPath`
  - `legacyUiPath`
  - legacy HUD flow branching in frame decisions
- Removed legacy backend preference hooks from `IRenderBackend`:
  - `prefersLegacyGameRenderPath()`
  - `prefersLegacyGameUiPath()`

## Current Direction
- Keep one gameplay presentation path (shared contracts).
- Continue housework around module boundaries and file size reduction.
- Keep OpenGL backend implementation as a first-class backend; only legacy gameplay path was retired.
