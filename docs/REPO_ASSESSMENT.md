# Repo Assessment

Status: Active
Type: Assessment
Last updated: 2026-03-31

This is a living repo-health assessment. Update it when the overall
maintainability read changes in a meaningful way, not on every small edit.

## Current Overall Read
- Strong prototype-scale C++ game/engine repo with unusually good layering,
  testing, and renderer/perf discipline for a solo project.
- The main maintainability cost is still concentration: too much runtime and
  renderer coordination lands in a small number of large files.
- Recent docs and VFX/tooling cleanup improved the repo's clarity, but the main
  runtime/render seams are still the long pole.

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

## Current Repo-Level Red Flags
- `tools/full_check.ps1` is currently red because
  `PAC_Tests.render_model_cache_contract` fails on Growl sparkle UV
  preservation.
- There is still no automated perf baseline gate in CI.
- The renderer restructuring story is only partially complete: Phase 1 ideas
  have landed in code, but the larger submission/dataflow work is still ahead.

## Current Priority Order
1. Restore a clean all-green local quality path by fixing the current
   `render_model_cache_contract` failure.
2. Keep shrinking `GameSession.cpp` and `GameRunner.cpp` into clearer ownership
   seams.
3. Continue renderer restructuring work that reduces shared projected
   render-build CPU cost.
4. Reduce manual-only preview and perf validation where practical.
5. Keep the docs honest as the repo changes, especially around renderer,
   tooling, and VFX ownership boundaries.
