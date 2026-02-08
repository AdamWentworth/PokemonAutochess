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
- Animset role resolution test (CPU-only, JSON-driven).

## Coverage Gaps (High Risk)
- Render pipeline setup (GL context, shader compile, draw).
- Animation setup correctness (animset ↔ glTF name mismatches).
- End-to-end phase flow (placement to combat).
- Script API contract drift.

## Next Tests to Add (Priority)
1. End-to-end headless scenario that runs a short combat slice with units spawned.
2. Render pipeline smoke test (minimal GL setup + shader compile).

## Manual Smoke Checklist (Release Builds)
1. Launch the game and reach the board.
2. Place a few units and start combat.
3. Verify movement looks correct and units do not overlap.
4. Verify animations play and models appear.
