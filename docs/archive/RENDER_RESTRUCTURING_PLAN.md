Archived on: 2026-03-31
Reason: Historical broad restructuring plan; active remaining restructuring work now lives in a narrower current doc, while this file remains useful as design history.
Superseded by: `docs/RENDER_RESTRUCTURING_OUTSTANDING.md`, `docs/RENDERER_PARITY_ROADMAP.md`

# Render Restructuring Plan

Date: 2026-03-22

Purpose: document the current renderer ceiling in the heavy animated-board
scene, explain what is still realistically available from incremental tuning,
and define the deeper restructuring path required for materially larger gains.

## Summary

Short version:

- incremental experiments are still worth doing, but they are now mostly
  yielding tenths of a millisecond or low double-digit percentage wins
- the current retained path is already offloading the obvious CPU skinning work
  to the GPU
- the main remaining cost is now structural CPU work around per-unit prep,
  per-batch construction, sorting, state setup, and backend submission
- the next meaningful gain tier requires renderer/dataflow restructuring, not
  just more local hot-loop tuning

Current best retained heavy-scene Debug reference:

- `benchmark/render_matrix_20260322_115900.json`
  - `D3D12`: `206.514 FPS`, `4.769 ms` frame CPU, `4.092 ms` render build,
    `0.629 ms` GPU
  - `OpenGL`: `168.517 FPS`, `5.857 ms` frame CPU, `4.961 ms` render build,
    `0.585 ms` GPU

The important signal is that GPU time is still much lower than CPU time in the
heavy scene. The renderer is CPU-bound, but that does not automatically mean
"move one more thing to a shader and get 2x FPS." The remaining CPU work is no
longer just animation math.

## What The Current Architecture Already Solved

The retained path already includes several meaningful GPU offloads:

- GPU clip skinning for indexed model batches
- shader-side `jointGlobal * inverseBind` composition on both `D3D12` and
  `OpenGL`
- D3D12 full-skin shared upload policy where it measured as a win
- GPU application of rigid-node transforms for indexed submeshes
- scene-pose caching and quantized sampling for repeated animation states

Relevant code areas:

- `src/game/runtime/shared/projected/SharedProjectedUnitRenderer.cpp`
- `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshPrep.cpp`
- `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshRenderer.cpp`
- `src/game/runtime/shared/backend/SharedBackendPoseEval.cpp`
- `src/game/runtime/shared/world/SharedWorldIndexedBatches.cpp`
- `src/engine/render/d3d12/D3D12RenderBackendWorldDraw.cpp`
- `src/engine/render/opengl/OpenGLRenderBackendWorldDraw.cpp`

This matters because it changes what "the next bottleneck" actually is.

## What The Benchmarks Say Now

On the retained heavy-scene reference in
`benchmark/render_matrix_20260322_115900.json`:

- `D3D12 avg_projected_pose_eval_ms` is only `0.123 ms`
- `D3D12 avg_projected_model_ms` is `1.017 ms`
- `D3D12 avg_render_build_ms` is `4.092 ms`
- `D3D12 avg_gpu_frame_ms` is only `0.629 ms`

- `OpenGL avg_projected_pose_eval_ms` is only `0.138 ms`
- `OpenGL avg_projected_model_ms` is `1.096 ms`
- `OpenGL avg_render_build_ms` is `4.961 ms`
- `OpenGL avg_gpu_frame_ms` is only `0.585 ms`

That means:

- clip pose evaluation is no longer the dominant steady-state cost
- the expensive part is the surrounding per-unit/per-batch machinery
- `OpenGL` still has a larger indexed-submit/state-change problem than `D3D12`
- `D3D12` has more headroom and is the correct backend to optimize first for
  high-FPS targets

## Why Baby Steps Are Slowing Down

The recent experiments tell a consistent story:

- wins came from removing whole categories of CPU work
- misses tended to rearrange already-small costs or add more submission
  complexity without shrinking batch count or backend work

Examples:

