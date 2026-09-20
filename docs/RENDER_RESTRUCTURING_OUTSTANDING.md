# Renderer restructuring candidates

Status: Active
Type: Roadmap
Last updated: 2026-09-19

Renderer restructuring is deferred while repository presentation and ownership
cleanup are the focus. The [March planning record](archive/2026-03-31-render-restructuring.md)
is historical. Its D3D12-first sequencing does not override the current
[three-renderer parity contract](RENDERER_PARITY_CONTRACT.md).

## Existing foundation

The projected path already has GPU clip skinning, reusable transforms and retained
render items. Field and character shader policy now belongs to the game; generic
rendering and standard PBR belong to Phlosion Engine. Material ownership extraction
is complete, as recorded in [character materials](CHARACTER_MATERIALS.md#boundary-verification-2026-09-19).

## Conditions for future work

- Capture a fresh Release profile at fixed content, resolution, timing, inking
  and backend settings before choosing a bottleneck.
- Consider repeated projected-model preparation, compatible animated submission,
  draw/descriptor churn and static/dynamic payload residency only where the
  measured workload justifies a change.
- Keep generic backend work in Phlosion Engine and game presentation decisions
  in Pokemon Autochess. Do not reintroduce material-specific engine policy.
- An implementation may differ between APIs, but accepted behavior must be
  verified on OpenGL, Vulkan and Direct3D 12, including the editor when affected.
  A faster result on one API does not excuse missing content on another.
- GPU-side animation sampling is a possible later investigation, not an active
  implementation milestone.

## Acceptance

Use [the test plan](TEST_PLAN.md) and [renderer qualification](RENDERER_PARITY_CONTRACT.md).
Require comparable before/after measurements, native expected-content checks and
cross-backend visual results. Preserve runtime smoothness and report the limits
of local GPU coverage. No new restructuring is required for the present docs/media pass.
