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

## Coverage Gaps (High Risk)
- Render pipeline setup (GL context, shader compile, draw).
- Animation setup correctness (animset ↔ glTF name mismatches).
- End-to-end phase flow (placement to combat).
- Script API contract drift.

## Next Tests to Add (Priority)
1. Script API contract test that verifies stable inputs and outputs.
2. End-to-end headless scenario that runs a short round and asserts final state.
3. Animation name validation against animset roles (CPU-only).

## Manual Smoke Checklist (Release Builds)
1. Launch the game and reach the board.
2. Place a few units and start combat.
3. Verify movement looks correct and units do not overlap.
4. Verify animations play and models appear.
