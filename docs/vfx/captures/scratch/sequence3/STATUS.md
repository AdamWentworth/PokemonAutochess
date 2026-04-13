# Scratch Sequence3 Status

Date: 2026-04-13
Working branch during reconstruction: `scratch-vfx-clean-slate`
Source capture set: `D:\VFX\Scratch\sequence3`
Primary capture under reconstruction: `frame9740`

This document is the handoff note for the current Scratch reconstruction state.
It records:

- what is source-backed and working
- what has been intentionally accepted as the current baseline
- what we tried for the 3D orientation problem and did not solve

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

## Successful Commit Checkpoints

These commits are the useful historical checkpoints in this reconstruction:

- `44e0f9e` Add scratch glow VFX lab pass
- `1fc2af7` Refine scratch eid1344 quad layout and blend parity
- `504208b` Tune scratch eid1344 group scale and spread
- `da7dd0a` Tighten scratch eid1344 spread
- `a5152b2` Soften scratch eid1330 backdrop

Those commits represent the currently trusted front-view baseline.

## Current Unsolved Problem

The remaining issue is not the front view anymore. The remaining issue is the
3D presentation of `eid1344` when the camera is allowed to orbit around the
effect in VfxLab or in broader gameplay camera scenarios.

### Problem Description

The source game effectively presents Scratch from one favorable battle camera
angle. In that context the slash pattern only needs to read correctly from that
one view.

Our game has:

- a top-down 3D presentation
- camera panning
- development workflows where the camera can pivot around the scene

That means the scratch VFX is exposed from angles the original effect was never
asked to survive.

### What looked wrong before the latest experiment

When `eid1344` used camera-facing billboards:

- the front view looked close to source
- but each sprite leaned toward the camera
- left-side marks leaned toward roughly `1 o'clock`
- right-side marks leaned toward roughly `11 o'clock`

That was not source-accurate. In the source capture, the orange slashes read as
nearly straight up/down with no obvious left/right leaning.

### What was tried next and why it failed

We tried replacing the per-sprite camera-facing rotation with a non-camera-
facing orientation mode for `eid1344`.

The goal was:

- preserve the front-view look
- stop the slashes from reorienting as the camera moves
- make the effect feel baked into the 3D scene

The first non-camera-facing attempt made the slash plane attack-oriented. That
looked wrong from side views because the slashes turned sideways and stopped
reading like claw marks on the target.

The next attempt, which is now present in code, freezes one shared slash-plane
rotation per emission from the current view at spawn time.

Implementation summary of the current attempt:

- `eid1344` is set to `camera_facing: false` in
  `config/vfx/moves/scratch_draw_passes.json`
- `SharedAuthoredBatchVFX` now stores a per-emission frozen rotation in
  `RingInstance::rot` together with `hasFrozenViewRotation`
- Scratch preview emission now passes the last preview view matrix into
  `ScratchGlowVFX::emitAt(...)`
- The runtime uses a shared frozen rotation for all authored claw sprites in
  the pass instead of a per-sprite billboard-to-camera rotation

Relevant files for the current attempt:

- `src/vfx/effects/shared/SharedAuthoredBatchVFX.cpp`
- `src/vfx/effects/shared/SharedAuthoredBatchVFX.h`
- `src/vfx/effects/scratch/ScratchGlowVFX.cpp`
- `src/vfx/effects/scratch/ScratchGlowVFX.h`
- `src/vfx/preview/shared/SharedPreviewControllerBase.h`
- `src/vfx/preview/scratch/ScratchPreviewController.cpp`

Observed result in VfxLab:

- visually, this still did not produce the intended improvement
- the user reported that it looked no different in the lab
- therefore this orientation problem is still unsolved

### Current Interpretation

At the time of this handoff, the likely explanation is one of these:

- the frozen view orientation is still too close in practice to the previous
  result to matter visually
- the wrong plane basis is being frozen
- the "good" front-view look is not actually controlled by the whole-plane
  rotation alone
- the claw mark texture itself, plus the current local layout, still encodes
  the clock-like lean even when the plane is frozen

In other words: the orientation problem is no longer a blind guess, but it is
not solved yet.

## Recommendations For Next Session

The stable parts should not be reopened casually:

- keep `eid1330` red glow layout, blend, and softened alpha
- keep `eid1344` group interpretation as four sprites per mesh quad
- keep `eid1344` spread at `0.35`
- keep the validated TEV constants for both passes

The active investigation area should be limited to `eid1344` orientation only.

Recommended next steps:

1. Instrument the current orientation mode so it is visually obvious which path
   is active
   - for example by forcing a temporary extra spin or a debug tint in the
     non-camera-facing path
   - this would confirm whether the new frozen path is truly being exercised in
     the exact VfxLab replay flow being tested

2. If the frozen path is active, inspect the basis itself
   - verify which local quad axis is being treated as the slash's visual
     "vertical" axis
   - verify whether the texture's actual bright line orientation matches that
     assumption

3. If billboard-style quads keep failing, consider promoting `eid1344` to a
   more explicit target-facing plane or authored mesh solution
   - the source effect may simply be too view-dependent for generic billboard
     logic to survive free camera orbit cleanly

4. Do not retune spread/TEV/glow balance until the orientation question is
   resolved
   - those pieces are already good enough and should remain fixed for now

## Build State

The full project build was verified after the latest preview-side include fix
with:

- `cmake --build --preset debug`

That full build completed successfully after adding the required `Camera3D`
include to `src/vfx/preview/shared/SharedPreviewControllerBase.h`.

## Bottom Line

The Scratch front-view reconstruction is in a good state.

What is solved:

- red backdrop
- orange claw group composition
- blend/TEV parity for the two current passes
- size and spread relationship between the claw groups

What is not solved:

- how `eid1344` should behave in a free-orbit 3D camera without losing the
  source-authentic front view or turning into an obviously unprofessional
  side-view smear

That is the live problem for the next session.
