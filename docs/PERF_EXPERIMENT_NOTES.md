# Performance Experiment Notes

Date: 2026-03-14

Purpose: keep a short record of performance hypotheses that were validated or
rejected in live game captures so the repo does not repeat the same work
without the same context.

## Rules
- Record the target hot bucket before changing code.
- Record whether the optimized path was materially present in the measured scene.
- Keep only lessons that change future decisions.
- Revert misses quickly; do not defend them on theory alone.

## Current Notes

### 2026-03-21: GPU skin batch-state map lookup was a miss
- Status:
  - uncommitted experiment; reverted after live capture regression
- File:
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshRenderer.cpp`
- Hypothesis:
  - replace the per-batch linear scan of `gpuSkinBatchStateEntries()` with
    direct `gpuSkinBatchStateMap()` lookups in the GPU-skinned fast path
- Expected win:
  - reduce `projected_model_prep_ms`
  - reduce steady-state `render_build_ms`
  - possibly reduce `render_submit_ms`
- What the workload showed:
  - the run was functionally correct, but the hot-path structure did not change
  - `projected_gpu_clip_skin_batches` stayed at `4` for 1-unit scenes and `12`
    for 3-unit scenes, so the experiment did not remove meaningful work from
    the measured board
  - `D3D12` 1-unit frames still regressed versus `d668a7d`:
    - `d668a7d`: `render_build_ms ~1.26-1.34`,
      `render_submit_ms ~0.093-0.100`,
      `projected_model_prep_ms ~0.032-0.035`
    - map-lookup run: `render_build_ms ~1.55-1.63`,
      `render_submit_ms ~0.143-0.154`,
      `projected_model_prep_ms ~0.041-0.044`
  - `D3D12` 3-unit frames regressed too:
    - `d668a7d`: `render_build_ms ~1.78-1.91`,
      `render_submit_ms ~0.096-0.100`,
      `projected_model_prep_ms ~0.084-0.086`,
      `projected_model_geometry_ms ~0.454-0.465`
    - map-lookup run: `render_build_ms ~2.20-2.32`,
      `render_submit_ms ~0.153-0.155`,
      `projected_model_prep_ms ~0.114-0.115`,
      `projected_model_geometry_ms ~0.605-0.623`
  - `OpenGL` also regressed:
    - `d668a7d` 1-unit: `render_build_ms ~1.59-1.68`
    - map-lookup run 1-unit: `render_build_ms ~1.95-2.03`
    - `d668a7d` 3-unit: `render_build_ms ~2.45-2.80`
    - map-lookup run 3-unit: `render_build_ms ~3.06-3.18`
  - conclusion:
    the lookup swap alone did not buy enough to offset its cost, and it did not
    move the counters that matter in this workload
- Decision:
  - revert and do not keep this exact map-lookup-only change
- Re-entry conditions:
  - only revisit if it is paired with a change that measurably reduces
    clip-skinned batch count, indexed submit work, or other structural hot-path
    cost in the same scene

### 2026-03-21: vector-owned GPU skin matrix scratch was a miss
- Status:
  - uncommitted experiment; reverted after live capture regression
- Files:
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshSupport.h`
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshSupport.cpp`
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshRenderer.cpp`
  - `src/game/runtime/session/SessionRenderScratch.cpp`
- Hypothesis:
  - replace thread-local `unitSkinMatrices()` hash-map ownership with a small
    vector-owned scratch path that better matches the measured `4/8/12`
    GPU-skinned batch working set
- Expected win:
  - reduce `projected_model_prep_ms`
  - reduce steady-state `render_build_ms`
  - possibly reduce `render_submit_ms`
- What the workload showed:
  - the first version of this experiment was functionally wrong:
    vector-owned skin matrices were cleared per unit while
    `worldIndexedBatches` still held pointers to that data for the rest of the
    frame, which caused cross-unit visual corruption once later units spawned
  - a follow-up fix moved that scratch lifetime to the frame boundary, which
    resolved the corruption, but the corrected run still regressed versus
    `d668a7d`
  - `D3D12` 1-unit frames regressed versus `d668a7d`:
    - `d668a7d`: `render_build_ms ~1.26-1.34`,
      `render_submit_ms ~0.093-0.100`,
      `projected_model_prep_ms ~0.032-0.035`
    - corrected vector-scratch run: `render_build_ms ~1.56-1.67`,
      `render_submit_ms ~0.152-0.165`,
      `projected_model_prep_ms ~0.044-0.047`
  - `D3D12` 3-unit frames also regressed:
    - `d668a7d`: `render_build_ms ~1.78-1.91`,
      `render_submit_ms ~0.096-0.100`,
      `projected_model_prep_ms ~0.084-0.086`,
      `projected_model_geometry_ms ~0.454-0.465`
    - corrected vector-scratch run: `render_build_ms ~2.13-2.36`,
      `render_submit_ms ~0.155-0.158`,
      `projected_model_prep_ms ~0.109-0.111`,
      `projected_model_geometry_ms ~0.542-0.584`
  - `OpenGL` regressed too:
    - `d668a7d` 1-unit: `render_build_ms ~1.59-1.68`
    - corrected vector-scratch run 1-unit: `render_build_ms ~1.88-2.14`
    - `d668a7d` 3-unit: `render_build_ms ~2.45-2.80`
    - corrected vector-scratch run 3-unit: `render_build_ms ~2.96-3.31`
  - draw counts and `projected_gpu_clip_skin_batches` stayed effectively the
    same, so the experiment did not reduce the structural work that dominates
    the scene
  - conclusion:
    this ownership swap added risk and complexity, and even the corrected
    frame-lived version did not beat the existing map-backed path
- Decision:
  - revert and do not retry this exact vector-owned GPU skin matrix ownership
    scheme
- Re-entry conditions:
  - only revisit if a later experiment can keep correct frame lifetime without
    changing ownership semantics visible to `worldIndexedBatches`
  - if this area is revisited later, prefer proving a measurable reduction in
    batch count or indexed submit work instead of just swapping scratch
    containers

### 2026-03-21: transform-ready stamp invalidation was a miss
- Status:
  - uncommitted experiment; reverted after live capture regression
