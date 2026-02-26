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
8. Slice 8 complete: closed a renderer-path boundary gap by teaching the backend model cache loader to self-heal on cache miss/corrupt/stale source metadata (CPU fastgltf parse -> write `.pacmdl` -> retry load), so `d3d12` no longer depends on `opengl`/`opengl_shared` to regenerate backend model caches after asset updates or manual cache deletion.
9. Slice 9 complete: continued the `D3D12RenderBackend.cpp` split by extracting D3D12 sprite/world texture loading methods (`ensureFallbackSpriteTexture`, `ensureSpriteTexture`, `ensureWorldTexture`) into `src/engine/render/d3d12/D3D12RenderBackendTextures.cpp`, reducing backend monolith size without touching the recently stabilized shared capture/pokeball gameplay path.
10. Slice 10 complete: continued the D3D12 backend split by extracting D3D12 debug draw methods (`drawDebugQuads`, `drawDebugLines`, `drawDebugTriangles`, `drawDebugSprites`) into `src/engine/render/d3d12/D3D12RenderBackendDebugDraw.cpp`, isolating backend UI/debug overlay draw code from the core world/device/render backend implementation.
11. Slice 11 complete: continued `GameSession.cpp` decomposition by extracting shared growl VFX TEV/pass-classification/texture-bake helper math into `src/game/runtime/SharedGrowlVfxHelpers.h/.cpp`, rewiring the shared growl bridge in `GameSession` to call the helper directly, and adding `shared_growl_vfx_helpers_contract` so growl pass baking/classification behavior stays locked during future shared VFX module extraction.
12. Slice 12 complete: continued `GameSession.cpp` decomposition by extracting the shared growl pass geometry/batch assembly loop (ring iteration, direction jitter, fade/alpha scaling, quarter-ring and mesh batch generation, sort-depth computation) into `src/game/runtime/SharedGrowlWaveBatches.h/.cpp`, keeping mesh/texture cache ownership in `GameSession`, and adding `shared_growl_wave_batches_contract` to lock additive payload defaults and quarter-ring batch assembly during future shared VFX bridge extraction.
13. Slice 13 complete: continued `GameSession.cpp` decomposition by extracting shared growl bridge orchestration (draw-pass iteration + shared TEV resolution + mesh/texture resolver callback dispatch into the growl batch-builder) into `src/game/runtime/SharedGrowlWaveBridge.h/.cpp`, shrinking the `appendSharedGrowlWaveVfx` lambda to snapshot gating plus cache-backed resolver lambdas, and adding `shared_growl_wave_bridge_contract` to guard callback usage and pass append behavior during future shared VFX extraction.

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

