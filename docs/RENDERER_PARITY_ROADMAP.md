# Renderer Parity + Performance Roadmap

Status: Active
Type: Roadmap
Last updated: 2026-08-20

This is the current renderer roadmap for the repo. It replaces the older
"pre-merge D3D12 gate" and generic housework framing with one current parity
and performance roadmap.

## Relationship To The Contract
- `docs/RENDERER_PARITY_CONTRACT.md` is the hard baseline that all active backends
  must satisfy at all times.
- This roadmap is the changing plan for how we improve parity, performance, and
  renderer hygiene from here.
- If a rule should be stable and testable, it belongs in the contract.
- If an item is a current priority, experiment lane, or cleanup target, it
  belongs here.

## Current State
- Shared gameplay presentation is the default path for `OpenGL`, `Vulkan`, and `D3D12`.
- Renderer parity now has a manifest-driven deterministic three-backend
  screenshot matrix with fixed simulation timing, quantitative error metrics,
  aggregate/per-scene JSON reports, and amplified heatmaps. Its current cases
  cover static PBR/environment rendering, transparent native animated
  materials, Route 1
  combat, and startup UI. D3D12 readback preserves the same top-left screenshot
  orientation as OpenGL and Vulkan.
- The local renderer qualification runner records adapter/driver evidence,
  validates the three backend contract signatures, runs the native matrix, and
  reruns the matrix through Vulkan's forced direct compatibility path in one
  aggregate pass/fail report.
- Authored dual-source alpha/additive blending now uses native OpenGL and D3D12
  paths plus native Vulkan support when the selected device exposes
  `dualSrcBlend`; the shared policy owns the explicit capability fallback.
- Vulkan has a complete frame/world/debug/UI route with retained device-local
  geometry, GPU palette skinning, rigid/skinned instancing, and a direct shared
  world-scene fast path. Its world path consumes base-color, normal,
  metallic/roughness, occlusion, and emissive maps with direct GGX lighting,
  neutral-room PMREM/IBL, specialized authored materials, and character
  inking. Frame-local palette/uniform reuse and prepared scene-material plus
  command-state caches now reduce steady-state submission work. Keyed geometry
  now resides in paged device-local arena buffers, allowing base-vertex draws
  to reuse vertex/index bindings. On capable devices, fixed descriptor-indexed
  material tables and draw-ID-addressed state now batch contiguous world draw
  classes with indexed indirect multi-draw. Character outlines now use a
  measured indirect replay on that path, while unsupported devices retain the
  same direct compatibility path. Sprites use the same order-preserving
  instanced-run model as the established backends.
  Its neutral PMREM now uploads and samples linear RGBA16F data, and the runtime
  contract derives Vulkan's reported precision from the actual `VkFormat`.
  The current Vulkan parity checkpoint is complete. Cross-frame static palette
  retention was measured and rejected because the paired-palette strategy
  regressed dense-scene CPU/build time; indirect outline replay was retained
  because it improved both CPU and GPU time.
- Character outline extrusion is now shader-driven on all three backends. The
  inverted hull renders before the textured surface so alpha-blended models do
  not become black silhouettes. The final four-scene matrix passes with inking
  enabled, and the representative captures were inspected directly.
- Perf telemetry is now good enough to make local decisions:
  - `frame_cpu_ms`
  - `render_build_ms`
  - `render_submit_ms`
  - `present_wait_ms`
  - `gpu_frame_ms`
  - projected render buckets
  - fixed-step buckets
  - startup/flow trace timing
- The biggest recent wins were:
  - moving gameplay hot paths out of Lua and into native code
  - reducing projected render CPU work
  - removing major first-use stalls with targeted prewarm/persistent caches
- The biggest remaining performance problem is not startup. It is steady-state
  render-build CPU cost in heavier scenes.

## Working Categories
- `Measurement`
  - Keep perf counters, benchmark discipline, and log accuracy trustworthy.
- `Runtime Hot Path`
  - Reduce steady-state frame cost in real gameplay.
  - This is the highest-value category.
- `Cold Path / Hitch Removal`
  - Fix startup and first-use stalls when they are clearly user-visible.
  - This is worth doing, but it is not the mainline performance strategy.
- `Backend / Product Hygiene`
  - Keep `OpenGL`, `Vulkan`, and `D3D12` behavior aligned where possible.
  - Keep settings, docs, and logs honest.