- File:
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshTransforms.cpp`
- Hypothesis:
  - per-frame clears of projected transform-ready arrays were wasting CPU on the
    GPU-skinned fast path, so switching those caches to stamp-based invalidation
    would reduce projected prep cost
- Expected win:
  - reduce `projected_model_prep_ms`
  - reduce steady-state `render_build_ms`
- What the workload showed:
  - the target bucket barely moved, and overall frame cost regressed on both
    backends
  - `D3D12` 1-unit frames regressed versus `d668a7d`:
    - `d668a7d`: `render_build_ms ~1.26-1.34`,
      `render_submit_ms ~0.093-0.100`,
      `projected_model_prep_ms ~0.032-0.035`
    - transform-stamp run: `render_build_ms ~1.54-1.66`,
      `render_submit_ms ~0.157-0.202`,
      `projected_model_prep_ms ~0.038-0.039`
  - `D3D12` 3-unit frames regressed the same way:
    - `d668a7d`: `render_build_ms ~1.78-1.91`,
      `render_submit_ms ~0.096-0.100`,
      `projected_model_prep_ms ~0.084-0.086`
    - transform-stamp run: `render_build_ms ~2.18-2.43`,
      `render_submit_ms ~0.171-0.176`,
      `projected_model_prep_ms ~0.091-0.094`
  - `OpenGL` also regressed:
    - `d668a7d` 1-unit: `render_build_ms ~1.59-1.68`
    - transform-stamp run 1-unit: `render_build_ms ~1.92-2.07`
    - `d668a7d` 3-unit: `render_build_ms ~2.45-2.80`
    - transform-stamp run 3-unit: `render_build_ms ~2.98-3.16`
  - conclusion:
    the cleared ready-arrays were not a meaningful hot cost in the measured
    scene, so removing those clears did not pay for its added complexity
- Decision:
  - revert and do not revisit this exact stamp-based ready-array scheme
- Re-entry conditions:
  - only revisit after instrumenting a much larger transform-cache footprint
    than the current `4/8/12` GPU-skinned batch scene

### 2026-03-21: preserved prepared projected batches were a miss
- Status:
  - uncommitted experiment; reverted after live capture regression
- Files:
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshPrep.cpp`
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshRenderer.cpp`
- Hypothesis:
  - keep template-backed projected indexed batches "warm" across frames by
    skipping repeated shallow template application when metadata already
    matched, and preserve those prepared batches by copying them into
    `worldIndexedBatches` instead of moving them out
- Expected win:
  - reduce `projected_model_prep_ms`
  - reduce `render_build_ms`
  - possibly reduce `render_submit_ms`
- What the workload showed:
  - the saved template-refresh work was too small, and the added hot-path copy
    cost was worse than the work it removed
  - `D3D12` 1-unit frames regressed versus `d668a7d`:
    - `d668a7d`: `render_build_ms ~1.26-1.34`,
      `render_submit_ms ~0.093-0.100`,
      `projected_model_prep_ms ~0.032-0.035`,
      `projected_model_geometry_ms ~0.188-0.196`
    - preserved-batch run: `render_build_ms ~1.67-1.93`,
      `render_submit_ms ~0.173-0.187`,
      `projected_model_prep_ms ~0.043-0.050`,
      `projected_model_geometry_ms ~0.243-0.273`
  - `D3D12` 3-unit frames regressed the same way:
    - `d668a7d`: `render_build_ms ~1.78-1.91`,
      `render_submit_ms ~0.096-0.100`,
      `projected_model_prep_ms ~0.084-0.086`,
      `projected_model_geometry_ms ~0.454-0.465`
    - preserved-batch run: `render_build_ms ~2.37-2.61`,
      `render_submit_ms ~0.179-0.187`,
      `projected_model_prep_ms ~0.111-0.114`,
      `projected_model_geometry_ms ~0.605-0.610`
  - `OpenGL` regressed too:
    - `d668a7d` 1-unit: `render_build_ms ~1.59-1.68`
    - preserved-batch run 1-unit: `render_build_ms ~1.86-2.03`
    - `d668a7d` 3-unit: `render_build_ms ~2.45-2.80`
    - preserved-batch run 3-unit: `render_build_ms ~2.94-3.45`
  - conclusion:
    `WorldIndexedBatch` is too large for this copy-based preservation approach
    to be a win in the measured scene
- Decision:
  - revert and do not retry this exact preserved-batch approach
- Re-entry conditions:
  - only revisit if prepared projected batches can stay move-only
  - if this area is revisited later, prefer a lightweight view/handle design
    over copying whole `WorldIndexedBatch` structs on the hot path

### 2026-03-21: D3D12 per-instance skin-palette instancing was a miss
- Status:
  - uncommitted experiment; reverted after automated heavy-scene regression
- Files:
  - `src/engine/render/IRenderBackend.h`
  - `src/engine/render/D3D12RenderBackend.h`
  - `src/engine/render/d3d12/D3D12RenderBackendCachedWorldMeshes.cpp`
  - `src/engine/render/d3d12/D3D12RenderBackendLifecycle.cpp`
  - `src/engine/render/d3d12/D3D12RenderBackendPipelines.cpp`
  - `src/engine/render/d3d12/D3D12RenderBackendWorldDraw.cpp`
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshRenderer.cpp`
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshSupport.cpp`
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshSupport.h`
  - `src/game/runtime/shared/world/SharedWorldIndexedBatches.cpp`
- Hypothesis:
  - collapse repeated D3D12 clip-skinned draws by batching compatible units
    behind a shared instanced path while keeping skin palettes on the GPU
- Expected win:
  - reduce `render_build_ms`
  - reduce draw-call and descriptor-table churn in dense heavy-board scenes
  - possibly trade a little GPU time for a larger CPU win
- What the workload showed:
  - the path was materially present: `backend_indexed_instanced_draws` lit up
    in the heavy-scene capture
  - repeated-unit draws fell in some heavy frames, but total draw/state work
    did not collapse enough to matter
  - retained heavy-scene baseline:
    - `avg_fps 145.592`
    - `avg_render_build_ms 5.804`
    - `avg_gpu_frame_ms 0.448`
  - instanced skin-palette run:
    - `avg_fps 124.611`
    - `avg_render_build_ms 6.980`
    - `avg_gpu_frame_ms 1.049`
  - conclusion:
    this changed where cost landed, but it did not remove enough upstream CPU
    structure to pay for the added GPU/setup work
- Decision:
  - revert and do not keep this exact D3D12 skin-palette instancing path
- Re-entry conditions:
  - only revisit if it is paired with a larger cut in upstream animation,
    pose, or batch-setup work

### 2026-03-21: D3D12 GPU skin node-global composition was a real win
- Status:
  - local experiment; retained
- Files:
  - `src/engine/render/IRenderBackend.h`
  - `src/engine/render/D3D12RenderBackend.h`
  - `src/engine/render/d3d12/D3D12RenderBackendLifecycle.cpp`
  - `src/engine/render/d3d12/D3D12RenderBackendPipelines.cpp`
  - `src/engine/render/d3d12/D3D12RenderBackendWorldDraw.cpp`
  - `src/game/runtime/session/SessionProjectedWorldView.cpp`
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshRenderer.cpp`
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshSupport.h`
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshTransforms.cpp`
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshTransforms.h`
  - `src/game/runtime/shared/projected/SharedProjectedUnitModelRenderer.h`
  - `src/game/runtime/shared/projected/SharedProjectedUnitRenderer.cpp`
  - `src/game/runtime/shared/projected/SharedProjectedUnitRenderer.h`
  - `src/game/runtime/shared/world/SharedWorldIndexedBatches.cpp`
  - `src/game/runtime/shared/world/SharedWorldIndexedBatches.h`
- Hypothesis:
  - stop precomputing final skin matrices on the CPU for D3D12 clip-skinned
    batches, and instead upload paired `jointGlobal` and `inverseBind`
    matrices so the vertex shader can compose them on the GPU
- Expected win:
  - reduce `projected_units_ms`
  - reduce `projected_model_ms`
  - reduce `render_build_ms` in dense heavy-board scenes without materially
    increasing GPU time
- What the workload showed:
  - the D3D12 heavy-scene benchmark improved materially versus the retained
    pre-change baseline
  - retained baseline:
    - `avg_fps 145.592`
    - `avg_frame_cpu_ms 6.831`
    - `avg_render_build_ms 5.804`
    - `avg_gpu_frame_ms 0.448`
    - `avg_projected_units_ms 2.683`
  - paired global/inverse-bind run:
    - `avg_fps 176.060`
    - `avg_frame_cpu_ms 5.614`
    - `avg_render_build_ms 4.701`
    - `avg_gpu_frame_ms 0.524`
    - `avg_projected_units_ms 1.720`
  - the shorter confirmation run pointed the same way:
    - `benchmark/render_matrix_20260321_212742.json`
      `avg_fps 179.421`, `avg_render_build_ms 4.570`
  - conclusion:
    this is the first GPU-offload change in this area that clearly reduced CPU
    hot-path work enough to justify the trade
- Decision:
  - keep
- Follow-up:
  - if we revisit GPU animation offload again, extend from this paired
    upload/composition path rather than retrying per-instance palette
    instancing first

