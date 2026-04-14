# Scratch Sequence3 Status

Date: 2026-04-13
Working branch during reconstruction: `scratch-vfx-clean-slate`
Source capture set: `C:\Code\VFX\Scratch\sequence3`
Primary capture under reconstruction: `frame9740`

This document is the handoff note for the current Scratch reconstruction state.
It records:

- what is source-backed and working
- what has been intentionally accepted as the current baseline
- how the 3D orientation problem was resolved

The intent is that future work can continue from this baseline without
re-deriving the same assumptions from RenderDoc and without forgetting which
parts were already validated visually in VfxLab.

## Source Scope

The current Scratch work is focused on the glow, the burst overlay, and the
first four claw-mark draws from the same source frame:

- `eid1330`: the red glow backdrop using `Texture7566`
- `eid1382`: the burst overlay using `Texture7567`
- `eid1344`: the bright orange claw mark pass using `Texture7568`
- `eid1353`: the second claw mark pass
- `eid1362`: the third claw mark pass
- `eid1371`: the fourth claw mark pass

Relevant source docs already in repo:

- `docs/vfx/captures/scratch/sequence3/frame9740/eid1330/README.md`
- `docs/vfx/captures/scratch/sequence3/frame9740/eid1382/README.md`
- `docs/vfx/captures/scratch/sequence3/frame9740/eid1344/README.md`
- `docs/vfx/captures/scratch/sequence3/frame9740/eid1353/README.md`
- `docs/vfx/captures/scratch/sequence3/frame9740/eid1362/README.md`
- `docs/vfx/captures/scratch/sequence3/frame9740/eid1371/README.md`

Relevant runtime manifest:

- `config/vfx/moves/scratch_draw_passes.json`

Relevant runtime effect code:

- `src/vfx/effects/scratch/ScratchGlowVFX.cpp`
- `src/vfx/effects/shared/SharedAuthoredBatchVFX.cpp`
- `src/vfx/runtime/shared/SharedAuthoredVfxBatches.cpp`

## Working Baseline

The following pieces are considered successfully reconstructed enough to use as
the current baseline.

### EID 1330: Red Glow Backdrop

This pass is the soft red/orange backdrop behind the claw marks.

What is working:

- Texture source is `Texture7566.png`
- The effect is authored as four touching quarter pieces that meet at the
  center
- The quarter orientation is correct only when rotated `180` degrees from the
  earlier incorrect version
- The pass uses the shared scratch shader pair
- TEV constants match the RenderDoc PS block:
  - `C0 = (158, 46, 18)`
  - `C1 = (62, 32, 37)`
- Blend parity is now documented from the framebuffer panel:
  - color src `Src1 Alpha`
  - color dst `One Minus Src1 Alpha`
  - alpha src `Zero`
  - alpha dst `One`
  - write mask `RGB`
- Runtime manifest now matches that with:
  - `blend_mode: "alpha"`
  - `dual_source_blend: true`
  - `write_mask: "rgb"`
- `alpha_mul` was reduced to `0.8` so the red reads as a backdrop instead of
  overpowering the claw marks in the middle

What is accepted for now:

- Size is close enough for the current package-level tuning phase
- Lifetime/timing is not final yet
- Camera-facing behavior is still acceptable for this glow pass

### EID 1382: Burst Overlay

This pass sits on the same impact center as `eid1330`, but uses a different
quarter texture and additive blend.

What is working:

- Texture source is `Texture7567.png`
- Blend parity from the framebuffer panel is matched:
  - color src `Src1 Alpha`
  - color dst `One`
  - alpha src `Zero`
  - alpha dst `One`
  - write mask `RGB`
- TEV colors from the PS block are authored as:
  - `C0 = (238, 189, 106)`
  - `C1 = (97, 46, 31)`
- Current reconstruction reuses the touching-quarter cluster approach, because
  the source mesh is a single quad centered exactly on the red glow center.

Current assumptions:

- `Texture7567` should be reconstructed as a quarter texture like the red glow
  cluster rather than as a literal one-quad billboard.
- `Texture6458` is not actually sampled by this draw.
- The current rotated/scaled fit (`spin_deg ~= 50.05`) is a good starting point
  but still needs visual confirmation in VfxLab.

### EID 1344: Orange Claw Marks

This is the complex pass with the overlapping bright orange slash groups.

