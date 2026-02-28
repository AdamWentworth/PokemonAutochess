# Housework Roadmap (Post-Merge Stabilization)

Date: 2026-02-28

This roadmap starts after D3D12 merge gates pass. It keeps momentum on quality and performance without reopening parity uncertainty.

## Objective
- Stabilize shared rendering architecture.
- Convert measured bottlenecks into targeted optimization slices.
- Prepare the codebase for future API expansion only after proven discipline.

## Track A: Measurement and Benchmark Automation
1. Add structured perf output file support (CSV/JSON) for benchmark sessions.
2. Add a repeatable benchmark launcher script (`tools/benchmark_render_matrix.ps1`).
3. Keep one canonical benchmark scene definition under source control.

Definition of done:
- One command runs OpenGL and D3D12 benchmark rows and emits comparable artifacts.

## Track B: D3D12 Frame Execution Efficiency
1. Replace end-of-frame global GPU waits with per-frame fence progression.
2. Validate command allocator/list reuse and buffer lifetime safety with frames-in-flight.
3. Re-measure CPU frame time and present wait deltas after each change.

Definition of done:
- D3D12 no longer pays unnecessary frame-by-frame full-GPU waits.

## Track C: Render Workload Optimization
1. Prioritize expensive combat-path costs (draw-call churn, animated unit submission, overdraw-heavy effects).
2. Add lightweight in-game counters for draw calls, triangles, and active FX by scene.
3. Introduce quality/performance presets with explicit cost controls where needed.

Definition of done:
- Measured reductions in heavy-scene frame time with no major visual regressions.

## Track D: UX and Runtime Transparency
1. Wire or remove placeholder display options (VSync/FPS cap/UI scale/quality).
2. Ensure runtime logs describe real behavior (requested backend, active backend, fallback reasons, effective sync mode).
3. Keep backend switch + restart flow deterministic and easy to verify.

Definition of done:
- Users can trust menu options and logs for backend/perf testing.

## Deferred Until After Tracks A-D
- Vulkan backend implementation.
- Broad rendering feature expansion that increases complexity before baseline discipline is in place.
