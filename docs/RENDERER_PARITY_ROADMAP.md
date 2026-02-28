# Renderer Parity + Performance Roadmap

Date: 2026-02-28

This is the authoritative pre-merge plan for the D3D12 branch.

## Scope
In scope:
- OpenGL/D3D12 parity and performance readiness.
- Runtime instrumentation needed to make backend comparisons trustworthy.
- Merge gating criteria before D3D12 returns to `master`.

Out of scope:
- New renderer backend implementation (Vulkan remains placeholder until this plan is complete).

## Baseline Evidence (Current)
Hardware reference (user machine):
- OS: Windows 10 Home, Version 10.0.19045
- GPU: NVIDIA GeForce GTX 1050 (discrete)
- CPU: Intel i7-7700HQ
- RAM: 32 GB

Observed runtime facts from recent logs:
- Discrete GPU selection is working (game runs on GTX 1050, not Intel HD 630).
- OpenGL and D3D12 both launch and run gameplay.
- D3D12 combat sample shows render-heavy frames around ~16-17 ms render time with total ~20-22 ms frame time (~46-49 FPS).
- Perf logs now include `render_build_ms`, `render_submit_ms`, `present_wait_ms`, `gpu_frame_ms`,
  `draw_calls`, `triangles`, `visible_animated_units`, and `particle_count`.
- Perf logs now also include projected-unit breakdown:
  - `projected_units_ms`
  - `projected_pose_eval_ms`
  - `projected_model_ms`
  - `projected_overlay_ms`
  - `projected_units_processed`
  - `projected_model_units`
  - `projected_clip_skinned_units`
- OpenGL still reports `gpu_frame_valid=0` (no GPU timestamp path yet on that backend).

Code facts affecting measurement quality:
- Main loop uses a 60 Hz fixed update step, but render is not hard-capped by that step (`GameRunner.cpp`).
- Display menu VSync/FPS/UI-quality controls are placeholders (`scripts/states/main_menu.lua`).
- OpenGL sets `SDL_GL_SetSwapInterval(1)` (`src/engine/platform/Window.cpp`).
- D3D12 calls `Present(1, 0)` and then `waitForGpu()` every frame (`src/engine/render/d3d12/D3D12RenderBackendLifecycle.cpp`).

## Interpretation
- Current numbers indicate a render-bound combat workload, not a simulation-bound one.
- API comparisons are not yet trustworthy enough for merge signoff because instrumentation is incomplete and frame pacing behavior differs.
- The biggest immediate D3D12 performance risk is forced CPU/GPU synchronization each frame.

## Pre-Merge Gates (Must Pass)

### Gate 1: Measurement Correctness
Required:
- Benchmark in `Release` (or `RelWithDebInfo`) only.
- Add a clear timing split at minimum:
  - `fixed_ms`
  - `render_build_ms`
  - `render_submit_ms`
  - `present_wait_ms`
  - `frame_cpu_ms`
  - `gpu_frame_ms` (timestamp query path)
- Add scene counters at log time:
  - draw calls
  - triangles
  - visible animated units
  - particle count

### Gate 2: Apples-to-Apples Benchmark Matrix
Required matrix for the same deterministic scene setup:
- APIs: OpenGL, D3D12
- Resolutions: 1280x720, 1600x900, 1920x1080
- Build: Release
- VSync state: explicit and actually wired (not placeholder)
- Output: avg FPS, 1% low FPS, CPU frame ms, GPU frame ms, draw calls, triangles

### Gate 3: Functional Parity Sanity
Required:
- No user-visible regressions in board, unit models/materials/animation, HUD readability, and core combat VFX.
- Backend switching + restart remains stable.
- Runtime/backend logs reflect actual route behavior (remove stale wording like "debug-world render path" if no longer true).

### Gate 4: Performance Acceptance
Minimum acceptance for target laptop:
- D3D12 is within 20% of OpenGL frame time in each matrix row.
- No backend drops below 30 FPS average in the heavy benchmark scene at 1080p.
- Stretch target: 60 FPS average at 900p in the same heavy scene.

### Gate 5: Test + Documentation Alignment
Required:
- `docs/TEST_PLAN.md` benchmark protocol executed and results recorded.
- Runtime smoke tests remain green for `opengl` and `d3d12`.
- Docs reflect actual current behavior (no references to retired route models as active paths).

## Execution Roadmap

### Phase 0: Instrumentation Hardening (First)
1. Add CPU timing split for build/submit/present.
2. Add GPU timestamp frame duration path.
3. Emit one structured perf line format used by both backends.

### Phase 1: Benchmark Harness
1. Define one repeatable heavy combat scene and one light scene.
2. Automate scene startup as much as possible (seed + scripted actions + startup video override envs).
3. Run the full resolution/backend matrix and store results.
   - `tools/benchmark_render_matrix.ps1` is now the baseline runner for this gate.
4. Add baseline-to-current comparison summary so matrix runs can fail fast on regressions.

### Phase 2: Immediate Performance Wins
1. Remove per-frame `waitForGpu()` behavior in normal D3D12 frame flow.
2. Keep fences for resource safety, but allow frames-in-flight.
3. Re-run matrix and compare deltas.

### Phase 3: Workload Optimization
1. Reduce CPU-side projected-unit cost first (pose eval, clip skinning, procedural per-vertex deform).
2. Keep fast-path toggles benchmarkable:
   - `PAC_BACKEND_VERTEX_DEFORM`
   - `PAC_BACKEND_CLIP_SKINNING`
   - `PAC_BACKEND_CLIP_SKINNING_ADAPTIVE`
   - `PAC_BACKEND_CLIP_SKINNING_MAX_UNITS`
3. Move validated hot paths from CPU to GPU in controlled slices (starting with skinning/deform equivalents).
   - Completed slice: projected model lighting now runs in backend world shaders (`materialMode=2`) instead of CPU per-vertex shading.
   - Completed slice: indexed model path now uses position-only CPU transforms (normal skinning skipped on CPU for indexed draws).
   - In progress slice: indexed world mesh vertices now upload in model space with per-batch model matrix consumed by backend world shaders (GPU applies model transform).
4. Reduce per-frame render submission overhead (batch/state churn).
5. Prioritize high-impact combat costs (animated units, overdraw-heavy VFX, expensive passes).
6. Validate gains with matrix reruns after each slice.

### Phase 4: Merge Decision
Merge D3D12 branch into `master` only when Gates 1-5 pass.

## API Expansion Rule
Do not start Vulkan implementation until:
- D3D12 branch is merged,
- benchmark pipeline is stable,
- and OpenGL/D3D12 regressions are caught by repeatable measurements.