What is working:

- Texture source is `Texture7568.png`
- The correct interpretation was not UV-tiling one quad into four copies
- The correct interpretation is: each source mesh quad expands into four
  separate sprites
- There are `8` source groups and each group produces `4` sprites, for `32`
  total sprite instances
- Relative group sizing is driven from the captured mesh data
- Group positions are close enough to source for the current baseline
- Spread tuning was visually iterated and the accepted value is:
  - `authored_billboard_position_scale: 0.35`
- Blend parity from the framebuffer panel is matched:
  - color src `Src1 Alpha`
  - color dst `One`
  - alpha src `Zero`
  - alpha dst `One`
  - write mask `RGB`
- TEV constants match the RenderDoc PS block:
  - `C0 = (233, 97, 46)`
  - `C1 = (87, 64, 36)`

What is accepted for now:

- The front-on composition is close to the source and was considered "perfect"
  enough to commit before the later 3D orientation experiments
- Group spread `0.35` is the current approved baseline
- The claw pass should remain the visual foreground over the red glow
- The pass is locked to the attack plane via `billboard_facing_mode: "attack_plane"`
  so orbiting cameras no longer reorient the marks
- Frame-driven offsets are now supported for the claw marks
  (`authored_billboard_offset_fps` / `authored_billboard_offset_frames`)
  so per-frame quad motion can be applied consistently at 30fps

### EID 1353: Second Claw Mark

What is working:

- Mesh-derived quad sizes and centers are authored into billboards
- Each quad is expanded into a 2x2 sprite grid (4 sprites per quad)
- Scale is derived from quad width/height using the same 0.0602848 factor
- Quad centers are derived relative to quad 0 and doubled to match the
  established scratch layout convention
- Frame-driven offsets are authored from `9740/eid1353 -> 9748/eid1679`
- Per-quad size animation is authored from the same start/end mesh pair
- Per-quad texture orientation follows the source quad basis
- Uses the same texture (`Texture7568.png`) and shader pair as eid1344
- Uses the same TEV/blend setup as eid1344

### EID 1362: Third Claw Mark (Initial Assumption Pass)

This pass is now added as the next vertical claw-mark reconstruction.

What is working:

- Mesh-derived quad sizes and centers are authored into billboards
- Each quad is expanded into a 2x2 sprite grid (4 sprites per quad)
- Frame-driven offsets are authored from `9740/eid1362 -> 9748/eid1688`
- Per-quad size animation is authored from the same start/end mesh pair
- Motion is currently treated as Y-only, matching the upright source quads

Current assumptions:

- Quad identity is currently tracked by size rank between `1362` and `1688`
- Translation-only normalization is the current starting point for end-frame
  alignment
- Uses the same texture (`Texture7568.png`) and shader / TEV / blend setup as
  the other scratch claw-mark passes

### EID 1371: Fourth Claw Mark (Initial Assumption Pass)

This pass is now added as the next angled claw-mark reconstruction.

What is working:

- Mesh-derived quad sizes, centers, and angles are authored into billboards
- Each quad is expanded into a 2x2 sprite grid (4 sprites per quad)
- Per-quad texture spin follows the source quad basis
- Frame-driven offsets are authored from `9740/eid1371 -> 9748/eid1697`
- Per-quad size animation is authored from the same start/end mesh pair
- Per-quad spin animation is authored too, to catch the known end-frame angle
  outlier without hard-coding a different static basis
- `eid1371` now uses the UV-consistent quad basis (`v0->v1` = local X,
  `v0->v2` = local Y) after a near-square outlier revealed that the naive
  longest-edge heuristic could pick the wrong side for texture alignment

Current assumptions:

- Quad identity is currently tracked by index between `1371` and `1697`
- Translation-only normalization is the current starting point for end-frame
  alignment
- Uses the same texture (`Texture7568.png`) and shader / TEV / blend setup as
  the other scratch claw-mark passes

## Successful Commit Checkpoints

These commits are the useful historical checkpoints in this reconstruction:

- `44e0f9e` Add scratch glow VFX lab pass
- `1fc2af7` Refine scratch eid1344 quad layout and blend parity
- `504208b` Tune scratch eid1344 group scale and spread
- `da7dd0a` Tighten scratch eid1344 spread
- `a5152b2` Soften scratch eid1330 backdrop

