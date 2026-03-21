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
