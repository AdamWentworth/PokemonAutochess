# Kanto Character Material Evidence

Status: Active
Type: Evidence
Last updated: 2026-08-13

This directory promotes deterministic metadata, hashes, and conclusions from
static Switch character-material research. Proprietary model, texture, and
shader payloads remain in the repository's existing asset locations or the
private source depot; this directory does not duplicate them.

`sv_kanto_shader_inventory.json` expands exact source-program selection from
the Eevee fixture to every Scarlet/Violet Kanto model selected by the canonical
asset catalog. It covers 77 species, 174 model manifests, 726 material
instances, 38 distinct material permutations, and all eight selected shader
families. Every permutation resolves uniquely to one of 19 BNSH programs.
The evidence also records the Trinity ABI boundary discovered by the broad
pass: `Standard` uses two 32-bit shader-option words plus one global-option
word (6,074 variations), whereas the other seven selected families use one
shader word plus one global word. Source archive/metadata SHA-256 identities,
RomFS hash identities, packed keys, and selected variation indices are
promoted; proprietary shader bytes remain private.

Exact variation selection proves which compiled program the source material
requests. It does not yet name every program resource or constant, reproduce
scene lighting and blend state, or prove final framebuffer color. Those are
separate static data-flow and optional runtime-evidence stages.

`sv_kanto_selected_program_abi.json` is the next static layer. All 19 selected
programs were extracted and translated offline with hash verification. The
ledger records each fragment/vertex stage's anonymous samplers, sampler types,
static texture-call counts, constant-buffer symbols, constant versus dynamic
indices, and vertex interfaces. Across the corpus it finds 18 fragment sampler
symbols, one declared vertex sampler symbol, eight referenced fragment-buffer
symbols, and seven referenced vertex-buffer symbols. These names are compiled
ABI identities such as `fp_t_tcb_8` and `fp_c7`; semantic material names must
still be proven through controlled option differentials or use-site data flow.

`sv_kanto_program_differentials.json` performs the first corpus-wide semantic
mapping pass. Nine exact source program pairs differ in one texture-enable
option and isolate one sampled fragment symbol. They prove six family-specific
bindings: SSS roughness=`fp_t_tcb_10`; Standard
metallic=`fp_t_tcb_A`, normal=`fp_t_tcb_C`,
roughness=`fp_t_tcb_10`, emission=`fp_t_tcb_12`; and Transparent
normal=`fp_t_tcb_C`. SSS roughness, Standard normal, and Standard roughness
each have two independent selected-program confirmations. The plan also
retains 79 role checks as unresolved because no exact one-option archived
counterpart exists or the family exposes no direct enable slot. Those checks
must not be converted into semantic mappings by guesswork.

`sv_kanto_runtime_bridge.json` closes the loop from those proven bindings to
the selected model manifests and Phlosion transport. It checks 348 authored
material-level uses of the six proven mappings and finds 348 exact runtime
translations with no mismatch. It also audits all 308 selected SSS materials:
every one retains the complete base/normal/roughness/AO/SSS-mask stack, every
mask uses its neutral authored scale/offset, and the bridge carries the mask as
linear scalar data with `SubsurfaceColor`. Native SSS is therefore corpus-wide;
the optional fibre reconstruction remains explicitly limited to `pm0133_*`
instead of leaking into smooth-skinned SSS materials. This is still a static
transport proof, not proof of the source game's final lighting or framebuffer.

`sv_eevee_static_material_report.json` is the first vertical slice. It was
produced without launching a game, emulator, editor, or renderer. The audit
combines:

- the canonical `0133_Eevee_SV.phmodel` material document;
- all 11 distinct decoded texture-role inputs referenced by that manifest;
- the retained Scarlet 3.0.1 `sss.bnsh` / `sss.trsha` pair;
- the retained Scarlet 3.0.1 `eye_clear_coat.bnsh` / `eye_clear_coat.trsha`
  pair;
- offline Maxwell decompilation of the uniquely selected programs; and
- set-difference analysis of five SSS and four EyeClearCoat compiled
  permutations;
