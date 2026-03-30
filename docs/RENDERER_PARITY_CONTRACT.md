# Renderer Parity Contract

Status: Active
Type: Contract
Last updated: 2026-03-30

## Goal
Define the minimum non-negotiable rendering contract that both OpenGL and D3D12 must satisfy so gameplay visuals stay consistent without backend-specific game logic.

## Contract Values
- World front-face convention: clockwise (`CW`).
- World depth function: `LESS_EQUAL`.
- World culling: disabled.
- World opaque pipeline blending: disabled.
- World blend pipeline blending: enabled.
- Debug/UI pipeline blending: enabled.
- Framebuffer sRGB conversion: disabled (shader path handles tone-map + encode).
- Target anisotropy policy: `16`.
- Neutral PMREM atlas key: `__neutral_room_pmrem_rgba16f_v1__`.
- Shared PBR tunables source: `WorldPbrShaderShared::getTunables()`.

## Runtime Validation Log
At backend startup, each backend emits a parity contract line:
- Prefix: `[ParityContract][<Backend>]`
- Status: `PASS` or `FAIL`
- Signature: stable FNV-1a hash of the active contract payload
- Key fields: PBR tunables, front-face/depth/cull/blend policy, framebuffer sRGB, anisotropy target, PMREM key

This gives an immediate, greppable signal when a backend drifts from policy.

Current expected baseline signature:
- `a30e1ca79f60f2d3`

If contract values change intentionally, update:
1. `kExpectedBaselineSignature` in `RendererParityContract.h`
2. This doc
3. CI/local parity checks

## Enforcement Scope
- This contract enforces render-state and color pipeline policy, not pixel-perfect identity.
- Remaining visual differences should be treated as shader/material parity issues, not scene/gameplay logic issues.
- This document is the non-negotiable baseline, not the full renderer strategy.
- Broader parity, performance priorities, and current cleanup work live in
  `docs/RENDERER_PARITY_ROADMAP.md`.

## Relationship To The Roadmap
- Keep this doc small and stable.
- Use it for rules that should remain true unless there is an intentional
  contract change.
- Do not put active optimization priorities, perf experiments, or temporary
  cleanup items here; those belong in the roadmap.

## Required Follow-up When Contract Changes
1. Update `RendererParityContract.h` constants.
2. Update this document.
3. Verify startup logs for both OpenGL and D3D12 report `PASS`.
4. Re-run parity checks from `TEST_PLAN.md`.

## Automated Checks
- Unit/CI drift checks:
  - `renderer_parity_contract_baseline`
  - `renderer_parity_contract_detects_drift`
- Runtime contract check (optional smoke suite):
  - `tools/check_renderer_parity_contract.ps1`
  - CTest name: `PAC_RuntimeSmoke.parity_contract` (when `PAC_ENABLE_RUNTIME_SMOKE_TESTS=ON`)
