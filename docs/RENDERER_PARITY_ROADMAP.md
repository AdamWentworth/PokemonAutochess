# Renderer Parity Roadmap (Living)

Goal
- Deliver the same game experience in OpenGL and D3D12: same menus, layout, cards, board, models, materials, animation, VFX, and gameplay readability.

Scope
- In scope: render/UI parity, backend switch UX, parity tests, perf tracking.
- Out of scope for this roadmap: new gameplay features unrelated to parity.

Definition Of Done
- OpenGL and D3D12 produce matching menu and in-game presentation for the same seed/state.
- No backend-specific gameplay UI behavior differences.
- Same board/unit/material/animation/VFX behavior across both backends.
- Settings allow backend choice with restart flow and return to video menu after restart.
- Parity regression tests and runtime smoke tests pass for both backends.

Current Reality (Baseline)
- Non-OpenGL path uses backend debug view and backend-specific UI path.
- OpenGL path uses legacy world/UI path.
- Text in backend UI is quad-based debug text, causing rectangle-like glyph artifacts.
- World model/material/animation rendering is still split and not fully equivalent.
- Temporary parity validation mode is available: `renderer_backend=opengl_shared` runs OpenGL with shared-contract render/UI routes (while `renderer_backend=opengl` keeps legacy routes).

Evidence Anchors (Key Files)
- Render-flow split: `src/game/runtime/RenderFlowDecisions.h`, `src/game/runtime/GameSession.cpp`
- Backend UI policy split: `src/game/state/BackendUiPolicy.h`
- Backend quad-text path: `src/game/runtime/BackendDebugText.h`
- Legacy world renderer: `src/game/world/GameWorldRender.cpp`
- D3D12 backend implementation: `src/engine/render/D3D12RenderBackend.cpp`
- Backend selection/restart prefs: `src/game/runtime/GameRunner.cpp`, `src/game/runtime/VideoPreferences.cpp`, `scripts/states/main_menu.lua`

Workstreams
1. Unified Frame Flow
- Remove legacy-vs-backend gameplay render branching.
- Keep debug visualization as optional tooling only.

2. Unified UI Stack
- Use one menu/shop/HUD rendering path for both APIs.
- Replace debug quad text with proper glyph text rendering.

3. Unified World Stack
- Use one backend-neutral world command path (board, units, bench, health bars).
- Remove backend-specific model proxy fallback as primary rendering path.

4. Model/Material/Animation Parity
- Ensure skinned mesh draw, material sampling, alpha behavior, and clip playback parity.
- Remove visible quality downgrades from D3D12 path.

5. VFX Parity
- Ensure move VFX and combat readability match across backends.

6. Backend UX + Safety
- Stable apply+restart flow.
- Return users to video settings after restart.
- Warn about unsaved run progress when relevant.

7. Test + Perf Guardrails
- Add visual parity checks for key scenes.
- Add crash regression tests for backend switching and starter selection.
- Add perf snapshots for OpenGL vs D3D12 scenes.

Milestones
- M1: UI/Menu parity (no backend-only menu visuals)
- M2: World baseline parity (board + units + bench + HUD)
- M3: Model/material/animation parity
- M4: VFX parity
- M5: Test/perf hardening and cleanup

Remaining Estimate (as of 2026-02-22)
- Estimated remaining work to true renderer-agnostic parity: 9-12 focused iterations.
- Why this is still non-trivial: game/runtime still owns backend debug-world rendering and legacy world/HUD paths, so route contracts are cleaner now but visual stack ownership is not fully unified yet.
- Migration target (contract-first):
- 1) Treat OpenGL legacy behavior as source-of-truth and encode that behavior into shared contracts.
- 2) Validate parity using three runtime modes: `opengl` (legacy), `opengl_shared` (shared contracts), and `d3d12` (shared contracts).
- 3) Flip OpenGL default to shared contracts once parity is stable, then retire legacy world/HUD paths.
- Next parity-first slice:
- 1) Unify frame flow and frame-layer dispatch (remove backend-specific flow branching at render orchestration level).
- 2) Continue frame-flow unification by keeping one world-layer decision path and isolating legacy-only HUD layering.
- 3) Then proceed to UI and world command unification workstreams.

