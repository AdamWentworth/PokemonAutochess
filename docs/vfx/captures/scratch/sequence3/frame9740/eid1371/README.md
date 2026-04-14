# Scratch VFX Capture - sequence3 / frame9740 / eid1371

Source:
- RenderDoc capture series in `C:\Code\VFX\Scratch\sequence3`
- Start mesh: frame `9740`, event id `1371`
- End mesh: frame `9748`, event id `1697`

Notes:
- This is another angled claw-mark draw, closer in character to `eid1353`
  than the upright `eid1344` / `eid1362` passes.
- Mesh contains `8` quads; each quad is expanded into a `2x2` sprite grid
  (four sprites per quad), matching the established scratch layout convention.
- Quad orientation is derived from the source edge vectors and applied both to
  placement and texture spin (`spin_deg`).
- Quad size uses the same mesh-to-authored geometric-mean factor `0.0602848`.
- Quad aspect is preserved from the source through `scale_x_mul` /
  `scale_y_mul`.
- Per-quad spin animation is now authored too, so quads whose end-frame basis
  rotates relative to the start frame can correct that drift over time.

Current Starting Assumption:
- For this draw, quad identity is tracked by index between `9740/eid1371` and
  `9748/eid1697`.
- That assumption is based on the angle signature being unusually distinctive:
  the start and end frames both preserve the same broad angle family ordering
  (`+120`, `-50..-60`, `+120`, `+60`, etc.).
- Texture/basis alignment is now derived from the UV-consistent quad edges:
  `v0->v1` is treated as the local texture X edge and `v0->v2` as the local
  texture Y edge. This fixes the known outlier where one near-square quad
  picked the wrong side when using a naive "longest edge" heuristic.

Offsets:
- Per-frame offsets are authored at `30fps`.
- End-frame centers are aligned back into the start-frame mean-center space
  using translation-only normalization.
- Motion is projected onto each quad's own major edge direction, so the marks
  slide along their angled basis instead of world Y.

Scale Animation:
- Per-quad scale animation is authored through
  `authored_billboard_scale_frames`.
- Final scale targets come from the matched `1697` quad widths/heights relative
  to the `1371` quad widths/heights.

Spin Animation:
- Per-quad spin deltas are authored through `authored_billboard_spin_frames`.
- This is primarily to catch the known outlier where one quad's end-frame angle
  diverges noticeably from its start-frame angle.

Assumptions (call out if wrong):
- Index identity is stable enough for this angled draw to serve as the initial
  mapping.
- Translation-only normalization is a safer first pass than full size-based
  camera compensation.
- The draw uses the same texture (`Texture7568.png`), shader pair, and TEV /
  blend setup as the other scratch claw-mark passes.

Files:
- `9740-eid1371-mesh-vsout.csv`
- `9748-eid1697-mesh-vsout.csv`
