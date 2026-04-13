# Scratch VFX Capture - sequence3 / frame9740 / eid1344

Source:
- RenderDoc capture series in `D:\VFX\Scratch\sequence3`
- Frame 9740, event id 1344

Notes:
- Texture: `Texture7568.png` (32x32) in `assets/textures/moves/scratch/`.
- Mesh contains 8 quads; each quad has UVs in the 0..2 (U) / 1..3 (V) range.
  - This implies a 2x2 texture tile per quad (four copies per quad).
- TEV colors from PSBlock:
  - C0 = (233, 97, 46)
  - C1 = (87, 64, 36)
- Shader IDs 2576/2577 match eid 1330 (shared shader files below).
- Blend state (RenderDoc framebuffer panel):
  - Enabled: true
  - Color: Src=Src1 Alpha, Dst=One, Op=Add
  - Alpha: Src=Zero, Dst=One, Op=Add
  - Write mask: RGB (alpha disabled)
  - Depth test: LessEqual, Depth write: disabled
- Validated: each mesh quad is expanded into four sprites
  placed on a 2x2 grid (using quad corners to derive tile size/placement).
  If this looks wrong, we will switch back to the single-quad UV-tiling path.
- Size note: quad size is applied via per-sprite `scale_mul`
  (geometric mean of quad width/height) because per-axis scaling did not
  show visible size differences in the lab pass. Revisit if aspect ratio
  or sizing looks off.
- Implementation: `billboard_facing_mode: "attack_plane"` locks the quads to the
  attack plane so orbiting cameras do not rotate the marks.

Files:
- `9740-eid1344-buffer1682-psblock.csv`
- `9740-eid1344-buffer1682-vsblock.csv`
- `9740-eid1344-mesh-vsout.csv`
- `assets/shaders/vfx/moves/scratch/capture_frame9740_shared_vertex_2576.glsl`
- `assets/shaders/vfx/moves/scratch/capture_frame9740_shared_frag_2577.glsl`