### 2026-03-21: OpenGL GPU skin node-global composition via skin UBO was a real win
- Status:
  - local experiment; retained
- Files:
  - `src/engine/render/OpenGLRenderBackend.h`
  - `src/engine/render/opengl/OpenGLRenderBackendWorldDraw.cpp`
  - `src/engine/render/opengl/OpenGLRenderBackendWorldPipeline.cpp`
  - `src/game/runtime/session/SessionProjectedWorldView.cpp`
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshRenderer.cpp`
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshSupport.h`
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshTransforms.cpp`
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshTransforms.h`
  - `src/game/runtime/shared/projected/SharedProjectedUnitModelRenderer.h`
  - `src/game/runtime/shared/projected/SharedProjectedUnitRenderer.cpp`
  - `src/game/runtime/shared/projected/SharedProjectedUnitRenderer.h`
  - `src/game/runtime/shared/world/SharedWorldIndexedBatches.cpp`
  - `src/game/runtime/shared/world/SharedWorldIndexedBatches.h`
- Hypothesis:
  - bring the D3D12 paired `jointGlobal` / `inverseBind` composition idea to
    OpenGL, but use a dedicated skin UBO instead of pushing a doubled payload
    through plain matrix uniforms
- Expected win:
  - reduce `projected_units_ms`
  - reduce `projected_model_ms`
  - reduce `render_build_ms` in the heavy auto-loaded board snapshot
- What the workload showed:
  - OpenGL remained CPU-heavy enough to benefit from the same offload direction
  - baseline 20s run:
    - `benchmark/render_matrix_20260321_213705.json`
    - `avg_fps 129.883`
    - `avg_frame_cpu_ms 7.565`
    - `avg_render_build_ms 6.585`
    - `avg_projected_units_ms 2.173`
    - `avg_projected_model_ms 1.792`
  - paired node-global + skin-UBO run:
    - `benchmark/render_matrix_20260321_214551.json`
    - `avg_fps 141.897`
    - `avg_frame_cpu_ms 7.001`
    - `avg_render_build_ms 6.049`
    - `avg_projected_units_ms 1.465`
    - `avg_projected_model_ms 1.081`
  - longer confirmation run stayed ahead of the prior OpenGL 35s capture too:
    - `benchmark/render_matrix_20260321_214635.json`
    - `avg_fps 138.006`
    - `avg_frame_cpu_ms 7.184`
    - `avg_render_build_ms 6.249`
    - `avg_projected_units_ms 1.597`
  - GPU time rose somewhat versus the shorter baseline capture, but the trade
    still favored the CPU-heavy scene we care about
- Decision:
  - keep
- Follow-up:
  - if OpenGL needs another pass later, continue from the skin-UBO path instead
    of returning to `glUniformMatrix4fv` uploads for skinned world meshes

### 2026-03-21: backend-agnostic full-skin shared GPU upload was mixed
- Status:
  - local experiment; not retained as-is
- Hypothesis:
  - stop rebuilding per-batch skin palettes when a whole skin already fits on
    the GPU, let all batches for that unit/skin share one full skin payload,
    and raise the clip-skin matrix ceiling from `64` to `128`
- Expected win:
  - reduce `projected_model_prep_ms`
  - reduce `projected_model_geometry_ms`
  - reduce steady-state `render_build_ms`
- What the workload showed:
  - the broad version clearly helped `D3D12`, but the same setting was not a
    clean win on `OpenGL`
  - short `D3D12` run moved in the right direction:
    - `benchmark/render_matrix_20260321_220010.json`
    - `avg_fps 186.579`
    - `avg_frame_cpu_ms 5.298`
    - `avg_render_build_ms 4.486`
    - `avg_projected_units_ms 1.511`
  - matching short `OpenGL` run was only mixed versus the retained OpenGL
    node-global baseline:
    - `benchmark/render_matrix_20260321_220011.json`
    - `avg_fps 137.688`
    - `avg_frame_cpu_ms 7.239`
    - `avg_render_build_ms 6.234`
    - `avg_projected_units_ms 1.444`
  - conclusion:
    the "share full skin everywhere" idea was worth keeping for the stronger
    backend, but not worth forcing onto `OpenGL` without a clearer measured win
- Decision:
  - do not keep the backend-agnostic version
  - narrow the retained change to the backend where it clearly pays

### 2026-03-21: D3D12-preferred full-skin shared GPU upload was a real win
- Status:
  - local experiment; retained
- Files:
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshSupport.h`
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshSupport.cpp`
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshRenderer.cpp`
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshTransforms.cpp`
  - `src/engine/render/d3d12/D3D12RenderBackendPipelines.cpp`
  - `src/engine/render/d3d12/D3D12RenderBackendWorldDraw.cpp`
  - `src/engine/render/opengl/OpenGLRenderBackendWorldPipeline.cpp`
  - `src/engine/render/opengl/OpenGLRenderBackendWorldDraw.cpp`
- Hypothesis:
  - if a skinned node fits the GPU clip-skin limit, keep authored joint indices
    intact, upload one full skin payload per unit/skin, and let all matching
    batches share that payload instead of repacking palette subsets on the CPU
- Expected win:
  - reduce `projected_model_prep_ms`
  - reduce `projected_model_geometry_ms`
  - reduce `projected_units_ms`
  - lower `render_build_ms` further on the heavy auto-loaded board snapshot
- What the workload showed:
  - `D3D12` improved clearly over the previous retained GPU node-global baseline
  - previous retained `D3D12` baseline:
    - `benchmark/render_matrix_20260321_212834.json`
    - `avg_fps 176.060`
    - `avg_frame_cpu_ms 5.614`
    - `avg_render_build_ms 4.701`
    - `avg_projected_units_ms 1.720`
  - retained full-skin run:
    - `benchmark/render_matrix_20260321_220350.json`
    - `avg_fps 203.560`
    - `avg_frame_cpu_ms 4.838`
    - `avg_render_build_ms 4.125`
    - `avg_projected_units_ms 1.369`
    - `avg_projected_model_ms 0.934`
  - the narrowed version kept `OpenGL` in-family instead of regressing it:
    - `benchmark/render_matrix_20260321_220442.json`
    - `avg_fps 141.132`
    - `avg_frame_cpu_ms 7.001`
    - `avg_render_build_ms 6.040`
    - `avg_projected_units_ms 1.500`
- Decision:
  - keep
- Follow-up:
  - if we want another GPU-first pass after this, the next logical step is to
    attack the remaining per-batch/model-prep work or move more animation
    sampling itself onto the GPU
  - keep validating `OpenGL` separately when we widen any future D3D12-focused
    clip-skin changes

### 2026-03-21: OpenGL indexed dynamic-uniform payload caching was a miss
- Hypothesis:
  - OpenGL heavy-scene indexed world draws were still paying too much repeated
    `glUniform*` cost for camera/material state, so caching the last dynamic
    world-program payload across sorted indexed draws would lower
    `render_world_indexed_ms` and total `render_build_ms`
- Expected win:
  - reduce `render_world_indexed_ms`
  - reduce `render_build_ms`
  - keep `OpenGL` closer to the retained `D3D12` direction without touching the
    already-kept skin-UBO path
- What the workload showed:
  - the extra compare/copy work was worse than just reissuing the uniforms on
    this scene
  - retained `OpenGL` baseline:
    - `benchmark/render_matrix_20260321_220442.json`
    - `avg_fps 141.132`
    - `avg_frame_cpu_ms 7.001`
    - `avg_render_build_ms 6.040`
    - `avg_gpu_frame_ms 0.559`
    - `avg_projected_units_ms 1.500`
    - `avg_projected_model_ms 1.118`
  - cached-uniform run:
    - `benchmark/render_matrix_20260321_221450.json`
    - `avg_fps 125.583`
    - `avg_frame_cpu_ms 7.919`
    - `avg_render_build_ms 6.859`
    - `avg_gpu_frame_ms 0.652`
    - `avg_projected_units_ms 1.676`
    - `avg_projected_model_ms 1.238`
  - the indexed draw count stayed effectively identical, so the additional CPU
    bookkeeping did not buy enough driver-side savings to matter
