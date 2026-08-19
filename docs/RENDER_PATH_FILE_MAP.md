# Render Path File Map

Status: Active
Type: Reference
Last updated: 2026-08-15

Use this file to find render/runtime ownership quickly when working on parity,
performance, or maintainability tasks. This is a reference doc: it should map
where behavior lives today, not serve as the active renderer roadmap or debt
register.

## Runtime Route Model
- Gameplay rendering routes are shared-path based for active renderers.
- Legacy `opengl_shared` remains a backward-compat token in preference parsing, canonicalized to `opengl` (`src/game/runtime/video/VideoPreferences.cpp`).
- Preference parsing and the Display menu expose `vulkan`; bootstrap creates a Vulkan SDL window and falls back to OpenGL if Vulkan initialization fails.

## Ownership Map

### 1) App + Frame Loop + Perf Logging
- `src/game/runtime/GameRunner.cpp`
  - Main loop timing (`fixed`, `render`, `swap`)
  - Perf log line emission (`[Perf]`)
- `src/game/runtime/session/GameSession.*`
  - owns the running gameplay session, render delegation, state stack, and session-local caches
- `src/game/runtime/session/GameUpdateGraph.*`
  - fixed-step update ordering policy inside the gameplay session
  - phase transition side effects between gameplay systems
- `src/game/runtime/session/SessionBackendRenderHelpers.*`
  - backend-model animation lookup fallbacks, card-label formatting, and world-texture prewarm payload generation
  - shared session-side helpers used by startup model prewarm and backend shop card UI
- `src/game/runtime/session/SessionCoreBootstrapRuntime.*`
  - core session bootstrap extracted from `GameSession`
  - asset-store selection, RNG seed setup, services/world/state creation, scheduler wiring, and update-graph configuration
- `src/game/runtime/session/SessionBackendInventoryUi.*`
  - backend inventory panel refresh, paging, selection, and hit/input routing
  - session-side adapter between world inventory state and backend UI controls
- `src/game/runtime/ui/*.h`
  - backend menu, HUD, card, inventory, text, and scaling helpers used by game-side UI presentation
  - examples: `CardRenderer.h`, `CardVisuals.h`, `DebugText.h`, `InventoryPanel.h`, `StatusText.h`, `UiScale.h`
  - game-facing backend presentation utilities, distinct from render API backends under `src/engine/render/*`
- `src/game/runtime/render_prep/*.h`
  - API-agnostic render-prep helpers shared by `GameSession`, render model cache decode, and the shared projected path
  - examples: `MaterialShading.h`, `ProceduralPose.h`, `UnitVisuals.h`, `WorldProjection.h`, `WorldProxyGeometry.h`, `MeshNormals.h`
  - material shading math, procedural pose/deformation, world proxy geometry, board projection, mesh-normal generation, and portrait/tint decisions
- `src/game/runtime/session/SessionBackendUnitHydration.*`
  - backend unit model-path hydration, animation-role cache population, and importer-scale correction
  - flyer default application and per-unit backend animation cache setup
- `src/game/runtime/session/SessionDebugSnapshot.*`
  - debug snapshot path resolution, JSON file IO, and summary formatting
  - session/world snapshot metadata parsing for save/load
- `src/game/runtime/session/SessionFrameMetrics.*`
  - session-side publication of per-frame projected/world metrics into `EngineServices`
  - render build breakdown writeback before the shared overlay submit path
- `src/game/runtime/session/SessionLegacyWorldView.*`
  - legacy non-projected board/bench/unit composition used when backend world projection is unavailable
  - board grid, bench panel, fallback portrait sprites, and legacy unit HUD overlay assembly
- `src/game/runtime/session/SessionLoopRuntime.*`
  - session input/dev-pause/fixed-step orchestration extracted from `GameSession`
  - routes resize, hotkeys, backend inventory UI input, and fixed-step backend hydrate timing
- `src/game/runtime/session/SessionProjectedWorldView.*`
  - projected board/bench world composition for the backend world path
  - camera/view-proj setup, projected unit draw dispatch, capture-model bridge, VFX bridge, and depth flush timing
- `src/game/runtime/session/SessionRenderScratch.*`
  - frame-local render scratch ownership for backend debug/world rendering
  - projected-backdrop cache keys, scratch-capacity policy, and per-frame scratch reset/reuse
