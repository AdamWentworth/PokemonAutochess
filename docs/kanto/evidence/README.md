# Kanto Character Material Evidence

Status: Active
Type: Evidence
Last updated: 2026-08-15

This directory promotes deterministic metadata, hashes, and conclusions from
static Switch character-material research. Proprietary model, texture, and
shader payloads remain in the repository's existing asset locations or the
private source depot; this directory does not duplicate them.

`sv_kanto_shader_inventory.json` expands exact source-program selection from
the Eevee fixture to every Scarlet/Violet Kanto model selected by the canonical
asset catalog. It covers 99 species, 226 model manifests, 946 material
instances, 44 distinct material permutations, and all nine selected shader
families. Every permutation resolves uniquely to one of 22 BNSH programs.
The evidence also records the Trinity ABI boundary discovered by the broad
pass: `Standard` uses two 32-bit shader-option words plus one global-option
word (6,074 variations), whereas the other eight selected families use one
shader word plus one global word. Source archive/metadata SHA-256 identities,
RomFS hash identities, packed keys, and selected variation indices are
promoted; proprietary shader bytes remain private.

`sv_kanto_material_census.json` is the current, drift-tolerant catalog view.
It covers 99 species, 226 model manifests, 946 material instances, 44
permutations, and nine shader families. The retained offline source resolves
all 44 permutations / 946 materials exactly. The four Tentacool-family
`FresnelEffect` materials select variation 0 of the six-program archive with
packed keys `0x59 / 0x0`. The archive and metadata were extracted from the
retained Scarlet 3.0.1 RomFS by their registered hash identities; no emulator
or game process was used.

Exact variation selection proves which compiled program the source material
requests. It does not yet name every program resource or constant, reproduce
scene lighting and blend state, or prove final framebuffer color. Those are
separate static data-flow and optional runtime-evidence stages.

`sv_kanto_selected_program_abi.json` is the next static layer. All 22 selected
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
retains 91 role checks as unresolved because no exact one-option archived
counterpart exists or the family exposes no direct enable slot. Those checks
must not be converted into semantic mappings by guesswork. The differential
ledger retains Tentacool's BaseColorMap, BaseColorMap1, NormalMap, NormalMap1,
AOMap, and LocalSpecularProbe roles as unresolved by single-option comparison
alone. The later FresnelEffect use-site report below maps those material inputs
without weakening the differential rule.

`sv_kanto_runtime_bridge.json` closes the loop from those proven bindings to
the selected model manifests and Phlosion transport. It checks 936 authored
material-level uses of seven proven mappings and finds 936 exact runtime
translations with no mismatch. Six mappings come from strict compiled-program
differentials. The seventh maps `EyeClearCoat.NormalMap1` to `fp_t_tcb_1E.xy`
through named material data flow and verifies that all four selected programs
sample it. All 486 selected EyeClearCoat materials enable and retain that
source normal, and Phlosion now bridges it with `NormalHeight1` instead of the
older retained `NormalMap`. The audit also checks all 392 selected SSS materials:
every one retains the complete base/normal/roughness/AO/SSS-mask stack, every
mask uses its neutral authored scale/offset, and the bridge carries the mask as
linear scalar data with `SubsurfaceColor`. Native SSS is therefore corpus-wide;
the optional fibre reconstruction remains explicitly limited to `pm0133_*`
instead of leaking into smooth-skinned SSS materials. This is still a static
transport proof, not proof of the source game's final lighting or framebuffer.

`sv_fresnel_effect_static_material_report.json` is the emulator-free
Tentacool/Tentacruel vertical slice. It hash-verifies variation 0
(`shader=0x59`, `global=0x0`) and maps the live program's primary sRGB color,
normal, AO, secondary linear color, secondary normal, local specular probe,
and diffuse-irradiance resources from compiled use sites. It also maps the
directly used color, UV, normal-height, layer-scale, saturation, local-probe,
and Fresnel constants. The recovered alpha response is the exact fifth-power
form:

