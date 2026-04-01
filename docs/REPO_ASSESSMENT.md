# Repo Assessment

Status: Active
Type: Assessment
Last updated: 2026-03-31

This is a living repo-health assessment. Update it when the overall
maintainability read changes in a meaningful way, not on every small edit.

Execution plan: `REPO_CLEANUP_ROADMAP.md`

## Current Grade
- Overall production-readiness grade: `8.6 / 10`
- Read this as: stronger than a typical solo C++ game repo in structure,
  testing, and docs discipline, but still carrying enough concentration and
  architectural drag that it would be high-friction to scale up without more
  cleanup.

## Category Breakdown
| Category | Grade | Notes |
| --- | --- | --- |
| Code health | `8.2 / 10` | Most code is readable and intentional, and the worst projected-runtime kitchen-sink files have now been broken into smaller seams. |
| Maintainability | `8.3 / 10` | Recent cleanup helped materially; the main remaining concentration is now more in backend mega-files and broad renderer interfaces than in the outer runtime/session or projected-runtime owners. |
| Modularity and boundaries | `8.3 / 10` | Engine/game split is real, Growl now has a true reusable VFX boundary, the renderer interface has a first role split, and the projected runtime now has clearer local seams. |
| Repo organization | `8.6 / 10` | Top-level structure, naming, docs organization, and projected-runtime folder layout are strong. |
| Testing and verification | `8.8 / 10` | Full check covers docs, build, and 196 tests; the main remaining downside is manual visual/perf verification. |
| Production discipline | `8.0 / 10` | Build flags, parity contracts, hygiene, and runtime/tooling logging discipline are improving materially; perf and visual validation are not yet fully automated. |

## Current Overall Read
- Strong prototype-to-production-minded C++ game/engine repo with unusually
  good layering, testing, and renderer/perf discipline for a solo project.
- The biggest score drag is still concentration: renderer coordination and a
  few broad engine/runtime interfaces still land in a small number of large
  files.
- The docs/tooling/VFX cleanup materially improved clarity, but the main
  runtime/render seams are still the long pole.
- The projected runtime cleanup is now through a meaningful first pass: the
  old concentrated backend-mesh, world-scene, and projected VFX hotspots now
  have dedicated helper seams instead of one or two giant kitchen-sink files.
- The projected family is also easier to navigate now: `core`, `unit`,
  `backend_mesh`, `world_scene`, and `world_vfx` make the ownership split more
  visible at the filesystem level instead of relying only on file prefixes.
- That projected-render split is now broader than a single fast path: the
  backend-mesh renderer also delegates persistent-item sync, indexed CPU
  rewrite/finalization, triangle prep, and the fallback triangle submission
  loop to dedicated helpers, which makes the remaining file read more like
  orchestration than implementation soup.
- The projected runtime now also has a dedicated cached indexed-batch builder
  and a shared GPU skin-batch-state helper, which is the right long-term
  direction: the main backend-mesh renderer is getting smaller without
  reintroducing the same clip-skin state-matching logic in multiple places.
- The neighboring support surface is now following the same direction:
  `SharedProjectedUnitBackendMeshSupport.cpp` no longer owns the fast-textured
  material and geometry template cache builders inline, which lowers the risk
  that projected-runtime cache work and general support-policy work keep
  colliding in the same file.
- The projected-world seams are also getting more honest: world-scene
  trace/env/file logging now lives in
  `src/game/runtime/shared/projected/world_scene/SharedProjectedUnitWorldSceneTrace.*`,
  and cached board/bench 3D geometry ownership now lives in
  `src/game/runtime/shared/projected/world_scene/SharedProjectedBoardBenchGeometryCache.*`
  instead of hiding inside the larger renderer/helper files.
- The shared projected world bridge layer is also much more honest now:
  `SharedProjectedWorldVfxBridges.cpp` is just a coordinator, while Growl,
  particle, Tail Fire, and capture routing each have their own dedicated homes
  under `src/game/runtime/shared/projected/` instead of being mixed into the
  same file as board/depth/model utilities.
