# Tech Debt

Status: Active
Type: Tracker
Last updated: 2026-03-30

This file stays intentionally short. It tracks strategic debt that still drives
engineering priority; concrete file-by-file issues live in
`OUTSTANDING_ISSUES.md`.

## Strategic Debt
1. No automated performance regression gate.
   - Local benchmark tooling exists, but CI still does not enforce a protected
     baseline.

2. Shared projected render/build CPU is still the main steady-state hotspot.
   - The next biggest wins are still in shared runtime render preparation and
     submission, not in generic startup work.

3. Runtime composition is still too centralized.
   - `src/game/runtime/session/GameSession.cpp` and
     `src/game/runtime/GameRunner.cpp` still combine too many responsibilities.

4. Renderer interfaces and backend families are broader than ideal.
   - `src/engine/render/IRenderBackend.h` and the D3D12/OpenGL mega-files still
     concentrate too much change risk.

5. Tooling and documentation still lean on manual discipline in a few places.
   - Preview visuals are mostly manual smoke-checked.
   - Docs now have hygiene automation, but the workflow still depends on people
     keeping ownership boundaries honest.
