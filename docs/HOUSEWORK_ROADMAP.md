# Housework Roadmap (Post-Parity Cleanup)

Goal
- Preserve current shared-path parity wins while improving maintainability, boundaries, and retirement readiness for legacy OpenGL.

Current Position (2026-02-25)
- Core parity is manually signed off by the user for the features that matter most across `opengl`, `opengl_shared`, and `d3d12`.
- Legacy OpenGL remains the fallback path for safety.
- The next phase is housework: reduce code ownership sprawl and prepare for eventual legacy retirement without destabilizing gameplay.

Progress Log
1. Slice 1 complete: moved shared capture absorb timing semantics into `GameWorld::CaptureAttemptRenderSnapshot` (normalized phase progress + late absorb visual ramp), removing hardcoded capture timing assumptions from shared render paths in `GameSession`.
2. Slice 2 complete: extracted reusable shared capture helpers in `GameSession.cpp` (snapshot cache/index lookup, pokeball clip-time mapping, pokeball transform builder, pokeball anim-index lookup) and reused them across backend shared capture rendering and the OpenGL-shared direct pokeball draw path.
3. Slice 3 complete: moved the shared capture helper set into a dedicated runtime module (`SharedCapturePresentation.h/.cpp`) and rewired `GameSession.cpp` to consume that module, reducing capture-specific utility code in the giant `GameSession` translation unit and creating a clean seam for a later full shared-capture render extraction.
4. Slice 4 complete: added housework guardrails by wiring an `opengl_shared` runtime smoke test in `CMakeLists.txt` (when `PAC_ENABLE_RUNTIME_SMOKE_TESTS` is enabled), plus automated contracts for shared capture presentation helpers (`SharedCapturePresentation`) and `GameWorld` capture snapshot normalized timing fields (`phaseNorm01`, `absorbNorm01`, `absorbLateVisual01`).
5. Slice 5 complete: extracted the `opengl_shared` capture pokeball draw loop out of `GameSession.cpp` into `SharedCapturePresentation` (`drawOpenGlSharedCapturePokeballModels(...)`), so shared capture model presentation logic for the OpenGL-shared route now lives with the capture presentation helpers and `GameSession` only dispatches the call.
6. Slice 6 complete: extracted shared world indexed-batch submission (texture payload assembly + opaque/mask first pass + stable depth-sorted blend pass) out of `GameSession.cpp` into `SharedWorldIndexedBatches.h/.cpp`, moved the shared `WorldIndexedBatch` type into that module, and added an automated ordering contract test (`shared_world_indexed_batches_contract`) to lock batch draw ordering during future refactors.
7. Slice 7 complete: started the `D3D12RenderBackend.cpp` backend split in earnest by extracting D3D12 backend-internal shared structs/constants/helpers (`D3D12RenderBackendInternal.h`), moving debug/sprite/world pipeline creation method definitions into `src/engine/render/d3d12/D3D12RenderBackendPipelines.cpp`, and adding a D3D12 helper contract test (`d3d12_world_material_constants_contract`) to lock `alignUp`, wrap-mode sanitization, and `WorldPsConstants` material payload mapping during further backend refactors.

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
1. Extract shared capture presentation helpers/path from `GameSession.cpp`. (In progress: timing + helper module extraction complete; render path body extraction still pending.)
2. Extract shared VFX bridge appenders from `GameSession.cpp`.
3. Extract shared world board/unit command generation helpers.
4. Start splitting `D3D12RenderBackend.cpp` by subsystem.
5. Re-evaluate legacy retirement gating after those splits stabilize.

Out Of Scope (for this file)
- New gameplay features
- Non-parity UI redesign/polish
- Balance/content changes
