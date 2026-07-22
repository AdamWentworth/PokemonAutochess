# Project Goals

Status: Active
Type: Goal
Last updated: 2026-07-22

This repository is a portfolio project and reusable engine foundation.
Pokemon Autochess is the first game client built on an engine and VFX base that
is intended to support more games over time.

## Primary Goals
- Keep one shared gameplay presentation path that works in `OpenGL`, `Vulkan`, and `D3D12`.
- Use measured frame-time budgets instead of subjective FPS-only tuning.
- Maintain a deterministic, headless gameplay core that stays testable.
- Keep Windows-first delivery stable and reviewer-friendly.
- Grow reusable engine-level systems that can serve future games, starting with
  a reusable VFX surface under `src/vfx/`.

## Reusable VFX + Preview Goals
- Treat Pokemon Autochess as the first shipped client of a broader reusable VFX
  foundation rather than a one-off effects sandbox.
- Keep the reusable VFX layer independent enough that the core effect behavior
  could survive replacing or even deleting `src/game/` and be mounted into a
  different game.
- Keep reusable effect code, runtime bridges, authoring conventions, and
  reusable preview/render helpers in `src/vfx/` whenever they do not require
  game-only ownership.
- Keep game-specific adapters thin so the same VFX can stay visually consistent
  across different combat formats, camera systems, and future game clients.
- Maintain an engine-only VFX preview path for reusable effect authoring and
  iteration without depending on Pokemon Autochess gameplay code.
- Maintain an in-game VFX preview path whose primary job is to confirm that a
  reusable effect still reads and behaves consistently once it is mounted into
  Pokemon Autochess presentation rules.
- Use the two preview layers together: `VfxLab` for reusable effect
  development, and `PAC_VfxPreviewer` for faster in-game validation without
  depending on a full gameplay run loop.

## Rendering + Performance Success Criteria
- Functional parity: the same gameplay state is readable and behaviorally equivalent across `OpenGL`, `Vulkan`, and `D3D12`.
- Measurement parity: active backends are compared using the same scene, same settings, same build type, and same logging fields.
- Performance discipline: runtime work is prioritized by measured bottlenecks,
  especially steady-state frame time in real gameplay scenes, while protecting
  important visual quality such as texture fidelity and presentation stability.
- Stability gate: backend switching + restart flow remains predictable and user-visible first-use stalls stay controlled.
- Shared-path preference: renderer and VFX performance work should benefit both
  established backends and Vulkan where supported unless API-specific behavior forces a split.
- Release-build expectation: representative scenes on the target development
  machine should continue trending toward the high-hundreds FPS range already
  seen today, without relying on quality cuts that would undercut the intended
  presentation.
- Preview performance: VFX preview tools should stay responsive enough to be
  practical daily tools, even when validating real runtime content.

## Non-Goals (Current Phase)
- Adding another renderer backend before Vulkan reaches the project's required fidelity and performance maturity.
- Chasing generic feature checklists without in-game profiling evidence.
- Large visual redesign work unrelated to renderer/runtime quality.
- Letting reusable VFX code drift back into game-specific ownership without a
  clear gameplay-only reason.
- Chasing higher FPS by knowingly sacrificing important texture rendering
  quality or other visible presentation quality.

## Success Signals
- CI remains green.
- Release benchmark baselines are captured and comparable.
- Renderer docs, logs, and menus match current behavior.
- Performance work continues reducing real gameplay frame cost instead of drifting into speculative complexity.
- Reusable VFX continues to land in `src/vfx/` and stays portable beyond
  Pokemon Autochess, with only thin game adapters living under `src/game/`.
- `VfxLab` remains useful for reusable effect iteration, and
  `PAC_VfxPreviewer` remains useful for validating that those same effects stay
  consistent in-game.
- Release builds continue delivering the current expectation of very high frame
  rates in representative scenes without obvious quality regression.