- compiled constant-buffer use-site analysis for the named material fields;
- direct inspection of each selected BNSH reflection header; and
- the retained Z-A Eevee manifest as topology/material-role corroboration.

The static option decoder proves that Trinity slot masks store a zero-based
choice index and that `bool1` is the default choice index when a material omits
a system-controlled option. Eevee therefore resolves uniquely to:

- SSS variation 56 (`shader=0x41F`, `global=0x1`);
- EyeClearCoat variation 20 (`shader=0x24`, `global=0x0`).

The SSS permutation differential maps every material texture exactly:
`BaseColorMap=fp_t_tcb_8` (XYZ), `NormalMap=fp_t_tcb_C` (XY),
`RoughnessMap=fp_t_tcb_10` (X), `AOMap=fp_t_tcb_14` (X), and
`SSSMaskMap=fp_t_tcb_1A` (X). Two environment cube resources remain unnamed.
The source body roughness atlas is high-resolution and visibly structured, but
it is not a two-component fibre-direction map. Phlosion's current extra
fibre-relief/sheen response is consequently a visual reconstruction, not
source-proven shader behavior.

The same compiled data flow maps every named Eevee body material parameter:
`UVScaleOffset=fp_c8.data[1].xyzw`,
`NormalHeight=fp_c7.data[4].z`,
`SSSMaskScale=fp_c7.data[17].z`,
`SSSMaskOffset=fp_c7.data[41].x`, and
`SubsurfaceColor=fp_c8.data[41].xyz`. The mask equation is directly visible as
`clamp(mask * scale + offset)` before its RGB subsurface-color multiplication.

The eye differential proves that optional `BaseColorMap1` is
`fp_t_tcb_1A` (XYZ) and that `EnableHighlight` adds no texture binding. The
normal-reconstruction path and shared material-buffer layout also map
`NormalMap1=fp_t_tcb_1E` (XY), `NormalHeight1=fp_c7.data[4].w`,
`UVRotation=fp_c7.data[16].x`, and `UVScaleOffset=fp_c8.data[1].xyzw`.
`fp_t_tcb_3E` is sampled with coordinates projected from world/scene inputs and
modulates a lighting path, so it is a scene resource rather than Eevee's
`LayerMaskMap`. The selected vertex stage has no texture operations. Why the
material document retains `BaseColorMap`, `LayerMaskMap`, and `NormalMap`
without either selected shader stage directly sampling them remains open; a
packing or preprocessing path must not be invented without evidence.
The mapped eye fields are a proven subset: EyeClearCoat's roughness, metallic,
base/emission color, and layer constants still need named use-site mappings.

Both selected BNSH binary-program records have null reflection pointers. The
shipped archives therefore contain no stage reflection headers and no
recoverable sampler or constant-buffer dictionaries. Static data flow remains
useful, but it cannot restore names that were stripped from the archives.

Reproduce the report from the repository root after supplying the private
shader-study directory and optional retained Z-A comparison manifest. First
create the required compiled differentials using the offline extraction command
documented in `tools/research/README.md`, then run:

```powershell
python .\tools\research\analyze_sv_eevee_static_material.py `
  --game-root . `
  --shader-study D:\private\sv-v3.0.1-shader-study `
  --za-manifest D:\private\0133_Eevee_ZA.phmodel `
  --output .\artifacts\character-static-evidence\sv-eevee-modern-surface-v1.json

.\tools\research\validate_sv_eevee_static_material.ps1 `
  -ReportPath .\artifacts\character-static-evidence\sv-eevee-modern-surface-v1.json
```

Use `-PromotedReportPath docs/kanto/evidence/sv_eevee_static_material_report.json`
with the validator to prove a newly generated report matches the promoted
source identities and derived measurements.

This pass does not prove source framebuffer color, scene lights, bound constant
buffer values, reflection probes, exposure/tone mapping, active mip selection,
or runtime anisotropic sampling. Those remain explicit gaps; the lack of a
runtime capture must not be disguised as static proof.
