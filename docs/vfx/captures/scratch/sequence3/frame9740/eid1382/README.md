# Scratch VFX Capture - sequence3 / frame9740 / eid1382

Source:
- RenderDoc capture series in `C:\Code\VFX\Scratch\sequence3`
- Frame `9740`, event id `1382`

Files:
- `9740-eid1382-buffer1682-vsblock.csv`
- `9740-eid1382-buffer1682-psblock.csv`
- `9746-eid1648-1682-psblock.csv`
- `9740-eid1382-mesh-vsout.csv`
- `assets/textures/moves/scratch/Texture7567.png`

What We Know For Sure:
- This pass uses the shared scratch shader family again.
- The mesh is only a single quad, centered at the exact same point as
  `eid1330`.
- The framebuffer blend state from RenderDoc is:
  - color src `Src1 Alpha`
  - color dst `One`
  - alpha src `Zero`
  - alpha dst `One`
  - write mask `RGB`
  - depth test `LessEqual`
  - depth write disabled
- PS block TEV colors decode to:
  - `C0 = (238, 189, 106)`
  - `C1 = (97, 46, 31)`
- Fade clue from PS blocks:
  - `9740/eid1382`: `color[1].a = 255`
  - `9746/eid1648`: `color[1].a = 36`
  - The pass is gone on the next frame, so the current approximation fades to
    that captured final value by frame `9746` and then reaches `0` on frame
    `9747`.
- `Texture6458` is present in the capture set but there is currently no source
  evidence that this draw samples it.

Current Reconstruction Assumption:
- `Texture7567` is a quarter texture and should be reconstructed like the red
  glow cluster rather than treated as a literal single full-screen quad.
- Because the `eid1382` quad shares the same center as `eid1330`, the current
  authored pass reuses the touching-quarter cluster approach and fits a single
  rotated/scaled quarter cluster on top of that same impact center.
- The current fitted transform is a best-effort approximation from the accepted
  `eid1330` cluster to the `eid1382` mesh:
  - `spin_deg = 50.05`
  - `scale_x_mul = 0.684332`
  - `scale_y_mul = 0.930865`

Open Questions:
- Whether `eid1382` should sit directly on top of `eid1330` as a separate pass
  or whether its look wants a bit more custom blending/timing against it.
- Whether the fitted rotation should be nudged after visual review in VfxLab.

Assumptions (call out if wrong):
- `Texture7567` should be reconstructed through `texture_touching_quarters`.
- `Texture6458` is not used by this draw.
- The shared scratch shader pair is still the correct runtime shader for this
  pass.
