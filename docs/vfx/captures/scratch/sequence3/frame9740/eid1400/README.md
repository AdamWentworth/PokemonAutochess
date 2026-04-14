# Scratch VFX Capture - sequence3 / frame9740 / eid1400

Source:
- RenderDoc capture series in `C:\Code\VFX\Scratch\sequence3`
- Start mesh: frame `9740`, event id `1400`
- End mesh: frame `9748`, event id `1715`

Notes:
- This is another angled claw-mark draw, closer to `eid1371`.
- Mesh contains `8` quads; each quad is expanded into a `2x2` sprite grid
  (four sprites per quad), matching the established scratch layout convention.
- Quad orientation is derived from the UV-consistent source edges:
  - `v0->v1` = local texture X
  - `v0->v2` = local texture Y
- Quad size uses the same mesh-to-authored geometric-mean factor `0.0602848`.
- Quad aspect is preserved from the source through `scale_x_mul` /
  `scale_y_mul`.

Current Starting Assumption:
- This draw is currently using the angle/size/translation-matched mapping:
  - `q0 -> q0`
  - `q1 -> q7`
  - `q2 -> q1`
  - `q3 -> q3`
  - `q4 -> q4`
  - `q5 -> q5`
  - `q6 -> q6`
  - `q7 -> q2`
- This is not proven identity data; it is the best current heuristic from the
  two mesh snapshots.

Offsets:
- Per-frame offsets are authored at `30fps`.
- End-frame centers are aligned back into the start-frame mean-center space
  using translation-only normalization.
- Motion is projected onto each quad's local texture-Y / slash axis so the
  marks slide along their angled basis instead of world Y.

Scale Animation:
- Per-quad scale animation is authored through
  `authored_billboard_scale_frames`.
- Final scale targets come from the matched `1715` quad widths/heights relative
  to the `1400` quad widths/heights.

Spin Animation:
- Per-quad spin deltas are authored through `authored_billboard_spin_frames`.
- One quad currently wants a larger spin correction than the rest, so this pass
  is a good candidate for visual follow-up if any single outlier still reads
  wrong in VfxLab.

Assumptions (call out if wrong):
- The above mesh-matched mapping is good enough as a starting point.
- Translation-only normalization is a safer first pass than full size-based
  camera compensation.
- The draw uses the same texture (`Texture7568.png`), shader pair, and TEV /
  blend setup as the other scratch claw-mark passes.