- `src/game/runtime/session/SessionWorldBackdrop.*`
  - projected board/bench backdrop composition for the backend world view
  - cache-aware backdrop rebuild and reuse policy for projected board geometry
- `src/game/runtime/session/SessionSnapshotRuntime.*`
  - live session save/load orchestration for debug snapshots
  - state-stack restore, runtime flag restore, inventory refresh, and restore-time indexed-world prewarm
- `src/game/runtime/session/SessionStartupRuntime.*`
  - post-bootstrap session startup orchestration extracted from `GameSession`
  - render-model preload, startup asset prewarm, world-layer warmup scheduling/drain, initial menu push, and final init logging
- `src/game/runtime/session/SessionTailFirePrewarm.*`
  - session-side startup prewarm for legacy and authored tail-fire texture assets
  - resolves combined/premultiplied atlases and authored mesh flipbook uploads before first use
- `src/game/runtime/session/SessionTextureCache.*`
  - session-local runtime texture cache population for world/UI/debug paths
  - white fallback texture, procedural sprite atlases, and on-demand image decode/caching
- `src/game/runtime/session/SessionRenderConfig.*`
  - session-local env/config gates for backend prewarm, projected rendering, and snapshot render restore
  - backend model triangle limits and GPU clip-skinning policy used by GameSession render flow
- `src/game/runtime/session/SessionRenderLayout.*`
  - viewport-derived board/UI layout used by `GameSession` render routing
  - board bounds, cell sizing, UI padding, and shared HUD config packaging
- `src/game/runtime/session/SessionWorldRenderRuntime.*`
  - session-side backend world render orchestration extracted from `GameSession`
  - projected vs legacy world path selection, prewarm-only indexed-layer path, frame-metric publication, and shared overlay submit wiring
- `src/game/runtime/renderer/RendererBackendBootstrap.*`
  - Backend creation/fallback
  - Backend capability-to-window API mapping
- `src/game/runtime/renderer/RendererStartupDiagnostics.*`
  - GPU adapter inventory logging
  - backend startup summary logging
- `src/game/runtime/renderer/RuntimeRendererActivation.*`
  - active renderer/GPU identity resolution after backend creation
  - startup summary emission and discrete-GPU requirement evaluation
- `src/game/runtime/renderer/RuntimeRendererRecovery.*`
  - backend creation and OpenGL fallback recovery
  - init-time failure-stage ownership for fallback diagnostics
- `src/game/runtime/renderer/RuntimeRendererStartupState.*`
  - renderer identity capture from backend + GL strings
  - startup GPU/service finalization and summary logging
- `src/game/runtime/startup/RuntimeStartupConfig.*`
  - startup preference/env resolution
  - one-shot boot-menu consumption
  - startup video override parsing
- `src/game/runtime/startup/RuntimeStartupPresentation.*`
  - font subsystem startup result handling
  - default camera creation and initial loading-frame handoff
- `src/game/runtime/startup/RuntimeStartupSession.*`
  - preference load/consume flow for startup
  - startup service state population and adapter inventory capture
- `src/game/runtime/startup/RuntimeStartupVideoOverride.*`
  - startup video override application policy
  - startup video override success/failure messaging
- `src/game/runtime/startup/RuntimeStartupAssetPrewarm.*`
  - world shading, tail-fire, UI sprite, card-art, and backend card-UI startup prewarm orchestration
  - backend card-art proxy selection for eager startup warming
- `src/game/runtime/startup/RuntimeUiCardPrewarm.*`
  - one-frame backend shop-card UI warmup used during startup prewarm
  - portrait filtering and debug/UI submission ownership for first-shop-entry hitch avoidance
- `src/game/runtime/startup/RuntimeWorldLayerPrewarm.*`
  - world/board prewarm scheduling, startup drain, and deferred frame completion
  - init title/progress ownership for world-layer warmup
- `src/game/runtime/video/RuntimeWindowBootstrap.*`
  - initial/fallback window open orchestration
  - startup window GL-context state capture
- `src/game/runtime/RuntimeRestartPolicy.*`
  - launch-time stale restart cleanup
  - post-run restart request consumption
  - restart preference persistence semantics
