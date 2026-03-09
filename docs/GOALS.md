# Project Goals

Date: 2026-03-08

This repository is a portfolio project and reusable engine foundation. The
current game is the first client.

## Primary Goals
- Keep one shared gameplay presentation path that works in both `OpenGL` and `D3D12`.
- Use measured frame-time budgets instead of subjective FPS-only tuning.
- Maintain a deterministic, headless gameplay core that stays testable.
- Keep Windows-first delivery stable and reviewer-friendly.

## Rendering + Performance Success Criteria
- Functional parity: the same gameplay state is readable and behaviorally equivalent across `OpenGL` and `D3D12`.
- Measurement parity: both backends are compared using the same scene, same settings, same build type, and same logging fields.
- Performance discipline: runtime work is prioritized by measured bottlenecks, especially steady-state frame time in real gameplay scenes.
- Stability gate: backend switching + restart flow remains predictable and user-visible first-use stalls stay controlled.

## Non-Goals (Current Phase)
- Adding a new renderer backend before the current shared-path renderer remains stable and well-measured.
- Chasing generic feature checklists without in-game profiling evidence.
- Large visual redesign work unrelated to renderer/runtime quality.

## Success Signals
- CI remains green.
- Release benchmark baselines are captured and comparable.
- Renderer docs, logs, and menus match current behavior.
- Performance work continues reducing real gameplay frame cost instead of drifting into speculative complexity.
