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
- `sequence3\leer-frame7462..7471` is treated as the startup window that
  precedes the `5181..5190` capture
- `sequence1\leer-frame4973..4982` is treated as a later pre-fade window in
  the middle-to-late life of the effect
- `sequence2\leer-frame6108..6111` is treated as the shutdown window; the
  fourth kept frame already behaves like a cleanup / almost-gone sample

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
- The new startup sequence confirms the effect really does have an earlier
  pre-roll before `5181`, but we still do not have exported per-frame mesh
  sidecars for those startup frames, so the current `0/1/2/5/11` Y-scale curve
  remains the best documented approximation
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

## Veiny UV Drift Note

User correction that changed the read:

- the "alive" drift in the veiny eye layer is **not** primarily the
  `Texture11230` highlight family
- the stronger source-backed drift lives in the main-eye modulate pass that
  matches `leer-frame5181`, EID `1291` to `leer-frame5190`, EID `1284`

What stays stable across that pair:

- raw VS-in positions match to capture tolerance
- raw VS-in `rawtex0` UVs match to capture tolerance
- raw VS-in vertex colors match to capture tolerance

What changes:

- VS-out `VertexData.tex0.xy` changes every sampled frame
- the drift is therefore shader / uniform-driven texture mapping, not a
  rewritten mesh

Current working reconstruction:

- the current authored representative for that logical right-eye pass is
  `leer_eid_1284`
- the current authored representative for the mirrored left-eye partner is
  `leer_eid_1343`
- both now use source-backed `pass_uv_scale_frames` and
  `pass_uv_offset_frames` at `30fps`
- we currently hold the frame-`14` UV state through the rest of the effect
  because the `5181..5190` capture window is the source-backed range we have
  for this pass

Captured right-eye UV transform solve:

- frame `5` (`5181` / EID `1291`):
  - `texX = 1.826087 * u - 0.913043`
  - `texY = 1.826087 * v - 1.278227`
- frame `6` (`5182` / EID `1291`):
  - `texX = 1.800000 * u - 0.900000`
  - `texY = 1.800000 * v - 1.259967`
- frame `11` (`5187` / EID `1291`):
  - `texX = 1.680000 * u - 0.840000`
  - `texY = 1.680000 * v - 1.175969`
- frame `12` (`5188` / EID `1291`):
  - `texX = 1.657895 * u - 0.828947`
  - `texY = 1.657895 * v - 1.160496`
- frame `14` (`5190` / EID `1284`):
  - `texX = 1.615385 * u - 0.807692`
  - `texY = 1.615385 * v - 1.130740`

Runtime note:

- the mesh runtime now supports animated UV transforms for shared mesh passes
- this was necessary because static mesh UVs plus position drift were not
  enough to make the veiny layer feel alive in the lab

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
- The `1355..1432` pupil highlight family now uses its own authored
  retirement schedule, thinning before the shared tail fade and reaching
  forced invisibility by frame `50`

New source-backed refinement from the extra capture windows:

- the highlight family does **not** just hit `12` logical passes by frame `14`
  and then stay static until the shared fade
- `sequence1` shows a pre-fade retirement trend in the later lifetime
- `sequence2` shows the family dropping through its last visible state and then
  disappearing entirely in the next kept frame

## Highlight Family Notes

We re-audited the actual `Texture11230` pupil-highlight family across
`leer-frame5181.rdc` through `leer-frame5190.rdc`.

Correct source-backed read:

- the highlight family is the `Texture11230` / program-`1278` quad family that
  lands at user-facing EIDs `1355..1432`
- the capture window maps to approximate global frames `5..14`
- frame `5` shows `3` logical highlight passes
- frame `6` shows `4`
- frame `7` shows `5`
- frame `8` shows `6`
- frame `9` shows `7`
- frame `10` shows `8` logical passes because the final draw temporarily
  carries two quads-per-eye in one merged submission
- frame `11` shows `9`
- frame `12` shows `10`
- frame `13` shows `11` logical passes for the same merged-draw reason
- frame `14` shows the full `12`

Per-pass behavior in the captured window:

- each logical pass contains a right-eye quad and a left-eye quad
- the dominant change is transform-driven: center offsets drift slightly while
  quad extents scale in and out over frames `5..14`
- the family also stages in over time instead of all `12` passes existing from
  the start of the current capture window
- we have not yet found a strong source-backed case for UV swirl in this
  `5181..5190` window

Current implementation status:

- generic per-pass animated `mesh_local_offset` frames are now wired into both
  the runtime path and the VfxLab preview path
- the `Texture11230` passes now use source-backed frame `5..14` scale and mesh
  offset curves
- each highlight pass now remains invisible until its first observed capture
  frame, then follows a two-stage retirement plan:
  - a pre-fade thinning pass in the middle-to-late lifetime
  - a final shutdown burst in the last visible frames before cleanup

Current working assumption:

- when a merged count-`20` draw temporarily contains two logical passes, we map
  them in sequential EID order so the reconstructed family stays monotonic from
  `1355` through `1432`
