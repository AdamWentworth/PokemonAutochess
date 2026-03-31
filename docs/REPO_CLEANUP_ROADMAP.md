# Repo Cleanup Roadmap

Status: Active
Type: Roadmap
Last updated: 2026-03-31

This roadmap turns `REPO_ASSESSMENT.md` into a ranked execution plan. The
ordering is based on maintainability payoff per engineering day, not just raw
importance.

## Ordering Rules
- Keep `tools/full_check.ps1` green or greener after each cleanup slice.
- Prefer seam extraction over big-bang rewrites.
- Prefer boundary fixes that unlock future reuse.
- Do not start deep renderer rewrites until the current runtime ownership seams
  are cleaner.

## Priority Ladder

### 1. Restore a clean all-green quality path
- Rank: `#1`
- Payoff/day: `Very high`
- Estimated effort: `1-2 days`
- Current state: `Completed on 2026-03-30`
- Why this is first:
  - A red baseline makes every later refactor less trustworthy.
  - The repo is now back to a stable green baseline: `191 / 191` tests pass.
- Focus:
  - Fix `PAC_Tests.render_model_cache_contract` for
    `assets/meshes/growl_1275_mesh.glb` UV preservation.
- Exit criteria:
  - `tools/full_check.ps1` completes successfully end to end.

### 2. Make the reusable VFX boundary real
- Rank: `#2`
- Payoff/day: `High`
- Estimated effort: `3-6 days`
- Current state: `Completed on 2026-03-30`
- Why this is early:
  - It directly supports the long-term goal that `src/vfx/` survives replacing
    or deleting `src/game/`.
  - The current gap is concrete and narrow enough to attack without rewriting
    the whole renderer.
- Focus:
  - Remove `game::runtime` dependencies from:
    - `src/vfx/runtime/growl/SharedGrowlWaveBatches.*`
    - `src/vfx/runtime/growl/SharedGrowlWaveBridge.*`
    - `src/vfx/preview/growl/GrowlSharedRenderer.*`
  - Introduce thin adapter seams for mesh loading, texture lookup, and indexed
    batch submission so game-specific bindings live outside `src/vfx/`.
- Exit criteria:
  - Reusable VFX runtime/preview code no longer includes `game/runtime/*`
    headers directly.
  - Game-specific adapters are the only place that translate into
    `game::runtime` types.

### 3. Split `GameRunner.cpp` into clearer ownership slices
- Rank: `#3`
- Payoff/day: `High`
- Estimated effort: `3-5 days`
- Current state: `Completed on 2026-03-31`
- Why this is before `GameSession.cpp`:
  - `GameRunner.cpp` is broad but still easier to cut cleanly than the deeper
    session/runtime seam.
  - It is one of the most frequent coordination points for runtime changes.
- Progress so far:
  - Window/video presentation state now lives in
    `src/game/runtime/video/RuntimeWindowPresentationController.*`.
  - Initial saved-window / startup-override bootstrap now lives in
    `src/game/runtime/video/RuntimeGameRunnerWindowBootstrap.*`.
  - Renderer recovery / OpenGL fallback bootstrap now lives in
    `src/game/runtime/renderer/RuntimeGameRunnerRendererBootstrap.*`.
  - Post-renderer activation, font init, default-camera creation, and initial
    loading-frame priming now live in
    `src/game/runtime/startup/RuntimeGameRunnerStartupFinalize.*`.
  - The SDL event pump now lives in
    `src/game/runtime/loop/RuntimeGameRunnerEventPump.*`.
  - Frame observation, perf-summary emission, and Growl terminal logging now
    live in `src/game/runtime/loop/RuntimeGameRunnerFrameDiagnostics.*`.
  - Steady-state fixed-step/render/present execution now lives in
    `src/game/runtime/loop/RuntimeGameRunnerFrameExecution.*`.
  - The outer relaunch entry now lives in
    `src/game/runtime/RuntimeGameRunnerEntry.cpp`, while
    `GameRunner.cpp` only owns single-session startup/run/shutdown behavior.
  - `src/game/runtime/GameRunner.cpp` dropped from about `900` lines to about
    `400` lines and now delegates window metrics, live VSync, video-mode
    persistence, uncapped-window normalization, startup window policy, renderer
    fallback bootstrap, post-renderer presentation startup, SDL event
    dispatch, frame execution, and frame diagnostics/logging instead of
    storing those policies inline.
- Exit criteria:
  - `src/game/runtime/GameRunner.cpp` becomes a thin coordinator.
  - The extracted helpers each own one obvious concern.