- The world-scene fast-path renderer itself is also getting narrower:
  scratch-vector ownership, transform initialization, and GPU skin-batch-state
  resolution now live in
  `src/game/runtime/shared/projected/world_scene/SharedProjectedUnitWorldSceneBatchState.*`
  instead of being embedded inline in the renderer body.
- Growl's reusable boundary is now materially real: runtime batching/submission
  is reusable, game translation lives at the edge, and `VfxLab` no longer needs
  `game/runtime/*` headers to render Growl.
- `GameRunner.cpp` is no longer carrying its full window/video presentation
  state inline; that state now lives in
  `src/game/runtime/video/RuntimeWindowPresentationController.*`, which is a
  real maintainability improvement.
- `GameRunner.cpp` also no longer owns its full renderer recovery / OpenGL
  fallback bootstrap lambda forest inline; that logic now lives in
  `src/game/runtime/renderer/RuntimeGameRunnerRendererBootstrap.*`.
- `GameRunner.cpp` also shed its startup window policy and post-renderer
  presentation finalization into
  `RuntimeGameRunnerWindowBootstrap.*` and
  `RuntimeGameRunnerStartupFinalize.*`, narrowing the remaining file to more
  obviously runner-centric orchestration.
- `GameRunner.cpp` now also delegates SDL event polling/translation through
  `src/game/runtime/loop/RuntimeGameRunnerEventPump.*`, which trims more
  loop-specific plumbing out of the runner and keeps input/window dispatch in a
  named seam.
- `GameRunner.cpp` now also delegates frame observation, perf-summary emission,
  and Growl terminal logging through
  `src/game/runtime/loop/RuntimeGameRunnerFrameDiagnostics.*`, which gives the
  runner loop a clearer separation between execution and reporting.
- `GameRunner.cpp` now also delegates steady-state fixed-step/render/present
  execution through
  `src/game/runtime/loop/RuntimeGameRunnerFrameExecution.*`, which leaves the
  remaining loop more obviously about policy and orchestration.
- The outer relaunch entry now lives in
  `src/game/runtime/RuntimeGameRunnerEntry.cpp`, so `GameRunner.cpp` no longer
  owns the relaunch wrapper around single-session startup/run/shutdown.
- Runtime/tooling observability is improving: startup/session bootstrap,
  runner loop policy/diagnostics, relaunch handling, Growl preview logging, and
  `VfxPreviewApp` screenshot / warning diagnostics now share
  `src/engine/utils/LogSink.*` instead of each hand-writing raw stream output.
- The startup/runtime logging first pass now also covers
  `GameBootstrap.cpp`, `GamePreload.cpp`, and
  `session/SessionStartupRuntime.cpp`, so the outer startup path is materially
  less fragmented than it was before this step.
- That logging cleanup now also covers startup prewarm helpers
  (`RuntimeWorldLayerPrewarm.*`, `RuntimeRenderModelPrewarm.*`,
  `RuntimeStartupAssetPrewarm.*`), `GameSession.cpp` model-cache/shutdown
  diagnostics, Tail Fire coordinator/atlas/billboard debug output, and shared
  capture model warnings, so the remaining mixed logging surface is more
  specialized than it was before.
- Tail Fire logging is now more internally consistent end to end: config-load
  warnings, authored-flipbook prewarm reporting, CPU atlas/debug diagnostics,
  and backend upload logs all use the same helper path and consistent tags,
  which also leaves room for feature-scoped terminal log modes later.
- That terminal-mode story is now more concrete too: `Performance`,
  `Growl VFX`, and `Tail Fire Debug` are all first-class runtime modes, while
  `PAC_TAIL_FIRE_DEBUG` still works as a force-on override for targeted local
  investigation.
- Engine-side observability is starting to follow the same rules too:
  `Application.cpp`, D3D12 startup/screenshot lifecycle logs, and model-cache
  debug traces now use the shared sink instead of writing raw streams directly.

## What Is Strong
- Engine/game layering is real, not aspirational.
- The top-level reusable `src/vfx/` split is intentional and now documented as
  a reusable engine surface instead of game-only glue.
