# Outstanding Issues

Status: Active
Type: Tracker
Last updated: 2026-03-31

This file tracks concrete maintainability and organization issues that still
need follow-through. Strategic priority lives in `TECH_DEBT.md`.

## Active Issues
Keep this table ordered roughly by current impact so the top rows reflect the
most urgent repo-level issues first.

| Issue | Category | Impact | Current behavior | Recommended owner |
| --- | --- | --- | --- | --- |
| `render_model_cache_contract` is currently failing on Growl sparkle UV preservation | Asset/cache validation | High | `tools/full_check.ps1` is red because `PAC_Tests.render_model_cache_contract` reports `assets/meshes/growl_1275_mesh.glb` falling back to generated planar UVs instead of preserving the expected raw texture coordinates. | Render-model-cache / content pipeline owner |
| `src/game/runtime/session/GameSession.cpp` is still oversized | Runtime architecture | High | Session bootstrap, routing, debug snapshot, render wiring, and lifecycle concerns still meet in one file. | Runtime/session owner |
| `src/game/runtime/GameRunner.cpp` is still oversized | Runtime architecture | High | Window policy, backend selection, loop orchestration, perf logging, and restart flow remain centralized. | Runtime/platform owner |
| Backend mega-files remain high-churn risk | Renderer architecture | High | D3D12/OpenGL renderer families and shared projected runtime modules still absorb many unrelated edits. | Renderer owner |
| No protected perf baseline in CI | Process | High | Benchmarks exist locally, but merge-time perf regressions can still slip through without an automated gate. | Perf/CI owner |
| `src/engine/render/IRenderBackend.h` is too broad | Renderer architecture | Medium | World, debug, sprite, timing, and capability responsibilities still share one large interface surface. | Renderer owner |
| Preview visual smoke is still mostly manual | Tooling | Medium | `PAC_VfxPreviewer` and `VfxLab` now build cleanly, but visual correctness still depends on manual launches and spot checks. | Preview tooling / VFX owner |
| Asset-path ambiguity between `assets/textures` / `assets/meshes` and `assets/vfx` | Content organization | Medium | Runtime growl/tail-fire paths resolve out of canonical runtime folders, but `assets/vfx/` still exists as a tempting alternate landing zone for new effect assets. | VFX/content owner |
| Logging is split between `LogBus` and direct `std::cout` / `std::cerr` prints | Observability | Medium | Startup, preview, and runtime diagnostics still use mixed logging styles. | Runtime/platform owner |
| Preview project composition is improved but still concentrated | Tooling architecture | Medium | `src/game/preview/PokemonAutochessVfxPreviewProject.cpp` is smaller and cleaner than before, but it is still the main game-facing preview composition seam. | Preview tooling owner |
| Leech Seed preview is projectile-only | VFX preview | Low | `src/game/preview/effects/LeechSeedPreviewEffect.cpp` does not preview the drain/attach phase yet. | Game preview owner |

## Recently Retired In This Pass
| Issue | Outcome |
| --- | --- |
| Growl preview loop duplication between `PAC_VfxPreviewer` and `VfxLab` | Collapsed behind `src/vfx/preview/growl/GrowlPreviewController.*`. |
| Charmander Tail Fire preview fallback mismatch | Preview and runtime now share authored-vs-fallback policy via `SharedTailFirePlaybackPolicy.*`, and the preview bridge no longer suppresses fallback just because the species is Charmander. |
