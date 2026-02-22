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
