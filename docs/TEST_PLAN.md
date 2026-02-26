# Test Plan

Focus on catching real regressions while keeping tests fast and headless.

## Goals
- Detect movement logic regressions early.
- Detect render pipeline setup regressions early.
- Enforce determinism in core gameplay systems.
- Validate content before runtime.

## Current Coverage (What Exists)
- ECS correctness and invariants.
- Determinism hooks (RNG and time).
- Lua bindings smoke checks.
- Content validation and battle invariants.
- Movement invariants (headless movement planning).
- Model parse smoke test (fastgltf, CPU-only).
- GLTF asset structure smoke test (nodes/meshes/animations present).
- Script API contract test (bindings + basic behavior).
- Headless round flow test (Lua round system + update graph).
- Headless placement -> combat state flow.
- Animset role resolution test (CPU-only, JSON-driven).
- Animset role names validated against GLB animation names (CPU-only).
- Animset clip names validated against GLB animation names (CPU-only).
- Headless combat slice test (Lua combat system + damage applied).
- Minimal end-to-end headless flow (placement -> combat -> round resolution).
- Render pipeline smoke test (shader include checks; optional GL compile + board + real model draw via `PAC_TEST_GL=1`).
- Combat animation clip-cache contract (`combat_anim_index_cache_contract`) to ensure backend/no-`Model*` alias resolution still maps move clip names (including `.gfbanm` + case variants) to cached animation indices.
- Layering enforcement check (engine cannot include game headers).
- Render-route contract coverage (`RenderRoutes`, frame-flow policy, backend UI policy, and route ownership guardrail that only `GameSession` probes backend route preferences).
- Unified frame-flow coverage for backend-neutral world/HUD layer decisions, including backend menu-backdrop world-layer routing.
- Route API guardrails (`render_policy_api_contract`) to keep policy surfaces route-object only (no bool overload regressions).
- State UI route guardrail (`state_ui_route_policy_contract`) to prevent gameplay states from bypassing route-policy helpers with direct `GameServices` UI-route checks.
- Top-banner layout contract coverage (`backend_top_banner_contract`) to keep placement/combat header geometry and text anchoring on one shared helper.
- Service-route mapping coverage (`game_service_render_routes_contract`) for `GameServices` -> `RenderRoutes` conversion.
- GameWorld backend-render mode regression coverage (render-enabled world can skip legacy OpenGL model attachment).
- Shared capture presentation helper contract coverage (`shared_capture_presentation_contract`) for pokeball clip-time mapping, transform assembly, anim-index lookup fallback, and shared capture snapshot cache lookup behavior.
- Shared capture overlay fallback VFX contract coverage (`shared_capture_overlay_vfx_contract`) for screen-space pokeball icon/seam generation and phase-driven projected ring emission after moving the shared capture overlay helper out of `GameSession`.
- Shared particle VFX style resolver contract coverage (`shared_particle_vfx_styles_contract`) for shader-fragment-path style classification (proc texture/tint/alpha rules) after moving shared particle billboard style selection out of `GameSession`.
- Shared particle billboard batch-builder contract coverage (`shared_particle_billboard_batches_contract`) for generic shared particle quad assembly, clip rejection, flipbook UV frame selection, and style tint propagation after moving non-tail-fire billboard batch assembly out of `GameSession`.
- Shared particle VFX bridge dispatch contract coverage (`shared_particle_vfx_bridge_dispatch_contract`) for ordered per-effect snapshot append orchestration and tail-fire/leech-drain result flag forwarding after moving shared particle dispatch sequencing out of `GameSession`.
- Shared tail-fire atlas helper contract coverage (`shared_tail_fire_atlas_helpers_contract`) for premultiplied atlas baking and combined-atlas packing/rect layout after moving tail-fire atlas prep math out of `GameSession`.
- Shared tail-fire exact GPU batch-builder contract coverage (`shared_tail_fire_exact_gpu_batches_contract`) for projection/unprojection quad assembly, exact fire-tail material payload fields, and atlas rect forwarding after moving exact tail-fire batch assembly out of `GameSession`.
- GameWorld capture render snapshot timing contract coverage (`gameworld_capture_render_snapshot_timing_contract`) for normalized phase fields (`phaseNorm01`, `absorbNorm01`, `absorbLateVisual01`) and monotonic capture phase sequencing.
- Shared world indexed-batch submission contract coverage (`shared_world_indexed_batches_contract`) for opaque/mask insertion-order draws, stable depth-sorted blend draws, and owned-texture fallback payload forwarding.
- Shared growl VFX helper contract coverage (`shared_growl_vfx_helpers_contract`) for TEV-state resolution/clamping, growl pass classification, baked texture key stability, pass texture baking, and line-alpha quantization used by shared growl VFX submission in `GameSession`.
- Shared growl wave batch-builder contract coverage (`shared_growl_wave_batches_contract`) for quarter-ring batch assembly, additive blend payload defaults, texture-key tagging, and sort-depth generation after moving growl geometry submission out of `GameSession`.
- Shared growl wave bridge orchestration contract coverage (`shared_growl_wave_bridge_contract`) for pass iteration, mesh-resolver/texture-resolver callback usage, and valid-pass batch append behavior after moving growl bridge orchestration out of `GameSession`.
- D3D12 backend helper/material-constant contract coverage (`d3d12_world_material_constants_contract`) for D3D12 shared world helper extraction (`alignUp`, wrap-mode sanitization, and `WorldPsConstants` payload mapping/clamping).
- Runtime smoke coverage wiring includes `opengl_shared` alongside `opengl` and `d3d12` when `PAC_ENABLE_RUNTIME_SMOKE_TESTS` is enabled.

