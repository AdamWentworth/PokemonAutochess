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
- `texture_cache_group`: optional shared baked-texture cache scope for passes
  that use the same texture and TEV inputs
- `texture_bake_mode`: optional mesh texture bake formula
  - `tev_lerp` (default): `tev_c0 * mix(tev_c1, tev_k0, texture)`
  - `modulate_c0`: `tev_c0 * texture`
  - `texture`: raw texture RGB with tint only
- `texture_alpha_mode`: optional baked texture alpha source
  - `texture` (default): keep source texture alpha
  - `one` / `vertex`: bake opaque alpha so vertex/pass alpha controls blending
- `render_mode`: optional rendering path selector (default `mesh`)
  - `mesh`: draw glTF mesh as-is
  - `texture_quarter_ring`: no mesh; draws one textured quarter quad multiple times around forward
  - `streak_quad`: no mesh asset; draws a shared tapered streak quad oriented along each generated/authored direction
- `write_mask`: optional color write mask for the draw pass
  - string forms: `rgba` (default), `rgb`, `rg`, `r`, `g`, `b`, `a`, `none`
  - array form: `[true, true, true, false]` for RGBA booleans
- `quarter_count`: number of rotated quarter draws (default `4`)
- `quarter_step_deg`: rotation step between quarter draws (default `90.0`)
- `quarter_start_deg`: starting rotation offset (default `0.0`)
- `forward_offset`: per-pass world offset along cast direction (higher = farther from caster)
- `height_offset`: per-pass base radial start spread from origin (does not change aim angle)
  - Applied in local fan plane as `(right * direction_local.x + up * direction_local.y) * height_offset`.
- `start_radius_mul`: optional multiplier for line start radius (default `1.0`)
  - Use values `< 1.0` to tighten where lines begin while keeping the same aim cone.
- `mesh_forward_axis`: optional per-pass authored-forward axis override `[x, y, z]`
  - Use this when a mesh's "length/forward" axis differs from shared config (e.g. line mesh authored in +Z).
- `mesh_local_offset`: optional local-space mesh translation `[x, y, z]`
  - Use this when a RenderDoc sibling draw relies on per-draw VS constants that are not replayed in the lab, so its raw VS-in mesh needs registration back onto the base surface.
- `position_local_offset`: optional pass origin translation `[right, up, forward]`
  - Use this to place sibling draws side-by-side in emitter-local space without changing their facing direction or mesh-internal registration.
- `direction_local`: optional per-pass aim vector in caster-local space `[right, up, forward]`
  - Example: `[0.22, -0.14, 1.0]` = slightly caster-right, slightly downward, mostly forward
- `directions_local`: optional array of local aim vectors for multi-line fan/cone from a single pass
  - Example: `[[0.24, -0.14, 1.0], [-0.24, -0.14, 1.0], [0.0, -0.28, 1.0]]`
  - For cone behavior, keep `forward` positive (usually `1.0`). If `forward` is `0.0`, you get a flat side/up/down fan plane.
- `generated_direction_count`: optional generated circular direction set when `directions_local` is omitted
- `generated_direction_mode`: optional generated direction layout
  - `circle` (default): evenly spaced ring in the local right/up plane
  - `sphere`: spherical burst with directions above and below the plane as well
- `generated_direction_start_deg`: starting angle for generated circular directions
- `generated_direction_arc_deg`: total arc covered by generated circular directions (default full `360`)
- `generated_direction_forward`: forward component for generated directions
- `direction_spacing_jitter_deg`: optional per-emission angular jitter for `directions_local` (degrees)
  - `0.0` keeps authored spacing exactly.
  - Higher values keep the circular pattern but randomize gaps between lines each cast.
- `radial_distance_min_mul` / `radial_distance_max_mul`: optional random multiplier range for radial spawn distance
  - Use this to keep generated bursts away from the exact center while still jittering how far from impact each streak/card begins.
- `line_alpha_min` / `line_alpha_max`: optional per-line alpha factor range (multiplies `alpha_mul`)
  - Use this to make some lines brighter and others more subtle in the same burst.
  - Example: `0.55` to `1.45`.
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
