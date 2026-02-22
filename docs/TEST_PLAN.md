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
