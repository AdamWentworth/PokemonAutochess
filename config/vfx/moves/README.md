# Move Draw Pass Manifests

This folder defines mesh draw passes for move VFX in a data-driven way.

## Naming Convention

- Manifest file: `<move>_draw_passes.json`
  - Example: `growl_draw_passes.json`
- Shared shader for a move:
  - `assets/shaders/vfx/moves/<move>/<move>_ring_shared.vert`
  - `assets/shaders/vfx/moves/<move>/<move>_ring_shared.frag`
- Per-EID mesh:
  - `assets/meshes/<move>_<eid>_mesh.glb`
- Per-EID texture:
  - `assets/textures/moves/<move>/Texture####.png` (keep source ID for traceability)
- Pass ID in JSON:
  - `<move>_eid_<eid>`

## TEV Color Inputs (RenderDoc)

When source textures are grayscale masks, hue comes from TEV constants.

For Growl passes you can set optional per-pass overrides:

- `override_tev`: `true`
- `tev_c0`: vec3 from `color[1].rgb / 255`
- `tev_c1`: vec3 from `color[2].rgb / 255`
- `tev_k0`: vec3 from `k[0].rgb / 255`
- `tev_k1a`: float from `k[1].a / 255`
- `forward_offset`: per-pass world offset along cast direction (higher = farther from caster)
- `radius_mul`: scales ring radius axes (per-pass, default `1.0`)
- `thickness_mul`: scales mesh forward axis thickness (per-pass, default `1.0`)

Utility script:

- `c:/Code/VFX/extract_psblock_tev.py`
- Example:
  - `python c:/Code/VFX/extract_psblock_tev.py c:/Code/VFX/Growl/growl_1076_psblock128.csv --eid 1076`
- Script prints a ready-to-paste JSON snippet for `draw_passes[]`.

## Why This Layout

- One shader pair can be reused across multiple draws.
- Mesh/texture swaps are isolated to JSON, not C++ code.
- Source IDs remain visible for RenderDoc and extraction traceability.
