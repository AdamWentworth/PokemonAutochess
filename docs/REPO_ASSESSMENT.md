# Repo Assessment

Status: Active
Type: Assessment
Last updated: 2026-03-31

This is a living repo-health assessment. Update it when the overall
maintainability read changes in a meaningful way, not on every small edit.

Execution plan: `REPO_CLEANUP_ROADMAP.md`

## Current Grade
- Overall production-readiness grade: `8.4 / 10`
- Read this as: stronger than a typical solo C++ game repo in structure,
  testing, and docs discipline, but still carrying enough concentration and
  architectural drag that it would be high-friction to scale up without more
  cleanup.

## Category Breakdown
| Category | Grade | Notes |
| --- | --- | --- |
| Code health | `7.8 / 10` | Most code is readable and intentional, but several renderer/runtime hotspots are still too dense. |
| Maintainability | `7.6 / 10` | Recent cleanup helped materially; the main remaining concentration is now more in session/renderer surfaces than in the outer runtime runner. |
| Modularity and boundaries | `7.8 / 10` | Engine/game split is real, and Growl now has a true reusable VFX boundary; the remaining drag is in broader runtime/renderer seams. |
| Repo organization | `8.5 / 10` | Top-level structure, naming, and docs organization are strong. |
| Testing and verification | `8.7 / 10` | Full check covers docs, build, and 191 tests; the main remaining downside is manual visual/perf verification. |
| Production discipline | `8.0 / 10` | Build flags, parity contracts, hygiene, and runtime/tooling logging discipline are improving materially; perf and visual validation are not yet fully automated. |

## Current Overall Read
- Strong prototype-to-production-minded C++ game/engine repo with unusually
  good layering, testing, and renderer/perf discipline for a solo project.
- The biggest score drag is still concentration: too much runtime and renderer
  coordination lands in a small number of large files and broad interfaces.
- The docs/tooling/VFX cleanup materially improved clarity, but the main
  runtime/render seams are still the long pole.
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
     `D3D12RenderBackendPipelines.cpp`, `SessionWorldBackdrop.cpp`,
     `SharedProjectedUnitBackendMeshRenderer.cpp`,
     `OpenGLRenderBackendWorldDraw.cpp`, and `D3D12RenderBackendWorldDraw.cpp`.

2. Runtime and renderer seams are still broader than ideal.
   - `src/game/runtime/session/GameSession.cpp` and
     `src/engine/render/IRenderBackend.h` still centralize too much policy and
     coordination, and backend mega-files remain broad.

3. Visual and performance verification still lean on manual discipline.
   - Tooling is stronger than before, but preview correctness and perf baselines
     are not yet protected by the same level of automation as contracts/builds.

4. Observability is still inconsistent.
   - The repo still contains about `181` direct `std::cout` / `std::cerr` calls
     across `src/` and `tools/`, so logging style and diagnostics flow are not
     yet unified.

## Main Risks
- `src/game/runtime/session/GameSession.cpp` is still the largest runtime
  concentration seam.
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
1. Finish the remaining logging-unification pass on secondary runtime/tooling
   surfaces.
2. Keep shrinking `GameSession.cpp` into clearer ownership seams.
3. Continue renderer restructuring work that reduces shared projected
   render-build CPU cost.
4. Reduce manual-only preview and perf validation where practical.
5. Keep the docs honest as the repo changes, especially around renderer,
   tooling, and VFX ownership boundaries.
