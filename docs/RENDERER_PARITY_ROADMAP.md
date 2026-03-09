# Renderer Parity + Performance Roadmap

Date: 2026-03-08

This is the current renderer roadmap for the repo. It replaces the older
"pre-merge D3D12 gate" framing and rolls active parity/performance priorities
into one current document.

It supersedes the old roles of:
- `docs/PARITY_OUTSTANDING.md`
- `docs/HOUSEWORK_ROADMAP.md`

## Current State
- Shared gameplay presentation is the default path for both `OpenGL` and `D3D12`.
- Perf telemetry is now good enough to make local decisions:
  - `frame_cpu_ms`
  - `render_build_ms`
  - `render_submit_ms`
  - `present_wait_ms`
  - `gpu_frame_ms`
  - projected render buckets
  - fixed-step buckets
  - startup/flow trace timing
- The biggest recent wins were:
  - moving gameplay hot paths out of Lua and into native code
  - reducing projected render CPU work
  - removing major first-use stalls with targeted prewarm/persistent caches
- The biggest remaining performance problem is not startup. It is steady-state
  render-build CPU cost in heavier scenes.

## Working Categories
- `Measurement`
  - Keep perf counters, benchmark discipline, and log accuracy trustworthy.
- `Runtime Hot Path`
  - Reduce steady-state frame cost in real gameplay.
  - This is the highest-value category.
- `Cold Path / Hitch Removal`
  - Fix startup and first-use stalls when they are clearly user-visible.
  - This is worth doing, but it is not the mainline performance strategy.
- `Backend / Product Hygiene`
  - Keep `OpenGL` and `D3D12` behavior aligned where possible.
  - Keep settings, docs, and logs honest.

## Priority Order
1. Steady-state runtime render/build performance.
2. Benchmark discipline and perf regression evidence.
3. User-visible first-use hitch removal.
4. Settings and docs cleanup.
5. Backend-specific optimization only when shared-path work is exhausted or API behavior forces it.

## What Is No Longer The Right Framing
- The repo should not be organized around "D3D12 merge blockers" anymore.
- The repo should not treat startup prewarm work as the primary performance lane.
- The repo should not assume more generic GPU feature work is automatically the next best return.

## Current Policy
- Shared-path improvements are the default.
- Backend-specific work is justified only when API/runtime behavior requires it.
- Performance work should be guided by measured frame-time buckets, not by generic GPU/CPU slogans.
- Cold-path work is worth prioritizing only when it removes an obvious user-visible stall.

## Near-Term Roadmap
1. Keep Release benchmark runs repeatable and comparable.
2. Re-center on dense-combat steady-state cost:
   - `render_build_ms`
   - projected pose/model/prep/geometry buckets
   - draw submission overhead
3. Continue GPU offloading only where it removes CPU render-build work.
4. Keep cold-path fixes surgical:
   - startup
   - first-shop-entry
   - first-species-use
   - first-VFX-use
5. Clean up user-facing graphics/settings behavior so menus and logs reflect reality.

## Deferred Next Iteration Candidates
- Retained/dirty overlay submission.
  - Goal: stop rebuilding unchanged UI and overlay draw data every frame.
  - Primary buckets to watch: `render_overlay_prep_ms`, `render_ui_submit_ms`.
- More fundamental projected-unit submission redesign.
  - Goal: move farther away from per-unit rebuild/submit work and toward shared prepared data plus smaller per-unit deltas.
  - Primary buckets to watch: `projected_model_prep_ms`, `projected_model_geometry_ms`, `render_world_indexed_ms`.
- Tail-fire path changes are intentionally deferred until there is a clearer product/visual direction.

## Current Engineering Guidance
- If work scales with triangle count or per-vertex visual math, prefer pushing it toward GPU/shader-side handling.
- If work is deterministic gameplay truth, keep it on CPU.
- If a change improves startup but risks reintroducing runtime hitches, prefer runtime smoothness.
- If a doc describes an old blocker that is no longer true in code/logs, rewrite or remove it.

## Success Signals
- Dense combat frame time continues trending down on the target laptop.
- Startup and first-use stalls stay below the threshold of being user-visible problems.
- Shared-path changes benefit both `OpenGL` and `D3D12` unless there is a documented reason otherwise.
- Docs, logs, and menu behavior match the current engine reality.

## Related Docs
- `docs/TEST_PLAN.md`
- `docs/CPU_GPU_WORK_SPLIT.md`
- `docs/RENDER_PATH_FILE_MAP.md`
- `docs/TECH_DEBT.md`
- `docs/DISPLAY_GRAPHICS_ROADMAP.md`
