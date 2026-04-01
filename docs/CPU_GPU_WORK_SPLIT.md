# CPU vs GPU Work Split

Status: Active
Type: Architecture
Last updated: 2026-03-30

Use this document as a current decision guide for render/perf work. It should
describe what the code actually does today, not re-argue the older "make the
GPU matter at all" phase the repo has already moved past.

## Core Rule
- CPU owns gameplay truth, frame orchestration, render-path selection, and
  resource lifetime decisions.
- GPU owns the high-throughput visual work after the CPU has prepared draw
  inputs.
- The projected model path is intentionally hybrid: the CPU still does
  meaningful prep/orchestration work, while the GPU already owns real skinning,
  shading, and raster work on eligible paths.

Do not move authoritative gameplay state to GPU if determinism, replayability,
or future networking correctness would suffer.

## What The Code Does Today

### CPU-owned today
- Gameplay truth and fixed-step orchestration remain CPU-owned:
  - `src/game/runtime/GameRunner.cpp`
  - `src/game/runtime/session/GameSession.cpp`
  - `src/game/runtime/session/Session*.*`
- Scene-pose evaluation is still a CPU job.
  - `src/game/runtime/shared/backend/SharedBackendPoseEval.cpp`
  - `src/game/runtime/shared/projected/unit/SharedProjectedUnitRenderer.cpp`
  - The repo now caches canonicalized scene-pose samples per mesh/clip/time, but
    the pose is still evaluated on CPU before submission.
- Projected model prep and path selection remain CPU-owned.
  - `src/game/runtime/shared/projected/unit/SharedProjectedUnitModelRenderer.cpp`
  - `src/game/runtime/shared/projected/world_scene/SharedProjectedUnitWorldSceneRenderer.cpp`
  - `src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshRenderer.cpp`
  - `src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshPrep.cpp`
- CPU fallback transforms still exist for batches that do not qualify for the
  GPU clip-skinning path.
  - `src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshTransforms.cpp`
- CPU still owns env/config gating and chooses whether GPU clip skinning is
  allowed for the active backend.
  - `src/game/runtime/session/SessionRenderConfig.cpp`

### GPU-owned today
- Both backends already own the final visual work:
  - vertex shading
  - material evaluation
  - texturing
  - depth/stencil/blend/raster
  - present
- GPU clip skinning is real and live, not aspirational.
  - Shared projected prep populates `gpuSkinningMode`, skin payloads, and batch
    state for eligible units/batches.
  - Backend draw code consumes those payloads in:
    - `src/engine/render/opengl/OpenGLRenderBackendWorldDraw.cpp`
    - `src/engine/render/d3d12/D3D12RenderBackendWorldDraw.cpp`
- D3D12 currently has the specialized world-scene fast path.
  - `src/engine/render/D3D12RenderBackend.cpp`
  - `src/engine/render/d3d12/D3D12RenderBackendWorldScene.cpp`
  - It advertises fast-path capabilities such as skinned instancing and
    execute-indirect support through `IRenderBackend::WorldSceneFastPathCaps`.
- OpenGL still benefits from GPU clip skinning on the shared indexed path even
  though the specialized world-scene fast path is currently D3D12-backed.

### Hybrid details that matter
- `SharedProjectedUnitModelRenderer.cpp` first tries the world-scene path, then
  falls back to the legacy/shared backend-mesh path if the fast path is not a
  fit for that unit/material/batch.
- Scene pose is still evaluated on CPU even when the final skin math is pushed
  to GPU.
- The current GPU skin payload is backend-aware:
  - D3D12 can upload paired node-global + inverse-bind data so the vertex
    shader composes the final skin transform on GPU.
  - OpenGL also has a GPU node-global mode, but it travels through the shared
    indexed draw path instead of the D3D12 world-scene specialization.
- Some batches still remain rigid or CPU-rewritten by design.
  - That is why the perf logs track:
    - `projected_shared_rigid_batches`
    - `projected_gpu_clip_skin_batches`
    - `projected_gpu_clip_palette_batches`
    - `projected_cpu_rewrite_batches`
    - `projected_indexed_batches_queued`

## Path Cheat Sheet

### 1. World-scene fast path
- File entry:
  - `src/game/runtime/shared/projected/world_scene/SharedProjectedUnitWorldSceneRenderer.cpp`
- Best description:
  - CPU prepares reusable geometry/material/object handles and per-instance
    scene state; backend submits a more scene-oriented batch of rigid/skinned
    instances.
- Current status:
  - D3D12-specialized path.
- Good fit for:
  - stable fast-textured model batches with valid materials and skinned
    instance state.

### 2. Shared indexed world-batch path
- File entry:
  - `src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshRenderer.cpp`
- Best description:
  - CPU assembles shared indexed batches and GPU still performs final shading
    and eligible skinning.
- Current status:
  - common shared path across active backends.
- Good fit for:
  - most projected model rendering when the world-scene path is unavailable or
    not valid for a given batch.

### 3. CPU rewrite fallback path
- File entry:
  - `src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshTransforms.cpp`
- Best description:
  - CPU resolves/transforms vertex data directly when the batch cannot use the
    eligible GPU clip-skinning route.
- Current status:
  - still necessary, but not the preferred steady-state path.
- Warning sign:
  - if this path grows, `projected_model_prep_ms`, `projected_model_geometry_ms`,
    and `render_build_ms` usually get worse.