- The headless and contract-test surface is strong relative to the size of the
  project.
- Shared gameplay presentation across `OpenGL` and `D3D12` is backed by docs,
  contracts, and perf vocabulary rather than vague parity claims.
- The docs set now has a live-vs-archive split plus doc-type metadata, which
  makes the repo easier to navigate and maintain.
- The build and validation path is serious for a repo of this size:
  `tools/full_check.ps1` is currently green end to end.

## Biggest Score Reducers
1. Responsibility concentration is still high.
   - The top 10 C++ files account for about `13.5%` of the repo's C++ surface;
     the top 20 account for about `22.2%`.
   - The biggest hotspots are renderer/runtime files such as
      `D3D12RenderBackendWorldPipeline.cpp`, `SessionWorldBackdrop.cpp`,
      `src/game/runtime/shared/projected/unit/SharedProjectedUnitRenderer.cpp`,
      `OpenGLRenderBackendWorldDraw.cpp`, and `D3D12RenderBackendWorldDraw.cpp`.

2. Renderer seams are still broader than ideal.
   - `src/engine/render/IRenderBackend.h` has a meaningful first split now, but
     backend mega-files and the remaining top-level backend surface are still
     broader than they should be.
   - The shared projected runtime is now much more decomposed: the backend-mesh
     renderer, world-scene renderer, trace, cached board/bench geometry,
     sidecar Tail Fire assembly, world-scene submission, Growl bridge, and
     particle/Tail Fire bridge now have dedicated homes. The remaining
     concentration points are now more in neighboring files such as
     `SharedProjectedUnitRenderer.cpp`,
     `SharedProjectedUnitBackendMeshTransforms.cpp`,
     `SharedProjectedUnitBackendMeshPrep.cpp`, and the backend mega-files than
     in one giant projected kitchen-sink file.

3. Visual and performance verification still lean on manual discipline.
   - Tooling is stronger than before, but preview correctness and perf baselines
     are not yet protected by the same level of automation as contracts/builds.

4. Observability is still inconsistent.
   - The repo still contains about `181` direct `std::cout` / `std::cerr` calls
     across `src/` and `tools/`, so logging style and diagnostics flow are not
     yet unified.

## Main Risks
- `src/engine/render/IRenderBackend.h` and the backend mega-files still absorb
  broad change risk.
- Shared projected render/build CPU remains the main steady-state renderer cost
  center.
- Preview visual validation and some renderer/perf validation still rely on too
  much manual discipline.

## What Improved Recently
- Live docs are now typed and indexed more clearly.
- The old raw perf journal was split into a concise active decisions doc plus an
  archived experiment log.
- Reusable VFX ownership and preview-tool separation are more explicit.
- Growl preview duplication was removed, and Tail Fire preview/runtime policy is
  better aligned.
- Growl's shared runtime/preview path now uses neutral mesh/batch types, a
  reusable indexed submit helper, and game-side adapters instead of direct
  `game::runtime` dependencies.
- `GameRunner.cpp` shed its window/video presentation cluster into
  `RuntimeWindowPresentationController.*`, which makes the runner more
  orchestration-focused and gives the runtime video policy a dedicated home.
- `GameRunner.cpp` also shed its renderer fallback/bootstrap cluster into
  `RuntimeGameRunnerRendererBootstrap.*`, which narrows the remaining runner
  work to startup orchestration, loop control, and restart policy.
- `GameRunner.cpp` further shed startup window policy and post-renderer
  presentation finalization into dedicated helpers, reducing its size from
  roughly `900` lines to roughly `530`.
- `GameRunner.cpp` also shed its SDL event pump into
  `RuntimeGameRunnerEventPump.*`, leaving the remaining loop more focused on
  fixed-step, render, perf capture, and restart orchestration.
- `GameRunner.cpp` also shed its frame diagnostics/logging path into
  `RuntimeGameRunnerFrameDiagnostics.*`, reducing its size from roughly `900`
  lines to roughly `470`.