## Housework Slice Regression Sets (Targeted)
- D3D12 backend split slices (Pipelines / DebugDraw / Textures / CachedWorldMeshes / WorldDraw / Lifecycle):
  - Automated: `PAC_Tests.d3d12_probe_contract`, `PAC_Tests.d3d12_world_material_constants_contract`, `PAC_Tests.render_pipeline_smoke`, plus full `ctest` before merge.
  - Manual (short): one `d3d12` boot -> menu -> gameplay -> capture/FX smoke to validate world draw/UI draw/device lifecycle paths still render and shut down cleanly.
- `GameSession` shared-path extraction slices:
  - Automated: `shared_capture_presentation_contract`, `shared_capture_overlay_vfx_contract`, `shared_particle_vfx_styles_contract`, `shared_particle_billboard_batches_contract`, `shared_particle_vfx_bridge_dispatch_contract`, `gameworld_capture_render_snapshot_timing_contract`, `shared_world_indexed_batches_contract`, `shared_growl_vfx_helpers_contract`, `shared_growl_wave_bridge_contract`, `shared_growl_wave_batches_contract`, `render_flow_decisions_contract`, plus full `ctest`.
  - Add `shared_tail_fire_atlas_helpers_contract` / `shared_tail_fire_exact_gpu_batches_contract` when the slice touches tail-fire atlas prep or exact tail-fire GPU batch assembly helpers.
  - If the slice moves `d3d12`-only shared capture fast-path logic (for example pokeball cached rigid/per-submesh direct draws), require a short `d3d12` Adventure capture smoke in addition to the `opengl_shared` smoke.
  - If the slice moves projected-unit rendering/model/HUD composition (for example `drawProjectedUnits` extraction), require `opengl_shared` combat smoke that covers unit models + per-unit HUD + shared VFX + one Adventure capture, plus optional `d3d12` sanity.
  - Manual (short): `opengl_shared` parity smoke on the feature being extracted (capture, VFX bridge, HUD, etc.).

## Coverage Gaps (High Risk)
- No packaged build smoke run (installer output or `dist/Release` execution).
- No automated visual parity/image-diff harness across `opengl` / `opengl_shared` / `d3d12`.
- No automated runtime menu click/input smoke across renderer APIs (startup/menu correctness still requires manual signoff).

## Next Tests to Add (Priority)
- Packaged build smoke checklist automation (optional).
- Scripted runtime menu-interaction smoke across renderer APIs (click/select main menu actions and verify no crash).
- Seeded screenshot capture + image diff harness for a small parity scene matrix (board, 3-model combat, capture throw, key VFX).

## Manual Smoke Checklist (Release Builds)
1. Launch the game and reach the board.
2. Place a few units and start combat.
3. Verify movement looks correct and units do not overlap.
4. Verify animations play and models appear.

