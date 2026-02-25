# Housework Roadmap (Post-Parity Cleanup)

Goal
- Preserve current shared-path parity wins while improving maintainability, boundaries, and retirement readiness for legacy OpenGL.

Current Position (2026-02-25)
- Core parity is manually signed off by the user for the features that matter most across `opengl`, `opengl_shared`, and `d3d12`.
- Legacy OpenGL remains the fallback path for safety.
- The next phase is housework: reduce code ownership sprawl and prepare for eventual legacy retirement without destabilizing gameplay.

Guardrails (Do Not Regress)
1. Keep `opengl` legacy available as fallback during cleanup.
2. Prefer refactors that preserve behavior and move logic behind shared contracts.
3. Rebuild + run test suite after each meaningful cleanup iteration.
4. Use `opengl_shared` and `d3d12` runtime smoke checks when touching shared world/UI paths.

Primary Cleanup Targets

1. `GameSession.cpp` decomposition
- Problem: `GameSession.cpp` still owns too much rendering orchestration, shared-world command generation, capture presentation, VFX bridging, and UI assembly.
- Target split (incremental):
  - `GameSessionSharedWorld.cpp` (board/unit/world command generation)
  - `GameSessionSharedVfx.cpp` (growl/particle/tail-fire/leech-seed bridges)
  - `GameSessionSharedCapture.cpp` (pokeball render + capture presentation timing)
  - `GameSessionSharedUi.cpp` (shared HUD/shop/menu overlays)
- Immediate win: fewer accidental cross-feature regressions and easier parity maintenance.

2. `D3D12RenderBackend.cpp` decomposition
- Problem: backend implementation remains a large monolith (device setup, pipelines, shaders, texture upload, draw paths, world materials).
- Target split (incremental):
  - device/swapchain init
  - world pipelines + shaders
  - texture upload/mips/samplers
  - debug text/geometry
  - shared-material branches (e.g., tail-fire exact path)
- Immediate win: easier backend parity debugging and safer optimization work.

3. Contract ownership cleanup
- Move renderer-agnostic presentation rules out of local lambdas and into shared contracts/snapshots/helpers.
- Example already started:
  - capture absorb timing normalized in `GameWorld::CaptureAttemptRenderSnapshot`
- Continue with:
  - VFX timing/presentation fields
  - per-unit HUD layout/policy contracts
  - shared board/bench presentation policy

4. Route ownership simplification
- Keep pushing toward:
  - one shared world command path for `opengl_shared` and `d3d12`
  - minimal renderer-branching at final dispatch only
- Legacy path should become a fallback implementation, not a competing source of gameplay render logic.

5. Retirement readiness checks (before removing legacy)
- Shared path becomes default for OpenGL (legacy still available behind explicit fallback toggle).
- Manual parity checklist remains green for a sustained period.
- No known crashers in backend switching / menu / gameplay startup.
- Performance in shared paths is acceptable for target hardware classes.

First Recommended Housework Sequence
1. Extract shared capture presentation helpers/path from `GameSession.cpp`.
2. Extract shared VFX bridge appenders from `GameSession.cpp`.
3. Extract shared world board/unit command generation helpers.
4. Start splitting `D3D12RenderBackend.cpp` by subsystem.
5. Re-evaluate legacy retirement gating after those splits stabilize.

Out Of Scope (for this file)
- New gameplay features
- Non-parity UI redesign/polish
- Balance/content changes