- `src/game/runtime/loop/RuntimeLoopConfig.*`
  - fixed tick budget env parsing
  - frame-delta clamping and dropped-tick policy
- `src/game/runtime/loop/RuntimeFixedStepPhase.*`
  - fixed-step execution and dropped-tick enforcement
  - per-frame fixed-update breakdown capture
- `src/game/runtime/loop/RuntimeFrameObservation.*`
  - engine-service frame snapshot capture
  - perf sample assembly from frame timings and service counters
- `src/game/runtime/loop/RuntimeFramePerfCapture.*`
  - backend timing/stat interpretation
  - per-frame render/perf metric derivation
- `src/game/runtime/loop/RuntimeLoopControl.*`
  - stop-reason ownership for the main loop
  - SDL quit and auto-quit bookkeeping
- `src/game/runtime/RuntimeOpenGlBootstrap.*`
  - OpenGL function bootstrap for loading/preload paths
  - initial loading-frame presentation and preload pump sequencing
- `src/game/runtime/video/RuntimeSdlEventDispatch.*`
  - SDL event dispatch orchestration
  - resize sync and translated input delivery policy
- `src/game/runtime/loop/RuntimePerfAccumulator.*`
  - rolling perf-window accumulation and averaging
- `src/game/runtime/loop/RuntimePerfLogging.*`
  - `[Perf]` and `[PerfJSON]` formatting
- `src/game/runtime/RuntimeRelaunchLoop.*`
  - outer relaunch loop around restart-on-exit preferences
  - session re-entry ownership outside `GameRunner`
- `src/game/runtime/RuntimeBootLoading.*`
  - preload-abort event policy
  - fallback loading-screen quad layout for backend boot frames
- `src/game/runtime/video/RuntimeSdlInput.*`
  - SDL event translation into `InputEvent`
  - resize-event detection and mouse-coordinate scaling
- `src/game/runtime/video/RuntimeSdlVideoMode.*`
  - SDL fullscreen/windowed transition policy
  - video mode sanitization and current-mode snapshots
  - SDL error/fallback handling for window mode changes
- `src/game/runtime/video/GpuAdapters.*`
  - SDL/DXGI adapter inventory helpers and discrete/integrated classification support
  - preferred GPU matching utilities shared by startup and settings
- `src/game/runtime/video/D3D12Probe.*`
  - startup/settings D3D12 device probe against the selected DXGI adapter
  - preflight capability check used before committing to a D3D12 launch path
- `src/game/runtime/video/VideoPreferences.*`
  - persistent display/backend preference parsing and serialization
  - restart-required display settings contract used by startup and the menu
- `src/game/runtime/video/VideoInitGuards.h`
  - SDL/TTF/window lifecycle guard helpers for video/bootstrap code

### 2) Runtime Route Policy
- `src/game/runtime/routes/RenderRoutes.h`
- `src/game/runtime/routes/StartupRenderRoutePolicy.h`
- `src/game/runtime/routes/RenderFlowDecisions.h`

### 3) Shared Gameplay Presentation Path
- `src/game/runtime/session/GameSession.cpp`
- `src/game/runtime/shared/projected/core`
- `src/game/runtime/shared/projected/unit`
- `src/game/runtime/shared/projected/backend_mesh`
- `src/game/runtime/shared/projected/world_scene`
- `src/game/runtime/shared/projected/world_vfx`
- `src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshRenderer.cpp`
  - authored fire-mesh UV flipbook override for the Charmander line
  - per-unit fire mesh batch texture/material rewrites
- `src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshSupport.*`
  - reusable projected mesh batching support extracted from the renderer body
  - fast-textured mesh template cache, GPU skin batch state, and fire-mesh override helpers
- `src/game/runtime/shared/capture/SharedCapture*.*`
- `src/vfx/runtime/shared/SharedAuthoredVfx*.*`
- `src/game/runtime/shared/vfx/particles/SharedParticle*.*`
- `src/game/runtime/shared/vfx/tail_fire/SharedTailFireMeshPlayback*.*`
  - authored fire-mesh flipbook spec selection
  - cached fire-submesh detection and startup prewarm inputs
- `src/game/runtime/shared/vfx/tail_fire/SharedTailFire*.*`
- `src/game/world/GameWorldVfx.cpp`
  - legacy tail-fire emitter config and current species filter