- Decision:
  - revert; do not assume repeated OpenGL dynamic uniform uploads are the next
    dominant cost in the heavy snapshot
- Follow-up:
  - prefer attacking OpenGL work that can actually remove per-draw submission or
    resource churn rather than adding another CPU-side state cache
  - keep future OpenGL passes focused on measured hot buckets like indexed
    submission structure or texture/buffer churn

### 2026-03-21: OpenGL world-texture lookup and fallback/env handle caching was a real win
- Hypothesis:
  - the heavy OpenGL indexed path was still spending too much CPU time resolving
    cached texture handles and re-running fallback / neutral-PMREM texture
    lookup work every draw, even when the actual GL bind count was unchanged
- Expected win:
  - reduce `render_world_indexed_ms`
  - reduce `render_build_ms`
  - reduce steady-state frame CPU cost on the heavy auto-loaded board snapshot
- What the workload showed:
  - this was a clear retained win
  - retained `OpenGL` baseline:
    - `benchmark/render_matrix_20260321_220442.json`
    - `avg_fps 141.132`
    - `avg_frame_cpu_ms 7.001`
    - `avg_render_build_ms 6.040`
    - `avg_gpu_frame_ms 0.559`
    - `avg_projected_units_ms 1.500`
    - `avg_projected_model_ms 1.118`
  - new cached-handle run:
    - `benchmark/render_matrix_20260321_222447.json`
    - `avg_fps 172.848`
    - `low_1pct_fps 157.912`
    - `avg_frame_cpu_ms 5.748`
    - `avg_render_build_ms 4.909`
    - `avg_gpu_frame_ms 1.158`
    - `avg_projected_units_ms 1.422`
    - `avg_projected_model_ms 1.058`
  - representative scored samples moved `render_world_indexed_ms` from roughly
    `2.33-2.43` down to roughly `1.40-1.60`
  - `backend_gl_texture_bind_calls` stayed effectively unchanged at `202`, so
    the win came from removing CPU-side texture resolution / fallback lookup
    churn rather than from reducing GL bind count directly
- Decision:
  - keep
- Follow-up:
  - if we want another OpenGL pass after this, keep targeting the indexed hot
    path, but prefer changes that remove repeated CPU-side resource resolution
    or submission work rather than caching large dynamic uniform payloads

### 2026-03-21: OpenGL per-material texture resolve-entry caching was a miss
- Status:
  - uncommitted experiment; reverted after heavy-scene regression
- Files:
  - `src/engine/render/OpenGLRenderBackend.h`
  - `src/engine/render/opengl/OpenGLRenderBackendWorldDraw.cpp`
- Hypothesis:
  - after the retained OpenGL world-texture handle caching win, the indexed hot
    path might still be spending too much CPU time repeatedly resolving the
    same per-material texture IDs across adjacent sorted draws
- Expected win:
  - reduce `render_world_indexed_ms`
  - reduce `render_build_ms`
  - hold draw counts and bind counts roughly flat while trimming remaining
    CPU-side texture-cache lookup work
- What the workload showed:
  - the extra resolver/cache bookkeeping cost more than the saved lookups in
    the heavy snapshot
  - retained `OpenGL` baseline:
    - `benchmark/render_matrix_20260321_222447.json`
    - `avg_fps 172.848`
    - `avg_frame_cpu_ms 5.748`
    - `avg_render_build_ms 4.909`
    - `avg_gpu_frame_ms 1.158`
    - `avg_projected_units_ms 1.422`
    - `avg_projected_model_ms 1.058`
  - regressed resolve-cache run:
    - `benchmark/render_matrix_20260321_223512.json`
    - `avg_fps 122.847`
    - `avg_frame_cpu_ms 8.067`
    - `avg_render_build_ms 6.830`
    - `avg_gpu_frame_ms 0.924`
    - `avg_projected_units_ms 1.976`
    - `avg_projected_model_ms 1.449`
  - `backend_gl_texture_bind_calls` stayed at `202`, and
    `render_world_indexed_ms` was still roughly `1.9-2.3 ms`, so the extra
    CPU-side cache logic did not remove enough real indexed submission work
- Decision:
  - revert; do not keep another OpenGL texture-resolution cache layer on top of
    the retained handle/fallback/env caching win
- Follow-up:
  - keep future OpenGL passes focused on work that can remove per-draw state
    submission or uniform setup rather than adding more lookup bookkeeping

### 2026-03-21: OpenGL indexed frame/camera UBO migration was a miss
- Status:
  - uncommitted experiment; reverted after a controlled heavy-scene retry
- Files:
  - `src/engine/render/OpenGLRenderBackend.h`
  - `src/engine/render/opengl/OpenGLRenderBackendWorldPipeline.cpp`
  - `src/engine/render/opengl/OpenGLRenderBackendWorldDraw.cpp`
- Hypothesis:
  - after the broken frame+material UBO pass, a smaller OpenGL change that
    moved only indexed frame/camera data into a shared `WorldFrameBlock` UBO
    might trim repeated uniform submission without disturbing the retained
    material path
- Expected win:
  - reduce indexed uniform setup cost
  - reduce `render_build_ms`
  - keep draw counts and texture-bind behavior aligned with the retained
    OpenGL baseline
- What the workload showed:
  - this retry was visually stable, but still slower than the retained
    baseline
  - retained `OpenGL` baseline:
    - `benchmark/render_matrix_20260321_222447.json`
    - `avg_fps 172.848`
    - `low_1pct_fps 157.912`
    - `avg_frame_cpu_ms 5.748`
    - `avg_render_build_ms 4.909`
    - `avg_gpu_frame_ms 1.158`
    - `avg_projected_units_ms 1.422`
    - `avg_projected_model_ms 1.058`
  - controlled frame-block retry:
    - `benchmark/render_matrix_20260321_230211.json`
    - `avg_fps 149.732`
    - `low_1pct_fps 112.621`
    - `avg_frame_cpu_ms 6.629`
    - `avg_render_build_ms 5.611`
    - `avg_gpu_frame_ms 0.540`
    - `avg_projected_units_ms 1.680`
    - `avg_projected_model_ms 1.238`
  - unlike the earlier broken UBO pass, the indexed path stayed healthy here:
    - `avg_draw_calls 58`
    - `backend_gl_texture_bind_calls 202`
  - so this was a clean perf miss, not another rendering failure
- Decision:
  - revert; do not keep the frame/camera UBO migration on the current OpenGL
    path
- Follow-up:
  - treat this as evidence that OpenGL uniform transport is not the next
    attractive bottleneck
  - prefer future OpenGL work that removes real indexed submission, state
    churn, or texture/buffer work instead of repackaging already-cheap frame
    uniforms

### 2026-03-21: OpenGL full-skin shared GPU upload retry was a miss
- Status:
  - local experiment; not retained
