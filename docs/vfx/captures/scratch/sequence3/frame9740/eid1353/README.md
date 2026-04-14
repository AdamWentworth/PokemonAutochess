# Scratch VFX Capture - sequence3 / frame9740 / eid1353

Source:
- RenderDoc capture series in `C:\Code\VFX\Scratch\sequence3`
- Frame 9740, event id 1353

Notes:
- Mesh-only capture so far (vsout data only).
- Mesh contains 8 quads; each quad is expanded into a 2x2 sprite grid
  (four sprites per quad) matching the eid1344 layout convention.
- Quad size is derived from mesh width/height using the same scale factor
  `0.0602848` (geometric mean of width/height).
- Quad aspect is now preserved experimentally by keeping that geometric-mean
  base scale but also applying per-quad `scale_x_mul` / `scale_y_mul` derived
  from the source minor/major edge lengths.
- Quad centers are derived from mesh centers relative to quad 0, then
  doubled to match the established scratch layout convention.
- Quad orientation is preserved by deriving edge vectors from the
  vsout vertex order and placing the 2x2 sprite grid along those axes.
- Per-quad texture rotation is applied via `spin_deg` so each sprite
  aligns its vertical axis to the longer quad edge (edge-vector method).
- X/Y layout is authored into `position_local` and later scaled by
  `authored_billboard_position_scale` in the draw pass.
- Per-frame offsets are authored from the frame 9740 mesh toward the matched
  frame 9748 / eid1679 mesh.
- Per-frame scale animation is now authored too, so each quad can shrink/grow
  toward its matched 1679 size instead of staying at its 9740 size forever.

Assumptions (verify when PS/VS blocks are captured):
- Uses `Texture7568.png` and the same shader pair as eid1344.
- Uses the same TEV constants and blend state as eid1344.

Files:
- `9740-eid1353-mesh-vsout.csv`

Offsets:
- Frame-driven offsets now authored at 30fps using the mapping
  from frame 9740 (eid1353) to frame 9748 (eid1679).
- Frame 9748 is currently normalized by translation only.
- We intentionally removed the extra size-based normalization because it was
  making eid1353 read taller than eid1344 in our authored preview.
- Each quad is displaced along its own major edge direction (edge-vector
  basis) and interpolated across 9 frames.
- Per-quad width/height scale is interpolated across the same 9 frames using
  `authored_billboard_scale_frames`, with final X/Y multipliers derived from
  the matched 1679 quad minor/major edge ratios relative to 9740.

Assumptions for the faithful-size experiment:
- Sprite local X corresponds to the quad minor edge.
- Sprite local Y (after `spin_deg`) corresponds to the quad major edge.
- The `1353 -> 1679` quad correspondence is correct, so scale animation is
  being applied to the intended destination quad for each group.
