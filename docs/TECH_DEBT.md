# Tech Debt

Status: Active
Type: Tracker
Last updated: 2026-03-31

This file stays intentionally short. It tracks strategic debt that still drives
engineering priority; concrete file-by-file issues live in
`OUTSTANDING_ISSUES.md`.

## Strategic Debt
1. No automated performance regression gate.
   - A first local Release perf smoke guard now exists and `full_check` now
     runs it through a stable prebuilt-Release path with display-aware
     protected resolution selection, but CI still does not enforce a protected
     baseline.

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
     wrapper path, but broader runtime visuals and CI-level performance still
     remain only partly automated.
   - Docs now have hygiene automation, but the workflow still depends on people
     keeping ownership boundaries honest.
