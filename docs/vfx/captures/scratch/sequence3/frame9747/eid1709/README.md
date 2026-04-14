# Scratch VFX Capture - sequence3 / frame9747 / eid1709

Source:
- RenderDoc capture series in `C:\Code\VFX\Scratch\sequence3`
- Start mesh: frame `9747`, event id `1709`
- End mesh: frame `9748`, event id `1733`

Notes:
- This draw only appears late in the source sequence.
- It is not drawn during frames `9740..9746`; it first appears on frame `9747`
  and then continues into frame `9748`.
- This is an angled claw-mark draw.
- Mesh contains `8` quads; each quad is expanded into a `2x2` sprite grid
  (four sprites per quad), matching the established scratch layout convention.
- Quad orientation is derived from the UV-consistent source edges:
  - `v0->v1` = local texture X
  - `v0->v2` = local texture Y

Current Starting Assumption:
- Quad identity is stable by index between `9747/eid1709` and `9748/eid1733`.
- This one is also unusually clean: the end-frame mesh preserves the same
  angle families and size ordering closely enough that index identity is the
  lowest-risk starting point.

Timing:
- The authored pass starts at `7/30` seconds, matching the source behavior of
  not being present in the first seven frames of the effect.
- The current authored window is `7/30 -> 9/30` seconds, so it appears for the
  last two visible source frames only.

Offsets, Scale, and Spin:
- Per-frame offsets are authored at `30fps`.
- End-frame centers are aligned back into the start-frame mean-center space
  using translation-only normalization.
- Motion is projected onto each quad's local texture-Y / slash axis.
- Per-quad scale animation is authored through
  `authored_billboard_scale_frames`.
- Per-quad spin deltas are authored too, but the required corrections are
  tiny compared with the earlier angled passes.

Assumptions (call out if wrong):
- Index identity is correct for this late-start angled draw.
- Translation-only normalization is sufficient here.
- The draw uses the same texture (`Texture7568.png`), shader pair, and TEV /
  blend setup as the other scratch claw-mark passes.