- Files:
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshSupport.cpp`
- Hypothesis:
  - now that the OpenGL path has the retained skin-UBO and texture-handle
    caching wins, retry the earlier "prefer full shared GPU skin payloads"
    policy on OpenGL and see if it now pays back
- Expected win:
  - reduce `projected_model_prep_ms`
  - reduce `projected_model_geometry_ms`
  - reduce steady-state `render_build_ms`
- What the workload showed:
  - the retry still moved the current heavy snapshot in the wrong direction
  - fresh retained OpenGL baseline for the current local snapshot:
    - `benchmark/render_matrix_20260321_230951.json`
    - `avg_fps 145.068`
    - `avg_frame_cpu_ms 6.906`
    - `avg_render_build_ms 5.904`
    - `avg_projected_units_ms 1.691`
    - `avg_projected_model_ms 1.240`
  - retry run:
    - `benchmark/render_matrix_20260321_232349.json`
    - `avg_fps 134.533`
    - `avg_frame_cpu_ms 7.375`
    - `avg_render_build_ms 6.216`
    - `avg_projected_units_ms 1.784`
    - `avg_projected_model_ms 1.281`
- Decision:
  - revert; do not widen the full-skin shared upload policy to OpenGL on the
    current path
- Follow-up:
  - keep OpenGL on the retained skin-UBO path only
  - if we revisit OpenGL full-skin sharing later, require a stronger measured
    scene-specific reason than "it helped D3D12"

### 2026-03-21: D3D12 world-shader skin ceiling alignment was a small win
- Status:
  - local experiment; retained for now
- Files:
  - `src/engine/render/d3d12/D3D12RenderBackendPipelines.cpp`
- Hypothesis:
  - the D3D12 shared world vertex shader was still clamping joint indices to
    `64` even though the retained shared upload path and CPU-side limits
    already support `128`, so aligning the shader ceiling might keep more
    authored skin data on the intended GPU path
- Expected win:
  - reduce `projected_units_ms`
  - reduce `render_build_ms`
  - stay correctness-aligned with the existing `128`-joint CPU/shared-upload
    path
- What the workload showed:
  - the change did not alter aggregate batch counts on the current heavy scene,
    so this is not a dramatic coverage expansion
  - it still stayed ahead of the fresh retained D3D12 baseline in the longer
    confirmation run:
    - baseline:
      - `benchmark/render_matrix_20260321_231107.json`
      - `avg_fps 153.169`
      - `avg_frame_cpu_ms 6.413`
      - `avg_render_build_ms 5.419`
      - `avg_projected_units_ms 1.871`
      - `avg_projected_model_ms 1.336`
    - confirmation:
      - `benchmark/render_matrix_20260321_232904.json`
      - `avg_fps 160.221`
      - `avg_frame_cpu_ms 6.135`
      - `avg_render_build_ms 5.204`
      - `avg_projected_units_ms 1.755`
      - `avg_projected_model_ms 1.241`
- Decision:
  - keep; this removes a backend mismatch and measured as a modest positive on
    the current heavy snapshot
- Follow-up:
  - treat this as a small D3D12-only cleanup, not a major new baseline shift
  - if we later find assets with more than `64` live skin joints in the hot
    path, remeasure because this change should matter more there

### 2026-03-22: indexed rigid-node GPU transform reuse was a real win
- Status:
  - local experiment; retained for now
- Files:
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshRenderer.cpp`
- Hypothesis:
  - the fast indexed textured path was still paying CPU cost to rewrite
    rigidly attached submesh vertices into transformed space even though both
    backends already have a shared GPU skin-matrix path
  - if we reuse that path for rigid-node transforms by treating the owning node
    as a one-joint palette entry, we can keep the same indexed submission shape
    while moving more per-vertex transform work onto the GPU
- Expected win:
  - reduce `projected_model_geometry_ms`
  - reduce `projected_model_ms`
  - reduce `projected_units_ms`
  - reduce steady-state `render_build_ms`
- What the workload showed:
  - heavy-scene benchmark improved on both backends for the current local
    snapshot while keeping the same broad draw/batch shape
  - `D3D12` improved clearly over the previous retained baseline:
    - baseline:
      - `benchmark/render_matrix_20260321_232904.json`
      - `avg_fps 160.221`
      - `avg_frame_cpu_ms 6.135`
      - `avg_render_build_ms 5.204`
      - `avg_projected_units_ms 1.755`
      - `avg_projected_model_ms 1.241`
    - confirmation:
      - `benchmark/render_matrix_20260322_115900.json`
      - `avg_fps 206.514`
      - `avg_frame_cpu_ms 4.769`
      - `avg_render_build_ms 4.092`
      - `avg_projected_units_ms 1.439`
      - `avg_projected_model_ms 1.017`
  - `OpenGL` moved in the same direction from the fresh current-snapshot
    baseline:
    - baseline:
      - `benchmark/render_matrix_20260321_230951.json`
      - `avg_fps 145.068`
      - `avg_frame_cpu_ms 6.906`
      - `avg_render_build_ms 5.904`
      - `avg_projected_units_ms 1.691`
      - `avg_projected_model_ms 1.268`
    - confirmation:
      - `benchmark/render_matrix_20260322_115900.json`
      - `avg_fps 168.517`
      - `avg_frame_cpu_ms 5.857`
      - `avg_render_build_ms 4.961`
      - `avg_projected_units_ms 1.525`
      - `avg_projected_model_ms 1.096`
  - draw-call and indexed-batch counts stayed essentially flat, which matches
    the intent: this was not a draw-count reduction, it was a per-batch
    transform-offload win inside the existing indexed path
  - current perf counters still report this reused path under the existing
    GPU-skin batch metrics, so `projected_gpu_clip_skin_batches` /
    `projected_gpu_clip_palette_batches` now cover both authored clip-skin work
    and this rigid-node GPU-transform offload
- Decision:
  - keep; this is a meaningful CPU-to-GPU offload on the hot indexed path and
    it measured as a clear win on both retained backends
- Follow-up:
  - add a distinct rigid-node GPU-transform counter later if we want cleaner
    perf attribution than the existing clip-skin metrics provide
  - do a quick manual visual pass before treating this as a long-lived baseline
    commit

### 2026-03-22: cached rigid-node GPU template reuse was a miss
- Status:
  - local experiment; reverted
- Files:
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshSupport.h`
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshSupport.cpp`
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshRenderer.cpp`
- Hypothesis:
  - after moving rigid-node transforms onto the GPU, the remaining CPU work on
    that path might be the per-frame rebuild of rigid-node vertex buffers
  - if the fast textured cache also carried a prebuilt rigid-node GPU template,
    clip-driven units could reuse shared vertices and only upload the current
    node transform
- Expected win:
  - reduce `projected_model_prep_ms`
  - reduce `projected_model_geometry_ms`
  - reduce steady-state `render_build_ms`
- What the workload showed:
  - this went the wrong way on both backends in the current heavy snapshot
  - the batch shape stayed flat, but the cached rigid-GPU-template path was not
    cheaper than the retained shared-node-transform submission it displaced
  - retained baseline before this retry:
    - `benchmark/render_matrix_20260322_115900.json`
    - `OpenGL`: `avg_fps 168.517`, `avg_frame_cpu_ms 5.857`,
      `avg_render_build_ms 4.961`
    - `D3D12`: `avg_fps 206.514`, `avg_frame_cpu_ms 4.769`,
      `avg_render_build_ms 4.092`
  - regressed retry:
    - `benchmark/render_matrix_20260322_121017.json`
    - `OpenGL`: `avg_fps 133.036`, `avg_frame_cpu_ms 7.389`,
      `avg_render_build_ms 6.273`
    - `D3D12`: `avg_fps 160.106`, `avg_frame_cpu_ms 6.156`,
      `avg_render_build_ms 5.192`
- Decision:
  - revert; do not keep the cached rigid-node GPU template path
- Follow-up:
  - treat the retained rigid-node GPU-transform win as good enough by itself
  - if we revisit this area, instrument why the shared-node-transform path is
    still cheaper than the cached rigid-GPU-template variant before changing it

### 2026-03-22: revisiting full GPU skin sharing on OpenGL was still a miss
- Status:
  - local experiment; reverted
- Files:
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshSupport.cpp`
- Hypothesis:
  - now that OpenGL is on the retained skin-UBO path, it might finally benefit
    from the same "full skin on GPU" policy D3D12 keeps instead of rebuilding
    per-batch joint palettes on the CPU
