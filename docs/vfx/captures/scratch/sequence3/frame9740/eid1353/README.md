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
- Quad centers are derived from mesh centers relative to quad 0, then
  doubled to match the established scratch layout convention.
- Quad orientation is preserved by deriving edge vectors from the
  vsout vertex order and placing the 2x2 sprite grid along those axes.
- Per-quad texture rotation is applied via `spin_deg` so each sprite
  aligns its vertical axis to the longer quad edge (edge-vector method).
- X/Y layout is authored into `position_local` and later scaled by
  `authored_billboard_position_scale` in the draw pass.
- No per-frame offsets authored yet (only frame 9740 mesh known).

Assumptions (verify when PS/VS blocks are captured):
- Uses `Texture7568.png` and the same shader pair as eid1344.
- Uses the same TEV constants and blend state as eid1344.

Files:
- `9740-eid1353-mesh-vsout.csv`

Offsets:
- Frame-driven offsets now authored at 30fps using the mapping
  from frame 9740 (eid1353) to frame 9748 (eid1679).
- Frame 9748 is normalized by translation plus size-based scale
  (average major edge length) to compensate for camera zoom.
- Each quad is displaced along its own major edge direction (edge-vector
  basis) and interpolated across 9 frames.
