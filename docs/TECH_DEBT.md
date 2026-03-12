# Tech Debt

Date: 2026-03-12

This file is intentionally short. It should track active debt that still has
real engineering value, not old blocker language that has already been retired.

## Highest Priority Debt
1. No automated performance regression gate.
- Local benchmark tooling exists.
- CI still does not enforce perf baselines or fail on benchmark regressions.

2. Shared projected render/build CPU is still the main steady-state hotspot.
- The current biggest remaining performance returns are in dense combat render-build work, not gameplay scripting and not generic startup work.

3. Runtime/session composition is still too centralized.
- `src/game/runtime/GameSession.cpp` and `src/game/runtime/GameRunner.cpp` still combine too many concerns.
- Startup, prewarm, render-path routing, input, restart flow, debug snapshotting, and diagnostics need cleaner seams.

4. Display/settings honesty is still incomplete.
- Placeholder controls and visible-but-unimplemented options should not remain user-facing ambiguity.

5. Documentation drift risk is real.
- Perf/parity docs were allowed to describe outdated blockers.
- This should stay cleaned up going forward.

## Important Secondary Debt
1. Benchmark evidence discipline still needs harder validity rules.
- Historical local artifacts with zero or unusable perf samples should not be treated as baselines.
- Benchmark output quality matters almost as much as benchmark tooling existence.

2. Startup/cold-path caches add complexity and should stay justified.
- Tail-fire and card-art caches are worth keeping because they removed visible stalls.
- Future startup complexity should be held to the same standard.

3. Fire-tail rendering now deserves a targeted perf pass.
- The current Charmander-line fire path mixes authored fire-mesh flipbooks with the legacy tail-fire fallback/emitter flow.
- The visible first-use Charmander hitch is substantially improved now that startup prewarms the authored flipbook upload too.
- The remaining debt has shifted to cold-start CPU work: legacy premultiplied atlas bake plus authored flipbook decode still add noticeable startup cost.

4. Render submission still rebuilds too much unchanged work.
- The next sensible structural step is retained/dirty submission for UI and overlay layers instead of rebuilding the full draw-prep path every frame.

5. Projected-unit submission is still more per-unit than ideal.
- The next bigger render-side rework is to lean harder on shared prepared geometry/material state and reduce per-unit submission churn.

6. Large high-churn files remain risky.
- `src/game/runtime/GameSession.cpp`
- `src/game/runtime/GameRunner.cpp`
- backend render implementation families
- shared projected runtime render modules

7. Duplicate startup/data-store wiring should be reduced.
- Packed/dev asset-store fallback and related startup wiring are split between `GameBootstrap.cpp` and `GameSession.cpp`.
- That increases drift risk in content-loading behavior.

8. No dedicated benchmark hardware baseline policy yet.
- Local numbers are useful, but long-term regression decisions still need a clearer baseline process.

## Things That Are No Longer Good Debt Entries
- Old claims that D3D12 blocks every frame with a normal-path `waitForGpu()`.
- Old claims that OpenGL has no GPU frame timing path.
- Old "pre-merge D3D12 blocker" wording when the repo is now operating beyond that phase.