## Priority Order
1. Steady-state runtime render/build performance.
2. Benchmark discipline and perf regression evidence.
3. User-visible first-use hitch removal.
4. Settings and docs cleanup.
5. Backend-specific optimization only when shared-path work is exhausted or API behavior forces it.

## What Is No Longer The Right Framing
- The repo should not be organized around "D3D12 merge blockers" anymore.
- The repo should not treat startup prewarm work as the primary performance lane.
- The repo should not assume more generic GPU feature work is automatically the next best return.

## Current Policy
- Shared-path improvements are the default.
- Backend-specific work is justified only when API/runtime behavior requires it.
- Performance work should be guided by measured frame-time buckets, not by generic GPU/CPU slogans.
- Treat "move work to GPU" as a hypothesis, not as an automatic win; keep it
  only when the measured hot path actually uses the new path and target buckets
  improve.
- Cold-path work is worth prioritizing only when it removes an obvious user-visible stall.

## Near-Term Roadmap
1. Keep Release benchmark runs repeatable and comparable.
2. Re-center on dense-combat steady-state cost:
   - `render_build_ms`
   - projected pose/model/prep/geometry buckets
   - draw submission overhead
3. Add projected-path attribution for perf experiments:
   - count shared rigid, clip-skinned, CPU-rewritten, and cached-indexed paths
   - use that evidence before landing more projected GPU-offload changes
4. Keep the native Charmander-line scene as a layered-Unlit regression guard:
   - no synthetic atlas bake, prewarm, sidecar, or billboard work may return
   - measure steady-state board cost on OpenGL, D3D12, and Vulkan when the
     generic evaluator changes
   - preserve the accepted native material result unless there is a deliberate
     art change
5. Continue GPU offloading only where it removes CPU render-build work.
6. Keep cold-path fixes surgical:
   - startup
   - first-shop-entry
   - first-species-use
   - first-VFX-use
7. Clean up user-facing graphics/settings behavior so menus and logs reflect reality.
8. Preserve Vulkan parity with measured regression checks:
   - keep the deterministic scene manifest representative, adding cases only
     when their simulation and capture state are stable across backends
   - keep descriptor-indexed indirect batching capability-gated and validate
     both it and the direct fallback on representative adapters
   - keep indirect outline replay while it continues to improve the measured
     inked scene and preserve textured surfaces
   - do not retry the rejected paired static-palette strategy unless the data
     lifetime or shader transport design changes materially
   - compare representative scenes after each slice

## Deferred Next Iteration Candidates
- Retained/dirty overlay submission.
  - Goal: stop rebuilding unchanged UI and overlay draw data every frame.
  - Primary buckets to watch: `render_overlay_prep_ms`, `render_ui_submit_ms`.
- More fundamental projected-unit submission redesign.
  - Goal: move farther away from per-unit rebuild/submit work and toward shared prepared data plus smaller per-unit deltas.
  - Primary buckets to watch: `projected_model_prep_ms`, `projected_model_geometry_ms`, `render_world_indexed_ms`.
- Generic native layered-Unlit evaluator naming cleanup.
  - Goal: replace the historical Vulkan `world_tail_fire.glsl` name without
    changing serialized material mode 27 or backend behavior.
  - Require the six-model native Charmander-family contract and full renderer
    parity matrix before and after the rename.

## Current Engineering Guidance
- If work scales with triangle count or per-vertex visual math, prefer pushing it toward GPU/shader-side handling.
- If work is deterministic gameplay truth, keep it on CPU.
- If a change improves startup but risks reintroducing runtime hitches, prefer runtime smoothness.
- If a doc describes an old blocker that is no longer true in code/logs, rewrite or remove it.

## Success Signals
- Dense combat frame time continues trending down on the target laptop.
- Startup and first-use stalls stay below the threshold of being user-visible problems.
- Shared-path changes benefit all active backends unless there is a documented reason otherwise.
- Docs, logs, and menu behavior match the current engine reality.

## Related Docs
- `docs/TEST_PLAN.md`
- `docs/CPU_GPU_WORK_SPLIT.md`
- `docs/PERF_DECISIONS.md`
- `docs/RENDER_RESTRUCTURING_OUTSTANDING.md`
- `docs/RENDER_PATH_FILE_MAP.md`
- `docs/RENDERER_CONFIGURATION.md`
- `docs/TECH_DEBT.md`
- `docs/DISPLAY_GRAPHICS_ROADMAP.md`
