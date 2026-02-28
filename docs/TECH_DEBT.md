# Tech Debt

Date: 2026-02-28

This list is intentionally short and prioritized by impact on renderer merge readiness.

## Highest Priority Debt
1. D3D12 end-of-frame synchronization is too strict.
- `src/engine/render/d3d12/D3D12RenderBackendLifecycle.cpp` calls `waitForGpu()` each frame after `Present(1, 0)`.
- Impact: limits CPU/GPU overlap and can mask true backend performance potential.

2. Perf telemetry is not yet decision-grade.
- `GameRunner` now logs `render_build_ms`, `render_submit_ms`, `present_wait_ms`, `gpu_frame_ms`,
  `draw_calls`, `triangles`, `visible_animated_units`, and `particle_count`.
- Remaining gap: OpenGL GPU timestamps (`gpu_frame_valid` remains `0` on OpenGL).

3. Display settings expose placeholders as if active controls.
- `scripts/states/main_menu.lua` still labels VSync/FPS/UI scale/quality as placeholders.
- Impact: unreliable test conditions and user confusion during backend comparison.

4. Runtime log terminology is stale.
- D3D12 initialization still logs "debug-world render path" wording even though current route model is shared-path based.

5. No automated backend perf regression guard.
- CI verifies correctness but not frame-time thresholds.

## Important Secondary Debt
1. CPU-heavy render workload under combat still needs targeted profiling.
- Need per-feature cost attribution (submission, skinning, overdraw-heavy VFX).

2. Remaining large files are still high-churn risk.
- `src/engine/render/D3D12RenderBackend.cpp` family
- `src/game/runtime/GameSession.cpp` and shared runtime render modules

3. Vulkan remains visible in menu while unimplemented.
- Keep as placeholder only; do not treat as active roadmap work until OpenGL/D3D12 gates are complete.

## Recent Positive Changes
- Shared rendering route structure is significantly cleaner than earlier parity phases.
- D3D12 and OpenGL runtime smoke coverage exists in CMake (opt-in).
- Backend selection/preference parsing is explicit and tested.
