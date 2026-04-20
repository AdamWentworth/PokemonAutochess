# Leer Status

Date: 2026-04-20
Working branch during reconstruction: `master`
Source capture set: `C:\Code\VFX\Leer`
Primary static reconstruction capture: `leer-frame5190`

This note tracks the current timing assumptions for Leer so we can keep the
static mesh reconstruction and the time evolution work aligned.

## Timeline Baseline

Current global timing target:

- Visible Leer motion spans frame `0` through frame `50`
- Visible duration: `50 / 30 = 1.666667s`
- The lab keeps an extra cleanup sample at frame `51` (`51 / 30 = 1.700000s`)
  so frame `50` can stay partially visible and frame `51` can cleanly
  disappear

Current frame mapping assumption:

- `leer-frame5181.rdc` is treated as approximate global frame `5`
- The current reconstructed eye snapshot is therefore treated as a frame-`5`
  baseline rather than a frame-`0` fully-open pose

## Eye Emergence Plan

The first timing task is just the vertical growth of the two main eye meshes:

- Frames `0-1`: eyes are nearly invisible and extremely short on local Y
- Frame `2`: a thin line starts to become visible
- Frame `5`: matches the current captured baseline pose
- Frame `11`: appears to reach full height in source video

Current implementation status:

- Added per-pass frame-driven scale animation support for authored passes
- Applied that animation to the eight main eye draws:
  - right eye: `1254`, `1268`, `1284`, `1291`
  - left eye: `1308`, `1320`, `1336`, `1343`
- The frame curve is currently source-backed through frame `5`
- The frame-`11` full-height target is still a provisional approximation until
  we parse a later capture or otherwise measure that frame more directly

## Current Eye Scale Curve

The main eye passes currently use `pass_scale_frames` at `30fps` with local-Y
compression only:

- frame `0`: `[1.0, 0.03, 1.0]`
- frame `1`: `[1.0, 0.05, 1.0]`
- frame `2`: `[1.0, 0.14, 1.0]`
- frame `5`: `[1.0, 1.0, 1.0]`
- frame `11`: `[1.0, 1.12, 1.0]` (provisional)

The intent of this curve:

- preserve the trusted frame-`5` capture as-is
- make the first two frames almost absent
- let frame `2` read as a line rather than a full eye
- leave a documented knob for tuning the final open height once we have better
  frame-`11` evidence

## Tail Fade Plan

The next timing task is the shared Leer tail fade:

- Frames `0-40`: hold full opacity
- Frames `40-50`: fade down linearly
- Frame `50`: land around `0.5` opacity instead of disappearing early
- Frame `51`: fully invisible

Current implementation status:

- All current Leer passes now use shared `pass_alpha_frames` at `30fps`
- The old generic preview `fadeStart` tail is disabled for Leer so the lab only
  shows the authored frame-40..51 fade
- The eight main eye passes still keep their explicit `time_end_sec = 1.666667`
  window, so they remain visible through frame `50` and are naturally gone on
  frame `51`
- The `1355..1432` pupil highlight family follows the same authored fade curve,
  but reaches frame-`51` invisibility via `pass_alpha_frames` because those
  passes do not use explicit end times

## Open Follow-Up

After the eye-height emergence feels right, we still need to track:

- whether the highlight family `1355..1432` should inherit any of the same
  early-time squashing or appear on a later schedule
- any additional color, alpha, or position changes across the remaining
  `50`-frame lifetime
- whether one or more additional 10-frame RenderDoc windows are needed to pin
  down the later half of the effect instead of eyeballing from video
