# Renderer Parity Outstanding (Living)

Purpose
- Track the remaining parity gaps that still matter before shared contracts can be treated as the default gameplay path.
- Keep this list focused on user-visible parity first, then architecture/maintenance cleanup.
- See `docs/HOUSEWORK_ROADMAP.md` for the post-parity cleanup and legacy-retirement prep plan.

Status Summary (2026-02-25)
- Shared routes (`opengl_shared`, `d3d12`) are now close enough for real parity comparison against `opengl` legacy.
- Major progress landed for model rendering/animation, per-unit HUD, growl VFX, and tail-fire (backend GPU exact material path).
- Adventure shared route usability improved: backend/shared Adventure inventory now renders atlas-based item icon cards (not text-only rows), and shared routes now show a visible capture-attempt pokeball throw/shake/resolve overlay.
- User manual signoff pass (2026-02-25) reports core parity is now good across all three routes for the features that matter most: renderer switching/menu correctness, board+bench, models, animations, capture sequence, tail-fire, growl, leech-seed, and D3D12 Pidgey seam.
- Remaining work is now primarily housework/architecture cleanup plus optional perf optimization and non-parity UI polish.

## Manual Parity Signoff Snapshot (User-Verified 2026-02-25)

Confirmed good enough for parity across `opengl`, `opengl_shared`, and `d3d12`:
- renderer switching + menu correctness
- world board + bench visuals
- model rendering/materials (no major parity issues)
- animation behavior
- per-unit HUD parity (not exact, but acceptable; further changes are desired independently of legacy parity)
- Adventure inventory UI parity (acceptable; future changes are feature/polish work, not parity fixes)
- pokeball capture sequence
- Charmander tail-fire VFX
- growl VFX
- leech-seed VFX
- D3D12 Pidgey seam (no longer reproducing in user verification)
- overall stability (good enough to proceed)

## User-Visible Parity Gaps (Priority Order)

1. Performance optimization / backend suitability (non-blocking parity follow-up)
- User reports performance still leaves headroom, but the parity goal is sufficiently met for current needs.
- This should be treated as a separate optimization track, not a parity blocker, unless new perf regressions appear.

2. D3D12 pokeball animation cache freshness (operational check, not renderer parity logic)
- `d3d12` shared capture lid animation depends on the backend cache for `assets/models/pokeball.glb` (`cache/models/f664dfc73e402009.pacmdl`) containing the updated animation clip.
- If D3D12 shows no lid animation while `opengl_shared` does, rebuild the backend model cache entry from the updated GLB.

3. Optional visual polish items (out of parity scope)
- HUD and Adventure inventory UI are acceptable for parity but may still be redesigned/polished independently of legacy behavior.

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

1. Begin housework phase (module boundaries and render ownership cleanup) while keeping legacy OpenGL as fallback.
2. Split large files (`GameSession.cpp`, `D3D12RenderBackend.cpp`) into coherent subsystems.
3. Move more renderer-agnostic behavior into shared contracts so `GameSession` orchestrates less.
4. Add guardrails for parity stability while housework proceeds (smoke runs, route-specific checks).
5. Retire legacy only after shared path is the default and fallback is no longer needed in practice.

## Manual Signoff Notes (How To Use This File)

- When a gap is fixed and visually confirmed in both `opengl_shared` and `d3d12`, move it down or mark it resolved in a future edit.
- Keep this list short and user-visible; implementation details belong in `docs/RENDERER_PARITY_ROADMAP.md`.