- `src/game/runtime/shared/ui/Shared*.*`
- `src/game/runtime/shared/ui/SharedBackendDebugViewSupport.*`
  - retained overlay cache/hash helpers and item-atlas lookup support for the shared debug overlay
  - debug line trimming, icon UV lookup, and reusable retained-region support types
- `src/game/runtime/shared/world/Shared*.*`
- `src/game/runtime/render_model_cache/RenderModelCache*.*`

Native character-material translation:
- `tools/PhlosionNativeModelIr.cpp`
  - converts retained `.phmodel` roles and authored controls into renderer-neutral mesh/material payloads
  - owns source-profile qualification for native SSS, eye, animation, and FresnelEffect modes
- `tools/PhlosionForge.cpp`
  - `inspect-model-materials` reports each decoded submesh's runtime mode,
    subtype, indexed vertex-alpha range, and base/normal/MR/AO/emissive/
    environment texture statistics; use it to distinguish missing source data
    from runtime interpretation defects before changing shaders
- `src/game/runtime/render_model_cache/RenderModelCache.h`
  - canonical game-side material mode and packed-parameter contracts
  - mode 27 covers native layered/displaced effects; subtype 3 retains dynamic
    SSSEffect puff alpha, 3.25 retains opaque SV Gastly smoke, and 3.375 retains
    Z-A Gastly's already-composited base plus authored shadow/rim auxiliary
    without applying the material layers twice
  - mode 32 is the current Z-A `IkCharacter` body bridge: compiled-order
    `OcclusionMap * OcclusionStrength` shadow-color interpolation, layered
    metallic/specular offset/intensity/contrast, exact ShadowingBias and half-
    Lambert band, params0.w `ShadowingGIGain` scaling of the RGB shadow-color
    difference on all three backends, front/back rim domains, ordered middle/
    dark hue processing, raw rim controls, and the metallic-gated authored
    local-reflection cube
  - mode 33 is SV `SSS`: exact base/normal/scalar-roughness/AO/SSS-mask
    transport, smooth-vs-Eevee-fibre surface qualification, and source-proven
    diffuse/specular environment roles evaluated against the shared neutral
    environment until source scene cubes are available
  - mode 34 is SV `FresnelEffect`: primary sRGB color plus a secondary linear color layer, exact Fresnel controls, and a losslessly packed authored RGBA16F local-probe cube
  - mode 35 is the Z-A `IkCharacter` eye bridge: the mode-32 lighting stack
    plus the source-proven reciprocal-IOR refraction, normalized UV-derivative
    footprint, fifth-power view fade, 4-to-14 sample reverse-depth parallax
    march and linear refinement, `BaseColorLayer6` eyelid shadow, authored
    layer-5 highlight, and local reflection on all three backends
- `docs/kanto/evidence/za_ik_eye_runtime_coverage.json`
  - machine-checked mode-35 boundary: 768/928 selected eye bindings are
    consumed; the remaining 160 colored-shadow bindings are unbound but
    source-neutral in this selected eye corpus
  - decodes the shipped PHRC/PHMAT data and requires 38/38 files and 80/80 eye
    submesh records to contain mode 35, preventing stale-cooker false positives
- `docs/kanto/evidence/za_ik_character_dataflow_report.json`
  - maps 62 ordinary-body material fields to compiled registers and records the
    exact local rim, shadow/specular, color-process, diffusion, reflection, AO,
    emission, and scene-fade boundaries
  - proves the complete material-local refraction and height-march equation for
    eye variations 682/1214, including its 4-to-14 sample schedule and hit
    refinement
  - decodes all 52 selected PHMAT files and verifies fourteen authored native
    scalar lanes plus neutral runtime-only lanes in all 184 mode-32 records
- `docs/kanto/evidence/za_kanto_shader_inventory.json`
- `docs/kanto/evidence/za_kanto_option_dataflow.json`
  - broad browser-corpus boundary: 65 species, 212 outputs, 1,084 materials,
    20 exact permutations, 11 selected programs, and 183 one-option edges
    across five families; keep this denominator separate from the deeply
    qualified 52-model IkCharacter promotion subset