## Manual Parity Matrix (Renderer Routes)
1. Run with Display `Render API = OpenGL (Legacy)` (`renderer_backend=opengl`) and capture baseline screenshots/perf.
2. Run with Display `Render API = OpenGL (Shared Contracts)` (`renderer_backend=opengl_shared`) and compare against OpenGL legacy.
3. Run with Display `Render API = D3D12` (`renderer_backend=d3d12`) and compare against OpenGL shared-contract output.
4. In combat with 3+ units moving/attacking, verify no textured submesh dropout (example failures: eyes/bulbs missing, body pieces disappearing mid-motion).
5. Verify alpha behavior parity for MASK/BLEND submeshes (no triangular holes/gaps while moving) in both `opengl_shared` and `d3d12`.
6. Verify no mirrored-UV seam line artifacts appear on textured models in shared routes (example regression: a dark/bright line down Pidgey's face/front in `opengl_shared`/`d3d12`).
7. If the seam appears only in `d3d12` (not `opengl_shared`), retest after D3D12 sampler/gradient changes and verify it is not a D3D12-only mip seam from shared wrap emulation.
8. If the seam still appears only in `d3d12`, retest after D3D12 wrap-aware mip generation changes and verify it is not baked into D3D12-generated mip levels for repeat/mirrored-repeat world textures.
9. If the seam still appears only in `d3d12`, retest after D3D12 real sampler-wrap parity changes (clamp/repeat/mirror sampler combinations in world textured path) and verify mirrored/repeated UV seams are filtered the same as `opengl_shared`.
10. Verify shared-path idle/move motion does not add extra bounce versus `opengl` legacy when clip animation is active.
11. Verify per-unit HUD parity (HP bar size/color, energy bar color, level text placement, and player XP ring progress) between `opengl` legacy and shared routes.
12. While zooming camera in/out, verify shared per-unit HUD size remains visually stable, level text stays centered inside the XP ring, HUD sits clearly above the model, no floating Pokemon-name text is rendered above the HUD block, and no persistent white heading guide line follows each unit.
13. Verify faint parity in shared routes: faint animation completes, fade-out reaches full disappear (no lingering tiny proxies/models), and dead units are no longer rendered after faint finishes.
14. Verify shared combat FX timing in `opengl_shared` and `d3d12`: no non-legacy attacker telegraph rings or attacker-to-target connector lines are rendered, projectile traces follow pending projectile timing, and impact bursts appear near pending hit/impact times.
15. Verify shared card-shop textures are visually stable (reduced shimmer/grain) when cards are viewed at menu scale and during camera/mode transitions.
16. Verify shared move-VFX variety against legacy route mapping: `growl` emits source-centered layered rings (legacy draw-pass style, no target link), `tackle` shows impact burst behavior, `scratch`/`metal_claw` show slash-style overlays, `tail_whip`/`bubble`/`water_gun` show aqua-style overlays, and `vine_whip`/`leech_seed` show green burst behavior.
17. Verify Charmander tail-fire presentation is visible in both `opengl_shared` and `d3d12` during gameplay (tail flame should remain active while alive, not only during attacks).
18. Verify leech-seed shared visual flow: projectile trace appears at spawn/hit timing, then drain traces are visible from seeded target to source while seed is active.
19. Verify charged growl clip parity in shared routes (`opengl_shared`, `d3d12`): species that map growl to a dedicated charged clip in `config/attack_anim_config.json` (for example Bulbasaur/Charmander) should play that mapped clip rather than default attack1 during growl.
20. Verify legacy-growl-bridge parity in shared routes: with default `PAC_BACKEND_GROWL_LEGACY_VFX` (enabled), growl should use manifest-styled textured ring passes (no fallback orange telegraph artifacts); with `PAC_BACKEND_GROWL_LEGACY_VFX=0`, fallback procedural growl should be visibly different and used only for debugging.
21. Verify shared particle-VFX bridge parity in `opengl_shared` and `d3d12` (default `PAC_BACKEND_PARTICLE_LEGACY_VFX` enabled): `tackle`, grass impacts, claw swipes, aqua swooshes, leech-seed projectile/drain/heal, and Charmander tail-fire should follow the same simulation timing/positions as legacy OpenGL (only renderer/material differences should remain). Toggle `PAC_BACKEND_PARTICLE_LEGACY_VFX=0` to confirm the old procedural shared fallback path is still available for A/B debugging.
22. Verify growl visual-material parity in shared routes (`opengl_shared`, `d3d12`) against `opengl` legacy: growl passes should read as additive (brighter overlap, no muddy alpha blend), line-pass strands should show mesh-alpha falloff, and quarter-ring colors/alpha should track legacy TEV coloration more closely instead of flat-tinted meshes.
23. Verify Charmander tail-fire parity in shared routes (`opengl_shared`, `d3d12`) against `opengl` legacy: flame should use the same premultiplied/fire-like look (not flat flipbook cards), exhibit similar frame timing/flicker (time+seed-driven, not synchronized age-only cycling), and preserve dual-layer body/core look without dropping back to the projected fallback unless the particle snapshot is missing.
24. Regression check: switch repeatedly between `opengl`, `opengl_shared`, and `d3d12`, reopen starter/shop cards, and verify shared card art never appears upside-down (guards against leaked global `stb_image` flip state from VFX texture loads).
25. Charmander tail-fire fallback quality check (shared routes without legacy `Model*` tail anchor): if particle snapshots are absent, shared tail fire should still render as flame-textured dual-layer billboards at the tail (not line/ring placeholder art), and should animate/flicker over time.
26. Tail-fire orientation/anchor parity check (shared routes): Charmander tail flame should not be vertically flipped, should sit near the tail tip (not feet/body center or floating far away), and should remain plausibly attached during idle/attack motion when compared side-by-side with `opengl` legacy.
27. Tail-fire animated-anchor parity check (shared routes): in `opengl_shared` and `d3d12`, Charmander tail fire should follow the animated tail-tip motion (including turns/attack poses) rather than lagging at a fixed proxy-body offset; verify fallback still appears if backend clip pose is unavailable.
28. Tail-fire shader-look parity check (shared routes): compare Charmander tail fire against `opengl` legacy for shape breakup, inner core/hybrid layering, orange-red color boundary jitter, flicker timing, and premultiplied glow falloff; shared routes should no longer show the previous flat dual-billboard approximation.
29. Tail-fire exact-shader shared performance sanity check: with Charmander active in `opengl_shared` and `d3d12`, verify the shared exact `fire_tail` CPU path no longer causes catastrophic render spikes (for example >500ms render frames) during normal tail-fire playback; compare against `opengl` legacy and note remaining gap separately from visual parity.
30. Tail-fire exact-atlas orientation/quality check (shared routes): after the exact CPU tail-fire optimization path is active, verify Charmander flame is not vertically inverted and no longer appears obviously blocky/pixelated at normal gameplay zoom (compare `opengl_shared` and `d3d12` against `opengl` legacy).
31. Tail-fire emitter cadence/randomness parity check (shared routes): with Charmander idle/turning/attacking, compare `opengl_shared` and `d3d12` against `opengl` legacy for emission density over time, flicker timing variability, and motion inheritance from tail-tip movement; shared fallback should no longer read as a deterministic/static phase loop.
32. Tail-fire artifact regression check (shared routes): after cadence/randomness changes, verify shared Charmander tail fire does not collapse into flat orange/square billboard artifacts (especially while using the exact CPU `fire_tail` path with fallback snapshots) and still retains recognizable flame silhouette breakup.
33. Tail-fire mode sanity check: default shared routes should use the stable dual-atlas tail-fire path (exact CPU `fire_tail` emulation off unless `PAC_BACKEND_TAIL_FIRE_EXACT_CPU=1` is set); verify performance/visuals in default mode first, then optionally A/B the exact CPU path as a debug-only parity experiment.
34. Tail-fire exact GPU material parity/perf check (shared routes): with default settings in `opengl_shared` and `d3d12`, verify Charmander tail fire now uses the backend exact `fire_tail` shader material path (no CPU-baked square/orange tiles, no catastrophic CPU spikes), remains attached to the tail tip, and visually matches `opengl` legacy more closely than the prior dual-atlas approximation.
35. Tail-fire exact GPU orientation check (shared routes): after the backend exact `fire_tail` material path is active, verify Charmander tail fire is not vertically inverted in `opengl_shared` or `d3d12` (shared billboard quads should match legacy point-sprite orientation without an extra local UV Y flip).
36. Shared parity regression check (bench/faint/grid/leech-seed): in `opengl_shared` and `d3d12`, verify the bench strip is visible even when board framing is tall, the shared world-space bench grid row (adjacent to the board) is visible with slot boundaries, fainting textured models fade cleanly without mask-cutout/material popping, board grid lines remain readable at shallow camera angles, leech-seed projectile is a lobbed arc (not missing), and no dotted green drain connector fallback line appears when the legacy particle snapshot bridge is enabled.
37. Shared shop/sell-overlay parity check (`opengl_shared`, `d3d12`): while dragging a unit in shop/planning flow, verify the main shop card row is hidden while the sell/release drop overlay is visible (item row hidden too), matching legacy behavior; hidden shop cards should not remain clickable by mouse/number keys during the drag.
38. Adventure shared inventory/capture parity check (`opengl_shared`, `d3d12`): in Adventure mode, verify the inventory renders as icon cards (not text-only rows) using the item atlas, selection/highlight and paging controls remain clickable/functional, and capture attempts show a visible pokeball throw/shake/resolve animation overlay (no longer "missing" in shared routes) while comparing overall look/timing against `opengl` legacy.
39. Adventure shared pokeball-model parity check (`opengl_shared`, `d3d12`): during capture attempts, verify shared routes render `assets/models/pokeball.glb` only (no 2D icon/outline fallback), track throw/floor placement/roll correctly, and use the `Hinge_TopAction` clip during absorb so the ball opens/closes around target impact before floor shake/resolve. Verify the target turns red and fades toward half opacity while shrinking late in absorb (near the end of the clip, not immediately on impact). If `d3d12` does not animate while `opengl_shared` does, verify the backend pokeball cache (`cache/models/f664dfc73e402009.pacmdl`) was rebuilt from the updated GLB.
