# Renderer Parity Contract

Status: Active
Type: Contract
Last updated: 2026-09-12

## Goal
Define the minimum non-negotiable rendering contract that OpenGL, Vulkan, and D3D12 must satisfy so gameplay visuals stay consistent without backend-specific game logic.

All three APIs are equal supported targets. This applies to every visual feature,
including environments, unit models, animation, visibility, lighting, effects,
transitions and UI, in both the game and editor preview. A feature is incomplete
if it is missing or visibly different on any one API. Backend optimizations may
use different implementations while preserving the same content and behavior.

## Contract Values
- World front-face convention: clockwise (`CW`).
- World depth function: `LESS_EQUAL`.
- World culling: disabled.
- World opaque pipeline blending: disabled.
- World blend pipeline blending: enabled.
- Authored dual-source alpha/additive policy: enabled, with an explicit standard
  blend fallback when the active device lacks native support.
- Debug/UI pipeline blending: enabled.
- Framebuffer sRGB conversion: disabled (shader path handles tone-map + encode).
- Target anisotropy policy: `16`.
- Neutral PMREM encoding: linear HDR.
- Neutral PMREM GPU format: RGBA16F.
- Neutral PMREM atlas key: `__neutral_room_pmrem_rgba16f_v2__`.
- Shared PBR tunables source: `WorldPbrShaderShared::getTunables()`.

## Runtime Validation Log
At backend startup, each backend emits a parity contract line:
- Prefix: `[ParityContract][<Backend>]`
- Status: `PASS` or `FAIL`
- Signature: stable FNV-1a hash of the active contract payload
- Key fields: PBR tunables, front-face/depth/cull/blend policy, framebuffer
  sRGB, anisotropy target, PMREM encoding, PMREM GPU format, and PMREM key

This gives an immediate, greppable signal when a backend drifts from policy.

Current expected baseline signature:
- `2d637fef00f62903`

If contract values change intentionally, update:
1. `kExpectedBaselineSignature` in `RendererParityContract.h`
2. This doc
3. CI/local parity checks

## Enforcement Scope
- Gameplay-visible content and behavior must match exactly. Small numerical
  rasterization differences are measured using the versioned image thresholds;
  they do not excuse missing models, effects, altered timing, or material errors.
- Render-state signatures are necessary but do not prove visual correctness.
- Every visual change requires affected scenes/phases in the three-API GPU
  matrix, meaningful expected-content guards, relevant CPU tests, and editor
  preview validation when that is the affected surface. A partial backend run
  is diagnostic evidence only. A fallback to another API does not count.
- Do not widen image thresholds or remove content guards to obtain a pass.
- State unverified coverage explicitly; never describe one/two-API evidence as
  renderer parity. Hosted CI cannot replace local GPU validation.
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
3. Verify startup logs for OpenGL, Vulkan, and D3D12 report `PASS`.
4. Re-run parity checks from `TEST_PLAN.md`.

## Automated Checks
- Unit/CI drift checks:
  - `renderer_parity_contract_baseline`
  - `renderer_parity_contract_detects_drift`
- Runtime contract check (optional smoke suite):
  - `tools/check_renderer_parity_contract.ps1`
  - CTest name: `PAC_RuntimeSmoke.parity_contract` (when `PAC_ENABLE_RUNTIME_SMOKE_TESTS=ON`)
- Visual feature checks: `tools/render_parity_matrix.ps1`, with all three APIs
  by default. Travel covers recall, airborne throws, and opening/send-out.
  Captures use a gameplay frame origin after loading, so backend startup frame
  counts cannot shift the animation being compared. The runner rejects an
  unexpected native backend or an executable without the timeline handshake.
- Expected-content guard tests: `PAC_Tools.render_parity_content_guard`, including
  missing red/white ball shells and completely absent balls.