### 4. Unify runtime/tooling logging around one path
- Rank: `#4`
- Payoff/day: `High`
- Estimated effort: `2-4 days`
- Current state: `In progress on 2026-03-31`
- Why this is worth doing early:
  - This is a small-to-medium cleanup that improves debuggability everywhere.
  - It reduces the chance that future regressions hide in ad hoc console prints.
- Progress so far:
  - Added `src/engine/utils/LogSink.*` as the first shared logging helper for
    runtime/tooling diagnostics.
  - Converted runtime startup/session/window/bootstrap surfaces to use the sink
    while preserving stream-captured contract tests.
  - Converted runner loop-policy, relaunch, and frame-diagnostics emission to
    the sink so new runner reporting is no longer hand-writing raw stream
    output.
  - Converted `GameBootstrap.cpp`, `GamePreload.cpp`, and
    `session/SessionStartupRuntime.cpp` so broader startup-path diagnostics now
    follow the same helper instead of ad hoc console writes.
  - Converted startup helper seams
    `RuntimeWorldLayerPrewarm.*`,
    `RuntimeRenderModelPrewarm.*`, and
    `RuntimeStartupAssetPrewarm.*` to the same sink path, keeping their
    existing stream-captured tests green while removing more raw startup
    console logging contracts.
  - Converted `GameSession.cpp` model-cache failure and shutdown diagnostics,
    Tail Fire coordinator/atlas/billboard debug logging, and shared capture
    model warnings so those secondary runtime surfaces now share the same
    helper too.
  - Converted Tail Fire config-load warnings, authored-flipbook prewarm logs,
    and OpenGL/D3D12 Tail Fire texture-upload diagnostics to the same helper,
    which keeps the Tail Fire path consistent from config load through backend
    upload.
  - Converted `engine/runtime/Application.cpp`, D3D12 renderer startup /
    screenshot lifecycle logs, and model-cache debug traces so step 4 is now
    crossing from game/runtime code into the engine-side startup and renderer
    seams too.
  - Added `TailFireDebug` as a first-class terminal log mode alongside
    `Performance` and `Growl VFX`, and threaded Tail Fire anchor/billboard
    debug emission through the runtime mode path while keeping
    `PAC_TAIL_FIRE_DEBUG` as a force-on override.
  - Converted Growl preview/tool mesh-failure and hot-reload logs plus
    `VfxPreviewApp` screenshot / warning diagnostics to the same helper.
  - Added `tests/TestLogSink.cpp`, and `tools/full_check.ps1` is green with
    `191 / 191` tests after the latest pass.
  - The repo's remaining direct `std::cout` / `std::cerr` references across
    `src/` and `tools/` are down to about `181`.
- Focus:
  - Stop adding new direct `std::cout` / `std::cerr` usage in runtime/tooling.
  - Introduce one small logging helper or policy layer for startup, preview,
    and runtime diagnostics.
  - Keep new logging surfaces tag-shaped so feature-scoped terminal modes can
    grow later without another large cleanup pass.
  - Prefer adding new feature-specific debug output as explicit terminal modes
    when the code path is specialized enough to deserve its own signal.
  - Convert the noisiest runtime/tooling call sites first rather than trying to
    boil the ocean in one pass.
- Exit criteria:
  - New diagnostics use one agreed path.
  - The highest-churn runtime/tooling surfaces no longer depend on raw stream
    writes.

### 5. Split `GameSession.cpp` by lifecycle vs render vs snapshot concerns
- Rank: `#5`
- Payoff/day: `Medium-high`
- Estimated effort: `5-8 days`
- Current state: `Completed first pass on 2026-03-31`
- Why this comes after `GameRunner.cpp`:
  - It is valuable, but riskier and more coupled.
  - It is easier once the outer runtime seam is cleaner.