- good:
  - GPU skin composition
  - indexed rigid-node transform offload
  - OpenGL texture-handle resolution caching
- misses:
  - frame-local D3D12 skin upload reuse
  - OpenGL full-GPU-skin parity policy
  - rigid-node GPU template reuse
  - broader OpenGL UBO rewrites

This is a strong sign that local tuning is getting close to diminishing returns.

## Realistic Expectations

### Incremental path

If we continue only with measured local experiments:

- more gains are likely
- they are likely to be modest
- the most realistic expectation is another `10-20%` on `D3D12`
- `OpenGL` may still improve, but it is much more likely to hit driver/state
  overhead walls sooner

This path is still useful for:

- validating hypotheses cheaply
- keeping the renderer healthy
- finding the real pressure points before doing larger changes

It is not the path I would bet on for a dramatic jump from current heavy-scene
Debug numbers to a stable `300-400 FPS`.

### Restructuring path

If the goal is materially higher FPS with many animated models, the renderer
needs to stop paying so much per-unit/per-batch CPU work every frame.

That means restructuring around:

- persistent render items instead of rebuilding batch payloads from scratch
- persistent GPU-side skeleton/material resources instead of repeatedly packing
  transient payloads
- skinned instancing or otherwise sharing submission across animated copies
- fewer per-draw resource/state changes
- more GPU-driven animation/skin data flow rather than CPU-side prep followed by
  ordinary draw submission

## Is 300-400 FPS With Dozens Of Animated Units Realistic?

### In Debug

Probably not from baby steps alone.

On the retained D3D12 heavy-scene reference, `206.514 FPS` corresponds to about
`4.769 ms` frame CPU. To get to:

- `300 FPS`, frame CPU needs to be around `3.33 ms`
- `400 FPS`, frame CPU needs to be around `2.50 ms`

That is a very large cut from the current steady-state heavy-scene CPU cost.
There is no evidence in the current architecture that another series of small
offloads will remove that much time.

### In Release

Much more plausible, but still not something to promise without the deeper
renderer changes below.

If we combine:

- a `Release` build
- `D3D12`
- persistent render-item/batch restructuring
- real skinned batching/instancing improvements
- reduced descriptor/material churn

then `300 FPS` in heavier scenes becomes a reasonable aspirational target.

`400 FPS` with dozens of animated models should be treated as a stretch target,
not the planning baseline.

## Recommended Direction

Use the current safe experiment process, but shift it from small hot-loop
optimizations to larger D3D12-first structural slices.

Recommended order:

1. D3D12-first persistent render-item path
2. D3D12 skinned instancing / shared animated draw path
3. split static and dynamic skin payloads
4. GPU-side animation sampling / pose generation
5. only after D3D12 proves out, decide what subset is worth porting to OpenGL

## Proposed Phases

### Phase 0: Stabilize measurement

Goal:

- make sure future restructuring decisions are made against a reproducible
  heavy-scene baseline

Tasks:

- keep using the automated snapshot benchmark
- stamp a "current heavy scene baseline" whenever the snapshot changes
- compare `Debug` and `Release` on the same snapshot before setting FPS targets

Exit criteria:

- one agreed heavy-scene baseline for `Debug`
- one agreed heavy-scene baseline for `Release`

### Phase 1: Persistent render items instead of frame-local rebuild

Problem:

- the current path still prepares `WorldIndexedBatch` structures every frame in
  `SharedProjectedUnitBackendMeshPrep.cpp` and
  `SharedProjectedUnitBackendMeshRenderer.cpp`
- this includes repeated setup of model state, shared geometry pointers,
  skinning state, and material payload routing

Goal:

- convert repeated per-unit/per-submesh prep into persistent render items that
  only update the fields that truly changed this frame

Expected win:

- lower `projected_model_prep_ms`
- lower `render_build_ms`

Risk:

- invalidation bugs when animation state, material state, or mesh identity
  changes

Why this phase first:

- it attacks structural CPU work without committing us yet to a full GPU-driven
  renderer

