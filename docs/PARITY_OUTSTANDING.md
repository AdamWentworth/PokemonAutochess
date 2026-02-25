# Renderer Parity Outstanding (Living)

Purpose
- Track the remaining parity gaps that still matter before shared contracts can be treated as the default gameplay path.
- Keep this list focused on user-visible parity first, then architecture/maintenance cleanup.

Status Summary (2026-02-25)
- Shared routes (`opengl_shared`, `d3d12`) are now close enough for real parity comparison against `opengl` legacy.
- Major progress landed for model rendering/animation, per-unit HUD, growl VFX, and tail-fire (backend GPU exact material path).
- Adventure shared route usability improved: backend/shared Adventure inventory now renders atlas-based item icon cards (not text-only rows), and shared routes now show a visible capture-attempt pokeball throw/shake/resolve overlay.
- Remaining issues are now mostly specific visual mismatches and a smaller number of route-ownership cleanup tasks.

## User-Visible Parity Gaps (Priority Order)

1. D3D12-only Pidgey texture seam (high priority)
- Symptom: visible line down Pidgey's face/torso in `d3d12`, more visible at distance and mostly disappears up close.
- Why this matters: obvious model rendering mismatch and a confidence-breaker for D3D12 parity.
- Current state: several D3D12 sampler/wrap/mip parity fixes are in, but the seam is still unresolved.
- Next likely debug step:
  - force mip0 in D3D12 shared world textured path for targeted validation
  - log Pidgey submesh texture wrap/material path at runtime
  - inspect whether the remaining artifact is mip-level or shading-path specific

2. Leech Seed VFX exact parity (high priority)
- Shared routes improved (arc path restored, wrong dotted green connector fallback removed when legacy particle bridge is active).
- Still not confirmed 1:1 with legacy OpenGL for:
  - projectile appearance/material
  - drain visual look and timing read
  - overall move readability compared to legacy pass/shader behavior

3. Remaining move VFX exactness (high priority)
- Tail-fire and growl are much closer now.
- Other shared move VFX still rely on the shared particle bridge + generic billboard/material rendering in many cases, so they can match timing/positions but still differ visually from legacy.
- Effects needing final signoff / likely follow-up:
  - `tackle`
  - `grass impact`
  - `claw swipe`
  - `aqua` variants
  - `heal plus`
  - other shared particle-driven move effects

4. Board + bench final parity signoff (medium priority)
- Improvements landed:
  - board grid readability
  - bench overlay visibility under tight framing
  - shared world-space bench grid row adjacent to board
- Still needs final visual signoff against legacy:
  - exact layout/spacing
  - colors/line weight
  - readability at different camera angles

5. Fainting material/fade visual signoff (medium priority)
- Shared fade-out behavior is improved and textured submeshes now blend during faint fade.
- Still needs parity signoff for edge cases (material behavior / appearance during fade progression).

6. Shop / sell-overlay interaction parity signoff (medium priority)
- Recent fix landed in `ScriptedState` to hide main shop cards while sell/release drop overlay is active and remove hidden cards from snapshot hit-testing.
- Needs manual confirmation in both `opengl_shared` and `d3d12` across planning/shop flows.

7. Shop/starter card visual polish parity (medium priority)
- Shared/backend cards are much improved, but final signoff is still open for:
  - texture quality consistency
  - typography/spacing/frame polish
  - exact legacy presentation feel

8. Health bars / combat overlay parity (medium priority)
- Shared per-unit HUD is much closer, but final parity signoff remains for full overlay set and edge cases.

9. Adventure capture presentation exact parity (medium priority)
- Shared routes now render `pokeball.glb` through the backend/shared world mesh path (not just the earlier icon overlay fallback).
- Shared routes now support clip-driven pokeball absorb playback (updated `Hinge_TopAction` clip) and late absorb target red/fade/shrink timing, but D3D12 clip playback still depends on the backend `.pacmdl` cache containing the updated animation data.
- Follow-up needed for exact parity:
  - confirm the intended animated `pokeball.glb` asset version is present in the working tree and backend cache (`cache/models/f664dfc73e402009.pacmdl`)
  - tune/confirm exact open-hit-suck-close timing and target red/fade timing against the new desired behavior (not legacy)
  - confirm throw/shake/resolve timing and pokeball read feel correct in side-by-side shared route testing (`opengl_shared` + `d3d12`)

## Renderer-Agnostic Architecture Gaps (Still Important)

1. Backend debug-world is still a primary shared gameplay render path
- Goal remains: move this behind a dev-only flag and retire it as the main gameplay path.

2. UI route unification is incomplete
- `shouldUseBackendUi` split is still re-opened because some OpenGL legacy UI paths were restored to avoid regressions.

3. Shared world command generation and visual stack ownership are not fully unified
- Contracts are much better, but `GameSession` still owns too much orchestration/render detail.

4. Backend text path still uses line-stroke text, not final glyph text rendering
- Adequate for progress, but not final quality parity.

5. No automated visual parity harness
- Parity is still manually verified by eye, which slows signoff and makes regressions easier to miss.

6. No backend perf threshold enforcement
- Performance improvements are measured ad hoc; no automated guardrails yet.

## What Is Functionally Close (Likely Near Signoff)

- Shared OpenGL vs D3D12 overall gameplay presentation is much closer than before.
- Tail-fire parity architecture is in the right place (backend GPU exact material path), even if small polish issues remain.
- Growl VFX uses legacy-driven simulation/pass data rather than the old approximation.
- Shared per-unit HUD behavior/readability is substantially improved.

## Suggested Next Sequence (Pragmatic)

1. Finish D3D12 Pidgey seam root-cause debug (mip0/material path instrumentation).
2. Finish leech-seed exact parity (visual/material behavior against legacy).
3. Run a focused manual parity pass on:
   - sell-overlay drag behavior
   - board/bench visuals
   - fainting fade
   - remaining move VFX
4. Then resume architecture cleanup (retire backend debug-world as primary path / unify UI path).

## Manual Signoff Notes (How To Use This File)

- When a gap is fixed and visually confirmed in both `opengl_shared` and `d3d12`, move it down or mark it resolved in a future edit.
- Keep this list short and user-visible; implementation details belong in `docs/RENDERER_PARITY_ROADMAP.md`.
