# Scratch VFX Capture - sequence3 / frame9740 / eid1330

Source:
- RenderDoc capture series in `D:\VFX\Scratch\sequence3`
- Frame 9740, event id 1330

Notes:
- This draw covers all four Texture7566 quads (no separate quadrant draws).
- Texture7566 is a 128x128 grayscale/alpha falloff (brightest at bottom-right).
- Texture6458 is present in the capture set but not sampled in this draw.
- TEV stage outputs a red/orange tint (lerp between c1 and c0 using texture RGB).
- Fade clue from PS blocks:
  - `9740/eid1330`: `color[1].a = 255`
  - `9748/eid1342`: `color[1].a = 76`
  - Current approximation continues that fade to `0` by the 13th source frame.
- Blend state (RenderDoc framebuffer panel):
  - Enabled: true
  - Color: Src=Src1 Alpha, Dst=One Minus Src1 Alpha, Op=Add
  - Alpha: Src=Zero, Dst=One, Op=Add
  - Write mask: RGB (alpha disabled)
  - Depth test: LessEqual, Depth write: disabled
- Alpha test requires alpha >= 1.
- Shader IDs 2576/2577 match eid 1344 (see shared shader files below).

Files:
- `9740-eid1330-buffer1682-psblock.csv`
- `9740-eid1330-buffer1682-vsblock.csv`
- `9740-eid1330-mesh-vsout.csv`
- `9748-eid1342-1682-psblock.csv`
- `assets/shaders/vfx/moves/scratch/capture_frame9740_shared_vertex_2576.glsl`
- `assets/shaders/vfx/moves/scratch/capture_frame9740_shared_frag_2577.glsl`