`mix(FresnelAlphaMin, FresnelAlphaMax, pow(1 - max(NdotV - FresnelAngleBias, 0), 5))`.

Phlosion mode 34 now preserves both differently sampled color layers, the
normal and AO inputs, every authored control used by the bridge, and a
quality-dependent texture LOD on OpenGL, D3D12, and Vulkan. The authored
`LocalSpecularProbe` is a 128px, six-face, one-mip block-linear BNTX resource.
The exporter now losslessly carries its demonstrated RGBA16F runtime alias in
a two-pixel-per-texel PNG atlas, and every backend reconstructs the original
half-float HDR radiance before manual cube sampling. The report verifies the
packed PNG round-trip against the deswizzled payload hash. Hidden editor
captures are Phlosion validation, not evidence of the source game's final
framebuffer.

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
`SSSMaskMap=fp_t_tcb_1A` (X). Use-site data flow maps `fp_t_tcb_34` to diffuse
irradiance sampled along the mapped normal at LOD 0 and `fp_t_tcb_36` to
specular radiance sampled along the reflected view vector at a scalar-
roughness-derived LOD. Their shipped reflection names and bound source-scene
payloads remain unavailable.
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
The exact variation 0/20 `EnableHighlight` differential adds only
`c7[9].y`, `c7[57].w`, `c7[58].x`, `c8[24].xyz`, and the scene vector
`c8[96].xyzw`. Compiled BRDF use sites and the authored parameter schema map
the material fields as `EmissionIntensityLayer5`, `RoughnessHighlight`,
`MetallicHighlight`, and `EmissionColorLayer5`. The shared base path maps
`MetallicClearCoat=c7[4].x`, `RoughnessClearCoat=c7[7].w`, and
`BaseColorClearCoat=c8[18].xyzw`.
`fp_t_tcb_3E` is sampled with coordinates projected from world/scene inputs and
modulates a lighting path, so it is a scene resource rather than Eevee's
`LayerMaskMap`. The selected vertex stage has no texture operations. Why the
material document retains `BaseColorMap`, `LayerMaskMap`, and `NormalMap`
without either selected shader stage directly sampling them remains open; a
packing or preprocessing path must not be invented without evidence.
Every directly used authored Eevee EyeClearCoat constant is therefore named.
XYZ subtracts the interpolated fragment position and normalizes into the
highlight light vector; positive W enables that point-light override. The
disabled branch uses the negated dominant directional-light vector in
`c4[0].xyz`. The remaining eye gaps are the packed/preprocessed legacy maps,
the bound point-light value and color/intensity, projected/shadow/environment
resources, and complete equation order.

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

## Legends: Z-A corpus evidence

`za_kanto_shader_inventory.json` and `za_kanto_material_census.json` cover the
complete retained Kanto Z-A selection: 22 species, 52 manifests, 234 materials,
11 material permutations, and the `Eye`, `FresnelEffect`, and `IkCharacter`
families. All 234 materials resolve uniquely. The selected programs are Eye
146, FresnelEffect 0, and IkCharacter 514/594/682/1214.

`za_kanto_selected_program_abi.json` hash-verifies and inventories all six
selected programs. Their combined static ABI contains 17 fragment sampler
symbols, one vertex sampler symbol, eight fragment constant-buffer symbols,
and four vertex constant-buffer symbols. Direct inspection also proves that
the selected BNSH reflection pointers are null; anonymous scene resources
cannot be renamed from stripped reflection dictionaries.

`za_kanto_option_graph.json` is the exhaustive offline option study. It covers
144 exact one-option edges across 36 options and 133 unique compiled programs.
There are 126 fragment-stage changes, 28 vertex-stage changes, and 101
resource-changing edges, with no unresolved option choices. This is stronger
than guessing from material names: it demonstrates exactly which program and
resource ABI each retained material requests.

