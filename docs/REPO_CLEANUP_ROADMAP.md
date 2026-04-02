# Repo Cleanup Roadmap

Status: Active
Type: Roadmap
Last updated: 2026-04-01

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
  - The repo is now back to a stable green baseline: `196 / 196` tests pass.
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
    - `src/vfx/runtime/shared/SharedAuthoredVfxBatches.*`
    - `src/vfx/runtime/shared/SharedAuthoredVfxBridge.*`
    - `src/vfx/preview/shared/SharedAuthoredVfxRenderer.*`
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
- Current state: `Completed first pass on 2026-03-31`
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
    `196 / 196` tests after the latest pass.
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
- Current state: `Completed first pass on 2026-03-31`
- Why this is not first:
  - It matters, but it is easier to do badly than it looks.
  - It should be informed by the current runtime and projected-render cleanup,
    not done in isolation.
- Progress so far:
  - Shared backend payload types now live in
    `src/engine/render/RenderBackendTypes.h` instead of being declared inline
    inside `IRenderBackend.h`.
  - `IRenderBackend` now composes role-oriented seams through
    `src/engine/render/IRenderBackendFrame.h`,
    `src/engine/render/IRenderBackendWorld.h`, and
    `src/engine/render/IRenderBackendDebug.h`, which makes the top-level
    backend surface materially smaller and easier to reason about.
  - A compile-time contract now guards that split through
    `tests/TestRenderBackendInterfaceSplit.cpp`.
  - D3D12 pipeline creation now lives in focused private translation units
    `src/engine/render/d3d12/D3D12RenderBackendWorldPipeline.cpp`,
    `src/engine/render/d3d12/D3D12RenderBackendSpritePipeline.cpp`, and
    `src/engine/render/d3d12/D3D12RenderBackendDebugPipeline.cpp`, with shared
    shader compile/cache logic in
    `src/engine/render/d3d12/D3D12RenderBackendPipelineCompile.cpp`.
  - OpenGL world cached-mesh ownership, cached draw wrappers, batch-submission
    state restoration, and world-prewarm helpers now live in
    `src/engine/render/opengl/OpenGLRenderBackendWorldCachedMesh.cpp`,
    `src/engine/render/opengl/OpenGLRenderBackendWorldBatchSubmission.cpp`,
    and `src/engine/render/opengl/OpenGLRenderBackendWorldPrewarm.cpp`,
    bringing `OpenGLRenderBackendWorldDraw.cpp` below the four-digit line range
    and making cache/prewarm edits less likely to collide with the core draw
    path.
  - D3D12 non-instanced world-draw entrypoints and the cached front door now
    live in `src/engine/render/d3d12/D3D12RenderBackendWorldDrawEntryPoints.cpp`,
    which separates the public world-draw surface from the heavier internal
    cached/instanced implementation.
  - The renderer split now lands in conventional private `*.cpp` units instead
    of long-term `.inl` seams, which is the better steady-state shape for this
    codebase.
- Focus:
  - Split capability/debug/world/sprite/timing concerns into clearer role-based
    interfaces or supporting helper surfaces.
  - Keep `OpenGL` and `D3D12` implementations aligned while reducing unrelated
    change collisions.
- Exit criteria:
  - `src/engine/render/IRenderBackend.h` is materially smaller and clearer.
  - Common backend edits no longer require touching giant unrelated files as
    often.
  - New backend ownership seams land as ordinary private translation units
    unless there is a specific reason to prefer template-style include code.

### 7. Continue projected-render restructuring for CPU cost and clarity
- Rank: `#7`
- Payoff/day: `Medium`
- Estimated effort: `ongoing`
- Current state: `Completed first pass on 2026-03-31`
- Why this remains important:
  - It is still the biggest steady-state rendering cost center.
  - It affects both maintainability and performance.
