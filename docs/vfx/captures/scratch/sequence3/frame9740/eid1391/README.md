# Scratch VFX Capture - sequence3 / frame9740 / eid1391

Source:
- RenderDoc capture series in `C:\Code\VFX\Scratch\sequence3`
- Start mesh: frame `9740`, event id `1391`
- End mesh: frame `9748`, event id `1706`

Notes:
- This is another upright claw-mark draw, closer to `eid1362` than the angled
  passes.
- Mesh contains `8` quads; each quad is expanded into a `2x2` sprite grid
  (four sprites per quad), matching the established scratch layout convention.
- All source quads are axis-aligned (`U = 0°`, `V = -90°`), so the current
  reconstruction keeps motion Y-only and does not need per-quad spin.
- Quad size uses the same mesh-to-authored geometric-mean factor `0.0602848`.

Current Starting Assumption:
- Quad identity is tracked by size rank between `9740/eid1391` and
  `9748/eid1706`.
- Because the mesh CSVs do not provide stable emitter IDs and the quads are all
  upright, this is the lowest-risk starting heuristic.

Assumed mapping by size rank:
- `q7 -> q5`
- `q6 -> q7`
- `q5 -> q6`
- `q0 -> q4`
- `q2 -> q3`
- `q1 -> q0`
- `q3 -> q1`
- `q4 -> q2`

Offsets:
- Per-frame offsets are authored at `30fps`.
- End-frame centers are aligned back into the start-frame mean-center space
  using translation-only normalization.
- Motion is currently Y-only, because both source frames are vertically aligned
  and X drift appears negligible/noisy.

Scale Animation:
- Per-quad scale animation is authored through
  `authored_billboard_scale_frames`.
- Final scale targets come from the matched `1706` quad widths/heights relative
  to the `1391` quad widths/heights.

Assumptions (call out if wrong):
- Size rank is stable enough to act as quad identity for this draw.
- Translation-only normalization is a safer starting point than full
  size-based camera compensation.
- The draw uses the same texture (`Texture7568.png`), shader pair, and TEV /
  blend setup as the other scratch claw-mark passes.