### Phase 2: D3D12 skinned instancing

Problem:

- the current auto-instancing path in `SharedWorldIndexedBatches.cpp` excludes
  GPU-skinned draws
- animated units therefore still pay one draw worth of submission/state work
  per compatible submesh instance

Goal:

- support batching multiple animated units that share compatible geometry and
  material state into a single instanced submission with per-instance skeleton
  addressing

Expected win:

- reduce draw count in dense animated scenes
- reduce `backend_indexed_geometry_switches`
- reduce `backend_d3d12_descriptor_table_sets`
- reduce `render_world_indexed_ms`
- reduce `render_build_ms`

This is the first phase that could plausibly move whole milliseconds instead of
tenths.

Risk:

- significantly more complex instance payload design
- requires deliberate D3D12-first implementation
- not guaranteed to port cleanly to `OpenGL`

### Phase 3: Static vs dynamic skin payload split

Problem:

- current skin uploads still package data that does not change every frame

Goal:

- keep inverse-bind or other static skin data resident on GPU
- upload only dynamic joint-global data each frame

Expected win:

- reduce CPU copy volume during submit
- reduce per-draw upload cost
- improve the viability of Phase 2 instancing

This is a good companion to Phase 2, not a substitute for it.

### Phase 4: GPU-side animation sampling

Problem:

- clip pose evaluation is not the dominant steady-state cost anymore, but it is
  still CPU work and still spikes on cache misses or scene transitions

Goal:

- evaluate animation clips on GPU from compressed clip/sample data, or pre-bake
  animation sample atlases and sample those on GPU

Expected win:

- reduce `projected_pose_eval_ms`
- reduce warmup spikes
- simplify the CPU-side animation path once paired with a more persistent render
  item model

Caution:

- this is a larger engineering project than the current micro-optimization loop
- it should follow Phase 1/2 work, not replace it

Reason:

- right now the bigger steady-state CPU wall is still batch/submission structure

### Phase 5: OpenGL decision point

At this stage we should decide whether to:

- port the proven D3D12 design selectively to `OpenGL`
- keep `OpenGL` on a simpler supported path
- or treat `D3D12` as the high-performance target backend

Current evidence suggests `OpenGL` is more sensitive to submission/state churn,
so parity should not be the default goal if it blocks larger D3D12 wins.

## What To Avoid

Based on the retained and reverted experiments so far:

- do not keep retrying small cache variations that do not reduce draw count,
  batch count, or backend state work
- do not treat every CPU bucket as equally important; pose eval is no longer the
  main steady-state target
- do not force `OpenGL` parity for a D3D12-first structural optimization unless
  the D3D12 result is already clearly proven
- do not trust theory over capture data; revert misses quickly

## Success Criteria For The Restructure Track

The restructuring track is worth continuing only if it starts moving these
counters materially in the heavy scene:

- `avg_render_build_ms`
- `avg_projected_model_ms`
- `render_world_indexed_ms`
- draw count / indexed draw count
- D3D12 descriptor table sets
- OpenGL texture bind / uniform-heavy submit cost, if and when OpenGL is
  touched

The most meaningful sign of real progress will be:

- fewer animated indexed draws for comparable visible content
- less per-frame batch construction
- less backend submission work per visible unit

## Recommendation

Continue the safe experiment discipline, but treat the next workstream as
restructuring, not more micro-tuning.

Recommended planning baseline:

- aim for `D3D12` first
- expect incremental experiments to keep producing wins, but mostly modest ones
- treat `300 FPS` heavy-scene performance as a plausible longer-term `Release`
  target if Phase 1 and Phase 2 land well
- treat `400 FPS` with dozens of animated units as a stretch target that likely
  requires the full structural path, not just more local tuning

## Immediate Next Step

The next design document should be a D3D12-first implementation plan for
Phase 1 and Phase 2:

- define the persistent render-item data model
- define invalidation/update rules
- define the per-instance skeleton addressing scheme for skinned instancing
- define what metrics must improve before the design is considered successful
