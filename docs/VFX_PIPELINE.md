# VFX Pipeline (Current Runtime)

This document reflects the live VFX path used in game today.

## Runtime Model

- Move VFX behavior is implemented in C++ classes under `src/game/vfx/`.
- Move mesh draw passes are data-driven via:
  - `config/vfx/moves/<move>_draw_passes.json`
- For Growl:
  - Runtime class: `src/game/vfx/GrowlWaveVFX.*`
  - Manifest: `config/vfx/moves/growl_draw_passes.json`

## Asset Conventions In Use

- Meshes keep source EID names for traceability:
  - `assets/meshes/growl_1076_mesh.glb`
  - `assets/meshes/growl_1085_mesh.glb`
- Textures keep source extracted names:
  - `assets/textures/moves/growl/Texture3918.png`
  - `assets/textures/moves/growl/Texture3921.png`
- Shared shaders for the move:
  - `assets/shaders/vfx/moves/growl/growl_ring_shared.vert`
  - `assets/shaders/vfx/moves/growl/growl_ring_shared.frag`

## Adding Another Growl Draw Pass

1. Add mesh/texture assets.
2. Append a new entry in `config/vfx/moves/growl_draw_passes.json`.
3. Set `eid`, `mesh`, `texture`, and pass tuning fields (`tint_color`, `scale_mul`, `alpha_mul`).

No code change is required when adding a pass that follows the same shader/state path.