- `docs/kanto/evidence/za_scene_color_boundary.json`
  - cross-checks camera/final-fade fields across seven material fragments;
    proves their projected-mask plus 16-tap cascaded-shadow structure,
    IkCharacter's shadowed-N.L and max-direct-RGB insertion points, and the
    226-material enabled `ReceiveShadow` census; records the exact Z-A tone-map
    order plus unavailable runtime values
  - resolves `fp_c4[0]` as the dominant light vector, both indexed IkCharacter
    RGB records, the LOD-0 diffuse-cube Z flip, and the distinct no-flip
    `reflect(-view, mappedNormal)` material-local probe direction
- `tools/research/analyze_za_ui_offscreen_light.py`
- `tools/research/ZaUiOffscreenProbeExporter/`
- `tools/research/extract_za_ui_offscreen_lighting.ps1`
- `docs/kanto/evidence/za_ui_offscreen_*.json`
  - decode and fingerprint the exact retained `spl_ui_offscreen_poke` scene
    light plus its diffuse/specular BC6H cubes; generated runtime probe
    carriers remain under the ignored private asset tree
- `src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshPrep.cpp`
- `src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshMaterialTemplateCache.cpp`
  - translate cached mesh parameters and texture color-space declarations into batch and scene materials
- `src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshGraphicsQuality.cpp`
  - common Low/Medium/High/Ultra texture-detail policy; native foundational maps remain present while explicit LOD bias changes
- `src/game/editor/PokemonPrefabPreview.cpp`
  - model Inspector submission, including the transient review-lighting
    profile carried independently from graphics quality and Z-A-only binding
    of the exact off-screen Pokemon HDR probes; Source Stage currently consumes
    the diffuse scene probe while retaining the global specular probe for its
    still-open source-specular pass
- `src/game/runtime/shared/world/SharedWorldContentSubmit.*`
  - preserves that transient profile across the OpenGL indexed fallback while leaving gameplay submission at its default profile
- `src/engine/editor/EditorShell.cpp` and `src/engine/editor/PhlosionEditorMain.cpp`
  - Review Lighting selector plus reproducible `--asset-preview-lighting`
    automation state, including `za-source-stage`

### 4) Backend Implementations
OpenGL:
- `src/engine/render/OpenGLRenderBackend.*`
- `src/engine/render/opengl/OpenGLRenderBackend*.cpp`
  - world outline extrusion is performed in the vertex shader; draw submission
    records the outline before the textured surface
  - native character-material evaluation, including mode 34, lives in `OpenGLRenderBackendWorldPipeline.cpp`
  - mode 32/35 local-probe lookup is pinned through
    `zaIkLocalReflectionDirection`; the scene diffuse-cube sign convention is
    documented separately and is not applied to the material probe
  - native SSS mode 33's mapped-normal irradiance and roughness-filtered
    reflection sampling live in the same pipeline file

D3D12:
- `src/engine/render/D3D12RenderBackend.*`
- `src/engine/render/d3d12/D3D12RenderBackend*.cpp`
  - world outline extrusion is performed in the vertex shader; direct and
    cached draws replay the textured surface after the outline
  - `D3D12RenderBackendInternal.h` owns fixed-root-signature parameter transport; native material evaluation lives in `D3D12RenderBackendWorldPipeline.cpp`

Vulkan:
- `src/engine/render/VulkanRenderBackend.*`
- `src/engine/render/vulkan/VulkanRenderBackendLifecycle.cpp`
  - Vulkan device/swapchain/pipeline/frame/capture lifetime
- `assets/shaders/vulkan/world_material.glsl`
  - shared Vulkan native-material equations, including mode 33 SSS environment
    sampling; direct and indirect world fragment paths both supply the same
    environment texture
- `src/engine/render/vulkan/VulkanRenderBackendDraw.cpp`
  - debug geometry and world command recording
- `src/engine/render/vulkan/VulkanRenderBackendSprites.cpp`
  - order-preserving instanced sprite packing and texture-run submission
- `src/engine/render/vulkan/VulkanRenderBackendGeometry.cpp`
  - device-local geometry upload/cache lifetime and cached submission
- `src/engine/render/vulkan/VulkanRenderBackendGeometryArena.cpp`
  - paged device-local arena allocation and lifetime
- `src/engine/render/vulkan/VulkanGeometryArenaLayout.*`
  - tested byte-offset, first-index, and base-vertex allocation planning
