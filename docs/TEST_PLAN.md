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
- Script API contract test (bindings + basic behavior).
- Headless round flow test (Lua round system + update graph).
- Headless placement -> combat state flow.
- Animset role resolution test (CPU-only, JSON-driven).
- Animset role names validated against GLB animation names (CPU-only).
- Animset clip names validated against GLB animation names (CPU-only).
- Headless combat slice test (Lua combat system + damage applied).
- Render pipeline smoke test (shader include checks; optional GL compile + board + real model draw via `PAC_TEST_GL=1`).
- Layering enforcement check (engine cannot include game headers).

## Coverage Gaps (High Risk)
None listed.

## Next Tests to Add (Priority)
None listed.

## Manual Smoke Checklist (Release Builds)
1. Launch the game and reach the board.
2. Place a few units and start combat.
3. Verify movement looks correct and units do not overlap.
4. Verify animations play and models appear.
