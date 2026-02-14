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
- Optional per-pass shader override:
  - `vert_shader` / `frag_shader` in JSON (for passes that do not use the shared ring shader)
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
- `vert_shader`: optional vertex shader path for this pass
- `frag_shader`: optional fragment shader path for this pass
- `texture`: can be empty (`""`) for non-textured passes
- `render_mode`: optional rendering path selector (default `mesh`)
  - `mesh`: draw glTF mesh as-is
  - `texture_quarter_ring`: no mesh; draws one textured quarter quad multiple times around forward
- `quarter_count`: number of rotated quarter draws (default `4`)
- `quarter_step_deg`: rotation step between quarter draws (default `90.0`)
- `quarter_start_deg`: starting rotation offset (default `0.0`)
- `forward_offset`: per-pass world offset along cast direction (higher = farther from caster)
- `height_offset`: per-pass vertical start spread from origin (does not change aim angle)
  - Applied as `up * (height_offset * direction_local.y)` per line.
  - Positive values push top lines up and bottom lines down.
- `mesh_forward_axis`: optional per-pass authored-forward axis override `[x, y, z]`
  - Use this when a mesh's "length/forward" axis differs from shared config (e.g. line mesh authored in +Z).
- `direction_local`: optional per-pass aim vector in caster-local space `[right, up, forward]`
  - Example: `[0.22, -0.14, 1.0]` = slightly caster-right, slightly downward, mostly forward
- `directions_local`: optional array of local aim vectors for multi-line fan/cone from a single pass
  - Example: `[[0.24, -0.14, 1.0], [-0.24, -0.14, 1.0], [0.0, -0.28, 1.0]]`
  - For cone behavior, keep `forward` positive (usually `1.0`). If `forward` is `0.0`, you get a flat side/up/down fan plane.
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
