# Repo Assessment

Status: Active
Type: Assessment
Last updated: 2026-03-30

This is a living repo-health assessment. Update it when the overall
maintainability read changes in a meaningful way, not on every small edit.

Execution plan: `REPO_CLEANUP_ROADMAP.md`

## Current Grade
- Overall production-readiness grade: `8.0 / 10`
- Read this as: stronger than a typical solo C++ game repo in structure,
  testing, and docs discipline, but still carrying enough concentration and
  architectural drag that it would be high-friction to scale up without more
  cleanup.

## Category Breakdown
| Category | Grade | Notes |
| --- | --- | --- |
| Code health | `7.8 / 10` | Most code is readable and intentional, but several renderer/runtime hotspots are still too dense. |
| Maintainability | `7.2 / 10` | Recent cleanup helped, yet major change risk is still concentrated in a small number of large files. |
| Modularity and boundaries | `7.8 / 10` | Engine/game split is real, and Growl now has a true reusable VFX boundary; the remaining drag is in broader runtime/renderer seams. |
| Repo organization | `8.5 / 10` | Top-level structure, naming, and docs organization are strong. |
| Testing and verification | `8.6 / 10` | Full check covers docs, build, and 185 tests; the main remaining downside is manual visual/perf verification. |
| Production discipline | `7.5 / 10` | Build flags, parity contracts, and hygiene are good; perf and visual validation are not yet fully automated. |

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
   - `src/game/runtime/session/GameSession.cpp`,
     `src/game/runtime/GameRunner.cpp`, and
     `src/engine/render/IRenderBackend.h` still centralize too much policy and
     coordination.

3. Visual and performance verification still lean on manual discipline.
   - Tooling is stronger than before, but preview correctness and perf baselines
     are not yet protected by the same level of automation as contracts/builds.

4. Observability is still inconsistent.
   - The repo still contains over `240` direct `std::cout` / `std::cerr` calls
     across `src/` and `tools/`, so logging style and diagnostics flow are not
     yet unified.

## Main Risks
- `src/game/runtime/session/GameSession.cpp` is still the largest runtime
  concentration seam.
- `src/game/runtime/GameRunner.cpp` still centralizes too much launch, loop,
  and runtime policy.
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

## Current Repo-Level Red Flags
- There is still no automated perf baseline gate in CI.
- The renderer restructuring story is only partially complete: Phase 1 ideas
  have landed in code, but the larger submission/dataflow work is still ahead.

## Current Priority Order
1. Keep shrinking `GameSession.cpp` and `GameRunner.cpp` into clearer ownership
   seams.
2. Continue renderer restructuring work that reduces shared projected
   render-build CPU cost.
3. Reduce manual-only preview and perf validation where practical.
4. Keep the docs honest as the repo changes, especially around renderer,
   tooling, and VFX ownership boundaries.