- `GameRunner.cpp` also shed steady-state frame execution into
  `RuntimeGameRunnerFrameExecution.*` and the relaunch entry into
  `RuntimeGameRunnerEntry.cpp`, reducing its size further to roughly `400`
  lines.
- `GameSession.cpp` has now started the same treatment: debug snapshot
  save/load/auto-load control moved into
  `src/game/runtime/session/SessionSnapshotController.*`, and world-layer
  render submission plus state-script routing moved into
  `src/game/runtime/session/SessionWorldLayerBridge.*`.
- `GameSession.cpp` also shed backend mesh/texture cache ownership, backend
  hydration glue, and startup backend-asset prewarm callback wiring into
  `src/game/runtime/session/SessionBackendAssetBridge.*`.
- `GameSession.cpp` startup runtime argument assembly now also lives in
  `src/game/runtime/session/SessionStartupBridge.*`, trimming the session file
  further from roughly `740` lines to roughly `550`.
- `GameSession.cpp` now also delegates input/fixed-update callback assembly to
  `src/game/runtime/session/SessionLoopBridge.*` and frame/render orchestration
  to `src/game/runtime/session/SessionRenderBridge.*`, trimming the session
  file further to roughly `480` lines.
- `GameSession.cpp` backend inventory dependency assembly and refresh handling
  now also live in `src/game/runtime/session/SessionInventoryBridge.*`, and
  shutdown lifecycle teardown now lives in
  `src/game/runtime/session/SessionLifecycleBridge.*`, trimming the session
  file further to roughly `450` lines.
- `GameSession.cpp` also shed startup/init assembly into
  `src/game/runtime/session/SessionInitBridge.*` and final coordination glue
  into `src/game/runtime/session/SessionCoordinatorBridge.*`, bringing the
  session file down to roughly `340` lines and leaving it much closer to a thin
  owner/orchestrator than a catch-all runtime bag.
- `IRenderBackend.h` has now started a first real narrowing pass: shared
  backend payload types moved into `RenderBackendTypes.h`, and frame/world/debug
  concerns now have dedicated role interfaces under the same top-level backend
  type. That does not solve backend blast radius yet, but it is a meaningful
  foundation for step 6.
- D3D12 backend ownership is also narrowing at the source-file level: pipeline
  creation now lives in focused private translation units
  `src/engine/render/d3d12/D3D12RenderBackendWorldPipeline.cpp`,
  `src/engine/render/d3d12/D3D12RenderBackendSpritePipeline.cpp`, and
  `src/engine/render/d3d12/D3D12RenderBackendDebugPipeline.cpp`, with shared
  shader compile/cache logic in
  `src/engine/render/d3d12/D3D12RenderBackendPipelineCompile.cpp`. That is a
  cleaner long-term shape than the earlier include-driven split and removes the
  old `D3D12RenderBackendPipelines.cpp` choke point entirely.
- OpenGL backend ownership is also starting to narrow in a similar style:
  cached world-mesh management, cached draw wrappers, batch-submission state,
  and world-prewarm helpers now live in dedicated private translation units
  under `src/engine/render/opengl/`, which brings
  `OpenGLRenderBackendWorldDraw.cpp` below the four-digit line range and makes
  future cache/prewarm edits less likely to collide with material/shader-path
  changes.
- D3D12 world draw has also started the same transition: non-instanced
  world-draw entrypoints and the cached front door now live in
  `src/engine/render/d3d12/D3D12RenderBackendWorldDrawEntryPoints.cpp`, which
  separates the public renderer surface from the heavier internal cached and
  instanced implementation block.
- The renderer cleanup now lands in conventional private `*.cpp` files instead
  of long-term `.inl` seams, which is a better steady-state outcome for
  maintainability and code navigation.
- Shared projected runtime cleanup has now started landing in the same style:
  the direct fast-textured world-batch branch was pulled out of
  `SharedProjectedUnitBackendMeshRenderer.cpp` into
  `SharedProjectedUnitBackendMeshFastPath.*`, which makes the projected
  backend-mesh file less monolithic without reopening the higher-risk indexed
  fallback branch yet.
