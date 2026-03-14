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