`za_kanto_option_dataflow.json` follows all 144 exact edges through conservative
SSA dependency cones to the final vertex and fragment outputs. It hash-verifies
and analyzes 133 programs/266 stages, separating resource changes, material-
buffer changes, equation-only changes, and truly identical compiled output
slices. In particular, it proves the optional HairSpecular branch adds
`fp_t_tcb_1A`, while every selected Kanto IkCharacter material disables that
branch.

`za_ik_character_dataflow_report.json` traces all four selected IkCharacter
programs from sampled resources and constant-buffer fields to final output.
Every one of the 13 authored body resources is output-reachable. The report
now pins the ordinary-body operations that were previously conflated: layer-
mask RGBA is scaled by `fp_c7[10].yzw`/`fp_c7[11].x`, while
`fp_c7[8].yzw`/`fp_c7[9].xy` scale the five emission-color vectors. It also
maps normal strength, the literal local-reflection LOD, and the paired rim-mask
intensity path. It now also maps `OcclusionStrength=fp_c7[99].y`, shadow colors
at `fp_c8[127..131]`, metallic at `fp_c7[1].w`/`fp_c7[2].xyzw`, and the five
layer-resolved specular offset, intensity, and contrast groups. Their compiled
order is pinned as offset subtraction, smoothstep, then
`clamp(x * (1 + 2c) - c)` before intensity multiplication. The cook evaluates
the proven `OcclusionMap * OcclusionStrength` interpolation between base
`ShadowingColor` and `ShadowingColorMap` before ordered shadow layers, rather
than applying a second generic AO-darkening pass. The report inventories every
relevant authored scalar across all 140 body materials. The literal pass also
proves the ShadowingBias polynomial, squared half-Lambert shadow band, back-rim
light/view gate, middle/dark/shadow-process smoothstep and contrast domains,
and ordered hue cross-blend. Backward dependency closure independently ties
the middle HSV target to `MidAreaHueOffset` and the dark target to
`DarkAreaHueOffset`. Ordered metallic gates the local-reflection branch,
`ReflectionsBlur` remains its literal LOD, and `HueShiftBias` floors its shaped
probe channels; direct specular instead uses ordered `SpecularIntensity`. The
report also proves that reflection dictionaries are stripped in all four
selected binary programs. It decodes every selected cooked PHMAT and the
referenced uncompressed KTX2 controls. All 52 files contain the expected 184
mode-32 submesh records: 182 have a zero packed emission lane, while regular
and shiny Staryu alone preserve white layer 3 at intensity 0.5 (linear 0.5,
sRGB byte 188). This additionally guards the linear-to-sRGB encoding required
before the packed rim controls enter the legacy sRGB texture slot. The same
binary audit compares fourteen authored native scalar lanes per record to the
source manifests and verifies all 184 reserved surface and former hair-
auxiliary lanes are neutral.
All selected materials disable `EnableHairSpecular`, so no species-based sheen
or cross-game roughness graft remains in mode 32. The report also proves all
four selected fragment programs end with the exact material-to-scene fade
`mix(source composite, fp_c10[12].rgb, fp_c10[12].w)` and that variations 514
and 594 share a byte-identical fragment program. The selected eye subgraphs
remain mapped:
`ParallaxHeight=fp_c7[5].y`,
`ParallaxIOR=fp_c7[5].z`, `UVScaleOffset1=fp_c8[2].xyzw`,
`UVScaleOffset2=fp_c8[3].xyzw`, `UVRotation2=fp_c7[21].y`, and the two eye UV
centers at `fp_c8[139].xy`/`fp_c8[140].xy`.

`za_local_reflection_static_report.json` verifies every selected
`IkCharacter.LocalReflectionMap` binding end to end. The source is one shared
128px, six-face, eight-mip BC6H UF16 cube. Forge block-linear deswizzles and
decodes all faces/mips, stores the decoded RGBA16F payload losslessly in a
deterministic PNG carrier, and records both source and decoded hashes. The
report reconstructs that payload from the carrier and proves OpenGL, D3D12,
and Vulkan use the authored cube and `ReflectionsBlur` LOD.