- Progress so far:
  - The first step-7 seam is now out of
    `src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshRenderer.cpp`:
    the direct fast-textured world-batch path now lives in
    `src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshFastPath.*`.
  - Projected render-item sync and scene-pose hash helpers are now centralized
    in
    `src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshPersistentItems.*`,
    which removes more low-level cache/plumbing code from the backend-mesh
    renderer and gives the new fast-path helper the same shared utility seam.
  - The indexed fast-textured CPU rewrite/cache block now lives in
    `src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshCpuRewrite.*`,
    which takes another hot branch out of the backend-mesh renderer without
    changing the remaining indexed fallback structure yet.
  - Indexed batch finalization and world-queue handoff now live in
    `src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshIndexedFinalize.*`,
    which removes another self-contained responsibility from the backend-mesh
    renderer while keeping the indexed fallback path behavior unchanged.
  - Triangle-to-node lookup and rigid-node GPU palette preparation now live in
    `src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshTrianglePrep.*`,
    which makes the remaining per-triangle loop narrower and easier to read.
  - The fallback triangle submission loop now also lives in
    `src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshTriangleLoop.*`,
    which leaves the main backend-mesh renderer more focused on prep,
    fast-path selection, Tail Fire anchor export, and indexed finalization.
  - Cached indexed-batch construction and shared GPU skin-batch-state matching
    now also live in
    `src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshCachedIndexedBatches.*`
    and
    `src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshGpuSkinBatchState.*`,
    which takes another dense middle block out of the backend-mesh renderer and
    avoids copying the same GPU clip-skin batch-state logic across projected
    helper seams.
  - `SharedProjectedUnitBackendMeshSupport.cpp` is also starting to shed its
    heavier template-factory responsibilities: fast-textured material template
    caching now lives in
    `src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshMaterialTemplateCache.cpp`,
    and fast-textured geometry template caching now lives in
    `src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshGeometryTemplateCache.cpp`.
  - That extraction keeps the heaviest indexed fallback logic in place for
    now, but removes one cohesive responsibility from the backend-mesh
    renderer without disturbing the higher-risk debug/perf-sensitive branch.
  - The neighboring projected-world seams are starting to narrow too:
    world-scene trace/env/file logging now lives in
    `src/game/runtime/shared/projected/world_scene/SharedProjectedUnitWorldSceneTrace.*`,
    and cached board/bench 3D geometry ownership now lives in
    `src/game/runtime/shared/projected/world_scene/SharedProjectedBoardBenchGeometryCache.*`.
  - `SharedProjectedWorldSceneHelpers.cpp` is also starting to lose its
    mixed bridge responsibilities: Growl/Tail Fire/particle bridge logic now
    lives in
    `src/game/runtime/shared/projected/world_vfx/SharedProjectedWorldVfxBridges.cpp`,
    and capture-attempt bridge routing now lives in
    `src/game/runtime/shared/projected/world_vfx/SharedProjectedWorldCaptureBridge.cpp`.
  - That projected VFX bridge layer has now taken its next cut too:
    Growl bridge ownership now lives in
    `src/game/runtime/shared/projected/world_vfx/SharedProjectedWorldGrowlBridge.cpp`,
    particle/Tail Fire bridge ownership now lives in
    `src/game/runtime/shared/projected/world_vfx/SharedProjectedWorldParticleVfxBridge.cpp`,
    and `SharedProjectedWorldVfxBridges.cpp` is now just the thin coordinator
    that composes those two calls.
  - `SharedProjectedUnitWorldSceneRenderer.cpp` has now also shed its
    scratch-vector and GPU skin-batch-state resolution block into
    `src/game/runtime/shared/projected/world_scene/SharedProjectedUnitWorldSceneBatchState.*`,
    which keeps the world-scene renderer focused more on eligibility,
    sidecar policy, and render-object submission.
  - The world-scene renderer has now taken another strong cut too: authored
    Tail Fire sidecar assembly now lives in
    `src/game/runtime/shared/projected/world_scene/SharedProjectedUnitWorldSceneTailFireSidecar.*`,
    and render-object submission / item-handle / batch-hash wiring now lives in
    `src/game/runtime/shared/projected/world_scene/SharedProjectedUnitWorldSceneSubmission.*`.
    That brings `SharedProjectedUnitWorldSceneRenderer.cpp` down to roughly
    `237` lines and leaves it much closer to a true orchestration seam.
  - The last clear projected-runtime support/VFX hotspots have now narrowed too:
    `SharedProjectedUnitBackendMeshSupport.cpp` is down under `100` lines after
    shedding graphics-quality handling into
    `src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshGraphicsQuality.cpp`
    and Tail Fire override/policy ownership into
    `src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshTailFireOverride.cpp`.
    The particle/Tail Fire bridge also shed its Tail Fire-specific path into
    `src/game/runtime/shared/projected/world_vfx/SharedProjectedWorldTailFireVfxBridge.*`,
    leaving `SharedProjectedWorldParticleVfxBridge.cpp` closer to a pure
    particle snapshot bridge.
  - The projected-runtime family is now also organized by concern under
    `src/game/runtime/shared/projected/core`,
    `src/game/runtime/shared/projected/unit`,
    `src/game/runtime/shared/projected/backend_mesh`,
    `src/game/runtime/shared/projected/world_scene`, and
    `src/game/runtime/shared/projected/world_vfx`, which makes the new seam
    boundaries more obvious in daily navigation.