Those commits represent the currently trusted front-view baseline.

## Orientation Resolution (2026-04-13)

`eid1344` is now locked to the attack plane so the layout reads correctly from
the attacker view and stays flat from side/top/bottom angles.

Implementation summary:

- `billboard_facing_mode: "attack_plane"` in
  `config/vfx/moves/scratch_draw_passes.json`
- VfxLab preview path honors the mode in
  `src/vfx/runtime/shared/SharedAuthoredVfxBatches.cpp`
- Verified in VfxLab: head-on layout preserved and no camera-driven rotation

## Frame-Offset System (2026-04-13)

- Offsets are defined in `position_local` space and applied per quad-group
  (every 4 sprites).
- Frame 0 corresponds to capture frame `9740` and frame 1 to `9741`, using
  `authored_billboard_offset_fps: 30.0`.
- EID 1344 now uses a hybrid source-driven destination strategy:
  keep the previously validated size-rank identities from the authored
  interpretation, but replace the invented slot heights with heights sampled
  from frame 9748 (`eid1670`) after translation plus size-based scale compensation.
- EID 1353 still uses start/end mesh matching against frame 9748 (`eid1679`),
  but its current normalization is translation-only.
- EID 1353 now also preserves per-quad width/height variance experimentally by
  using mesh-derived `scale_x_mul` / `scale_y_mul` instead of treating each quad
  as isotropic after the geometric-mean size conversion.
- EID 1353 now animates those quad sizes over time as well via
  `authored_billboard_scale_frames`, using matched 9740->1679 minor/major edge
  ratios per quad.
- EID 1362 uses the same authored offset/scale-frame system, but its initial
  identity mapping is currently a size-rank assumption rather than a
  visually-validated correspondence.
- EID 1371 uses the same authored offset/scale-frame system, but its initial
  identity mapping is currently an index-identity assumption driven by the
  distinctive angle signature in the start/end meshes.
- For eid1344 the resulting offsets remain effectively Y-only, but the shared
  top/bottom lane heights are now sourced from `eid1670` rather than manually
  typed in.
- Target sizes still come from the same mesh-to-authored conversion factor
  `0.0602848`.
- Frames 9740-9748 are populated by linearly interpolating from the 9740 layout
  to the matched 9748 destinations, with target reached at frame 8.
- Timing tweak: destination is reached by frame 8 so the full spread is visible
  before fade/loop.
- The system interpolates smoothly between frames, so playback is FPS-independent.

- Lifetime tweak: both passes now run for 10 frames at 30fps (time_end_sec = 10/30)
  and only begin fading at the end (time_fade_start = 1.0).

Assumptions (call out if wrong):
- EID 1344 identity is tracked by the previously validated size-rank slot identities.
- EID 1353 identity is tracked by mesh matching between the start and end captures.
- Translation-only normalization is a better fit for EID 1353 than full size-based
  scale compensation.
- For EID 1353, sprite local X maps to the source minor edge and sprite local Y
  maps to the source major edge after spin is applied.
- For EID 1353, the matched end-frame quad identities are correct enough to
  drive per-quad size animation, not just position animation.
- For EID 1362, size rank is currently the best available identity heuristic
  until manual eyeballing confirms or corrects the mapping.
- For EID 1371, index identity is currently the best available starting
  heuristic because the angle pattern appears stable across the start/end
  captures.
## Remaining Work

- validate/correct `eid1362` quad correspondence in VfxLab
- validate/correct `eid1371` quad correspondence in VfxLab
- lifetime/timing final tuning for all scratch claw-mark passes
- confirm gameplay camera parity outside VfxLab

## Build State

The full project build was verified after the latest preview-side include fix
with:

- `cmake --build --preset debug`

That full build completed successfully after adding the required `Camera3D`
include to `src/vfx/preview/shared/SharedPreviewControllerBase.h`.

The VfxLab build for the billboard-facing fix was verified with:

- `cmake --build build --config Debug --target VfxLab`

## Bottom Line

The Scratch front-view reconstruction is in a good state.

What is solved:

- red backdrop
- orange claw group composition
- blend/TEV parity for the two current passes
- size and spread relationship between the claw groups
- `eid1344` orientation is now world-locked in orbiting cameras

What remains:

- final lifetime/timing tuning
