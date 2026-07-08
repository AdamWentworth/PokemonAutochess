# Growl Source Audit

Status: Active
Type: Reference
Last updated: 2026-07-08

This note summarizes what we learned from the two best source artifacts for Growl:

- `c:\Code\VFX\Growl\growl_1.rdc`
- `c:\Code\GC\Game Files Extracted\root\wzx_nakigoe_sp1.fsys`
- `c:\Code\GC\Game Files Extracted\root\wzx_nakigoe_attack.fsys`

The goal is not to overclaim a full decode. The goal is to separate:

- high-confidence source facts
- plausible control/data hints
- concrete Growl adjustments worth trying
- low-confidence speculation that should not drive implementation by itself

## Bottom Line

The current Growl VFX looks broadly consistent with the source, but the source data suggests we are still simplifying one important family:

- the early wave stack is probably fine in concept
- the late accent stack is probably fine in concept
- the middle `1128` family is likely where we are still the most structurally simplified

The Growl FSYS pair does not give us a rich "compare two different behavior payloads" story the way Tackle did. The two decompressed payloads are almost identical. So the RenderDoc capture is the stronger source for "what did we miss?" and the FSYS is mainly useful for global control hints.

## Files Generated During This Audit

- `debug/growl_capture_report.txt`
- `debug/growl_capture_report.json`
- `debug/nakigoe_sp1_report.txt`
- `debug/nakigoe_sp1_report.json`
- `debug/nakigoe_attack_report.txt`
- `debug/nakigoe_attack_report.json`

## What The Growl FSYS Pair Says

## High-confidence facts

- Both files decompress to the same payload length:
  - `431540` bytes
- Both contain `GPT1` at:
  - `0xFF20`
- Both have the same GPT1 layout:
  - `header_size = 0x20`
  - `subentry_count = 113`
  - same subentry offset table
  - same texture directory layout
  - same three texture blocks

## Important implication

`wzx_nakigoe_sp1.fsys` and `wzx_nakigoe_attack.fsys` are not giving us a strong "same schema, different behavior payload" contrast. After decompression, they differ in only a handful of bytes. That means:

- they are not a good source for diff-based behavioral discovery
- Growl's missing nuance is unlikely to come from "attack vs sp1" deltas
- the FSYS is still useful, but mainly as a single control payload

## Strongest FSYS control clue

The strongest float-rich region is around GPT1 subentry `27`:

- absolute offset: `0x109B0`
- notable float sequence around `0x109C0`:
  - `0.94`
  - `1.0`
  - `-0.2`
  - `-3.0`
  - `0.174533`
  - `-10.0`
  - `1.0`
  - `0.0`
  - `6.283185`

Interpretation:

- `0.174533` is almost certainly `10 degrees`
- `6.283185` is `2pi`
- `-10.0` and `-3.0` look like motion/radius/offset-style control terms
- `0.94` looks like a damping or persistence-style coefficient

This is the best current candidate for Growl's global effect-control block.

## Other FSYS observations

Many subentries have very regular header-like first words and occasional float-like constants such as:

- `0.1`
- `0.08`
- `0.05`
- `0.02`
- very small signed values around `0.001` and `-0.005`

Those are worth remembering, but they are not yet mapped strongly enough to specific Growl passes to drive implementation on their own.

## What The Growl RDC Proves

The Growl capture is much more informative than the FSYS diff.

Relevant contiguous families:

### Early wave family

- program `1055`
- shaders `901 / 904`
- chunks `1708..1749`
- `6` draws
- texture sequence:
  - `3918, 3921, 3921, 3918, 3918, 3921`
- draw mode:
  - `GL_TRIANGLE_STRIP`
- draw counts:
  - all `216`

This maps well to the current six early Growl mesh passes:

- `1076`
- `1085`
- `1092`
- `1101`
- `1108`
- `1117`

### Middle family

- program `1015`
- shaders `854 / 856`
- chunks `1760..1865`
- `16` draws
- texture binding:
  - `3921` on every draw
- draw counts:
  - mostly `50`
  - three `34` draws

Important correction:

- this family is not texture-sampled in the fragment shader
- `growl_1128_shader_856.glsl` shows `0 texgens`
- the fragment shader uses raster alpha and a constant TEV alpha path
- the `3921` texture being bound here does not mean the family is actually texture-driven

So the middle family is best understood as:

- a repeated strip/ribbon family
- using vertex/raster alpha
- not a sampled texture effect

### Late accent family

- program `1103`
- shaders `968 / 969`
- chunks `1877..1925`
- `6` draws
- texture sequence:
  - `3927, 3924, 3927, 3924, 3930, 3924`
- draw counts:
  - `5, 50, 5, 45, 10, 5`

This lines up broadly with the current late Growl stack:

- `1245`
- `1255`
- `1275`
- `1284`

but the source family is still richer than that simplified grouping.

## What The Exported Growl Assets Show

### Early family geometry

The exported early meshes are cumulative:

- `growl_1076_mesh.glb` contains `1` primitive
- `growl_1085_mesh.glb` contains `2`
- `growl_1092_mesh.glb` contains `3`
- `growl_1101_mesh.glb` contains `4`
- `growl_1108_mesh.glb` contains `5`
- `growl_1117_mesh.glb` contains `6`

This strongly supports the current idea that the early family is a staged build-up of wave meshes.

The early PS blocks also show a clean alpha progression through `k[1].a`:

- `1076`: `89`
- `1085`: `89`
- `1092`: `165`
- `1101`: `165`
- `1108`: `228`
- `1117`: `228`

That means the early family is not just different meshes. It also ramps alpha in paired steps.

### Middle family geometry

`growl_1128_mesh.glb` is a real authored strip mesh:

- `78` vertices
- `123` indices
- bounds:
  - `x = -0.20392 .. 0.20392`
  - `z = -12.90956 .. 0.00473`
- per-vertex alpha levels:
  - `0.0`
  - about `0.196`
  - about `0.392`
  - `1.0`

This is important because it means the source already contains a shaped alpha falloff in the mesh itself. The current synthetic `directions_local` approach may be visually close, but it is still a simplification of a captured authored strip family.

### Late sparkle family geometry

The exported VS inputs show:

- `growl_1255_vs_in.csv` is a group of `10` quads
- `growl_1275_vs_in.csv` is a group of `9` quads

So those passes are not single cards. They are small multi-quad sparkle clusters.

## High-confidence Growl conclusions

### 1. Early Growl is probably mostly right already

Evidence:

- six captured draws
- six authored early mesh passes
- cumulative mesh exports
- PS-block alpha steps that match the current staged model well

Worth checking:

- whether the current manifest alpha hierarchy matches the captured `k[1].a` steps closely enough

### 2. The middle Growl family is still the best "maybe we missed something" target

Evidence:

- source uses `16` draws
- source uses a real authored strip mesh
- source strip mesh already bakes alpha falloff
- current implementation compresses that into a single synthetic line-style pass with `16` directions

This does not mean the current Growl is wrong. It means this is the most likely place where the source is richer than our current implementation.

Most plausible missing nuances:

- authored strip shape instead of purely synthetic fan logic
- a more exact per-strip alpha progression
- a more exact staged multiplicity than one synthetic pass

### 3. Late Growl is probably conceptually right, but structurally simplified

Evidence:

- the source late family is `6` draws
- the current manifest models it as `4` conceptual passes
- exported sparkle inputs prove that `1255` and `1275` are multi-quad groups

This suggests our late Growl is close, but there may still be small count/order/coverage differences hidden by the current simplified grouping.

## Best experiments worth trying

These are ordered by likely payoff.

### A. Tighten the early family alpha steps

Reason:

- the RDC gives us exact `k[1].a` values for `1076..1117`
- this is a small, low-risk refinement

Target values from capture:

- pair 1: `89`
- pair 2: `165`
- pair 3: `228`

### B. Prototype a more authored `1128` path

Reason:

- this is the clearest place where the source is richer than our current Growl model

What to try:

- use the exported `growl_1128_mesh.glb` more directly
- preserve its built-in alpha shape
- compare against the current synthetic directional fan

### C. Check whether late Growl needs one or two extra micro-passes

Reason:

- source late family is `6` draws
- current manifest is `4` conceptual passes

What to inspect:

- whether one of the `3927` or `3924` draws in the source is acting like a second small accent layer we currently fold into an existing pass

### D. Only use FSYS for global timing/radius nudges

Reason:

- Growl FSYS is not giving strong per-family differential evidence
- the strongest control block looks global, not per-pass

Most plausible FSYS-backed nudges if Growl ever needs them:

- overall damping / persistence
- angular step / spread hints from `10 degrees`
- radius or displacement pressure from `-3.0` and `-10.0`

## Things We Should Not Claim

- We should not claim the Growl FSYS fully decodes the effect timeline.
- We should not claim `sp1` vs `attack` is giving us a meaningful behavioral split.
- We should not claim the `1128` family is texture-driven.
- We should not claim every late source draw maps one-to-one to the current manifest IDs.

## Recommended Next Step

If we do one more Growl refinement pass later, the best evidence-driven target is:

1. keep the current Growl broad structure
2. tune the early family alpha progression from the captured PS blocks
3. prototype a more authored `1128` middle-family variant
4. compare that against the current Growl before touching late accents

That gives the best chance of finding a real source-backed improvement without destabilizing an effect that is already in a good place.