- Progress so far:
  - Debug snapshot save/load/auto-load control now lives in
    `src/game/runtime/session/SessionSnapshotController.*` instead of being
    assembled inline inside `GameSession.cpp`.
  - World-layer submission and state-script routing now live in
    `src/game/runtime/session/SessionWorldLayerBridge.*`, so the session no
    longer hand-builds `SessionWorldRenderRuntime` args in two separate places.
  - Backend mesh/texture cache ownership, backend hydration glue, and startup
    backend-asset prewarm callback wiring now live in
    `src/game/runtime/session/SessionBackendAssetBridge.*`.
  - Startup runtime argument assembly now lives in
    `src/game/runtime/session/SessionStartupBridge.*`, so session startup/setup
    no longer hand-builds the full prewarm callback surface inline.
  - Input/fixed-update callback assembly now lives in
    `src/game/runtime/session/SessionLoopBridge.*`, and frame/render flow
    orchestration now lives in `src/game/runtime/session/SessionRenderBridge.*`.
  - Backend inventory dependency assembly and refresh handling now live in
    `src/game/runtime/session/SessionInventoryBridge.*`, and shutdown lifecycle
    teardown now lives in `src/game/runtime/session/SessionLifecycleBridge.*`.
  - Startup/init assembly now also lives in
    `src/game/runtime/session/SessionInitBridge.*`, so the session no longer
    hand-builds its startup bootstrap context inline.
  - Final session-level context assembly now lives in
    `src/game/runtime/session/SessionCoordinatorBridge.*`, which delegates
    snapshot, loop, render, world-layer, and lifecycle work through the
    smaller bridges.
  - `src/game/runtime/session/GameSession.cpp` is down from about `740` lines
    to about `340` lines locally after the current step-5 slices.
- Focus:
  - Extract session bootstrap/setup helpers.
  - Extract debug snapshot and restore helpers.
  - Extract render-runtime wiring and keep session state ownership local.
- Exit criteria:
  - `src/game/runtime/session/GameSession.cpp` stops acting like a catch-all.
  - Session initialization, debug state IO, and render wiring are easier to
    reason about independently.

### 6. Narrow `IRenderBackend` and reduce backend mega-file blast radius
- Rank: `#6`
- Payoff/day: `Medium`
- Estimated effort: `1-2 weeks`
- Why this is not first:
  - It matters, but it is easier to do badly than it looks.
  - It should be informed by the current runtime and projected-render cleanup,
    not done in isolation.
- Focus:
  - Split capability/debug/world/sprite/timing concerns into clearer role-based
    interfaces or supporting helper surfaces.
  - Keep `OpenGL` and `D3D12` implementations aligned while reducing unrelated
    change collisions.
- Exit criteria:
  - `src/engine/render/IRenderBackend.h` is materially smaller and clearer.
  - Common backend edits no longer require touching giant unrelated files as
    often.

### 7. Continue projected-render restructuring for CPU cost and clarity
- Rank: `#7`
- Payoff/day: `Medium`
- Estimated effort: `ongoing`
- Why this remains important:
  - It is still the biggest steady-state rendering cost center.
  - It affects both maintainability and performance.
- Focus:
  - Keep shrinking the shared projected build/submission hot path.
  - Continue moving from broad kitchen-sink projected helpers toward smaller
    composition seams.
- Exit criteria:
  - Shared projected runtime files keep getting smaller and more specialized.
  - `render_build_ms` trends down in representative scenes.

### 8. Automate more of preview and performance verification
- Rank: `#8`
- Payoff/day: `Medium`
- Estimated effort: `1-2 weeks`
- Why this is later:
  - It is valuable, but it pays off more after the structural seams above are
    cleaner.
- Focus:
  - Add a protected perf baseline in CI.
  - Add screenshot or image-diff smoke for key preview/runtime visual cases
    where practical.
  - Keep manual smoke as a supplement, not the primary truth source.
- Exit criteria:
  - Perf regressions are harder to merge accidentally.
  - A few critical preview/runtime visuals are checked automatically.

## Best Next 30-Day Sequence
1. Finish the remaining logging-unification pass on deeper session/renderer
   diagnostics and the still-specialized capture/debug surfaces.
2. Reassess before starting the larger `GameSession.cpp` split.
3. Continue projected-render restructuring in the hottest shared build paths.
4. Add at least one automated preview/perf guardrail.
5. Revisit `GameRunner` only if a new concrete ownership smell appears.

## What To Avoid Right Now
- Do not do a big-bang renderer rewrite.
- Do not try to split every large file at once.
- Do not paper over the reusable VFX goal with docs alone; the boundary has to
  become true in code.
- Do not add automation that relies on unstable manual setup steps.

## Expected Grade Movement
- If steps `3-5` land cleanly, the repo likely moves from about `8.0 / 10` to
  roughly the low-to-mid `8`s.
- If `GameSession.cpp`, `IRenderBackend`, and projected-render restructuring
  also improve materially, the repo can credibly move into the mid `8`s.
- Pushing beyond that likely requires stronger automated perf/visual validation
  and continued renderer simplification.
