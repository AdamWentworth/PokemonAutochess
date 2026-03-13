# Repo Assessment

Date: 2026-03-12

Overall rating: `7.6/10`

This repo is in better shape than most prototype-scale C++ game projects. The
architecture has real guardrails, the headless test surface is unusually strong,
and the renderer/perf docs are mostly honest about current priorities. The main
cost is concentration: too much runtime and render coordination still lands in a
small number of very large files.

## Category Scores
- Architecture and modularity: `7/10`
- Testability: `8.5/10`
- Performance discipline: `7/10`
- Documentation and process hygiene: `8/10`
- Product/runtime honesty: `7/10`

## What Is Strong
- Strict engine/game layering is stated and enforced by tests.
- The headless core is real, not aspirational:
  - `118` CTest entries were green on `2026-03-12`.
- `GameWorld` is split into focused translation units instead of one monolith.
- Render routing, parity policy, and perf logging have explicit contracts/docs.
- Docs are dated, short, and mostly aligned with the current codebase.

## What Existing Docs Already Capture Well
- No perf regression gate in CI yet.
- Shared projected render/build CPU is still the main steady-state hotspot.
- Display/settings honesty is incomplete because placeholder controls remain.
- Backend implementation families and shared projected render modules are still
  high-churn risk areas.

## Gaps Not Fully Called Out Before
- `src/game/runtime/session/GameSession.cpp` is still the repo's main god-object seam.
  - It owns startup, asset prewarm, runtime wiring, input handling, snapshot IO,
    backend debug rendering, overlay composition, and shutdown coordination.
- `src/game/runtime/GameRunner.cpp` also centralizes too much:
  - SDL event translation, backend selection/fallback, window/video policy,
    restart loop behavior, and perf/log output all meet there.
- Startup/data-store wiring is duplicated:
  - `GameBootstrap.cpp` and `GameSession.cpp` both perform packed/dev asset
    store selection and related startup decisions.
- Benchmark tooling exists, but benchmark evidence still needs policing:
  - historical files under `benchmark/` include zero-sample rows and should not
    be treated as valid baselines.
- Logging is split between `LogBus` and many direct `std::cout/std::cerr`
  startup/runtime prints, which weakens observability consistency.

## Modularity Read
- Healthy:
  - `src/game/world/`
  - route/policy helpers under `src/game/runtime/routes/`
  - many backend/shared helper modules with focused contract tests
- Risky:
  - `src/game/runtime/session/GameSession.cpp`
  - `src/game/runtime/GameRunner.cpp`
  - backend render pipeline/draw families

## Testability Read
- Strong:
  - deterministic headless simulation paths
  - layering guard test
  - route policy and backend contract tests
  - content/config loader coverage
- Weak:
  - runtime smoke remains optional, not default CI coverage
  - no automated perf threshold enforcement
  - startup/restart/first-use hitch behavior is still more measured manually than
    asserted automatically

## Performance Read
- Strengths:
  - good perf bucket vocabulary
  - benchmark harness exists
  - parity contract and runtime smoke hooks exist
  - cold-path caches are targeted, not purely speculative
- Risks:
  - steady-state shared projected render CPU is still the main cost center
  - render submission still rebuilds too much unchanged work
  - benchmark validity and baseline policy are not enforced end-to-end

## Documentation Comparison
- Still accurate:
  - `docs/GOALS.md`
  - `docs/CI.md`
  - `docs/RENDERER_PARITY_ROADMAP.md`
  - `docs/DISPLAY_GRAPHICS_ROADMAP.md`
- Updated as part of this assessment:
  - `README.md` stale shop script reference fixed
  - `docs/TECH_DEBT.md` expanded to reflect centralization and benchmark-validity debt
  - `docs/TEST_PLAN.md` now states that zero-sample benchmark rows are invalid

## Recommended Priority Order
1. Break `GameSession.cpp` into boot/prewarm, debug snapshot, and render-composition ownership seams.
2. Split `GameRunner.cpp` into backend/video bootstrap vs main-loop/runtime control.
3. Add a real perf-baseline policy plus at least one reduced benchmark gate.
4. Keep removing placeholder settings from player-facing menus unless wired and tested.
5. Continue shared-path render CPU reductions before chasing new backend-specific feature work.