- Projected render-item sync and scene-pose hashing are now also centralized
  in `SharedProjectedUnitBackendMeshPersistentItems.*`, which is a quieter
  improvement than the fast-path extraction but still reduces duplication and
  makes the projected backend-mesh file more about orchestration than cache
  plumbing.
- The projected backend-mesh renderer now also delegates its fallback
  per-triangle submission loop to
  `SharedProjectedUnitBackendMeshTriangleLoop.*`, which trims another dense
  chunk of batch/material/triangle plumbing out of the main file.
- The same file also no longer carries its cached indexed-batch build block
  inline: that work now lives in
  `SharedProjectedUnitBackendMeshCachedIndexedBatches.*`, while shared GPU
  clip-skin batch-state resolution lives in
  `SharedProjectedUnitBackendMeshGpuSkinBatchState.*`.
- `SharedProjectedUnitBackendMeshSupport.cpp` has also started to narrow:
  fast-textured material template caching now lives in
  `SharedProjectedUnitBackendMeshMaterialTemplateCache.cpp`, and fast-textured
  geometry template caching now lives in
  `SharedProjectedUnitBackendMeshGeometryTemplateCache.cpp`.
- The indexed fast-textured CPU rewrite/cache branch now also lives in
  `SharedProjectedUnitBackendMeshCpuRewrite.*`, which is another meaningful
  step toward smaller projected hot-path responsibilities even though the
  overall indexed fallback branch is still too broad.
- Indexed batch finalization/world handoff now also lives in
  `SharedProjectedUnitBackendMeshIndexedFinalize.*`, and triangle-to-node /
  rigid-node GPU palette setup now lives in
  `SharedProjectedUnitBackendMeshTrianglePrep.*`. Together those cuts bring
  `SharedProjectedUnitBackendMeshRenderer.cpp` below the four-digit line range
  and leave it noticeably closer to orchestration than to a kitchen-sink
  implementation file.
- The projected world/VFX side is now at the same level of honesty: the
  world-scene renderer delegates trace, batch-state resolution, authored Tail
  Fire sidecar assembly, and render-object submission to dedicated helpers, and
  the old projected VFX bridge blob has become a thin coordinator over
  dedicated Growl, particle, Tail Fire, and capture bridge files.
- The projected-runtime family is now also organized into coarse concern
  folders under `src/game/runtime/shared/projected/`, which makes the new seam
  structure easier to maintain and harder to accidentally flatten again.
- Runtime/tooling diagnostics now have a first shared logging helper in
  `src/engine/utils/LogSink.*`, and the noisiest startup/runner/Growl preview
  surfaces now use it while preserving the existing stream-captured contracts.
- The broader startup path also now routes `GameBootstrap.cpp`,
  `GamePreload.cpp`, and `session/SessionStartupRuntime.cpp` through that
  helper, which means the most common runtime startup logs are no longer all
  special-case console writes.
- Startup prewarm helpers, `GameSession.cpp` model-cache/shutdown logs, Tail
  Fire coordinator/atlas/billboard diagnostics, and shared capture model
  warnings now also route through `LogSink`, which makes the remaining logging
  inconsistency narrower and more intentional than before.
- Engine-side `Application.cpp`, D3D12 lifecycle startup/screenshot logs, and
  model-cache debug traces now also route through `LogSink`, which means step 4
  is no longer confined to only game/runtime call sites.

## Current Repo-Level Red Flags
- There is still no automated perf baseline gate in CI.
- The renderer restructuring story is only partially complete: Phase 1 ideas
  have landed in code, but the larger submission/dataflow work is still ahead.

## Current Priority Order
1. Reduce manual-only preview and perf validation where practical.
2. Revisit the remaining renderer/model ownership seams, including the
   lingering `Model.cpp` internal `.inl`, once the projected hot path is less
   dense.
3. Revisit projected-render CPU hotspots only if fresh measurement or parity
   work points to a concrete remaining concentration point.
4. Keep the docs honest as the repo changes, especially around renderer,
   tooling, and VFX ownership boundaries.