- for the later-life retirement, we currently retire the highest-EID
  `Texture11230` passes first (`1432` back toward `1404`), because that best
  matches the new sequence coverage without inventing a brand-new ordering

## Extra Sequence Coverage

The new RenderDoc windows are enough to fill in the broad lifecycle shape of
the highlight family even though the exact absolute global-frame mapping is
still approximate.

What the new sequences tell us:

- `sequence3` confirms that startup has a genuine earlier pre-roll before the
  old `5181..5190` window
- `sequence1` confirms that the highlight family starts thinning **before** the
  old shared frame-40 fade zone
- `sequence2` confirms that the highlight family has a final shutdown burst and
  does not simply coast unchanged until the generic cleanup frame

Working reconstruction choice:

- keep the trusted frame-`5..14` transform curves from the original capture
- keep the documented main-eye startup Y-growth curve
- add a late-life pre-fade retirement for the highest-EID highlight passes
- keep a short final shutdown burst for the last remaining highlight passes so
  the family is gone by the shutdown sample

## Orbit Audit

We also did a source-backed audit for the "maybe this is spiraling" question by
comparing the `Texture11230` quad centers and the shader uniform state across
`leer-frame5181.rdc` through `leer-frame5190.rdc`.

Current read:

- the highlight quads do **not** show strong orbital / swirl motion in their
  submitted mesh positions during this `5..14` frame window
- across the full window, all right-eye quad centers stay within about
  `0.255 x 0.708 x 0.173` world units
- across the full window, all left-eye quad centers stay within about
  `0.259 x 0.647 x 0.220` world units
- that is small enough that the family reads as "anchored to each eye" rather
  than quads orbiting around the eye surface

What *does* change strongly:

- the per-frame program-`1278` vertex uniform block at binding `2` changes a lot
- the highlight family keeps `ctexmtx[0..1]` and `cpostmtx[61..63]` at identity in
  the current capture window, so we do **not** have evidence for UV swirl here
- the changing part is the vertex position matrix (`cpnmtx[0..2]`), which
  rotates the highlight plane strongly between frames `5..8` and settles close
  to the frame-`14` basis by frames `9..14`
- the largest observed L2 deltas in the first 16 floats of that block are:
  - frame `5181 -> 5182`: `6.80`
  - frame `5182 -> 5183`: `6.81`
  - frame `5184 -> 5185`: `21.78`
- so the "swirl" or shimmer that we perceive in source is more likely living in
  the animated vertex basis than in raw quad placement or animated UVs

Current working inference:

- the FSYS / GPT1 payload is still relevant, but more as a procedural driver
  for the shared highlight orientation state than as an explicit per-frame quad
  orbit table
- the next runtime approximation should therefore animate a shared mesh-local
  rotation for the `Texture11230` highlight family, keyed from the captured
  `cpnmtx` basis over frames `5..14`

## Stable Matching Note

We are no longer treating "same numeric chunk / EID across captures" as proof
that a later draw is the same logical Leer pass.

Current matching method for the third veiny pass (`1369`) is:

- match on pipeline family first:
  - triangle-strip quad draw
  - same highlight-family program / VAO class for the later captures
  - same texture-binding family
- then confirm the geometry signature:
  - two restart-separated quads
  - same UV corner layout
  - same right-eye / left-eye spacing and span ratios
  - lowest per-eye raw-vertex error against the authored
    `leer_1369_right_mesh_vsin.csv` / `leer_1369_left_mesh_vsin.csv` pair

With that stricter method, the best current later-life match for the authored
third veiny pass is the later capture candidate exported from:

- `sequence1\leer-frame4975.rdc`, chunk `2168`
- `sequence1\leer-frame4981.rdc`, chunk `2168`
- `sequence2\leer-frame6108.rdc`, chunk `2168`

Important counterexample:

- `sequence2\leer-frame6110.rdc`, chunk `2168` is **not** the same logical draw
  anymore
- by that point the capture has collapsed to a `5`-index cleanup quad with a
  completely different VS-in topology
- this is the concrete example that justified the stricter matching rule

Current reconstruction choice:

- keep the original source-backed `5..14` motion for `1369`
- extend its mid/late-life motion using the stable-signature-matched later
  samples above
- because the matched transform deltas are subtle once the capture mesh is
  shrunk by the global `0.1` Leer world scale, the current runtime
  reconstruction also feeds a softened portion of the same `1369` drift into
  `pass_position_offset_frames` so the veiny layer actually reads as alive in
  the lab instead of disappearing into sub-pixel local mesh motion
- stop trusting the same numeric chunk once the topology / signature changes
  (as seen at `6110`)

## Open Follow-Up

After the eye-height emergence feels right, we still need to track:

- whether the main eye meshes need more startup keyframes than the current
  `0/1/2/5/11` curve once we export direct startup mesh sidecars
- whether the highlight family `1355..1432` needs shader/uniform-driven
  animation during the steady middle, rather than just the captured transform
  keys plus retirement scheduling
- whether the shared frame-40..51 alpha curve for the main eye family should be
  tightened now that the highlight-family shutdown is better constrained