`za_ik_character_static_material_report.json` separates exact transport from
the remaining reconstruction. It covers all 222 IkCharacter materials: 140
core-body, 80 eye/parallax, and two displacement materials. All 13 authored
texture roles are decoded and mapped to selected compiled sampler symbols. It
also records the remaining high-value gaps: complete literal IkCharacter
scene-level BRDF and eye-composite order, the missing middle/dark input light
scalar and ReceiveShadow value, the source scene/exposure domain needed to
replace the explicit presentation-side rim calibration, and anonymous scene
resources.

`za_ik_eye_runtime_coverage.json` prevents the eye path from being overstated.
The 80 selected IkCharacter eye materials span 38 models and 928 authored
texture bindings. Dedicated mode 35 consumes 768 bindings: base, normal,
layer-mask, highlight, parallax/refraction, eyelid-shadow, local-reflection,
AO, specular, and the neutral rim carrier. The remaining 160 colored-shadow
bindings are not sampled, but are verified source-neutral for this selected eye
corpus (white color maps with zero mask-map value). Seventy materials have
nonzero parallax height, four use non-unit IOR, 48 request eyelid-shadow maps,
and 24 have a nonzero authored highlight emission. The analyzer also decodes
the shipped PHRC/PHMAT payloads and requires all 38 cooked files to contain the
expected 80 mode-35 submesh records; importer source alone is not accepted as
runtime evidence. This is a source/runtime coverage statement, not a pixel-
similarity estimate or final framebuffer-parity claim.

`za_eye_static_material_report.json` covers Kakuna and Beedrill's eight
dedicated Eye materials and exact variation 146. It proves their base, layer,
normal, and highlight sampler mappings plus the selected option state. Other
retained Z-A eye materials route through the separately audited IkCharacter
eye/parallax variations rather than this dedicated family.

`za_fresnel_effect_static_material_report.json` covers the four Staryu/Starmie
jewel materials and exact variation 0 (`shader=0x159`, `global=0x0`). It proves
the six material samplers, fifth-power Fresnel alpha, roughness-driven cube
LOD, local-probe intensity, and lossless regular/shiny RGBA16F probe transport.
It does not claim the anonymous source scene buffers or final framebuffer.

Reproduce the promoted Z-A reports without launching a game or emulator:

```powershell
python .\tools\research\analyze_za_local_reflection_probe.py `
  --game-root . `
  --engine-root D:\Projects\Phlosion\PhlosionEngine `
  --output .\docs\kanto\evidence\za_local_reflection_static_report.json

python .\tools\research\analyze_za_ik_character_static_material.py `
  --game-root . `
  --engine-root D:\Projects\Phlosion\PhlosionEngine `
  --output .\docs\kanto\evidence\za_ik_character_static_material_report.json

python .\tools\research\analyze_za_ik_character_dataflow.py `
  --game-root . `
  --shader-study D:\private\za-v2.0.0-shader-study `
  --output .\docs\kanto\evidence\za_ik_character_dataflow_report.json

python .\tools\research\analyze_za_kanto_option_dataflow.py `
  --game-root . `
  --shader-study D:\private\za-v2.0.0-shader-study `
  --output .\docs\kanto\evidence\za_kanto_option_dataflow.json

python .\tools\research\analyze_za_ik_eye_runtime_coverage.py `
  --game-root . `
  --engine-root D:\Projects\Phlosion\PhlosionEngine `
  --output .\docs\kanto\evidence\za_ik_eye_runtime_coverage.json

python .\tools\research\analyze_za_eye_static_material.py `
  --game-root . `
  --shader-study D:\private\za-v2.0.0-shader-study `
  --output .\docs\kanto\evidence\za_eye_static_material_report.json

python .\tools\research\analyze_za_fresnel_effect_static_material.py `
  --game-root . `
  --shader-study D:\private\za-v2.0.0-shader-study `
  --output .\docs\kanto\evidence\za_fresnel_effect_static_material_report.json
```
