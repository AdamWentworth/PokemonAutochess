# Scratch VFX Capture - sequence3 / frame9740 / eid1362

Source:
- RenderDoc capture series in `C:\Code\VFX\Scratch\sequence3`
- Start mesh: frame `9740`, event id `1362`
- End mesh: frame `9748`, event id `1688`

Notes:
- Mesh-only capture so far.
- Like `eid1344`, this draw is upright in the attack plane and reads primarily
  as Y-axis separation rather than angled motion.
- Mesh contains `8` quads; each quad is expanded into a `2x2` sprite grid
  (four sprites per quad), matching the established scratch layout convention.
- Quad size uses the same mesh-to-authored geometric-mean factor `0.0602848`.
- Quad centers are derived relative to quad `0`, then doubled into
  `position_local` space.
- All source quads share the same aspect ratio, so `scale_x_mul` /
  `scale_y_mul` are effectively uniform across the set.

Current Starting Assumption:
- Quad identity is tracked by size rank between `9740/eid1362` and
  `9748/eid1688`.
- Because the mesh CSVs do not provide stable emitter IDs, this is an
  assumption-based starting point, not a proven identity mapping.

Assumed mapping by size rank:
- `q6 -> q3` (smallest)
- `q7 -> q1`
- `q5 -> q7`
- `q3 -> q4`
- `q0 -> q2`
- `q1 -> q6`
- `q4 -> q5`
- `q2 -> q0` (largest)

Offsets:
- Per-frame offsets are authored at `30fps` using translation-only alignment of
  the `1688` centers back into the `1362` mean-center space.
- Motion is currently Y-only, because both source frames are vertically aligned
  and X drift appears to be negligible/noisy.
- Frames `0..8` interpolate from the `9740` layout to the assumed `1688`
  destinations.

Scale Animation:
- Per-quad scale animation is authored through
  `authored_billboard_scale_frames`.
- Final scale targets come from the matched `1688` quad widths/heights relative
  to the `1362` quad widths/heights.

Assumptions (call out if wrong):
- Size rank is stable enough to act as quad identity for this draw.
- Translation-only normalization is a safer starting point than full
  size-based camera compensation.
- The draw uses the same texture (`Texture7568.png`), shader pair, and TEV /
  blend setup as the other scratch claw-mark passes.

Files:
- `9740-eid1362-mesh-vsout.csv`
- `9748-eid1688-mesh-vsout.csv`
