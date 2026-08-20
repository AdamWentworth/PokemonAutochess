# Tech Debt

Status: Active
Type: Tracker
Last updated: 2026-08-20

This file stays intentionally short. It tracks strategic debt that still drives
engineering priority; concrete file-by-file issues live in
`OUTSTANDING_ISSUES.md`.

## Strategic Debt
1. No merge-blocking performance regression gate in CI.
   - A first local Release perf smoke suite now exists and `full_check` now
     runs it through a stable prebuilt-Release path with display-aware
     protected resolution selection.
   - GitHub-hosted Windows turned out not to be a trustworthy perf threshold
     environment for this repo, so PRs are not yet blocked on an automated perf
     gate and perf smoke remains local-first until we have a self-hosted GPU or
     similarly controlled benchmark runner.

2. Shared projected render/build CPU is still the main steady-state hotspot.
   - The projected-runtime first pass is complete, but the next biggest wins
     are still in shared runtime render preparation/submission and better perf
     verification, not in generic startup work.

3. Runtime and renderer coordination still rely on a few broad owner files.
   - `src/game/runtime/session/GameSession.cpp`,
     `src/game/runtime/shared/projected/unit/SharedProjectedUnitRenderer.cpp`,
     and the renderer mega-files are much healthier than before, but they still
     carry more coordination than ideal.

4. Renderer interfaces and backend families are broader than ideal.
   - `src/engine/render/IRenderBackend.h` and the D3D12/OpenGL mega-files still
     concentrate too much change risk.

5. Tooling and documentation still lean on manual discipline in a few places.
   - Preview visuals now have a first automated smoke harness, and local perf
     smoke now uses a stable pinned snapshot path plus a prebuilt-Release
     wrapper path.
   - Runtime visuals now also have a first automated gameplay smoke harness,
     and CI now has a dedicated manual/nightly hosted runtime-smoke lane, but
     broader runtime visual coverage and merge-blocking CI enforcement still
     remain only partly automated.
   - Preview smoke still depends on local runs or a future self-hosted GPU CI
     lane because GitHub-hosted Windows runners are not a stable fit for the
     current OpenGL preview tools.
   - Perf smoke still depends on local runs or a future self-hosted GPU CI lane
     because GitHub-hosted Windows runners are not a stable fit for trustworthy
     perf thresholds.
   - Docs now have hygiene automation, but the workflow still depends on people
     keeping ownership boundaries honest.