- Expected win:
  - reduce `projected_model_prep_ms`
  - reduce `render_build_ms`
  - collapse `projected_gpu_clip_palette_batches` toward zero on OpenGL
- What the workload showed:
  - the policy change did remove palette batches, but the indexed path still got
    slower overall
  - current-snapshot baseline before the retry:
    - `benchmark/render_matrix_20260322_121843.json`
    - `OpenGL`: `avg_fps 138.053`, `avg_frame_cpu_ms 7.153`,
      `avg_render_build_ms 6.002`, `avg_projected_model_ms 1.296`
  - regressed retry:
    - `benchmark/render_matrix_20260322_121948.json`
    - `OpenGL`: `avg_fps 134.091`, `avg_frame_cpu_ms 7.319`,
      `avg_render_build_ms 6.121`, `avg_projected_model_ms 1.198`
  - detailed counter check from `benchmark/render_matrix_20260322_121948_raw/opengl_1280x720.log`:
    - `projected_gpu_clip_palette_batches` did fall to `0`
    - but `render_world_indexed_ms` climbed into the `~1.74-1.87 ms` range and
      outweighed the smaller prep-side savings
- Decision:
  - revert; keep full-GPU-skin preference D3D12-only for now
- Follow-up:
  - if we want another OpenGL-specific offload, it should target the indexed
    submit path directly instead of just replacing palette packing with larger
    skin uploads

### 2026-03-22: D3D12 frame-local skin upload reuse was a miss
- Status:
  - local experiment; reverted
- Files:
  - `src/engine/render/D3D12RenderBackend.h`
  - `src/engine/render/d3d12/D3D12RenderBackendLifecycle.cpp`
  - `src/engine/render/d3d12/D3D12RenderBackendWorldDraw.cpp`
- Hypothesis:
  - the heavy scene already shares full GPU skin payloads logically, but the
    D3D12 draw path only reused an uploaded skin buffer when matching batches
    were consecutive
  - a frame-local cache keyed by the existing skin payload pointer, mode, and
    count should let non-consecutive matching batches reuse the same GPU upload
    and avoid repeated CPU-side copies during submit
- Expected win:
  - reduce `avg_render_submit_ms`
  - slightly reduce `avg_frame_cpu_ms`
  - keep `avg_render_build_ms` flat
- What the workload showed:
  - this did not help on the current heavy snapshot
  - A/B on the same machine state:
    - patched run:
      - `benchmark/render_matrix_20260322_122737.json`
      - `D3D12`: `avg_fps 180.993`, `avg_frame_cpu_ms 5.420`,
        `avg_render_build_ms 4.613`, `avg_projected_units_ms 1.492`
    - reverted run:
      - `benchmark/render_matrix_20260322_122944.json`
      - `D3D12`: `avg_fps 183.033`, `avg_frame_cpu_ms 5.346`,
        `avg_render_build_ms 4.562`, `avg_projected_units_ms 1.427`
- Decision:
  - revert; do not keep the frame-local D3D12 skin upload cache
- Follow-up:
  - if we revisit D3D12 skin uploads, the next step should be a larger payload
    split such as static inverse-bind resources plus dynamic joint globals,
    rather than caching repeated copies of the current packed upload

### 2026-03-22: D3D12 static inverse-bind buffer split was a miss
- Status:
  - local experiment; reverted
- Files:
  - `src/engine/render/D3D12RenderBackend.h`
  - `src/engine/render/d3d12/D3D12RenderBackendCachedWorldMeshes.cpp`
  - `src/engine/render/d3d12/D3D12RenderBackendPipelines.cpp`
  - `src/engine/render/d3d12/D3D12RenderBackendWorldDraw.cpp`
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshSupport.cpp`
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshSupport.h`
- Hypothesis:
  - keep inverse-bind data resident in a static GPU buffer and upload only
    dynamic joint globals each frame for D3D12 clip-skinned batches
  - this should reduce repeated skin payload upload work without changing the
    higher-level batch structure
- Expected win:
  - reduce `avg_render_submit_ms`
  - slightly reduce `avg_frame_cpu_ms`
  - keep `avg_render_build_ms` flat or slightly lower
- What the workload showed:
  - this did not outperform the protected Phase 1 checkpoint on the current
    heavy snapshot
  - patched runs:
    - `benchmark/render_matrix_20260322_144511.json`
      - `avg_fps 170.824`
      - `avg_frame_cpu_ms 5.770`
      - `avg_render_build_ms 4.877`
    - `benchmark/render_matrix_20260322_144610.json`
      - `avg_fps 170.372`
      - `avg_frame_cpu_ms 5.791`
      - `avg_render_build_ms 4.910`
  - conclusion:
    inverse-bind transport is not the remaining big lever; moving that data to
    a static GPU resource did not remove enough real CPU work in this renderer
- Decision:
  - revert; do not keep the D3D12 static inverse-bind split
- Follow-up:
  - target a different structural bottleneck next, likely indexed submission
    work rather than skin payload transport

### 2026-03-22: projected submission-identity cache was a mixed miss
- Status:
  - local experiment; reverted