## Current Guidance For New Work
- If the work scales with triangle count, vertex count, skinning math, or other
  repeated visual math, it is a GPU candidate.
- If the work is gameplay truth, animation-state choice, path selection, cache
  ownership, or resource lifetime policy, keep it on CPU.
- If the work only moves cost from one CPU container to another without
  reducing `render_build_ms`, it is not a meaningful split improvement.
- If the work moves math to GPU but adds enough upload/setup/synchronization
  overhead to cancel the win, keep the simpler version.
- Prefer shared-path wins before backend-specific specialization unless the
  backend capability boundary is the whole point of the change.
- Use the world-scene fast path when it removes CPU structure cleanly; do not
  force every batch into it if material or batch validation says no.

## Current Hotspots
- The repo is no longer mainly blocked by "CPU gameplay scripting vs GPU."
- The main remaining steady-state render problem is still CPU-side projected
  build work, especially:
  - scene-pose evaluation
  - projected model prep
  - projected model geometry/batch setup
  - indexed/world submission setup
- Startup and first-use stalls are still worth fixing when they are visible, but
  they are secondary to steady-state `render_build_ms`.

## What To Measure Before Changing The Split
- Core frame buckets:
  - `frame_cpu_ms`
  - `render_build_ms`
  - `render_submit_ms`
  - `present_wait_ms`
  - `gpu_frame_ms`
- Projected-model buckets:
  - `projected_pose_eval_ms`
  - `projected_model_prep_ms`
  - `projected_model_geometry_ms`
  - `projected_units_ms`
- Render submit buckets:
  - `render_world_indexed_ms`
  - `render_overlay_prep_ms`
  - `render_ui_submit_ms`
- Path-mix counters:
  - `projected_shared_rigid_batches`
  - `projected_gpu_clip_skin_batches`
  - `projected_gpu_clip_palette_batches`
  - `projected_cpu_rewrite_batches`
  - `projected_indexed_batches_queued`
  - `backend_fast_scene_instances`
  - `backend_fast_scene_palette_upload_bytes`
- Only treat startup/first-use timing as the main metric if the change is
  explicitly about prewarm or hitch removal.

## Runtime Flags That Matter Most
- GPU clip skinning on/off by backend:
  - `PAC_BACKEND_GPU_CLIP_SKINNING`
  - `PAC_BACKEND_GPU_CLIP_SKINNING_OPENGL`
  - `PAC_BACKEND_GPU_CLIP_SKINNING_D3D12`
- Clip-skinning eligibility/throttling:
  - `PAC_BACKEND_CLIP_SKINNING`
  - `PAC_BACKEND_CLIP_SKINNING_ADAPTIVE`
  - `PAC_BACKEND_CLIP_SKINNING_MAX_UNITS`
- Backend-specific GPU skin payload mode:
  - `PAC_BACKEND_D3D12_GPU_SKIN_NODE_GLOBALS`
  - `PAC_BACKEND_OPENGL_GPU_SKIN_NODE_GLOBALS`
- CPU-side deform/procedural vertex behavior:
  - `PAC_BACKEND_VERTEX_DEFORM`
- Fast textured projected path:
  - `PAC_BACKEND_MODEL_FAST_TEXTURED`
- Scene-pose cache cadence:
  - `PAC_BACKEND_SCENE_POSE_CACHE_HZ`
  - `PAC_BACKEND_SCENE_POSE_CACHE_SPARSE_HZ`
  - `PAC_BACKEND_SCENE_POSE_CACHE_DENSE_HZ`
  - `PAC_BACKEND_SCENE_POSE_CACHE_MIN_UNITS`
  - `PAC_BACKEND_SCENE_POSE_CACHE_DENSE_MIN_UNITS`
  - `PAC_BACKEND_SCENE_POSE_CACHE_PREWARM`

Do not start with flag-flipping alone. Start with a measured scene and use the
flags to isolate the path you are studying.

## Current Conclusion
- The repo already has meaningful GPU ownership in the projected model path.
- The main next wins are still about reducing CPU-side projected build
  structure, not about assuming "more GPU" is automatically better.
- GPU offload is worth doing when it measurably lowers steady-state
  `render_build_ms` without causing a worse trade in `gpu_frame_ms`,
  correctness, or maintainability.
- Durable performance lessons belong in `docs/PERF_DECISIONS.md`.
- Detailed experiment history belongs in
  `docs/archive/PERF_EXPERIMENT_LOG_2026-03.md`, not in the main body of this
  document.

## Current Assessment
- The current split is directionally right, but it is not ideal yet.
- The architecture is in a healthier place than the earlier CPU-heavy phase:
  gameplay truth stays on CPU, and both backends already own real GPU-side
  skinning/shading/raster work.
- The main incompleteness is still CPU-side projected render-build work,
  especially scene-pose evaluation, model prep, batch setup, and fallback
  transform paths.
- D3D12 currently has the more specialized world-scene fast path, so the
  backend split is not yet equally mature in the same way on both backends.
- Treat this as "mostly correct architecture, still unfinished optimization"
  rather than a solved area.

## Related Docs
- `docs/GOALS.md`
- `docs/RENDERER_PARITY_ROADMAP.md`
- `docs/PERF_DECISIONS.md`
- `docs/RENDER_PATH_FILE_MAP.md`

