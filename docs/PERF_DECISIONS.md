# Performance Decisions

Status: Active
Type: Reference
Last updated: 2026-03-31

Purpose: keep the durable performance lessons and current decision rules short,
so future work starts from measured evidence instead of repeating old
experiments.

## Current Priority
- The main performance lane is still steady-state render/build CPU cost in real
  gameplay scenes.
- Startup and first-use hitch removal still matters, but it is not the main
  optimization strategy anymore.
- Shared-path improvements remain the default; backend-specific work is justified
  only when the measured backend behavior really differs.

## Durable Lessons
- Optimize measured hot buckets, not generic CPU/GPU theory.
  - Start with the exact target bucket and the exact scene that exposes it.
- GPU offload only pays when it removes meaningful CPU structure.
  - Real wins came from removing whole categories of repeated per-batch or
    per-vertex CPU work, not from swapping containers or caches in already-small
    paths.
- D3D12 and OpenGL do not always want the same perf policy.
  - Keep shared-path ownership as the default, but do not force symmetric
    backend behavior when one backend clearly benefits and the other clearly
    does not.
- Preserve visual correctness and stable presentation while optimizing.
  - Do not keep an optimization that wins on paper but regresses the actual
    scene, visual result, or runtime smoothness.
- Revert misses quickly.
  - The repo is healthiest when rejected experiments stay documented but do not
    linger in code.

## Confirmed Wins To Preserve
- GPU skin node-global composition was a real win on both `D3D12` and `OpenGL`.
- D3D12-preferred full-skin shared GPU upload was a real win.
- Indexed rigid-node GPU transform reuse was a real win.
- Retained debug-geometry GPU caching was a real win.
- OpenGL world-texture lookup and fallback/env handle caching was a real win.
- Shared opaque world-batch auto-instancing was a qualified win.
  - Keep it for the compatible repeated-content cases it actually helps.

## Confirmed Misses Or Deprioritized Lanes
- Micro container or lookup rewrites that do not reduce structural hot-path
  work.
  - Examples: map-lookup-only GPU skin batch state changes, vector-owned skin
    scratch ownership, hashed skin-batch lookups.
- OpenGL parity retries that copy a D3D12 idea without the same measured win.
  - Example: revisiting broader full-GPU skin sharing on `OpenGL`.
- Uniform/UBO/streaming rewrites that add churn without reducing the target
  submit buckets.
  - Examples: indexed frame/camera UBO migration, instance-buffer streaming,
    dynamic indexed geometry upload streaming.
- Cache or identity layers that preserve complexity without materially reducing
  `render_build_ms`.
  - Examples: preserved prepared projected batches, projected submission-identity
    cache, revision-gated overlay prep after the retained-geometry win.
- Upload reuse ideas that do not remove enough upstream CPU work to pay for
  themselves.
  - Examples: D3D12 frame-local skin upload reuse, static inverse-bind buffer
    split in the tested form.

## Current Guidance For New Perf Work
- Start by naming the exact hot bucket:
  - `render_build_ms`
  - `render_submit_ms`
  - `gpu_frame_ms`
  - projected model buckets
  - startup/first-use timing only when the change is explicitly cold-path work
- Prove the target path is materially present in the measured scene before
  changing code.
- Prefer structural reductions in repeated CPU work over local ownership
  reshuffles.
- Keep D3D12-first specialization as an option when it removes real cost, but
  only after the shared-path interpretation is understood.
- Keep changes only when the before/after capture improves the target buckets in
  practice.

## Current Guardrails
- The local starter-line Release perf smoke is expected to run against a
  prebuilt Release binary when used through `full_check`.
  - `tools/full_check.ps1 -IncludePerfSmoke` prebuilds Release early and later
    invokes `tools/perf_smoke_guard.ps1 -NoBuild`, so the scored run is not
    skewed by immediate post-build thermal noise.
- The local perf smoke now behaves as a small baseline suite rather than one
  scene.
  - It currently covers the Tail Fire starter-line snapshot and a denser
    planning-state gameplay roster snapshot.
  - Each baseline chooses the largest protected windowed resolution that fits
    the current primary-display working area.
  - If none of a baseline's protected resolutions fit, the harness fails
    clearly instead of trying to score a stretched or overflowing window.
- The local perf smoke remains a smoke-level guardrail, not the final word.
  - Use the full `benchmark_render_matrix.ps1` protocol for larger renderer or
    runtime perf decisions.

## Current Checklist
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

## Current Guardrails
- `tools/perf_smoke_guard.ps1` is the first lightweight protected baseline.
  - It currently uses the Tail Fire starter-line snapshot as a deterministic
    Release smoke scene.
  - The benchmark harness pins the scripted snapshot state during scoring so
    shop/menu timers do not transition the scene mid-run.
  - Treat it as a gross-regression guard, not as the final representative
    performance authority for the renderer.

## Full History
- The detailed raw experiment log for this cleanup cycle lives in
  `docs/archive/PERF_EXPERIMENT_LOG_2026-03.md`.
