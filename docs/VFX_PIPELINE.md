# VFX Pipeline

Status: Active
Type: Architecture
Last updated: 2026-03-30

This document describes the current ownership split for runtime VFX, reusable
preview code, game-specific preview adapters, and asset placement.

## Ownership Split
- Reusable VFX code lives in `src/vfx/`.
  - Effect implementation: `src/vfx/effects/`
  - Runtime bridge/helpers: `src/vfx/runtime/`
  - Reusable preview support: `src/vfx/preview/`
- Game-specific VFX code lives in `src/game/vfx/`.
  - Use this when the effect depends on Pokemon/runtime state or game-only
    presentation rules.
- Game-specific preview composition lives in `src/game/preview/`.
  - Use this for board placement, Pokemon rig selection, attack animation
    timing, and other game-only preview adapters.

This split is intentional. `src/vfx/` is the reusable top-level VFX surface and
should stay isolated from game-only concerns.

## Current Portability State
- The ownership direction is now materially real for Growl.
- Growl's reusable runtime and preview helpers no longer include
  `game/runtime/*` headers directly.
- Neutral Growl mesh/batch types and the reusable indexed submit/prewarm helper
  now live in `src/vfx/runtime/growl/`.
- Game-specific translation now lives at the edge in
  `src/game/runtime/shared/vfx/growl/SharedGrowlInterop.*`.
- `VfxLab` now loads Growl meshes/textures through its own reusable preview
  path instead of relying on game runtime cache/world-batch types.
- Long-term success still means extending this pattern beyond Growl so the
  reusable `src/vfx/` layer can survive deleting or replacing `src/game/` with
  only thin adapter changes at the edge.

## Current Runtime Examples

### Growl
- Reusable effect:
  - `src/vfx/effects/growl/GrowlWaveVFX.*`
- Reusable runtime bridge:
  - `src/vfx/runtime/growl/SharedGrowlBatchSubmission.*`
  - `src/vfx/runtime/growl/SharedGrowlWaveBridge.*`
  - `src/vfx/runtime/growl/SharedGrowlWaveBatches.*`
  - `src/vfx/runtime/growl/SharedGrowlVfxHelpers.*`
- Reusable preview controller:
  - `src/vfx/preview/growl/GrowlPreviewController.*`
  - `src/vfx/preview/growl/GrowlSharedRenderer.*`
- Game-facing preview adapter:
  - `src/game/preview/effects/GrowlPreviewEffect.*`
- Game-specific runtime adapter seam:
  - `src/game/runtime/shared/vfx/growl/SharedGrowlInterop.*`
- Reusable lab adapter:
  - `src/vfx/preview/effects/GrowlLabPreviewEffect.*`
- Manifest:
  - `config/vfx/moves/growl_draw_passes.json`

### Tail Fire
- Game-specific runtime effect:
  - `src/game/vfx/TailFireVFX.*`
  - `src/game/vfx/TailFireVFXConfigDB.*`
- Shared runtime support for projected/fallback/authored playback:
  - `src/game/runtime/shared/vfx/tail_fire/*`
- Preview bridge:
  - `src/game/preview/PreviewTailFireBridge.*`
- Shared authored-vs-fallback policy:
  - `src/game/runtime/shared/vfx/tail_fire/SharedTailFirePlaybackPolicy.*`

Tail Fire architecture today:
- Runtime gameplay effect authoring still lives in `src/game/vfx/`.
- Shared policy/config/anchor rules live under
  `src/game/runtime/shared/vfx/tail_fire/`.
- `SharedTailFireCoordinator.*` is the source of truth for species policy,
  backend skinning policy, config lookup, playback profile lookup, and authored
  anchor export.
- `SharedTailFireRenderContext.*` is the shared render-time plumbing used by
  both projected gameplay and preview-tail-fire billboard submission.
- The preferred render split is:
  - body through the normal projected/world-scene model path
  - authored fire mesh through explicit indexed sidecar batches when available
  - synthetic fallback only when authored playback is unavailable
- Preview should confirm the same playback mode the game would use, rather than
  re-implementing Tail Fire policy locally.
- Manual validation snapshot:
  - `config/debug/debug_state_snapshot_tail_fire_starter_line.json`
  - `tools/launch_tail_fire_starter_line_snapshot.ps1`
  - this places `charmander`, `charmeleon`, and `charizard` on the board for a
    quick Tail Fire visual check without overwriting the default debug snapshot
- Current expectation:
  - the full Charmander line should resolve authored Tail Fire playback when
    the authored fire mesh batches are available

### Leech Seed
- Game-specific projectile/drain effect:
  - `src/game/vfx/LeechSeedProjectileVFX.*`
  - `src/game/vfx/LeechSeedDrainVFX.*`
- Current preview scope:
  - `src/game/preview/effects/LeechSeedPreviewEffect.*`
  - preview currently shows projectile-only behavior

## Preview Tools
- `PAC_VfxPreviewer`
  - entry point: `tools/PAC_VfxPreviewer.cpp`
  - project adapter: `src/game/preview/PokemonAutochessVfxPreviewProject.*`
  - use when the effect needs real board constraints, Pokemon models, or
    attack-animation timing
- `VfxLab`
  - entry point: `tools/PAC_VfxLab.cpp`
  - project adapter: `src/vfx/preview/VfxLibraryPreviewProject.*`
  - use when the effect should stay reusable and game-agnostic

## Asset And Config Ownership
- Canonical runtime mesh assets belong under `assets/meshes/`.
- Canonical runtime texture assets belong under `assets/textures/`.
- Runtime model content belongs under `assets/models/`.
- Runtime/config-driven VFX manifests belong under `config/vfx/`.
- `assets/vfx/` is for reusable/reference/staging VFX content and folder
  organization. It is not the default destination for runtime-resolved mesh or
  texture paths when code/config already points at `assets/meshes/` or
  `assets/textures/`.

Current examples:
- Growl runtime meshes: `assets/meshes/growl_*.glb`
- Growl runtime textures: `assets/textures/moves/growl/*`
- Charmander-line authored fire flipbooks:
  - `assets/textures/charmander_fire_uv_flipbook.png`
  - `assets/textures/CharmeleonFireUVFlipbook.png`
  - `assets/textures/CharizardFireUVFlipbook.png`

## Rules Of Thumb
- Start in `src/vfx/` if the effect, runtime bridge, or preview controller can
  be reused by another game.
- Start in `src/game/vfx/` if the effect depends on Pokemon species rules,
  game-world ownership, or game-only data/config behavior.
- Keep reusable preview/render helpers in `src/vfx/preview/`; keep board and
  Pokemon rig adapters in `src/game/preview/`.
- When promoting an effect to runtime, move concrete runtime-referenced meshes
  and textures into `assets/meshes/` and `assets/textures/` instead of leaving
  the authoritative path under `assets/vfx/`.
