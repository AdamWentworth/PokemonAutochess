# Parity Outstanding (Pre-Merge Blockers)

Date: 2026-02-28

This file is intentionally short. If an item is here, it is still open and relevant to the D3D12 merge decision.

## Current Status
- Functional parity is close enough for ongoing testing.
- Merge is currently blocked by measurement and performance process gaps, not by broad feature absence.

## Blockers (Must Resolve Before Merge)
1. Release benchmark matrix is missing.
- Current conclusions rely too heavily on Debug runs.

2. Perf instrumentation is incomplete.
- Current `render`/`swap` timing cannot separate CPU submission, present wait, and GPU execution.

3. D3D12 frame pacing is conservative.
- `waitForGpu()` is called in the normal end-of-frame path, forcing CPU/GPU sync each frame.

4. Display settings behavior is ambiguous to users.
- VSync/FPS/UI-quality controls are shown as placeholders in UI.
- Backend comparison quality is reduced until these controls are either wired or hidden.

5. Runtime log wording is partially stale.
- D3D12 initialization still logs "debug-world render path", which is misleading for parity verification.

## Non-Blockers (Track After Merge)
- Deeper content optimization for larger fight counts.
- Additional API work (Vulkan) after the benchmark pipeline is stable.
- UI polish unrelated to parity/perf correctness.

## Exit Condition For This File
- Remove every blocker above.
- Record benchmark results per `docs/TEST_PLAN.md`.
- Keep this file as a short watchlist for post-merge regressions only.
