# Scratch VFX Capture - sequence3 / frame9740 / eid1344

Source:
- RenderDoc capture series in `C:\Code\VFX\Scratch\sequence3`
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
- Size note: quad size is applied via per-sprite `scale_mul`
  (geometric mean of quad width/height) because per-axis scaling did not
  show visible size differences in the lab pass. Revisit if aspect ratio
  or sizing looks off.
- Implementation: `billboard_facing_mode: "attack_plane"` locks the quads to the
  attack plane so orbiting cameras do not rotate the marks.
- Frame offsets: `authored_billboard_offset_fps: 30.0` with per-frame offsets in
  `authored_billboard_offset_frames` (frame 0 = 9740, frame 1 = 9741).
  X-axis offsets are forced to `0.0` (Y-only) to isolate camera shift contamination.
  Approach: ignore all frames after 9740 and derive the target layout from the 9740 mesh height data, then
  enforce these Y slots with zero overlap outside the two shared pairs:
  - max height: 4th smallest quad
  - half height: biggest quad
  - just above midpoint (same Y): smallest + 3rd smallest
  - midpoint: 4th biggest
  - half bottom: 2nd biggest
  - max bottom (same Y): 2nd smallest + 3rd biggest
  Slot spacing is computed from quad half-heights (9740 mesh), converted to local
  space using `0.0602848`, then linearly interpolated from 9740 to the target layout with X=0 (target reached at frame 8).
  Target offsets are computed as (desired slot Y - frame 9740 group center Y) so
  the final centers match the requested slot values (shared Y enforced on the final frame only).
  Timing tweak: destination is reached by frame 8 so the layout is visible before fade/loop.
  Lifetime tweak: pass runs for 10 frames at 30fps (time_end_sec = 10/30) and only begins fading at the end (time_fade_start = 1.0).

Assumptions (call out if wrong):
- Quad identity is tracked by size rank (smallest->largest) across frames.
- Slot spacing uses zero extra gap (touching bounds, no overlap).

Files:
- `9740-eid1344-buffer1682-psblock.csv`
- `9740-eid1344-buffer1682-vsblock.csv`
- `9740-eid1344-mesh-vsout.csv`
- `assets/shaders/vfx/moves/scratch/capture_frame9740_shared_vertex_2576.glsl`
- `assets/shaders/vfx/moves/scratch/capture_frame9740_shared_frag_2577.glsl`
