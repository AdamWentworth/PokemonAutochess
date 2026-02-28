# Project Goals

Date: 2026-02-28

This repository is a portfolio project and reusable engine foundation. The current game is the first client.

## Primary Goals
- Keep one shared gameplay presentation path that works in both OpenGL and D3D12.
- Use measurable frame-time budgets instead of subjective FPS-only tuning.
- Maintain a deterministic, headless gameplay core that stays testable.
- Keep Windows-first delivery stable and reviewer-friendly.

## Rendering + Performance Success Criteria
- Functional parity: the same gameplay state is readable and behaviorally equivalent across OpenGL and D3D12.
- Measurement parity: both backends are compared using the same scene, same settings, same build type (Release), and same logging fields.
- Performance gate: D3D12 frame time is not materially worse than OpenGL on the target laptop for the benchmark matrix defined in `docs/TEST_PLAN.md`.
- Stability gate: backend switching + restart flow remains stable and predictable.

## Non-Goals (Current Phase)
- Adding a new renderer backend (for example Vulkan) before OpenGL/D3D12 merge gates pass.
- Chasing synthetic benchmark scores without in-game profiling evidence.
- Large visual redesign work unrelated to parity/performance readiness.

## Success Signals
- CI remains green.
- Pre-merge benchmark table is captured for OpenGL and D3D12 in Release builds.
- Renderer docs and runtime logs match actual behavior (no stale route descriptions).
- D3D12 branch merges to `master` with clear follow-up optimization backlog, not open ambiguity.