- Files:
  - `src/game/runtime/shared/projected/SharedProjectedRenderItems.cpp`
  - `src/game/runtime/shared/projected/SharedProjectedRenderItems.h`
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshRenderer.cpp`
  - `src/game/runtime/shared/world/SharedWorldIndexedBatches.cpp`
  - `src/game/runtime/shared/world/SharedWorldIndexedBatches.h`
- Hypothesis:
  - projected render items already persist stable material, texture, and
    geometry identity, so indexed submission should be able to consume that
    cached identity instead of rebuilding `SubmissionSortKey` state from
    `WorldIndexedBatch` every frame
- Expected win:
  - reduce `avg_render_build_ms`
  - reduce CPU time in indexed submission
  - help both backends by removing repeated string-resolution and sort-key
    reconstruction work on projected model batches
- What the workload showed:
  - checkpoint baseline on the same snapshot/settings:
    - `benchmark/render_matrix_20260322_153004.json`
      - `OpenGL`: `avg_fps 164.716`, `avg_frame_cpu_ms 6.023`,
        `avg_render_build_ms 5.097`
      - `D3D12`: `avg_fps 198.691`, `avg_frame_cpu_ms 4.953`,
        `avg_render_build_ms 4.188`
  - broad submission-cache slice:
    - `benchmark/render_matrix_20260322_153343.json`
      - `OpenGL`: `avg_fps 169.896`, `avg_frame_cpu_ms 5.833`,
        `avg_render_build_ms 4.963`
      - `D3D12`: `avg_fps 185.385`, `avg_frame_cpu_ms 5.320`,
        `avg_render_build_ms 4.519`
  - OpenGL-only salvage attempt:
    - `benchmark/render_matrix_20260322_153819.json`
      - `OpenGL`: `avg_fps 154.205`, `avg_frame_cpu_ms 6.432`,
        `avg_render_build_ms 5.494`
      - `D3D12`: `avg_fps 189.603`, `avg_frame_cpu_ms 5.200`,
        `avg_render_build_ms 4.443`
  - conclusion:
    the broad version gave OpenGL a small win but clearly hurt D3D12, and the
    backend-gated salvage was worse overall
  - the direction still makes sense conceptually, but this implementation did
    not pay back enough real submission work to justify the extra cache upkeep
- Decision:
  - revert; do not keep the projected submission-identity cache in its current
    form
- Follow-up:
  - if we revisit submission-side restructuring, target the actual opaque
    grouping/sort mechanics first rather than only changing how submission
    identity is reconstructed

### 2026-03-20: retained debug-geometry GPU caching was a real win
- Commit:
  - `f4b5eb3` `Cache retained debug geometry on GPU`
- Hypothesis:
  - cached retained debug/UI overlay regions were still paying too much per-frame
    submit cost because stable quad and line geometry was being re-expanded and
    re-uploaded every frame
- Expected win:
  - reduce `render_ui_submit_ms`
  - reduce steady-state `render_build_ms`
- What the workload showed:
  - this was a real, repeatable win in comparable live captures
  - `D3D12` 1-unit frames improved:
    - before: `render_ui_submit_ms ~0.55-0.58`,
      `render_build_ms ~1.5-1.6`
    - retained-geometry run: `render_ui_submit_ms ~0.26-0.31`,
      `render_build_ms ~1.25-1.32`
  - `D3D12` 3-unit frames improved:
    - before: `render_ui_submit_ms ~0.65-0.68`,
      `render_build_ms ~2.1-2.3`
    - retained-geometry run: `render_ui_submit_ms ~0.29-0.31`,
      `render_build_ms ~1.76-1.90`
  - `OpenGL` improved in the same direction
  - `render_world_indexed_ms` stayed mostly flat, which matched the hypothesis:
    this change helped retained UI/debug submission, not indexed world work
- Decision:
  - keep; this is a confirmed perf win

### 2026-03-20: revision-gated overlay prep was a miss after the retained-geometry win
- Commit:
  - `f4afd43` `Gate overlay prep on source revisions`
- Hypothesis:
  - remaining `render_overlay_prep_ms` cost came from re-deriving retained
    inventory/log/roster source state every frame, so revision-gating those
    sources would lower overlay prep and total build time
- Expected win:
  - reduce `render_overlay_prep_ms`
  - reduce steady-state `render_build_ms`
- What the workload showed:
  - `render_overlay_prep_ms` improved slightly, but not enough to matter
  - overall frame build cost was flat to worse in the comparable gameplay scene
  - `D3D12` 1-unit frames regressed versus the retained-geometry baseline:
    - retained-geometry baseline: `render_build_ms ~1.25-1.32`,
      `render_overlay_prep_ms ~0.27-0.30`,
      `render_ui_submit_ms ~0.26-0.31`
    - revision-gated run: `render_build_ms ~1.39-1.47`,
      `render_overlay_prep_ms ~0.29-0.31`,
      `render_ui_submit_ms ~0.33-0.35`
  - `D3D12` 3-unit frames showed the same pattern:
    - retained-geometry baseline: `render_build_ms ~1.76-1.90`,
      `render_overlay_prep_ms ~0.33`,
      `render_ui_submit_ms ~0.29-0.31`
    - revision-gated run: `render_build_ms ~1.92-2.11`,
      `render_overlay_prep_ms ~0.29-0.30`,
      `render_ui_submit_ms ~0.33-0.34`
  - `OpenGL` did not show a clear end-to-end win either
  - conclusion:
    some CPU overlay-prep work was removed, but the remaining dynamic work and
    the added invalidation/caching machinery did not pay back enough in this
    workload
- Decision:
  - revert if the goal is a clean perf branch; do not keep this complexity on
    theory alone
- Re-entry conditions:
  - only revisit after breaking `render_overlay_prep_ms` down by source/region
  - only revisit if a measured scene shows overlay prep as a materially larger
    bucket than `~0.3ms`
  - separate dynamic perf-overlay cost from retained-region cost before trying
    more invalidation plumbing

### 2026-03-14: D3D12 indexed binding-state reuse was a miss
- Hypothesis:
  - reuse D3D12 indexed-world root state across batched draws so cached opaque
    scenes stop rebinding the same PSO and the same descriptor tables on every
    indexed submission
- Expected win:
  - reduce `render_world_indexed_ms`
  - reduce `render_submit_ms`
  - possibly lower steady-state `render_build_ms`
- What the workload showed:
  - the change reduced the intended counters but did not improve the measured
    hot buckets
  - `D3D12` 1-unit indexed frames regressed:
    - before: `render_world_indexed_ms ~0.08`,
      `render_submit_ms ~0.08-0.10`,
      `render_build_ms ~1.5-1.6`
    - binding-reuse run: `render_world_indexed_ms ~0.10`,
      `render_submit_ms ~0.12-0.15`,
      `render_build_ms ~1.8-1.9`
  - `D3D12` 3-unit indexed frames also regressed:
    - before: `render_world_indexed_ms ~0.18-0.20`,
      `render_submit_ms ~0.08-0.10`,
      `render_build_ms ~2.1-2.3`
    - binding-reuse run: `render_world_indexed_ms ~0.25-0.28`,
      `render_submit_ms ~0.13-0.15`,
      `render_build_ms ~2.5-3.1`
  - the counters confirmed the optimization worked mechanically:
    - `backend_d3d12_pso_sets` dropped from roughly `1 per indexed draw` to
      about `1 per batch submission`
    - `backend_d3d12_descriptor_table_sets` dropped from about `6 * indexed draws`
      to a lower but still significant value
  - the counters also showed why the win did not materialize:
    - geometry switches still tracked almost `1 per draw`
    - texture switches still tracked almost `1 per draw`
    - reducing backend bind calls did not touch the larger indexed submit costs
      in this workload
- Decision:
  - revert; do not keep D3D12 indexed bind-cache complexity without a real
    indexed-path gain
- Re-entry conditions:
  - only revisit if a capture shows PSO or descriptor binding churn dominating
    over geometry and texture churn
  - prefer attacking fewer indexed draws, fewer geometry switches, and fewer
    texture switches in the shared batch path first

### 2026-03-14: OpenGL indexed instance-buffer streaming was a miss
- Hypothesis:
  - replace per-draw `glBufferData` churn on the shared indexed instance buffer
    with frame-reset streaming plus `glBufferSubData`
- Expected win:
  - reduce `render_world_indexed_ms`
  - reduce steady-state `render_build_ms`
  - possibly lower `projected_model_ms` in cached indexed scenes
- What the workload showed:
  - the target bucket did not materially improve in the measured gameplay capture
  - `OpenGL` 1-unit indexed frames were flat to slightly worse:
    - before: `render_world_indexed_ms ~0.24-0.25`,
      `render_build_ms ~1.8`,
      `projected_model_ms ~0.22-0.23`
    - instance-stream run: `render_world_indexed_ms ~0.25-0.26`,
      `render_build_ms ~1.8-1.9`,
      `projected_model_ms ~0.22-0.25`
  - `OpenGL` 3-unit indexed frames were also flat to slightly worse:
    - before: `render_world_indexed_ms ~0.65-0.69`,
      `render_build_ms ~2.6-2.9`,
      `projected_model_ms ~0.53-0.56`
    - instance-stream run: `render_world_indexed_ms ~0.66-0.72`,
      `render_build_ms ~2.6-3.0`,
      `projected_model_ms ~0.53-0.58`
  - the measured scene clearly exercised cached indexed meshes, but reducing
    instance upload churn still did not move the hot bucket enough to justify
    the extra stream-offset and VAO-rebind logic
- Decision:
  - revert; do not keep instance-buffer streaming complexity without a measured
    indexed-path win
- Re-entry conditions:
  - instrument whether instance upload bytes are large enough to matter relative
    to texture binding, uniform updates, and cached draw-state churn
  - revisit only if a capture shows large instance payloads per indexed draw
  - prefer attacking cached indexed draw-state churn or batch ordering first

### 2026-03-14: OpenGL dynamic indexed geometry upload stream was a miss
- Hypothesis:
  - replace per-draw `glBufferData` churn for dynamic indexed world vertex and
    index uploads with capacity-managed streaming plus `glBufferSubData`
- Expected win:
  - reduce `render_world_indexed_ms`
  - reduce steady-state `render_build_ms`
  - possibly lower `projected_model_geometry_ms` in indexed gameplay scenes
- What the workload showed:
  - the target bucket did not materially improve in the measured capture
  - `OpenGL` 1-unit indexed frames were basically flat:
    - before: `render_world_indexed_ms ~0.24-0.25`,
      `render_build_ms ~1.8`,
      `projected_model_geometry_ms ~0.18-0.19`
    - streamed-upload run: `render_world_indexed_ms ~0.24-0.27`,
      `render_build_ms ~1.8`,
      `projected_model_geometry_ms ~0.19-0.20`
  - `OpenGL` 3-unit indexed frames were also flat to worse:
    - before: `render_world_indexed_ms ~0.64-0.66`,
      `render_build_ms ~2.6-2.7`,
      `projected_model_geometry_ms ~0.45`
    - streamed-upload run: `render_world_indexed_ms ~0.65-0.72`,
      `render_build_ms ~2.6-3.1`,
      `projected_model_geometry_ms ~0.45-0.52`
  - the likely reason is workload coverage:
    the hot scene mostly exercised cached indexed meshes, so dynamic geometry
    upload churn was not the dominant path
- Decision:
  - revert; do not keep dynamic-upload complexity when the measured scene does
    not show a real indexed-path gain
- Re-entry conditions:
  - instrument cached-vs-dynamic indexed draw share in the hot scene
  - only revisit if uncached indexed geometry is a meaningful share of the
    measured workload
  - prefer attacking cached-mesh or instance-buffer upload churn first

### 2026-03-14: OpenGL indexed material-uniform cache was a miss
- Hypothesis:
  - cache the last OpenGL indexed world-program uniform payload across batched
    draws so repeated submesh submissions stop re-sending the same material,
    camera, transform, and skinning uniforms
- Expected win:
  - reduce `render_world_indexed_ms`
  - reduce steady-state `render_build_ms`
  - possibly lower `projected_model_ms` in multi-submesh unit scenes
- What the workload showed:
  - the target bucket did not improve in the comparable gameplay captures
  - `OpenGL` 1-unit indexed frames were slightly worse instead of better:
    - before: `render_world_indexed_ms ~0.24`,
      `render_build_ms ~1.7-1.9`,
      `projected_model_geometry_ms ~0.18-0.19`
    - cached-uniform run: `render_world_indexed_ms ~0.25-0.28`,
      `render_build_ms ~1.8`,
      `projected_model_geometry_ms ~0.18-0.21`
  - `OpenGL` 3-unit indexed frames were also flat to worse:
    - before: `render_world_indexed_ms ~0.64-0.66`,
      `render_build_ms ~2.6-2.7`,
      `projected_model_geometry_ms ~0.45`
    - cached-uniform run: `render_world_indexed_ms ~0.67-0.70`,
      `render_build_ms ~2.7-2.9`,
      `projected_model_geometry_ms ~0.45`
  - the extra compare/copy work for the cached uniform payload did not earn
    back enough driver cost to matter on this scene
- Decision:
  - revert; do not assume uniform submission is the dominant OpenGL indexed
    cost without evidence from the measured workload
- Re-entry conditions:
  - instrument or sample whether uniform traffic is actually a larger cost than
    instance/geometry buffer upload churn in the hot scene
  - only revisit if a capture shows repeated identical materials dominating the
    indexed draw path
  - prefer attacking per-draw buffer upload churn first

### 2026-03-14: hashed projected skin-batch lookup was a miss
- Hypothesis:
  - replace the projected GPU skin batch-state linear scan with a hashed lookup
    keyed by `(unit, skin, palette)` to cut hot-path search cost
- Expected win:
  - reduce `projected_model_geometry_ms`
  - reduce `projected_units_ms`
  - lower steady-state `render_build_ms`
- What the workload showed:
  - the hot scene only carried a tiny projected skin-batch working set
  - the extra hash/key construction cost was worse than the previous
    small-vector scan plus adjacent-batch fast path
  - both backends regressed:
    - `OpenGL` before: `render_build_ms ~2.7-2.9`,
      `projected_model_geometry_ms ~0.45-0.46`,
      `render_world_indexed_ms ~0.66-0.69`
    - `OpenGL` regressed run: `render_build_ms ~3.3-3.7`,
      `projected_model_geometry_ms ~0.54-0.60`,
      `render_world_indexed_ms ~0.81-0.88`
    - `D3D12` before: `render_build_ms ~2.3-2.5`,
      `projected_model_geometry_ms ~0.43-0.46`,
      `render_submit_ms ~0.09-0.10`
    - `D3D12` regressed run: `render_build_ms ~2.9-3.2`,
      `projected_model_geometry_ms ~0.58-0.62`,
      `render_submit_ms ~0.17-0.19`
- Decision:
  - revert; do not replace tiny hot-set scans with heavier lookup structures
    unless the measured scene proves the set is large enough
- Re-entry conditions:
  - instrument actual projected skin-batch working-set size in the measured scene
  - only revisit hashed lookup if the hot set is materially larger than the
    current `~12`-batch combat path
  - compare against the existing adjacent-batch fast path, not against a
    theoretical baseline

### 2026-03-14: projected rigid-single-joint shortcut was a miss
- Hypothesis:
  - some projected "skinned" batches are effectively rigidly attached to one
    joint and can be treated as shared rigid geometry plus one joint transform
- Expected win:
  - reduce `projected_model_geometry_ms`
  - reduce `render_submit_ms`
  - lower steady-state `render_build_ms`
- What the workload showed:
  - real gameplay captures still reported `projected_clip_skinned_units=3`
  - the new shortcut path was not the dominant path in the measured scene
  - `D3D12` got worse instead of better:
    - pre-change: `render_build_ms ~2.1-2.9`,
      `projected_model_geometry_ms ~0.45-0.47`,
      `render_submit_ms ~0.08-0.10`,
      `render_world_indexed_ms ~0.18-0.19`
    - regressed run: `render_build_ms ~2.9-4.7`,
      `projected_model_geometry_ms ~0.60-0.66`,
      `render_submit_ms ~0.16-0.17`,
      `render_world_indexed_ms ~0.29-0.35`
  - `OpenGL` regressed in the same direction
- Decision:
  - revert; the idea was theory-correct but workload-wrong for the current game
    scene
- Re-entry conditions:
  - add counters showing how many projected submeshes are actually eligible for
    the rigid-single-joint path
  - confirm that eligible submeshes are a meaningful share of the hot scene
  - land only if the target buckets improve in the real capture

### 2026-03-14: shared opaque world-batch auto-instancing was a qualified win
- Hypothesis:
  - merge compatible cached opaque world batches into instanced draws instead
    of submitting them one by one
- Expected win:
  - reduce CPU-side world batch submit work
  - scale better as repeated opaque meshes increase
- What the workload showed:
  - the gain was modest in the current scene because there were few compatible
    repeated opaque meshes
  - it still moved work in the correct direction and did not regress the tested
    capture
- Decision:
  - keep; the change matches the actual CPU submit problem and should scale
    better in denser repeated-content scenes

## Checklist For Future CPU -> GPU Changes
1. Name the exact hot bucket that should move.
2. Prove the target path is materially present in the measured scene.
3. Compare before/after on the same scene and backend.
4. Watch both CPU and GPU-side counters:
   - `render_build_ms`
   - `render_submit_ms`
   - `gpu_frame_ms`
   - projected buckets
   - draw-call / indexed-submit signals
5. Keep the change only if the target buckets improve in practice.
6. If the measured scene does not hit the target path, instrument first and
   retry later with better evidence.
