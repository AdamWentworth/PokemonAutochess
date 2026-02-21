# Renderer Parity Roadmap (Living)

Goal
- Deliver the same game experience in OpenGL and D3D12: same menus, layout, cards, board, models, materials, animation, VFX, and gameplay readability.

Scope
- In scope: render/UI parity, backend switch UX, parity tests, perf tracking.
- Out of scope for this roadmap: new gameplay features unrelated to parity.

Definition Of Done
- OpenGL and D3D12 produce matching menu and in-game presentation for the same seed/state.
- No backend-specific gameplay UI behavior differences.
- Same board/unit/material/animation/VFX behavior across both backends.
- Settings allow backend choice with restart flow and return to video menu after restart.
- Parity regression tests and runtime smoke tests pass for both backends.

Current Reality (Baseline)
- Non-OpenGL path uses backend debug view and backend-specific UI path.
- OpenGL path uses legacy world/UI path.
- Text in backend UI is quad-based debug text, causing rectangle-like glyph artifacts.
- World model/material/animation rendering is still split and not fully equivalent.

Evidence Anchors (Key Files)
- Render-flow split: `src/game/runtime/RenderFlowDecisions.h`, `src/game/runtime/GameSession.cpp`
- Backend UI policy split: `src/game/state/BackendUiPolicy.h`
- Backend quad-text path: `src/game/runtime/BackendDebugText.h`
- Legacy world renderer: `src/game/world/GameWorldRender.cpp`
- D3D12 backend implementation: `src/engine/render/D3D12RenderBackend.cpp`
- Backend selection/restart prefs: `src/game/runtime/GameRunner.cpp`, `src/game/runtime/VideoPreferences.cpp`, `scripts/states/main_menu.lua`

Workstreams
1. Unified Frame Flow
- Remove legacy-vs-backend gameplay render branching.
- Keep debug visualization as optional tooling only.

2. Unified UI Stack
- Use one menu/shop/HUD rendering path for both APIs.
- Replace debug quad text with proper glyph text rendering.

3. Unified World Stack
- Use one backend-neutral world command path (board, units, bench, health bars).
- Remove backend-specific model proxy fallback as primary rendering path.

4. Model/Material/Animation Parity
- Ensure skinned mesh draw, material sampling, alpha behavior, and clip playback parity.
- Remove visible quality downgrades from D3D12 path.

5. VFX Parity
- Ensure move VFX and combat readability match across backends.

6. Backend UX + Safety
- Stable apply+restart flow.
- Return users to video settings after restart.
- Warn about unsaved run progress when relevant.

7. Test + Perf Guardrails
- Add visual parity checks for key scenes.
- Add crash regression tests for backend switching and starter selection.
- Add perf snapshots for OpenGL vs D3D12 scenes.

Milestones
- M1: UI/Menu parity (no backend-only menu visuals)
- M2: World baseline parity (board + units + bench + HUD)
- M3: Model/material/animation parity
- M4: VFX parity
- M5: Test/perf hardening and cleanup

Prioritized Backlog
- [x] Guard backend text-menu fallback behind explicit backend policy (regression safety for OpenGL menu path).
- [ ] Remove backend-specific gameplay render flow and unify frame graph.
- [ ] Remove `shouldUseBackendUi` backend split and unify UI policy.
- [ ] Replace backend quad text with glyph text rendering. (In progress: backend text menu now uses line-stroke text, full glyph path still pending.)
- [ ] Route both backends through the same menu/shop/HUD layout/render code.
- [ ] Route both backends through the same world command generation code.
- [ ] Remove `setRenderEnabled(legacyRenderPath)` behavior that disables non-OpenGL world resources.
- [ ] Move backend debug world rendering behind an explicit dev-only flag.
- [ ] Implement/align backend-neutral draw contracts in `IRenderBackend` for required scene features.
- [ ] Complete D3D12 material and alpha-mode parity.
- [ ] Complete D3D12 animation/skinning parity (no fallback-only pose path).
- [ ] Port/align board and bench rendering parity.
- [ ] Port/align health bars and combat overlays parity.
- [ ] Port/align shop/starter card style parity (image, frame, typography, spacing).
- [ ] Validate VFX parity for growl, tackle, grass impact, claw swipe, aqua, leech seed.
- [ ] Add backend-switch startup regression test (including starter selection path).
- [ ] Add visual parity test harness for key scenes.
- [ ] Add backend perf snapshot reporting and thresholds.
- [ ] Remove dead backend-specific fallback code once parity path is stable.

Iteration Log
- Iteration 0 (current): Established living roadmap and consolidated parity backlog with milestones and done criteria.
- Iteration 1: Restored policy-gated OpenGL text-menu rendering, switched backend text-menu/overlay text to line-stroke rendering (reducing rectangle artifacts), and added regression coverage for backend text line generation.
- Iteration 2: Added explicit backend text-menu policy helper and contract tests to prevent accidental OpenGL fallback-path regressions.

How This File Is Used
- Before each parity implementation iteration:
  - pick items from the prioritized backlog,
  - mark them `in progress` in this file.
- After each iteration:
  - mark completed items,
  - append one-line summary in Iteration Log,
  - include one-line commit message in the status update.