Prioritized Backlog
- [x] Guard backend text-menu fallback behind explicit backend policy (regression safety for OpenGL menu path).
- [x] Decouple `GameServices::renderEnabled` from legacy-path selection and route state UI fallbacks through legacy-route helpers (`usesLegacyGameUiPath`) instead of treating non-OpenGL as non-render.
- [x] Remove state-level direct backend route preference reads (`prefersLegacyGameUiPath`) and route gameplay UI decisions through `GameServices` route helpers (`usesLegacyGameUiPath` / `usesBackendGameUiPath`).
- [x] Route `GameSession` gameplay input/update/render decisions through `GameServices` route helpers (render path vs UI path) instead of directly branching on local `legacyRenderPath`.
- [x] Introduce a shared runtime route contract (`RenderRoutes`) and route render/UI policy helpers through it.
- [x] Add route ownership guardrails so only `GameSession` reads backend route-preference hooks.
- [x] Remove transitional bool-based route policy overloads so frame/UI render policy APIs are route-object only.
- [x] Centralize `GameServices` -> `RenderRoutes` conversion in one helper (`routesFromServices`) and use it in gameplay states/session route selection.
- [x] Remove direct `GameServices` UI-route reads in gameplay states (`services.usesLegacyGameUiPath` / `usesBackendGameUiPath`) and consume route helpers via `routesFromServices` + backend UI policy.
- [x] Unify top-banner overlay layout contract for gameplay states (`PlacementState`, `CombatState`) across backend and legacy UI paths.
- [x] Add temporary OpenGL shared-contract validation mode (`renderer_backend=opengl_shared`) so parity can be compared across `opengl` legacy, `opengl_shared`, and `d3d12` shared from in-game Display settings.
- [ ] Remove backend-specific gameplay render flow and unify frame graph. (In progress: frame-flow contract now uses backend-neutral world/HUD layer decisions and route-dispatched world-layer execution; remaining work is to retire backend debug-world as the primary gameplay world renderer.)
- [x] Unify frame-flow policy contract to backend-neutral world/HUD layer outputs (`renderWorldLayer`, `renderLegacyHudLayer`) with menu-backdrop-aware world routing.
- [x] Unify `GameSession` frame execution around one `renderWorldLayer` route dispatcher instead of backend-specific flow flags.
- [ ] Remove `shouldUseBackendUi` backend split and unify UI policy. (Re-opened: OpenGL legacy card/shop path restored to prevent visual regressions while parity work continues.)
- [ ] Replace backend quad text with glyph text rendering. (In progress: backend text menu now uses line-stroke text, full glyph path still pending.)
- [ ] Route both backends through the same menu/shop/HUD layout/render code. (In progress: text-menu rendering is now forced through one backend-neutral path for both OpenGL and D3D12; shop/starter parity remains.)
- [ ] Route both backends through the same world command generation code. (In progress: backend world path now prefers model mesh rendering with portrait fallback policy and suppresses tint-under-portrait artifacts.)
- [x] Remove `setRenderEnabled(legacyRenderPath)` behavior that disabled non-OpenGL world resources. (Now split into `renderEnabled` vs `legacyModelRenderPathEnabled` in `GameWorld`, with backend-mode regression coverage.)
- [ ] Move backend debug world rendering behind an explicit dev-only flag. (In progress: menu world-backdrop is now disabled by default and can be enabled via `PAC_BACKEND_MENU_BACKDROP=1`.)
- [ ] Implement/align backend-neutral draw contracts in `IRenderBackend` for required scene features. (In progress: indexed world-mesh draw contract landed and backend model submission now uses indexed batches on supporting backends.)
- [ ] Validate OpenGL shared-contract textured model parity against OpenGL legacy and D3D12 (focus: textured submeshes, alpha-mask/alpha-blend materials, and no movement-time mesh dropout).
- [ ] Complete D3D12 material and alpha-mode parity. (In progress: wrap-aware texture sampling, glTF-like base+emissive color composition, higher model detail budget, GL-clip-depth-to-D3D conversion, OpenGL-like sRGB+ACES textured world shading, indexed textured-submesh alpha-mode+cutoff + UV wrap controls, explicit D3D12 blend-depth-write-off world pipeline ordering, depth-sorted blend batch submission, and removal of textured-path alpha double-attenuation that was culling BLEND submesh regions landed in backend mesh path.)
- [ ] Add explicit emissive-texture parity in the backend indexed world path (OpenGL currently samples `u_EmissiveTex`; backend indexed path still approximates emissive via cached shading data).
- [ ] Complete D3D12 animation/skinning parity (no fallback-only pose path). (In progress: backend clip evaluation now applies root-motion carrier X/Z suppression like OpenGL.)
- [ ] Align shared-path model motion with OpenGL legacy clip-driven presentation (avoid procedural bob/lunge/tilt layering when clip pose is active).
- [ ] Align shared combat VFX timing/presentation with OpenGL legacy move-impact flow (attack telegraph, projectile timing, and impact burst readability).
- [ ] Finish shared per-unit HUD parity with OpenGL legacy (level ring geometry, XP progress arc, HP/energy bar sizing/color, and text anchoring).
- [ ] Finalize shared per-unit HUD zoom behavior so HUD size remains stable while camera zoom changes (legacy readability parity).
- [ ] Split D3D12 renderer implementation into smaller modules. (In progress: texture upload/mipmap staging moved from `src/engine/render/D3D12RenderBackend.cpp` to `src/engine/render/d3d12/D3D12TextureUpload.cpp`.)
- [ ] Port/align board and bench rendering parity.
- [ ] Port/align health bars and combat overlays parity.
- [ ] Port/align shop/starter card style parity (image, frame, typography, spacing). (In progress: D3D12/backend cards now use OpenGL-like image+gold-frame composition with `Lv` overlay and below-card labels; starter row geometry now matches legacy OpenGL constants, with texture-quality tuning still ongoing.)
- [ ] Validate VFX parity for growl, tackle, grass impact, claw swipe, aqua, leech seed.
- [ ] Add backend-switch startup regression test (including starter selection path).
- [ ] Add visual parity test harness for key scenes.
- [ ] Add backend perf snapshot reporting and thresholds.
- [ ] Remove dead backend-specific fallback code once parity path is stable.

