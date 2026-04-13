# Scratch Sequence3 Status

Date: 2026-04-13
Working branch during reconstruction: `scratch-vfx-clean-slate`
Source capture set: `D:\VFX\Scratch\sequence3`
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

The current Scratch work is focused on two draw events from the same source
frame:

- `eid1330`: the red glow backdrop using `Texture7566`
- `eid1344`: the bright orange claw mark pass using `Texture7568`

Relevant source docs already in repo:

- `docs/vfx/captures/scratch/sequence3/frame9740/eid1330/README.md`
- `docs/vfx/captures/scratch/sequence3/frame9740/eid1344/README.md`

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

## Historical Attempts (Archived)

- camera-facing billboards (caused clock-like lean)
- frozen-view rotation at emission (no visible improvement in VfxLab)

## Remaining Work

- lifetime/timing final tuning for both passes
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