- Focus:
  - Preserve the new smaller projected seams instead of re-accumulating broad
    responsibilities into the old kitchen-sink files.
  - Preserve the new folder-level ownership split too; avoid sliding unrelated
    projected helpers back into one flat directory without a clear reason.
  - Prefer targeted follow-up fixes inside the newer helper files over another
    broad projected-runtime rewrite unless a fresh hotspot reappears.
- Exit criteria:
  - Shared projected runtime files are meaningfully smaller and more
    specialized than the old concentrated baseline.
  - `render_build_ms` trends down in representative scenes.

### 8. Automate more of preview and performance verification
- Rank: `#8`
- Payoff/day: `Medium`
- Estimated effort: `1-2 weeks`
- Current state: `Completed first pass on 2026-03-31`
- Why this is later:
  - It is valuable, but it pays off more after the structural seams above are
    cleaner.
- Progress so far:
  - `src/engine/tools/vfx_preview/VfxPreviewApp.cpp` now supports screenshot-driven
    auto-exit plus clean capture flags, which makes the shared preview app
    practical for scripted smoke runs instead of only manual sessions.
  - `tools/vfx_preview_visual_smoke.ps1` now captures deterministic screenshots
    from `VfxLab` and `PAC_VfxPreviewer` and checks small image regions for
    non-background/color content, giving the repo its first automated preview
    visual guardrail.
  - `tools/runtime_visual_smoke.ps1` now captures deterministic gameplay
    screenshots from the pinned Tail Fire starter-line snapshot on `OpenGL` and
    `D3D12`, checks coarse HUD and board regions for plausible content, and
    auto-selects a supported smoke resolution that fits the current display.
  - `tools/full_check.ps1` now supports opt-in preview smoke through
    `-IncludePreviewSmoke` or `PAC_ENABLE_PREVIEW_SMOKE_TESTS=1`.
  - `tools/full_check.ps1` now also supports opt-in runtime visual smoke through
    `-IncludeRuntimeVisualSmoke` or `PAC_ENABLE_RUNTIME_VISUAL_SMOKE_TESTS=1`.
  - `tools/perf_smoke_guard.ps1` now provides a lightweight Release perf smoke
    check against a small local baseline suite under `config/perf/`, covering
    both the Tail Fire starter-line snapshot and a denser planning-state
    gameplay roster snapshot across `OpenGL` and `D3D12`.
  - The perf harness now also pins those scripted snapshot states during
    scoring so route/shop timers do not drift the benchmark into menu or other
    transient states mid-run.
  - That local perf smoke is now display-aware too: each baseline selects the
    largest protected resolution that fits the current primary-display working
    area instead of assuming one monitor size.
  - `tools/full_check.ps1` now also supports opt-in perf smoke through
    `-IncludePerfSmoke`.
  - `tools/full_check.ps1 -IncludePerfSmoke` now prebuilds Release before the
    long Debug gate and runs the perf smoke `-NoBuild`, which keeps the local
    protected perf path stable instead of measuring a just-built hot binary.
  - GitHub Actions now has a dedicated Windows runtime visual smoke lane on
    `workflow_dispatch` and nightly `schedule` for a hosted-runner-safe
    `D3D12` slice, with screenshot artifacts uploaded even on failure.
  - Hosted GitHub Windows runners turned out not to be representative enough
    for stable perf thresholds, so perf smoke remains local-first until we
    have a self-hosted GPU runner or another controlled benchmark environment.
- Focus:
  - Decide which parts of the new smoke suite are stable enough to graduate
    from nightly/manual CI lanes into merge-blocking PR gates.
  - Keep the protected local resolution ladder stable across different display
    sizes so the smoke path stays usable on contributor machines.
  - Decide whether preview smoke should stay local-only or move to a self-hosted
    GPU runner instead of GitHub-hosted Windows.
  - Decide whether perf smoke should stay local-only or move to a self-hosted
    GPU runner instead of GitHub-hosted Windows.
  - Add screenshot or image-diff smoke for key preview/runtime visual cases
    where practical.
  - Keep manual smoke as a supplement, not the primary truth source.
- Exit criteria:
  - Perf regressions are harder to merge accidentally.
  - A few critical preview/runtime visuals are checked automatically.

## Best Next 30-Day Sequence
1. Watch the new nightly/manual CI smoke lanes for flake rate, then decide
   whether the hosted runtime-visual lane is stable enough to become
   merge-blocking on PRs, and whether perf smoke needs a self-hosted GPU lane.
2. Revisit the remaining `Model.cpp` internal `.inl` seam once the projected
   hot path is in a calmer state.
3. Revisit projected-render CPU hotspots only if fresh measurement or parity
   work points to a concrete remaining concentration point.
4. Revisit backend mega-files only if a new concrete renderer ownership smell
   appears outside the seams already extracted.

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

