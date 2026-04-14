# Scratch VFX Capture - sequence3 / frame9747 / eid1700

Source:
- RenderDoc capture series in `C:\Code\VFX\Scratch\sequence3`
- Start mesh: frame `9747`, event id `1700`
- End mesh: frame `9748`, event id `1724`

Notes:
- This draw only appears late in the source sequence.
- It is not drawn during frames `9740..9746`; it first appears on frame `9747`
  and then continues into frame `9748`.
- This is an upright claw-mark draw.
- Mesh contains `8` quads; each quad is expanded into a `2x2` sprite grid
  (four sprites per quad), matching the established scratch layout convention.
- All source quads are axis-aligned (`U = 0°`, `V = -90°`), so the current
  reconstruction keeps motion Y-only and does not need per-quad spin.

Current Starting Assumption:
- Quad identity is stable by index between `9747/eid1700` and `9748/eid1724`.
- This one is unusually well-behaved: size, position, and angle all strongly
  support index identity as the starting point.

Timing:
- The authored pass starts at `7/30` seconds, matching the source behavior of
  not being present in the first seven frames of the effect.
- The current authored window is `7/30 -> 9/30` seconds, so it appears for the
  last two visible source frames only.

Offsets and Scale:
- Per-frame offsets are authored at `30fps`.
- End-frame centers are aligned back into the start-frame mean-center space
  using translation-only normalization.
- Motion is Y-only.
- Per-quad scale animation is authored through
  `authored_billboard_scale_frames`.

Assumptions (call out if wrong):
- Index identity is correct for this late-start upright draw.
- Translation-only normalization is sufficient here.
- The draw uses the same texture (`Texture7568.png`), shader pair, and TEV /
  blend setup as the other scratch claw-mark passes.