- `src/engine/render/vulkan/VulkanRenderBackendInstances.cpp`
  - transient instance records and frame-local skin-palette reuse
- `src/engine/render/vulkan/VulkanRenderBackendState.cpp`
  - frame-local uniform reuse, pipeline/descriptor/geometry command-state
    suppression, and cache telemetry
- `src/engine/render/vulkan/VulkanRenderBackendWorldScene.cpp`
  - shared scene cache lifetime and compatibility draw-class submission
- `src/engine/render/vulkan/VulkanRenderBackendIndirectWorldScene.cpp`
  - descriptor-indexed indirect scene planning, outline-first replay, and
    textured-surface submission
- `src/engine/render/vulkan/VulkanRenderBackendMaterialTable.cpp`
  - indexed world-material descriptor registration and frame synchronization
- `src/engine/render/vulkan/VulkanWorldIndirectBatch.*`
  - tested contiguous pipeline/geometry-buffer run planning
- `src/engine/render/vulkan/VulkanWorldIndirectState.h`
  - tested indirect draw-state and push-constant packing
- `src/engine/render/vulkan/VulkanWorldSceneData.h`
  - shared scene-material translation contract
- `src/engine/render/vulkan/VulkanRenderBackendTextures.cpp`
  - raw image upload, samplers, and sprite texture cache
- `src/engine/render/vulkan/VulkanRenderBackendMaterials.cpp`
  - five-map world material cache plus environment descriptor-set assembly
- `src/engine/render/vulkan/VulkanRenderBackendEnvironment.cpp`
  - neutral-room PMREM validation, linear RGBA16F upload, and lifetime
- `src/engine/render/vulkan/VulkanEnvironmentParity.h`
  - maps the actual Vulkan PMREM image format into the shared parity contract
- `src/engine/render/vulkan/VulkanWorldMaterialLayout.h`
  - tested world descriptor bindings
- `src/engine/render/vulkan/VulkanWorldMaterialState.h`
  - Vulkan-minimum-safe world push-constant packing
- `src/engine/render/vulkan/VulkanWorldViewState.h`
  - per-draw camera position/forward/target uniform packing
- `assets/shaders/vulkan/*`
  - focused debug, sprite, direct/indirect world, and material shader modules;
    compiled to SPIR-V by the build
  - `world_material.glsl`, `world.frag`, and `world_indirect.frag` share native mode 34 evaluation between direct and indirect paths

### 5) Display Settings and Backend Selection UX
- `scripts/states/main_menu.lua`
- `src/game/runtime/video/VideoPreferences.*`
- `config/user/video_settings.json` (runtime preference file)

## Common Cross-Cut Touch Points
1. Frame timing and perf logging:
- `src/game/runtime/GameRunner.cpp`
- `src/game/runtime/loop/RuntimeFramePerfCapture.*`
- `src/game/runtime/loop/RuntimePerfLogging.*`

2. Backend startup, activation, and fallback:
- `src/game/runtime/renderer/`
- `src/game/runtime/startup/`
- `src/game/runtime/video/VideoPreferences.*`

3. Backend implementation surfaces:
- `src/engine/render/OpenGLRenderBackend.*`
- `src/engine/render/opengl/OpenGLRenderBackend*.cpp`
- `src/engine/render/D3D12RenderBackend.*`
- `src/engine/render/d3d12/D3D12RenderBackend*.cpp`
- `src/engine/render/VulkanRenderBackend.*`
- `src/engine/render/vulkan/VulkanRenderBackend*.cpp`

4. Validation and smoke wiring:
- `tests/TestMain.cpp`
- `tools/check_renderer_parity_contract.ps1`
- `config/render_parity_scene_matrix.json`
  - deterministic static-PBR, transparent/VFX, combat, and UI capture cases
- `tools/render_parity_matrix.ps1`
  - case orchestration and aggregate parity reporting
- `tools/RenderParitySceneManifest.psm1`
  - scene-manifest validation, path resolution, and case selection policy
- `tools/render_parity_screenshot_diff.ps1`
  - atomic backend capture, image comparison, heatmap, and per-scene report
- `CMakeLists.txt`

## Rule of Thumb
- Shared rendering behavior changes should start in shared runtime modules, not backend-specific branches.
- Backend files should only own API-specific resource/pipeline/submission mechanics.



