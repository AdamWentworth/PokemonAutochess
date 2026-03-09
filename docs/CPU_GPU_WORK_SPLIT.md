# CPU vs GPU Work Split

Date: 2026-03-08

Goal: keep the CPU/GPU boundary explicit, then describe where the repo sits
right now.

## Core Rule
- CPU owns gameplay truth and frame orchestration.
- GPU owns parallel render math and pixel throughput.

Do not move authoritative gameplay state to GPU if determinism, replayability,
or future networking correctness matters.

## Recommended Split

### CPU
- Gameplay simulation: combat, economy, buffs/debuffs, round flow.
- AI and state machines.
- Input, UI, and scene orchestration.
- Asset IO, decode, cache management, and resource lifetime decisions.
- Animation control flow:
  - clip selection
  - blend/state decisions
  - gameplay-triggered animation events
- Deterministic collision/physics when gameplay depends on it.

### GPU
- Vertex transforms, skinning, and material evaluation.
- Lighting, shadows, depth, blending, and rasterization.
- Post-processing and screen-space passes.
- Large parallel particle or image-processing workloads.
- Optional culling/LOD work when it is truly throughput-bound.

## Current Repo State

### CPU-owned now
- Gameplay truth and fixed-step orchestration:
  - `src/game/runtime/GameSession.cpp`
  - `src/game/runtime/GameRunner.cpp`
- Shared projected render orchestration and draw-list assembly:
  - `src/game/runtime/shared/projected/SharedProjectedUnitRenderer.cpp`
- Projected mesh prep and batch-path selection:
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshPrep.cpp`
- Fallback projected mesh transforms where GPU paths are not eligible:
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshTransforms.cpp`
- Perf aggregation and structured logging:
  - `src/game/runtime/GameRunner.cpp`

### GPU-owned now
- World/model shading on both backends:
  - `src/engine/render/opengl/OpenGLRenderBackendWorldPipeline.cpp`
  - `src/engine/render/d3d12/D3D12RenderBackendPipelines.cpp`
- World draw submission and material/texture binding:
  - `src/engine/render/opengl/OpenGLRenderBackendWorldDraw.cpp`
  - `src/engine/render/d3d12/D3D12RenderBackendWorldDraw.cpp`
- Present, depth/stencil, blend, and raster execution in each backend.
- Particle draw after CPU simulation/upload.

### Hybrid / important nuance
- Gameplay hot paths are now native CPU code, not Lua hot loops.
- GPU clip skinning is available and default-on behind backend gates.
- Shared projected render path still has meaningful CPU cost in:
  - `render_build_ms`
  - `projected_pose_eval_ms`
  - `projected_model_prep_ms`
  - `projected_model_geometry_ms`
- Startup/first-use caches exist to remove user-visible stalls, but they are
  not the main performance strategy anymore.

## Current Performance Guidance
- If work scales with triangle count or per-vertex visual math, prefer GPU.
- If work scales with gameplay entities and must remain deterministic, prefer CPU.
- Prefer removing steady-state `render_build_ms` over shaving small startup-only costs.
- Prefer shared-path improvements over backend-specific ones unless API behavior forces otherwise.
- If a startup optimization risks reintroducing a runtime hitch, keep runtime smoothness.

## Runtime Flags That Matter
- `PAC_BACKEND_GPU_CLIP_SKINNING`
- `PAC_BACKEND_GPU_CLIP_SKINNING_OPENGL`
- `PAC_BACKEND_GPU_CLIP_SKINNING_D3D12`
- `PAC_BACKEND_CLIP_SKINNING`
- `PAC_BACKEND_CLIP_SKINNING_ADAPTIVE`
- `PAC_BACKEND_CLIP_SKINNING_MAX_UNITS`
- `PAC_BACKEND_VERTEX_DEFORM`
- `PAC_BACKEND_MODEL_FAST_TEXTURED`
- `PAC_GLTF_PARITY_STRICT`

## What To Measure
- CPU:
  - `frame_cpu_ms`
  - `fixed_ms`
  - `render_build_ms`
  - projected render buckets
- GPU:
  - `gpu_frame_ms`
  - draw-call / triangle counts
- Cold path:
  - startup and first-use flow trace timing

## Current Conclusion
- The repo is no longer mainly bottlenecked by gameplay scripting.
- The best remaining returns are mostly in shared projected render/build CPU work.
- Cold-path work is still worth doing when it removes an obvious hitch, but it
  is now secondary to steady-state frame time.

## Historical Appendix

Major completed shifts that matter for interpreting the current architecture:
- gameplay hot paths moved out of Lua and into native code
- projected render path added finer perf attribution and scene-pose caching
- fast textured projected path gained more GPU-skinning coverage and shared
  template/static-geometry reuse
- particle/capture/growl/board bridges added more zero-work short-circuits
- first-use hitch work introduced targeted prewarm and persistent caches for
  card art and tail-fire assets

Detailed optimization history belongs in git history and perf notes, not in the
main body of this doc.
