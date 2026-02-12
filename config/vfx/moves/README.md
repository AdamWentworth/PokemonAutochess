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

## Why This Layout

- One shader pair can be reused across multiple draws.
- Mesh/texture swaps are isolated to JSON, not C++ code.
- Source IDs remain visible for RenderDoc and extraction traceability.