Iteration Log
- Iteration 0 (current): Established living roadmap and consolidated parity backlog with milestones and done criteria.
- Iteration 1: Restored policy-gated OpenGL text-menu rendering, switched backend text-menu/overlay text to line-stroke rendering (reducing rectangle artifacts), and added regression coverage for backend text line generation.
- Iteration 2: Added explicit backend text-menu policy helper and contract tests to prevent accidental OpenGL fallback-path regressions.
- Iteration 3: Added line-layer text support to backend card visuals/renderer and wired shop/starter backend UI paths to use text lines instead of filled text quads.
- Iteration 4: Removed direct `stb_easy_font` metric usage from `ScriptedState` and switched backend menu/card text sizing to shared backend text metric helpers.
- Iteration 5: Added backend render policy helpers + tests, aligned frame-flow menu behavior to avoid backend debug-world draws in menu-only states, disabled backend menu world-backdrop by default (env-overrideable), and removed mandatory text-quad sinks from backend card rendering when line text is used.
- Iteration 6: Unified scripted/menu/shop UI policy across OpenGL and D3D12 by removing backend-id gating in `shouldUseBackendUi`, added explicit legacy opt-out hook, and updated backend UI policy contract tests.
- Iteration 7: Added world portrait policy controls (model-first, portrait fallback/force toggles), removed tint-under-portrait artifacts in fallback world rendering, and expanded backend unit visual contract coverage.
- Iteration 8: Fixed projected-world model draw detection to track the correct 3D depth container (preventing false fallback to portrait/rectangle proxies), and added a regression helper/test for model-geometry accumulation logic.
- Iteration 9: Restored OpenGL to the legacy starter/shop UI policy after a visual regression, while keeping D3D12 on backend UI and re-opening the UI-policy unification item until style parity is complete.
- Iteration 10: Reworked backend card visuals to match OpenGL styling (portrait fill + gold frame sprite + legacy-style `Lv` and label overlays), wired shop/starter state inputs to legacy text semantics, and expanded card renderer/visual tests for two-sprite (art+frame) output.
- Iteration 11: Added explicit gold-stroke fallback borders to backend cards (ensuring visible framing across backends) and unified text-menu rendering policy so OpenGL and D3D12 share the same menu visual path.
- Iteration 12: Added outlined menu panel/button styling in shared backend text-menu rendering, locked backend starter card row to legacy OpenGL geometry (220x150, spacing 50, y=300), and upgraded D3D12 sprite textures to mipmapped uploads with anisotropic sampling for closer card image quality.
- Iteration 13: Added backend text-menu `bold` and `underline` style parity (matching legacy menu semantics) so selected/active entries read consistently across OpenGL and D3D12.
- Iteration 14: Fixed D3D12 dynamic-vertex buffer overwrite hazards by adding per-frame write offsets for debug quads/lines/triangles and sprites, preventing later draws from clobbering earlier menu/background geometry in the same frame.
- Iteration 15: Improved backend model parity by honoring texture wrap modes, applying glTF-like base+emissive color composition in model cache sampling, increasing per-unit triangle fidelity defaults, and matching OpenGL root-motion carrier translation handling during backend clip pose evaluation.
- Iteration 16: Reduced backend model-path CPU cost by caching per-node world transforms and per-vertex skinned world results during triangle sampling, replacing repeated mesh-index-to-node scans with a precomputed map, and tuning default backend triangle budgets for better D3D12 combat-frame stability.
- Iteration 17: Added backend model frame-budget control (`PAC_BACKEND_MODEL_TRI_FRAME_BUDGET`), removed D3D12 world-triangle depth sorting from the 3D depth-tested path, and moved mesh-index-to-node lookup precomputation into model-cache load so per-frame unit rendering does less CPU work.
- Iteration 18: Replaced hot-path hash maps in backend unit model rendering with vector-indexed caches (skin matrices, node transforms, and skinned vertices), and enabled non-OpenGL startup preloading of backend model caches to avoid first-spawn hitching when models appear in combat.
- Iteration 19: Added an indexed world-mesh backend contract (`supportsWorldIndexedMeshes`/`drawWorldIndexedMesh`), implemented D3D12 indexed world draws with dedicated upload index buffers, and switched backend model submission in `GameSession` to emit indexed per-unit batches instead of only triangle streams.
- Iteration 20: Improved backend model visual fidelity by raising default model triangle/scene budgets and minimum per-unit LOD floor, switching model lighting to per-vertex directional/hemi/rim shading (including two-sided backface handling), and adding contract tests for the new shading helper in `BackendMaterialShading`.
- Iteration 21: Implemented textured indexed world-model rendering for D3D12 by extending backend draw contracts with textured mesh submission, adding D3D12 world-shader UV texture sampling + descriptor-table binding/caching, and wiring `GameSession` model batches per submesh to submit cached base-color textures from `BackendModelCache` instead of color-only geometry.
- Iteration 22: Reorganized D3D12-specific code by extracting texture upload helpers (`engine/render/d3d12`) and moving runtime probe files under `game/runtime/d3d12`, then improved visual parity with OpenGL by converting GL clip-space depth for D3D12 world draws and applying OpenGL-like sRGB+ACES color mapping for textured world meshes.
- Iteration 23: Added per-submesh textured material metadata plumbing (wrapS/wrapT, alpha mode, alpha cutoff) from backend model cache through `GameSession` into D3D12 world draws, and updated D3D12 world pixel shader logic to honor glTF-like OPAQUE/MASK/BLEND alpha behavior and UV wrap controls for indexed textured meshes.
- Iteration 24: Added a dedicated D3D12 world blend pipeline (depth test on, depth write off) and render-pass ordering (OPAQUE/MASK first, BLEND second), while always routing indexed world batches through textured draw metadata so alpha-mode behavior applies consistently to textured and non-textured submeshes.
- Iteration 25: Identified triangle-budget decimation as a core parity blocker for indexed D3D12 model path, switched indexed model submission to full-mesh by default (`PAC_BACKEND_MODEL_FULL_MESH` opt-out), and added depth-sorted blend batch ordering to reduce transparent submesh popping/invisibility while moving.
- Iteration 26: Parsed shipped `.glb` materials to confirm BLEND usage, traced OpenGL vs backend alpha flow, fixed backend indexed textured alpha double-attenuation (preventing BLEND regions from dropping out), and added one-shot backend model-cache miss diagnostics plus optional verbose preload logging (`PAC_BACKEND_MODEL_VERBOSE`).
- Iteration 27: Fixed D3D12 world indexed/triangle upload-buffer overwrite hazards by introducing per-frame world vertex/index write offsets, preventing later model draws from clobbering earlier draws in the same command list (a key cause of disappearing model regions when more units spawned); also logged that non-OpenGL path uses backend cache loading instead of OpenGL `ModelStartupLog`.
- Iteration 28: Restored startup loading parity for non-OpenGL backends by driving boot-progress updates during backend model-cache preload and adding a renderer-driven fallback loading gauge in `GameRunner` when no OpenGL `BootLoadingView` is available (e.g., D3D12).
- Iteration 29: Restored backend unit animation drive and scale parity by hydrating non-OpenGL unit anim roles/durations directly from backend mesh + `.animset.json` metadata each fixed tick, removing model-pointer gating from `GameWorldAnimation` so anim clocks advance without OpenGL `Model*`, and applying native-mode scale correction parity when only backend mesh scale is available.
- Iteration 30: Reduced D3D12 backend model CPU/render cost by skipping unnecessary base-color shading work on textured indexed batches and reusing per-submesh indexed vertices in full-mesh mode (instead of emitting duplicate vertices per triangle), targeting the combat-frame render bottleneck seen after animation parity fixes.
- Iteration 31: Added a faster textured-indexed backend path (`PAC_BACKEND_MODEL_FAST_TEXTURED`, default on) that bypasses per-triangle CPU lighting/culling for textured submeshes and emits flat-tinted indexed textured vertices directly, targeting the persistent D3D12 `render`-time bottleneck in combat.
- Iteration 32: Optimized fast textured backend submission by adding position-only vertex transform/skinning cache for the fast path (skipping normal work when not needed) and constrained that fast path to full-mesh indexed mode so aggressive triangle-budget decimation remains an explicit quality/perf tradeoff instead of being mistaken for texture corruption.
- Iteration 33: Removed duplicate CPU skinning work from the fast textured full-mesh path (avoid doing full normal+position resolve before fast-path early-out) and switched D3D12 dynamic upload buffers to persistent mapping to eliminate per-draw `Map/Unmap` churn in world/debug/sprite submission.
- Iteration 34: Reduced full-mesh fast-textured model CPU cost by resolving skinned world positions only on first-use vertex remap (instead of every triangle hit), and precomputed per-submesh node fallback mapping once per mesh draw to remove repeated triangle-loop lookup work.
- Iteration 35: Added a debug-performance mode (`PAC_OPTIMIZE_RENDER_HOTPATHS_IN_DEBUG`, default on) that compiles D3D12/GameSession render hotpaths with optimization in Debug builds, and removed per-call backend text vertex-buffer allocations via reusable scratch storage in `BackendDebugText` to cut UI/text CPU churn.
- Iteration 36: Reused backend debug-view frame buffers across frames (eliminating large per-frame vector allocations), cached per-node skin matrix prerequisites (including shared node-global inverses), and added an all-textured full-mesh position-only fast path to skip unnecessary normal-matrix work on D3D12 backend model rendering.
- Iteration 37: Fixed board-grid depth parity by rendering projected-world grid lines as depth-tested 3D world quads (instead of 2D overlay lines that could appear through models), and added rigid single-joint skinning fast paths plus reusable depth/blend temporary buffers to reduce backend combat-frame CPU cost.
- Iteration 38: Hardened backend-switch UX by adding an explicit runtime warning when `PAC_RENDER_BACKEND` env override is active (saved Display/API prefs ignored until unset) and a Display-menu mismatch hint when active API differs from preferred API (override/fallback visibility).
- Iteration 39: Improved D3D12 visual parity by clamping backend model anchor height against board floor to prevent floor penetration/bounce artifacts, disabling proxy shadow floor quads when a real model mesh is rendered, restoring battle/economy feed side placement parity with OpenGL in backend HUD text layout, and removing D3D12 textured world-path ACES remap so textured model colors track OpenGL appearance more closely.
- Iteration 40: Matched backend HUD anchoring closer to OpenGL by moving status block text (mode/backend/round/units/gold/selected item) to top-right and returning `Type Lines` to the left panel, while de-blueing backend board/cell/grid colors in both projected-world and 2D fallback paths to reduce D3D12 board tint drift.
- Iteration 41: Removed the projected-world axis-aligned board backdrop quad so D3D12 no longer draws a darker rectangular panel behind the perspective grid, aligning board/background blending with OpenGL and eliminating the visible board-area color block.
- Iteration 42: Moved render/UI route ownership away from backend string checks by adding backend-owned route hints on `IRenderBackend` (`prefersLegacyGameRenderPath`, `prefersLegacyGameUiPath`), wiring OpenGL/D3D12 implementations, switching `GameSession` legacy-path selection to backend hints, and updating backend-UI policy + tests to use route booleans instead of renderer id strings.
- Iteration 43: Split renderer-availability from legacy-path routing in `GameSession`/`GameServices` (set `renderEnabled` from renderer presence, carry legacy render/UI routes explicitly), updated `PlacementState` and `CombatState` legacy text fallback checks to use `usesLegacyGameUiPath()`, and added a `GameServices` route-helper contract test.
- Iteration 44: Removed remaining state-level renderer route probing in `ScriptedState` and `CombatState` (`prefersLegacyGameUiPath`) so only `GameSession` selects routes and gameplay UI logic now consumes `GameServices` route helpers, then revalidated with full 91-test pass.
- Iteration 45: Refactored `GameSession` hot-path branching (input inventory UX path, fixed-update backend animation hydration, world-backdrop policy, frame-flow selection, and shutdown UI teardown) to consume `GameServices` route helpers instead of direct `legacyRenderPath` checks, preserving behavior while tightening renderer-agnostic boundaries.
- Iteration 46: Introduced shared runtime `RenderRoutes` contract, migrated backend render policy + frame-flow decisions to route objects, and updated policy contract tests to consume route-based APIs.
- Iteration 47: Migrated backend UI policy to route objects (`RenderRoutes`) and updated `ScriptedState`/`CombatState` call sites to use route-based UI policy decisions.
- Iteration 48: Added `render_route_ownership_contract` to enforce that backend preference probes (`prefersLegacyGame*Path`) stay centralized in `GameSession`.
- Iteration 49: Extracted legacy/backend inventory input handling into dedicated `GameSession` helpers to remove mixed-branch event logic and keep route behavior explicit.
- Iteration 50: Extracted frame-flow selection/execution helpers (`currentFrameFlow`, `renderFrameFromFlow`) in `GameSession` for cleaner route-driven render orchestration.
- Iteration 51: Split `GameWorld` render contracts into backend-neutral `renderEnabled` and legacy-only model attachment flag (`legacyModelRenderPathEnabled`), and switched `GameSession` world setup to set both explicitly.
- Iteration 52: Added route contract regression test (`render_routes_contract`) plus backend-world model-attachment regression (`gameworld_backend_render_mode_skips_legacy_model_load`) and revalidated route/boundary test coverage.
- Iteration 53: Removed temporary bool-overload shims from backend render policy, frame-flow, and backend UI policy so all route decisions now require `RenderRoutes`.
- Iteration 54: Added `GameServiceRenderRoutes.h` (`routesFromServices`) and switched `CombatState`/`ScriptedState` route construction to this shared helper.
- Iteration 55: Simplified `GameSession` startup routing state by replacing separate local route booleans with one `RenderRoutes` snapshot and resolving active routes via `routesFromServices` when services are available.
- Iteration 56: Added `render_policy_api_contract` to prevent reintroduction of legacy bool-based route-policy signatures in policy headers.
- Iteration 57: Added `game_service_render_routes_contract` to verify `routesFromServices` mapping stays consistent with `GameServices` render/legacy route flags.
- Iteration 58: Added an explicit remaining parity estimate and parity-first sequencing to this living roadmap (9-12 iterations), so progress and expectations are tracked against a concrete plan.
- Iteration 59: Unified frame-flow decisions to backend-neutral world/HUD layers, routed `GameSession` world rendering through one route-dispatched `renderWorldLayer`, and added policy/flow contract coverage for backend menu-backdrop routing.
- Iteration 60: Removed remaining state-level direct UI route reads in `CombatState`/`PlacementState`, routed those decisions through `routesFromServices` + backend UI policy helpers, and added a `state_ui_route_policy_contract` guardrail test to prevent regressions.
- Iteration 61: Added shared `BackendTopBanner` layout/render helpers and switched `PlacementState` plus both `CombatState` banner paths (shop + non-shop, backend + legacy centering/Y policy) to one top-banner contract, with dedicated `backend_top_banner_contract` test coverage.
- Iteration 62: Documented the contract-first migration path and added temporary in-game OpenGL shared-contract mode (`opengl_shared`) with runtime route override in `GameSession`, updated Display menu labels/options for tri-mode parity checks, and expanded video-preference token tests.
- Iteration 63: Implemented OpenGL shared-contract backend draw support for 3D world triangles, indexed world meshes (including textured + alpha-mode/cutoff + wrap handling), and debug sprites with texture caching/fallbacks so `opengl_shared` now exercises the same world/sprite draw contracts used by D3D12; revalidated with full build + 98/98 tests.
- Iteration 64: Reduced shared-path model bounce drift by making backend world rendering use clip-driven unit transform defaults (legacy-like position/rotation/scale) whenever a valid animation clip pose is active, keeping procedural bob/lunge/tilt only for non-clip fallback cases.
- Iteration 65: Ported legacy per-unit HUD bar/ring math into shared world HUD rendering so OpenGL shared and D3D12 now use legacy-aligned HP/energy bar geometry/colors plus level text + player XP ring progress arc around level.
- Iteration 66: Integrated Pokemon name text into the same shared per-unit HUD builder and switched shared HUD sizing to a stable screen-space reference (with tight projected-size clamping) so level/ring/bar text no longer shrinks unexpectedly while zooming.
- Iteration 67: Tuned shared per-unit HUD presentation to match readability targets by scaling HUD geometry up (~20%), raising HUD vertical offset above units, recentering level text inside the XP ring, and removing floating Pokemon-name text above the HUD block.
- Iteration 68: Raised shared per-unit HUD anchor further above unit models, switched level-ring number centering to rendered glyph bounds (instead of coarse text metrics), and removed always-on white unit heading guide lines from shared world rendering.
- Iteration 69: Matched shared combat presentation closer to legacy by fixing backend faint progression when no OpenGL `Model*` is attached (dead units now finish fade and disappear), allowing shared model/proxy scale to reach zero during faint fade-out, driving shared attack FX from gameplay attack/pending-hit timing (not only procedural windows), adding projectile/impact burst overlays keyed to pending move events, and improving card-shop texture quality in OpenGL shared mode with mipmapped + anisotropic sprite sampling.
- Iteration 70: Expanded shared combat VFX variety to track legacy move routing more closely by classifying pending move names via `MoveImpactRouting` (growl/tackle/claw/aqua/grass routes), adding route-specific shared overlays (sound-wave rings, tackle bursts, claw slashes, aqua rings/beam/bubbles, grass/leech bursts), adding shared leech-drain traces and per-projectile burst accents, and restoring Charmander tail-fire presentation in shared world rendering for both OpenGL-shared and D3D12 paths.

How This File Is Used
- Before each parity implementation iteration:
  - pick items from the prioritized backlog,
  - mark them `in progress` in this file.
- After each iteration:
  - mark completed items,
  - append one-line summary in Iteration Log,
  - include one-line commit message in the status update.