Recent Housework / Stabilization Notes
- D3D12 shared capture pokeball path now self-heals backend `.pacmdl` cache entries (via backend cache load-or-build) and uses cached world meshes stored in default GPU buffers instead of upload heaps, reducing repeated rigid-phase capture draw cost while preserving `opengl_shared` behavior as the visual reference.
- Continued D3D12 backend modularization by moving cached world-mesh draw/cache methods (used by shared capture rigid phases and future reusable world prop caches) out of `D3D12RenderBackend.cpp` into a dedicated `d3d12/D3D12RenderBackendCachedWorldMeshes.cpp` translation unit.
- Continued D3D12 backend modularization by moving core world draw methods (`drawWorldTriangles`, dynamic indexed world draws, textured world draws, and the shared world dynamic upload path) out of `D3D12RenderBackend.cpp` into `d3d12/D3D12RenderBackendWorldDraw.cpp`, isolating the main backend world rendering submission path for safer future optimization and parity work.
- Continued D3D12 backend modularization by moving backend lifecycle/device-frame methods (`beginFrame`, `endFrame`, `onResize`, `shutdown`, `initDeviceAndSwapchain`, render-target/depth resource management, GPU sync, HWND resolution) out of `D3D12RenderBackend.cpp` into `d3d12/D3D12RenderBackendLifecycle.cpp`, leaving the monolith focused on constructor wiring only.
- Continued `GameSession` housework by extracting the shared capture overlay fallback builder (screen-space pokeball icon/seam overlay + phase-driven projected rings for the legacy 2D capture overlay path) into `src/game/runtime/SharedCaptureOverlayVfx.h/.cpp`, reducing another local lambda in `GameSession.cpp` and adding a narrow `shared_capture_overlay_vfx_contract` test to guard future shared capture presentation refactors.
- Continued `GameSession` housework by extracting shared particle billboard style selection (shader-fragment classification -> procedural texture/tint/alpha rules used by the shared particle VFX bridge for non-tail-fire effects) into `src/game/runtime/SharedParticleVfxStyles.h/.cpp`, shrinking a large local branching block in `GameSession.cpp` and adding `shared_particle_vfx_styles_contract` to guard style mapping behavior during further particle-bridge extraction.
- Continued `GameSession` housework by extracting generic shared particle billboard batch assembly (non-tail-fire particle quad generation, clip rejection, flipbook UV frame selection, style tint propagation, and sort-depth accumulation) into `src/game/runtime/SharedParticleBillboardBatches.h/.cpp`, reducing the size of `appendSharedParticleVfx` and adding `shared_particle_billboard_batches_contract` as a regression guard before extracting the remaining particle-bridge orchestration/tail-fire branches.
- Continued `GameSession` housework by extracting shared particle VFX dispatch sequencing (ordered per-effect snapshot append calls and forwarded tail-fire/leech-drain result flags) into `src/game/runtime/SharedParticleVfxBridgeDispatch.h/.cpp`, further shrinking `appendSharedParticleVfx` while keeping tail-fire exact/special/fallback logic in `GameSession`, and adding `shared_particle_vfx_bridge_dispatch_contract` to guard call order and returned-flag behavior.
- Continued `GameSession` housework by extracting shared tail-fire atlas prep helpers (premultiplied atlas bake and combined-exact-atlas packing/layout metadata) into `src/game/runtime/SharedTailFireAtlasHelpers.h/.cpp`, reducing local tail-fire atlas lambdas in `GameSession` and adding `shared_tail_fire_atlas_helpers_contract` to guard premul/packing behavior during future tail-fire bridge cleanup.
- Continued `GameSession` housework by extracting shared tail-fire exact GPU batch assembly (projection/unprojection quad generation and exact fire-tail material payload fill for the backend shader path) into `src/game/runtime/SharedTailFireExactGpuBatches.h/.cpp`, further shrinking `appendSharedParticleVfx` and adding `shared_tail_fire_exact_gpu_batches_contract` as a regression guard before moving the remaining tail-fire bridge orchestration.
- Continued `GameSession` housework with a larger line-count reduction pass by extracting backend pose evaluation/sampling logic into `src/game/runtime/SharedBackendPoseEval.h/.cpp` (scene pose + clip pose sampling used by shared model/capture rendering) and then replacing the temporary shared particle billboard include-fragment split with a proper runtime helper module (`src/game/runtime/SharedParticleSnapshotBillboards.h/.cpp`) so the large shared particle snapshot billboard builder block (including tail-fire special billboard branches) is no longer embedded via `.inl`; `GameSession.cpp` remains below the temporary housework target at `5929` lines.
- Continued `GameSession` housework by extracting the D3D12 shared capture pokeball fast path (cached rigid combined mesh prewarm/draw, per-submesh cached draw fallback, and clip-pose node-matrix application for absorb/open-close frames) into `src/game/runtime/SharedCaptureD3d12FastPath.h/.cpp`, replacing the large inlined D3D12 branch in `appendSharedCaptureAttemptModels`; this pushed `GameSession.cpp` below the next target to `4978` lines while preserving shared capture behavior.
- Continued `GameSession` housework by extracting the shared per-unit HUD geometry/text builder (HP/energy bars, player XP ring arc, and centered level text) into `src/game/runtime/SharedUnitHudBatches.h/.cpp`, replacing the local `xpToNextLevel`, `appendRingArc`, and `appendLegacyUnitHud` lambdas with a dedicated helper and reducing `GameSession.cpp` further to `4838` lines.
- Continued `GameSession` housework by extracting the shared capture-model bridge (`appendSharedCaptureAttemptModels`) into `src/game/runtime/SharedCaptureModelBridge.h/.cpp`, including shared capture snapshot refresh/prewarm gating, backend mesh cache load, D3D12 fast-path delegation, backend/shared clip-pose capture rendering, and non-D3D12 indexed-batch fallback assembly; `GameSession.cpp` dropped again to `4464` lines.
- Continued `GameSession` housework by extracting projected-scene helper wrappers into `src/game/runtime/SharedProjectedWorldSceneHelpers.h/.cpp` (shared growl/particle bridge wrappers, tail-fire fallback config lookup, board/bench grid projected wrapper, model-depth buffer lifecycle + flush, backend mesh resolver, shared capture model bridge convenience wrappers), then rewiring `GameSession` to use those helpers and cutting `GameSession.cpp` further to `3996` lines while keeping full build + `ctest` green.
- Continued `GameSession` housework by extracting backend debug-view overlay composition/submission (perf bar, status text, inventory panel/icon cards, log feeds, and backend debug draw submission calls) into `src/game/runtime/SharedBackendDebugViewOverlay.h/.cpp`, rewiring `renderBackendDebugView(...)` to delegate through a typed args object, and pushing `GameSession.cpp` below the next target to `3469` lines while keeping full build + `ctest` green.
- Continued `GameSession` housework with a major projected-unit render extraction by moving the large `drawProjectedUnits` lambda body into `src/game/runtime/SharedProjectedUnitRenderer.h/.cpp` (shared/backend model rendering, projected unit VFX/capture hooks, per-unit HUD appenders, model-depth callback plumbing, and shared portrait fallback branches), then rewiring `GameSession` to dispatch both board and bench unit passes through a typed args object; `GameSession.cpp` dropped to `2059` lines while full build + `ctest` stayed green.
- Continued projected-unit-renderer housework by extracting the post-model projected-unit overlay/HUD/VFX block (portrait fallback sprite, projected growl/leech/tail-fire fallback overlays, projectile/impact fallback overlays, and per-unit HUD appenders) out of `SharedProjectedUnitRenderer.cpp` into `src/game/runtime/SharedProjectedUnitOverlays.h/.cpp`, reducing `SharedProjectedUnitRenderer.cpp` from `1558` to `1313` lines while preserving full build + `ctest` stability.
- Continued projected-unit-renderer housework by extracting the large backend/shared model-render branch (backend mesh pose evaluation, shared indexed-batch/world-triangle submission, tail-anchor extraction, tint/fade/capture/faint model-state application, and triangle-budget handling) out of `SharedProjectedUnitRenderer.cpp` into `src/game/runtime/SharedProjectedUnitModelRenderer.h/.cpp`, reducing `SharedProjectedUnitRenderer.cpp` from `1313` to `346` lines while preserving full build + `ctest` stability.
- Continued projected-unit-renderer housework by splitting the extracted projected-unit backend/shared model-render implementation into a thin coordinator (`SharedProjectedUnitModelRenderer.cpp`) plus a dedicated backend-mesh implementation module (`src/game/runtime/SharedProjectedUnitBackendMeshRenderer.h/.cpp`), preserving behavior while making the next decomposition seam explicit (`pose/tint state vs triangle/batch submission`) and keeping full build + `ctest` green.
- Continued projected-unit backend-mesh housework by extracting model-state/pose/tint/budget prep + indexed-batch initialization out of `SharedProjectedUnitBackendMeshRenderer.cpp` into `src/game/runtime/SharedProjectedUnitBackendMeshPrep.h/.cpp`, leaving `SharedProjectedUnitBackendMeshRenderer.cpp` focused on skinning/node transforms and triangle/batch submission (`1091 -> 947` lines) while preserving full build + `ctest` stability.
- Continued projected-unit backend-mesh housework by extracting the skin/node transform caches and world-vertex/world-position resolvers (including skin-matrix cache, node transform cache, and deformed/skinned world vertex sampling) out of `SharedProjectedUnitBackendMeshRenderer.cpp` into `src/game/runtime/SharedProjectedUnitBackendMeshTransforms.h/.cpp`, reducing the renderer again (`947 -> 645` lines) so it is now focused primarily on triangle/batch submission and material/color handling while full build + `ctest` stayed green.
- Continued projected-unit backend-mesh housework by extracting the triangle/batch submission helper (fast textured indexed-batch append, indexed textured/untextured fallback batch append, and projected/world-triangle fallback submission) out of `SharedProjectedUnitBackendMeshRenderer.cpp` into `src/game/runtime/SharedProjectedUnitBackendMeshTriangleSubmit.h/.cpp`, reducing the renderer from `645` to `346` lines so it now primarily owns triangle iteration/material-color resolution and dispatch into specialized prep/transform/submission helpers while full build + `ctest` stayed green.
- Began `BackendModelCache.cpp` decomposition by extracting the raw source GLB parse/build stage (scene/mesh/material decode into `SourceCacheBuildData`) into `src/game/runtime/BackendModelCacheSourceBuild.h/.cpp`, leaving cache read/write/load-or-build orchestration in `BackendModelCache.cpp` and reducing that file from `1365` to `1110` lines while preserving full build + `ctest` stability.
- Continued `BackendModelCache.cpp` decomposition by extracting cache write/serialization (`writeBackendCacheFromSourceData`) into `src/game/runtime/BackendModelCacheWrite.h/.cpp` and moving shared cache format structs/constants into `src/game/runtime/BackendModelCacheFormat.h`, leaving `BackendModelCache.cpp` focused more tightly on cache read/validation/load-or-build orchestration and reducing it further from `1110` to `850` lines while keeping full build + `ctest` green.
- Continued `BackendModelCache.cpp` decomposition by extracting cache open/header/freshness validation + self-heal retry policy into `src/game/runtime/BackendModelCacheLoadOrRebuild.h/.cpp`, rewiring `loadMeshFromCache(...)` to delegate the cache-stream acquisition step before mesh decode, and reducing `BackendModelCache.cpp` further from `850` to `780` lines while keeping full build + `ctest` green.
- Continued `BackendModelCache.cpp` decomposition by extracting the validated-cache-stream mesh decode/material synthesis path (scene metadata decode, texture decode/sampling, triangle metadata synthesis, and vertex base-color accumulation) into `src/game/runtime/BackendModelCacheReadDecode.h/.cpp`, rewriting `BackendModelCache.cpp` as a thin coordinator over validate/open + decode + rebuild helpers, and reducing `BackendModelCache.cpp` again while keeping full build + `ctest` green.
- Began `OpenGLRenderBackend.cpp` decomposition by extracting texture cache/load methods (`ensureWorldTexture`, `ensureSpriteTexture`, `clearTextureCaches`) into `src/engine/render/opengl/OpenGLRenderBackendTextures.cpp`, mirroring the D3D12 backend split pattern and reducing the OpenGL backend monolith without changing rendering behavior.
