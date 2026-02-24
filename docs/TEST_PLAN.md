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

## Coverage Gaps (High Risk)
- No packaged build smoke run (installer output or `dist/Release` execution).

## Next Tests to Add (Priority)
- Packaged build smoke checklist automation (optional).

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
6. Verify shared-path idle/move motion does not add extra bounce versus `opengl` legacy when clip animation is active.
7. Verify per-unit HUD parity (HP bar size/color, energy bar color, level text placement, and player XP ring progress) between `opengl` legacy and shared routes.
8. While zooming camera in/out, verify shared per-unit HUD size remains visually stable, level text stays centered inside the XP ring, HUD sits clearly above the model, no floating Pokemon-name text is rendered above the HUD block, and no persistent white heading guide line follows each unit.
9. Verify faint parity in shared routes: faint animation completes, fade-out reaches full disappear (no lingering tiny proxies/models), and dead units are no longer rendered after faint finishes.
10. Verify shared combat FX timing in `opengl_shared` and `d3d12`: no non-legacy attacker telegraph rings or attacker-to-target connector lines are rendered, projectile traces follow pending projectile timing, and impact bursts appear near pending hit/impact times.
11. Verify shared card-shop textures are visually stable (reduced shimmer/grain) when cards are viewed at menu scale and during camera/mode transitions.
12. Verify shared move-VFX variety against legacy route mapping: `growl` emits source-centered layered rings (legacy draw-pass style, no target link), `tackle` shows impact burst behavior, `scratch`/`metal_claw` show slash-style overlays, `tail_whip`/`bubble`/`water_gun` show aqua-style overlays, and `vine_whip`/`leech_seed` show green burst behavior.
13. Verify Charmander tail-fire presentation is visible in both `opengl_shared` and `d3d12` during gameplay (tail flame should remain active while alive, not only during attacks).
14. Verify leech-seed shared visual flow: projectile trace appears at spawn/hit timing, then drain traces are visible from seeded target to source while seed is active.
15. Verify charged growl clip parity in shared routes (`opengl_shared`, `d3d12`): species that map growl to a dedicated charged clip in `config/attack_anim_config.json` (for example Bulbasaur/Charmander) should play that mapped clip rather than default attack1 during growl.
16. Verify legacy-growl-bridge parity in shared routes: with default `PAC_BACKEND_GROWL_LEGACY_VFX` (enabled), growl should use manifest-styled textured ring passes (no fallback orange telegraph artifacts); with `PAC_BACKEND_GROWL_LEGACY_VFX=0`, fallback procedural growl should be visibly different and used only for debugging.
17. Verify shared particle-VFX bridge parity in `opengl_shared` and `d3d12` (default `PAC_BACKEND_PARTICLE_LEGACY_VFX` enabled): `tackle`, grass impacts, claw swipes, aqua swooshes, leech-seed projectile/drain/heal, and Charmander tail-fire should follow the same simulation timing/positions as legacy OpenGL (only renderer/material differences should remain). Toggle `PAC_BACKEND_PARTICLE_LEGACY_VFX=0` to confirm the old procedural shared fallback path is still available for A/B debugging.
18. Verify growl visual-material parity in shared routes (`opengl_shared`, `d3d12`) against `opengl` legacy: growl passes should read as additive (brighter overlap, no muddy alpha blend), line-pass strands should show mesh-alpha falloff, and quarter-ring colors/alpha should track legacy TEV coloration more closely instead of flat-tinted meshes.
19. Verify Charmander tail-fire parity in shared routes (`opengl_shared`, `d3d12`) against `opengl` legacy: flame should use the same premultiplied/fire-like look (not flat flipbook cards), exhibit similar frame timing/flicker (time+seed-driven, not synchronized age-only cycling), and preserve dual-layer body/core look without dropping back to the projected fallback unless the particle snapshot is missing.
20. Regression check: switch repeatedly between `opengl`, `opengl_shared`, and `d3d12`, reopen starter/shop cards, and verify shared card art never appears upside-down (guards against leaked global `stb_image` flip state from VFX texture loads).
21. Charmander tail-fire fallback quality check (shared routes without legacy `Model*` tail anchor): if particle snapshots are absent, shared tail fire should still render as flame-textured dual-layer billboards at the tail (not line/ring placeholder art), and should animate/flicker over time.
22. Tail-fire orientation/anchor parity check (shared routes): Charmander tail flame should not be vertically flipped, should sit near the tail tip (not feet/body center or floating far away), and should remain plausibly attached during idle/attack motion when compared side-by-side with `opengl` legacy.
