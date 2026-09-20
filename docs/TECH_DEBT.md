# Tech Debt

Status: Active
Type: Tracker
Last updated: 2026-09-19

This file stays intentionally short. It tracks strategic debt that still drives
future engineering choices; current constraints live in
[Outstanding issues](OUTSTANDING_ISSUES.md). These are retained risks, not an
active feature plan for the repository presentation pass.

## Strategic Debt
1. No merge-blocking performance regression gate in CI.
   - A first local Release perf smoke suite now exists and `full_check` now
     runs it through a stable prebuilt-Release path with display-aware
     protected resolution selection.
   - GitHub-hosted Windows turned out not to be a trustworthy perf threshold
     environment for this repo, so PRs are not yet blocked on an automated perf
     gate and perf smoke remains local-first until we have a self-hosted GPU or
     similarly controlled benchmark runner.

2. Shared projected render/build CPU needs fresh measurement before optimization.
   - Earlier profiles identified shared render preparation/submission as a
     significant cost. Reproduce the workload before treating this as the next
     bottleneck; historical timings are not current performance guarantees.

3. Runtime and renderer coordination still rely on a few broad owner files.
   - `src/game/runtime/session/GameSession.cpp`,
     `src/game/runtime/shared/projected/unit/SharedProjectedUnitRenderer.cpp`,
     and the renderer mega-files are much healthier than before, but they still
     carry more coordination than ideal.

4. Renderer interfaces and backend families are broader than ideal.
   - The generic backend interfaces and backend families in Phlosion Engine
     remain broad maintenance surfaces. They are dependency-owned; the game
     owns the shared projected presentation adapters.

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
