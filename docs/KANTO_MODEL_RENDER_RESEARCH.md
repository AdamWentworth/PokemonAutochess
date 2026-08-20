# Kanto Model Render Research

Status: Active
Type: Roadmap
Last updated: 2026-08-16

This roadmap owns the research required to reproduce Kanto Pokemon character
models from every retained Switch source as accurately as practical. It does
not change the production source choices in `KANTO_MODEL_SOURCE_AUDIT.md`.
Production imports stay frozen while a source renderer is experimental.

The objective is semantic material parity: Phlosion should use the source
mesh, texture, material, animation, visibility, and lighting inputs for their
authored purposes. Pixel-identical final frames are not required because the
original games' environment, camera, exposure, fog, and post-processing also
affect the image.

## Current Inventory

Run the reproducible audit with:

```powershell
.\tools\assets\audit_kanto_model_materials.ps1 `
  -OutputDirectory .\artifacts\kanto-model-material-audit `
  -RequireAllSelectedModels
```

The 2026-08-15 baseline contains 376 selected Kanto manifests representing
148 distinct species/name identities, 1,640 materials, 11 shader families,
and 88 material permutations. All selected manifests were present. The audit
also found 22 of 23 planned capture canaries locally available; the missing
item is the deliberately unselected Sword Pinsir review import.

The Z-A comparison workspace was expanded after that baseline. Its 2026-08-17
source-research census is tracked independently below and covers 65 species,
212 regular/shiny outputs, 1,084 materials, five shader families, and 20 exact
material permutations. Do not reuse the older 22-species/52-model audit total
as a confidence denominator for the expanded browser corpus.

The checked-in assessment and capture queue live in
`tools/assets/kanto_model_confidence_policy.json`. Generated JSON and Markdown
are written under `artifacts/` and remain untracked. Confidence values are
engineering assessments, not measured image-similarity percentages.

| Source | Species | Models | Materials | Shader families | Permutations | Current | Target |
| --- | ---: | ---: | ---: | --- | ---: | ---: | ---: |
| Scarlet/Violet | 99 | 226 | 946 | Eye, EyeClearCoat, FresnelEffect, NonDirectional, SSS, SSSEffect, Standard, Transparent, Unlit | 44 | 96 | 97 |
| Legends: Arceus | 10 | 20 | 98 | Eye, Standard, Transparent, Unlit | 12 | 88 | 95 |
| Let's Go | 9 | 26 | 72 | PokeDefaultShader | 3 | 84 | 94 |
| Sword/Shield | 21 | 52 | 290 | PokeDefaultShader | 18 | 79 | 93 |
| Legends: Z-A | 65 | 212 | 1,084 | Eye, FresnelEffect, IkCharacter, NonDirectional, Unlit | 20 | 90 | 97 |

Permutation counts hash the shader family, transparency state, shader-option
values, and bound texture roles/slots. They measure the implementation space;
they do not imply that every material needs a distinct runtime program.

The current SV census resolves all 44 permutations and all 946 material
instances from the retained offline shader corpus. Tentacool and Tentacruel's
four `FresnelEffect` materials resolve to exact retained variation 0
(`shader=0x59`, `global=0x0`). Compiled use-site data flow now maps its five
2D material textures, local probe, diffuse irradiance cube, directly used
material constants, and fifth-power Fresnel equation. The authored local-probe
cube payload and final source lighting/framebuffer remain separately gated.

## Evidence Scale

Every interpretation and exception must be assigned one of these evidence
levels:

1. `gpu_capture`: observed shader program, bindings, constants, render state,
   and reference draw from the source game;
2. `controlled_reference_render`: repeatable source and Phlosion images under
   documented pose, camera, animation frame, and lighting conditions;
3. `source_parameter_mapping`: behavior follows decoded parameter names,
   texture roles, samplers, and corroborating models;
4. `source_payload`: data is retained losslessly, but its evaluation is not
   yet proven;
5. `visual_approximation`: visually useful behavior without a proven source
   equation;
6. `unknown`: no sufficient interpretation exists.

A visual approximation must remain narrowly qualified. It may not become a
global rule based only on Pokemon or texture names.

## Architecture Direction

Canonical manifests must retain native evidence even when the runtime cannot
evaluate it yet. Cooking may derive portable textures, but it must not discard
the source roles, parameter values, sampler state, material identity, or
animation/controller provenance needed for later research.

Phlosion's runtime material system should express rendering semantics rather
than game or Pokemon identities. Suitable semantic features include:

- object-space normal evaluation;
- light-table lookup and colored shadow response;
- diffuse wrapping and half-Lambert controls;
- layer-resolved base color, specular, metallic, emission, and masks;
- local reflection, blur, sphere-map, rim, and back-rim response;
- subsurface, fibre, feather, and soft-surface directionality;
- clear coat, eye highlight, iris parallax, and thin refraction;
- displacement and clip/controller-bound material animation;
- source color-processing controls.

OpenGL, D3D12, and Vulkan must consume the same cooked semantic payload. A
backend-specific visual correction is not source interpretation.

## Research Stages

### Stage 1: Reproducible evidence ledger

Status: complete for the initial selected-model baseline.

- Generate the selected model/material/permutation inventory from the asset
  catalog and canonical manifests.
- Record known unknowns, assessed confidence, target confidence, capture
  canaries, and required evidence in a machine-readable policy.
- Fail when selected manifests, required canaries, or source profiles drift.
- Keep production source selection independent from research experiments.

### Stage 2: Character CaptureLab

Status: workflow implemented; first source capture blocked on launchable Scarlet
program/content.

Extend the existing LGPE environment capture methodology to character draws.
For every capture, preserve:

- exact game version, Pokemon/form/gender/appearance, model package, and draw;
- camera, pose, animation clip/frame, location, weather, time, and game
  graphics settings;
- vertex and fragment programs or disassembly;
- texture/descriptors, samplers, uniforms, constant buffers, and vertex
  inputs;
- blend, depth, stencil, culling, render target, and draw order;
- light, shadow, reflection, environment, and color-processing resources;
- source color and useful intermediate targets when available.

Captures belong in the private research depot. Small sanitized reports,
hashes, disassembly excerpts, and reproduction metadata may be promoted to
tracked evidence when licensing and repository policy permit.

The workflow is defined by:

- `tools/research/character_capture_schema.json`: versioned metadata and
  evidence schema;
- `tools/research/validate_character_capture.ps1`: semantic validation against
  the selected canonical model and optional immutable workspace evidence;
- `tools/research/new_character_capture_workspace.ps1`: private workspace
  scaffolding without launching an emulator or editor;
- `tools/research/protect_character_capture.ps1`: stable-file, copy, SHA-256,
  and append-only protection step for RDCs;
- `tools/research/analyze_character_capture.py`: RenderDoc-Python draw
  inventory, selected-event pipeline evidence, bindings, and shader
  disassembly;
- `tools/research/captures/sv-eevee-modern-surface-v1.json`: the first planned
  controlled capture.

The Eevee specification validates against the current SV manifest and defines
neutral, grazing, dark-environment, blink, and shiny states. It intentionally
contains no captured evidence. The complete Scarlet 3.0.1 RomFS is retained,
but CaptureLab currently has no launchable Scarlet NSP/XCI/program identity;
the plan must remain `planned` until that source requirement is satisfied.

An emulator-free static pass is now complete and promoted under
`docs/kanto/evidence/`. It extracted the retained Scarlet SSS and EyeClearCoat
shader archives, decoded their Trinity variation tables, and decompiled the
uniquely selected Maxwell fragment programs offline. Eevee resolves to SSS
variation 56 (`0x41F` / `0x1`) and EyeClearCoat variation 20 (`0x24` / `0x0`).
All 11 distinct decoded texture-role inputs are hash-checked and measured.

The exact-program snapshot now covers all 99 selected SV Kanto species, 226
manifests, 946 material instances, 44 distinct permutations, nine shader
families, and 22 uniquely selected BNSH programs. Every permutation resolves
without a material fallback. The corpus pass also
corrected the metadata decoder's two-word assumption: `Standard` has a
three-word variation table because its shader option slots wrap into a second
32-bit word; the remaining selected families use two words total. Program
identities and source hashes are promoted in
`docs/kanto/evidence/sv_kanto_shader_inventory.json`.

All 22 selected programs are also decompiled offline and summarized in
`docs/kanto/evidence/sv_kanto_selected_program_abi.json`. The hash-verified
compiled ABI spans 18 fragment sampler symbols, eight referenced fragment
constant-buffer symbols, and seven referenced vertex constant-buffer symbols.
This proves resource/interface shape per selected program, while semantic
names and runtime values remain deliberately unclaimed until differential or
data-flow evidence maps them.

The first strict corpus differential pass now proves six semantic bindings
from nine one-option program pairs: SSS roughness=`tcb_10`; Standard
metallic=`tcb_A`, normal=`tcb_C`, roughness=`tcb_10`, and
emission=`tcb_12`; Transparent normal=`tcb_C`. It does not generalize these
mappings to families without direct evidence. Ninety-one requested role
checks remain explicit gaps because an exact one-option counterpart is absent
or the family has no direct enable slot; twelve of those checks explicitly
retain the six material roles on Tentacool's newly recovered program.

Five compiled option permutations map the exact SSS program's material
bindings: base color=`tcb_8` (XYZ), normal=`tcb_C` (XY), roughness=`tcb_10`
(X), AO=`tcb_14` (X), and SSS mask=`tcb_1A` (X). Use-site tracing maps
`tcb_34` as a diffuse-irradiance cube sampled along the mapped normal at LOD 0
and `tcb_36` as a specular environment cube sampled along the reflected view
vector at a scalar-roughness-derived LOD. This corrects an earlier assumption:
Eevee's RoughnessMap contains
authored high-resolution surface breakup, but it is not a two-component
fibre-direction map. Phlosion's additional fibre relief/sheen is a visual
reconstruction and must remain labeled as such.

Four EyeClearCoat permutations prove optional BaseColorMap1=`tcb_1A` (XYZ)
and prove that the highlight option changes equations/constants rather than
adding a texture. Compiled normal reconstruction and the shared material-buffer
layout further prove NormalMap1=`tcb_1E` (XY) with
NormalHeight1=`c7[4].w`. The scalar `tcb_3E` sample uses coordinates projected
from world/scene inputs and is therefore a scene resource, not Eevee's
LayerMaskMap. Eevee's retained BaseColorMap, LayerMaskMap, and NormalMap are
not directly sampled by either selected shader stage; their packing or
preprocessing path remains unresolved.

Constant-buffer data flow now maps all five named Eevee SSS material
parameters: UVScaleOffset=`c8[1].xyzw`, NormalHeight=`c7[4].z`,
SSSMaskScale=`c7[17].z`, SSSMaskOffset=`c7[41].x`, and
SubsurfaceColor=`c8[41].xyz`. It also maps EyeClearCoat
UVRotation=`c7[16].x`, UVScaleOffset=`c8[1].xyzw`, and the NormalHeight1 field
above. The exact `EnableHighlight=False/True` variation 0/20 differential then
isolates the remaining directly used eye material fields:
MetallicClearCoat=`c7[4].x`, RoughnessClearCoat=`c7[7].w`,
BaseColorClearCoat=`c8[18].xyzw`, RoughnessHighlight=`c7[57].w`,
MetallicHighlight=`c7[58].x`, EmissionIntensityLayer5=`c7[9].y`, and
EmissionColorLayer5=`c8[24].xyz`. The only additional vector introduced by
the highlight permutation is `c8[96].xyzw`. Its XYZ components subtract the
interpolated fragment position and normalize into the highlight light vector;
positive W enables that point-light override. Otherwise the program falls back
to the negated dominant directional-light vector in `c4[0].xyz`. This proves a
scene point-light position/enable field, not an authored material parameter.
Both exact
BNSH programs have null reflection pointers, so no named
sampler or constant-buffer dictionaries survive in the shipped archives.
Remaining scene/light mappings need corroborating static resources
or runtime evidence rather than guessed reflection names.

### Stage 3: Scarlet/Violet reference implementation

Status: exact source programs selected for all 44 current SV Kanto material
permutations. Tentacool-family `FresnelEffect` variation 0 is recovered,
decompiled, semantically mapped, and bridged as native material mode 34.
Eevee body bindings/constants and every directly used
EyeClearCoat material constant are mapped offline. All 486 selected
EyeClearCoat materials preserve `NormalMap1`/`NormalHeight1`, and Forge resolves
that proven highlight-normal input into the stable `EyeFinal` catchlight
footprint. Modes 28/30 now transport the complete authored coat/highlight
constant set and evaluate it consistently in OpenGL, D3D12, and Vulkan at every
graphics-quality tier.

This is not a claim of exact final-lighting parity. Feeding `NormalMap1`
directly into Phlosion's generic base-normal path creates eye-wide bands because
the compiled source program combines it with projected/shadow/environment
scene resources. `fp_c8[96]` is now proven as an optional point-light
position/enable field, but its bound source value and the light's color and
intensity are unavailable. Phlosion therefore uses the geometric eye-shell
normal plus a bounded viewer-light reconstruction. Hidden
Eevee canary captures confirm cross-backend parity and stable Low/Ultra
behavior; the authored constants are exact, while the final coat/highlight
equations remain explicitly reconstructed. FresnelEffect now preserves its
primary sRGB and secondary linear color layers, normal/AO inputs, exact
fifth-power alpha response, and local-probe intensity on all three backends.
Its 128px six-face BNTX local probe is now block-linear deswizzled offline and
transported losslessly as the demonstrated RGBA16F runtime alias, including
the authored 0-16 HDR range. All three backends reconstruct and bilinearly
sample the cube directly. Native SSS mode 33 now also evaluates the exact
program's proven diffuse-normal and roughness-filtered reflection environment
roles on every backend. The source scene cubes are unavailable runtime state,
so Phlosion's shared neutral environment supplies both roles without claiming
the source payload. The neutral bridge now carries an explicit exposure/fill
calibration for model review and gameplay while preserving authored AO and
keeping that calibration separate from the proven source equation. The same
calibration slightly restrains the optional fibre-sheen reconstruction so
scalar roughness variation does not become patchy directional glare. Remaining
scene/light resources and other selected-program data-flow paths are pending.

The 2026-08-15 hidden Inspector validation ran Eevee at Low and Ultra and
Bulbasaur at Ultra on OpenGL, D3D12, and Vulkan. Every requested backend stayed
active without fallback; Eevee retained a nonzero Low/Ultra model difference,
the three APIs agreed visually, and the Eevee-only fibre qualifier did not leak
onto Bulbasaur. Follow-up canaries verified Tentacool's raw white jewel atlas,
resolved red material color, and glossy Composite presentation on all three
APIs, while Gastly's Composite smoke/body presentation remained stable. These
are Phlosion regression captures, not source-game visual evidence.

The model Inspector now exposes backend-parity material diagnostics for
Composite, Raw base-color map, Resolved albedo, tangent-space normal,
roughness, metallic, AO, and emission/auxiliary-mask inputs. Composite is the
normal game render. Raw base-color map exposes the stored texture without
authored material tint. Resolved albedo applies the authored color factor with
no lighting; this distinction is required for source materials such as
Tentacool's white jewel atlas multiplied by its regular red or shiny green
constant. Missing inputs display their semantic neutral value (flat normal,
roughness/AO one, metallic/emission zero), rather than the backend's fallback
texture. The override is transient to the prefab preview, is restored before
unrelated rendering, and is supported by D3D12, OpenGL, and both Vulkan
world-scene paths. Hidden captures can select the same view with
`-AssetPreviewMaterialView`; this is an interpretation/debug aid, not
source-game visual evidence.

The Inspector also separates Graphics Quality from an explicit Review Lighting
profile. Neutral Studio is the default low-contrast import-review rig; Source
Bridge preserves the current recovered Composite path; Albedo-biased favors
authored color while retaining restrained gloss/translucency/emission; and
Grazing Check emphasizes surface breakup. The profile is transient view state,
is carried identically through the OpenGL indexed path and D3D12/Vulkan fast
scene paths, and never changes cooked material data or gameplay lighting. None
of these profiles is represented as captured SV scene lighting. The 2026-08-15
hidden validation confirmed the Neutral Studio profile on all three APIs,
verified a fixed profile across Eevee Low/Ultra captures, and retained
Tentacool's resolved red Fresnel jewel.

Use SV as the modern baseline because its material roles translate most
cleanly. Resolve the complete SSS diffusion equation, the remaining
EyeClearCoat scene-resource bridge,
additional lighting and thin transparency.
The priority canaries are Eevee, Pikachu, Golduck, Chansey, and Koffing.

For Eevee, use the now-proven input contract: scalar roughness plus authored
tangent-space normal detail feeding the SSS program. Tentacool's
`FresnelEffect` material inputs, equation, and retained local-probe cube are now
mapped. The SSS environment roles and EyeClearCoat point-light vector are now
mapped too. Continue the remaining SSS/EyeClearCoat scene buffers, light
color/intensity, shadow/projected resources, and complete equation order before
claiming final-lighting parity.

### Stage 4: PokeDefaultShader implementation

Status: pending captures.

LGPE and Sword share the broad `PokeDefaultShader` family but may use different
permutations and resources. Implement source object-space normals, light-table
lookup, sphere maps, packed ambient translation, deep-shadow colors, rim, and
layer compositing as semantic features. Nidoran-F, Tentacruel, Omanyte,
Rattata, and Doduo are the first local canaries. Reacquire or regenerate the
Sword Pinsir review import for its known pale/light-table regression.

### Stage 5: Legends: Arceus optical materials

Status: pending captures.

Resolve projected Eye normals, pupil/iris optical layering, transparent thin
lenses, and layered Unlit fire. Paras, Magnemite, Tangela, and Ponyta cover
the required feature set.

### Stage 6: Legends: Z-A IkCharacter

Status: emulator-free static analysis and runtime qualification in progress;
production promotion remains per-species and per-feature.

The retained Kanto Z-A browser corpus is now exhaustively selected: 65 species,
212 regular/shiny model outputs, 1,084 materials, 20 selected permutations,
and 1,084/1,084 material-to-program selections across `Eye`, `FresnelEffect`,
`IkCharacter`, `NonDirectional`, and `Unlit`. The selected-program ABI contains
11 exact programs. A complete one-option graph covers 183 compiled edges
across 49 options and resolves 171 unique programs with no unresolved option
choices. This proves selected program identity, resource ABI, and option-
controlled resource changes without launching a game or emulator.

The deeply qualified `IkCharacter` reports now use that full browser corpus as
their denominator: 1,036 `IkCharacter` materials across all 212 outputs. The
separate `Eye` and `FresnelEffect` reports retain their own denominators; broad
selection coverage still does not retroactively prove every runtime equation
for a different shader family.

The source-local probe boundary is also materially narrower. Across the broad
corpus, 210/212 models and all 1,032 qualifying material bindings point to one
authored 128px,
six-face, eight-mip BC6H UF16 cube. Forge now block-linear deswizzles and
decodes every face and mip, preserves the decoded RGBA16F payload losslessly in
a deterministic carrier, and records its source and decoded hashes. Phlosion
samples that authored cube on OpenGL, D3D12, and Vulkan using the source
`ReflectionsBlur` LOD. The four Staryu/Starmie `FresnelEffect` materials carry
two separate regular/shiny RGBA16F local probes; they use the already-qualified
single-mip local-specular transport rather than the shared IkCharacter cube.

Static data flow maps all 14 authored `IkCharacter` texture roles used by the
retained corpus, including base, normal, AO, layer, shadow-color, shadow mask,
specular mask, rim mask, local reflection, parallax, highlight, eyelid shadow,
vertex displacement, and Mega Gengar's upward-noise source. It also isolates the dedicated Eye variation 146 and
the FresnelEffect variation 0 sampler/constant subgraphs. The FresnelEffect
program proves the fifth-power Fresnel alpha, roughness-driven cube LOD, and
local-probe intensity.

Dedicated mode 35 now bridges the 428 selected `IkCharacter` eye materials on
all three APIs. Forge shares the existing six-slot material ABI without
discarding data: parallax remains in emission alpha and the authored local-
reflection cube remains unchanged. Compiled operation and output-reachability
proofs now establish the material-local color order for variations 682 and
1214. Variation 1214 first multiplies both layered base and layered shadow RGB
by `1 + eyelidMask * (BaseColorLayer6 - 1)`. Both variations then independently
replace those two color paths with
`EmissionColorLayer5 * EmissionIntensityLayer5` under
`HighlightMaskMap.r`, before shared lighting. Forge bakes that static order
losslessly for the selected corpus, while mode 35 samples the resulting base
and shadow colors after its live parallax offset. The eye programs retain the
same proven ShadowingBias polynomial, squared half-Lambert band, ordered color
process, direct-specular path, and metallic-gated local reflection as the body
program.

This now covers all 4,896 eye texture bindings, including the source shadow-
color path and its powered shadow/specular gate. The promoted analyzer decodes
the cooked PHRC/PHMAT data and verifies all 194 eye-bearing outputs contain the
expected 428 mode-35 eye submesh records, so this claim cannot pass against
importer source while the editor still holds stale cooked materials. The
view-dependent path is now literal as well. Both eye programs compute
`eta=1/ParallaxIOR`, refract the negative camera-view vector through the
normalized tangent frame, scale its projected axes by the normalized absolute
sum of the UV derivatives and `1-(1-|NdotV|)^5`, then march from depth 1 toward
zero. The source schedule is `12-10*|NdotV|` depth layers with
`floor(schedule)+2` samples (4 through 14), a `sampledHeight >= currentDepth`
hit, and the compiled two-sample linear refinement. OpenGL, D3D12, and Vulkan
share that exact local equation. Anonymous scene-light/final-frame terms remain
outside the material-local proof.

The ordinary-body constant-buffer pass now corrects an earlier overclaim in
the research ledger. The four `LayerMaskScale` controls are the registers that
multiply sampled layer-mask RGBA (`fp_c7[10].yzw`/`fp_c7[11].x`); the formerly
labeled registers actually scale the five base/layer emission vectors
(`fp_c7[8].yzw`/`fp_c7[9].xy`). Literal operation signatures additionally pin
`NormalHeight`, `ReflectionsBlur`, and the paired rim-mask intensity path. A
source census records the non-neutral lighting/color controls across all 604
ordinary body materials, and direct BNSH header inspection proves all five
selected programs have stripped reflection dictionaries. Mode 32 now evaluates
the compiled emission final-combine as well: the selected corpus has four
nonzero records. Regular and shiny Staryu `body_00` use white layer 3 at
intensity 0.5, while regular and shiny Mega Raichu X `body_c` use a chromatic
layer 1. Forge packs per-pixel emission luminance into the blue lane of its rim
auxiliary and the exact 24-bit material color into params0.z. Its red/green
linear rim controls are now
sRGB-encoded before the legacy sRGB texture upload so hardware decode returns
the intended values instead of crushing a 0.2 control to roughly 0.03. The
promoted analyzer decodes all 212 PHMAT files and their KTX2 base levels,
verifying every neutral mode-32 emission lane, both Staryu lanes reaching the
exact sRGB byte 188 for linear 0.5, and both Mega Raichu X records retaining
their packed chromatic color. A subsequent output-tail pass proves
all five selected fragments finish with the same exact operation,
`mix(source composite, fp_c10[12].rgb, fp_c10[12].w)`, and that ordinary body
variation 514 and displaced-body variation 594 use a byte-identical fragment
program. The bound meaning and values of that scene-fade field remain unknown.
These findings raise Z-A confidence to 85 by improving body/layer, emission,
rim, and final-composite-boundary certainty; they do not claim that anonymous
scene buffers or final-frame exposure are solved.

The following scene/color-boundary pass expands the ordinary-body register
map from 45 to 62 named fields. It pins the half-Lambert/shadow-band controls,
shadow/GI/diffusion controls, the complete middle/dark color-process register
groups, and the local front-rim offset/contrast path. The latter is exact:
normalize the view-angle domain by `RimLightOffset`, apply cubic smoothstep,
then apply `clamp(x * (1 + 2 * RimLightContrast) - RimLightContrast)`. The old
Phlosion power-law interpretation was therefore incorrect and has been
replaced on OpenGL, D3D12, and Vulkan. The later back-rim, scene-light, mask,
and exposure composite remains only partially reconstructed.

Cross-family analysis establishes a firmer runtime boundary without inventing
missing state. All seven examined forward material fragments--the four
selected `IkCharacter` programs, selected `FresnelEffect`, adjacent `Hair`, and
adjacent `IkStandard`--derive a normalized view vector from
`fp_c5[19].xyz` and finish with the same
`mix(source composite, fp_c10[12].rgb, fp_c10[12].w)` scene fade. Three exact
`ReceiveShadow` one-option edges compile to identical fragment programs, so
that choice is supplied through shared scene/draw state rather than a material
equation switch in the selected permutations.

The adjacent Z-A tone-map program also proves the final operation order:
sample scene color, apply its screen-space source blend, exponential exposure,
log-encoded 3D color-LUT sampling, piecewise sRGB output transfer, then a final
3x3 color transform plus RGB offset. This does not provide the bound exposure,
3D LUT payload, color-matrix values, render-target format, or presentation
state. Those are source-runtime values absent from the retained loose assets,
so Phlosion keeps those terms neutral. Together with the literal
AO/shadow/specular pass, this raises the emulator-free Z-A interpretation
confidence to 89/100; it is not visual framebuffer parity.

The next body-lighting pass closes the largest remaining material-local gap.
Compiled operation and dependency proofs establish that `ShadowingBias` is
`clamp(x + bias * (x^2 - x))`, not a power curve; `HalfLambertBias` squares
into the symmetric shadow-band endpoints; and the back rim reuses the
contrast-remapped front-rim shape behind
`smoothstep(clamp((0.4 - NdotL - NdotV) * 2.5))`. The middle and dark areas
each use clamp, cubic smoothstep, and the same symmetric contrast remap. Their
HSV targets are independently proven to depend on `MidAreaHueOffset` and
`DarkAreaHueOffset`, and their final cross-blend order is now literal on all
three APIs. `HueShiftBias` is no longer misused as an arbitrary hue strength:
the compiled program uses it as a floor inside the metallic-gated local-
reflection branch. Direct specular follows the layer-resolved specular
intensity path, while ordered metallic separately gates the local probe.
The compiled diffuse path also proves that `ShadowingGIGain` scales the RGB
difference between the unshadowed diffuse color and its AO-resolved shadow
color. That shadow RGB is an absolute destination color. OpenGL, D3D12, and
Vulkan now evaluate `mix(albedo, shadowColor, shadowAmount *
ShadowingGIGain)`. The former multiplication by a white-to-shadow-color tint
applied albedo a second time, which exaggerated eye-socket and mesh-edge bands
even after the retained 0.5 gain was restored. The corrected operation removes
that invented darkness without weakening the normal map or eye parallax.

At this restore point the loose archive still omitted the source scene-light
values entering the middle/dark domains and the bound shadow resources.
Phlosion therefore used its normalized review light and neutral shadow
visibility while preserving the now-proven local order. A bounded headless recook
also removed 64 obsolete feather-profile values from the reserved mode-32 lane.
The promoted audit now decodes all 52 PHMAT files and verifies all 184 body
records against their manifests: fourteen authored params0-3 scalar lanes per
record plus neutral runtime-only lanes. This raises emulator-free Z-A
interpretation confidence to 92/100. It remains an engineering-confidence
assessment, not measured pixel similarity or source-framebuffer parity.

The subsequent eye-composite pass proves the literal eyelid-then-highlight
order described above for both eye programs, removes the former eye-only
shadow curve and late additive glint from all three APIs, and recooks all 52
selected Z-A models. The promoted evidence now maps ten eye material-buffer
fields and verifies the new payloads for all 80 mode-35 records. That closes
the largest remaining material-local eye ambiguity and raises emulator-free
Z-A interpretation confidence to 94/100. At that restore point, the score
remained below the 95 target because the source parallax march, anonymous scene
inputs, exposure, and final framebuffer transfer were not yet literal.

The following parallax pass closes that remaining eye-local gap. Static
analysis of variations 682 and 1214 proves the vertex/fragment world-position,
normal, tangent, handedness, UV, and camera-position interface; the reciprocal-
IOR refraction; derivative footprint; fifth-power view fade; exact 4-to-14
sample schedule; reverse depth walk; native hit test; and native refinement.
Mode 35 now implements those operations on all three rendering APIs. This is a
shader-only correction: the already lossless `ParallaxMap.r` carrier in
emission alpha and the material ABI did not change, so no recook was needed.
It raises emulator-free Z-A interpretation confidence to 95/100 and eye/
expression confidence to 97/100. The next gap is scene-light and final-
framebuffer composition, not missing or approximate material-local eye math.

The following scene-light pass resolves the equation side of that boundary.
All seven forward programs use the same scene-shadow shape: one projected 2D
mask, a 16-tap cascaded depth-array filter with `1/16` tap weights, and a
companion integer texel-tag array. Their exact merge is
`cascadeVisibility * (1 + projectionWeight * (projectedMask - 1))`.
All four selected `IkCharacter` fragments then multiply this combined
visibility into wrapped N.L before `ShadowingShift`. The middle/dark input is
also no longer anonymous: it is `max(directDiffuse.rgb)` after three inverse-
pi scene-light channels and shadow composition. A complete selected-material
census shows that all 226 forward materials declaring `ReceiveShadow` request
it enabled; the other eight standalone `Eye` materials do not declare that
option. Its identical one-option fragments confirm that binding those scene
resources remains draw/runtime work, not a material-program permutation.
The direct-light branches use a separately proven effective visibility:
`clamp(combinedVisibility + fp_c7[97].w^2, 0, 1)`. The operation and its use
sites are exact, but stripped reflection leaves `fp_c7[97].w` conservatively
classified as an anonymous shadow-bypass scalar rather than assigning it an
invented source name.

Phlosion now stages both consumers behind one explicit scene-shadow visibility
boundary on OpenGL, D3D12, and Vulkan. Because the retained archive has no
bound shadow textures, cascade transforms, or scene-light RGB/intensity, that
boundary remains neutral `1.0` and the anonymous bypass remains neutral `0.0`;
under normalized unit-white light,
`biasedLambert * visibility` is the exact counterpart of the source maximum-
RGB driver. This intentionally preserves the current Inspector image while
removing the former structural mismatch and preventing the unrelated LGPE
projected-shadow format from being reused by accident. The pass raises
emulator-free Z-A interpretation confidence to 97/100 and complete-lighting
confidence to 94/100. It still does not claim source framebuffer parity.

The follow-up environment pass resolves the remaining `fp_c4` address math.
All seven forward programs use `-fp_c4[0].xyz` as the dominant fragment-to-
light vector and sample the diffuse-irradiance cube at LOD 0 with
`vec3(normal.x, normal.y, -normal.z)`. All four selected `IkCharacter`
programs select direct radiance from `fp_c4[1 + lightIndex].rgb / pi`; their
base diffuse-environment term is the sampled irradiance times
`(1 - layeredMetallic)`, `fp_c4[41].rgb`, `fp_c3[28].x`, `fp_c4[26].rgb`,
`fp_c4[27 + lightIndex].rgb`, and `1/pi`. Separately, symbolic operation
recovery proves the material-local probe direction is
`reflect(-view, mappedNormal)`. Its source max-component normalization is
homogeneous for cube lookup, and it has no diffuse-cube Z flip or anonymous
scene-vector input. Phlosion now names and regression-tests that distinction on
OpenGL, D3D12, and Vulkan without inventing the unavailable irradiance payload.

This is not yet a literal implementation of the complete Z-A shader. The
middle/dark and shadow insertion equations, indexed scene-light layout,
environment directions, off-screen Pokemon directional-light transform,
category intensities, and both global HDR probe payloads are now recovered
exactly as source data. Their transform-to-buffer convention and carrier
exposure remain calibrated rather than captured. The projected/cascaded shadow
textures and transforms, final color-grading LUT, and final framebuffer
transfer remain open. Every selected
`IkCharacter` material disables the optional
`EnableHairSpecular` branch, so mode 32 no longer fabricates a species-classified
fur/feather lobe or grafts an SV roughness atlas into Z-A. Every cooked mode-32
record carries neutral alpha in that former auxiliary lane. Fur and feather
relief therefore comes only from the selected source program's real base,
normal, shadow, specular, and rim inputs. Raw rim values also remain in the
asset; the local front- and back-rim shapes are exact, but the unresolved 0.25 review
calibration remains explicit presentation-side code on all three APIs rather
than irreversible asset data.
Machop, Pidgeot, Onix, and Kangaskhan remain the core visual canaries; Gastly
and the Staryu family cover displaced/facial overlays and `FresnelEffect`.

The 2026-08-17 serious engineering pass corrected the scope error behind the
former whole-source 97/100 claim. The broad 212-output corpus is now the
denominator, while 97 remains only the material-local confidence of the
qualified IkCharacter subset. The pass added `NonDirectional` and `Unlit` to
the exact program registry, expanded the graph to all 20 selected
permutations, measured every mip of the shared local probe, and restored the
compiled LOD-0 diffuse-irradiance branch through an explicit, measured offline
exposure bridge. The bridge is not source framebuffer proof because the bound
scene cube and exposure remain unavailable.

It also removed two concrete runtime interpretation defects. Z-A Gastly's
IkCharacter smoke now keeps its already ordered base composite, separately
bakes the authored shadow-color/rim response, applies the literal
`ShadowingBias=1` polynomial and 0.3465..0.3535 shadow band, and no longer
consumes its uniformly zero vertex alpha as opacity. D3D12 mode 27 now carries
camera position/forward/target in otherwise-unused projected-shadow rows,
preserving all authored layer-color vectors; OpenGL and Vulkan use the same
light/view domains. Hidden Ultra captures match between OpenGL and Vulkan for
the restored smoke contract, and the HLSL path compiles and is covered by the
constant-packing test.

The 2026-08-18 full-corpus material pass found and corrected a second systemic
scope error: the 212-output audit denominator had been expanded, but the color
bake itself still used an approximation inherited from the original 52-model
sample. The compiled variation-514 program does not begin with a fully weighted
base and normalize overlapping coverage. It begins with
`clamp(1 - sum(layerMask.rgba), 0, 1)`, gates the base and each ordered layer by
`1 - EmissionIntensity`, and applies `1 - BaseColorDarkness` after the ordered
composite. Forge now follows that order exactly. The old approximation was the
cause of ZA-wide harsh facial/body bands, especially on Machop, and made
Bulbasaur's layered color response compare much worse than its SV counterpart.

The 2026-08-19 shadow-stack pass found the matching omission in the packed
shadow-color bake. Variation 514 gives its AO-resolved base shadow the same
`clamp(1 - sum(layerMask.rgba), 0, 1)` residual coverage as base color and
multiplies it by `max(1 - EmissionIntensity, 0)`. Each ordered layer shadow is
likewise multiplied by `max(1 - EmissionIntensityLayerN, 0)` before its mask
lerp. Forge had instead started from a full-strength base shadow, leaving dark
base residue in filtered layer boundaries. Forge now follows the compiled
coverage/gate order. The renderer also no longer adds the provisional
`normalDetailDelta` multiplier: the mapped normal already drives the compiled
Lambert, shadow, color-process, specular, and rim terms, so that extra response
was applying normal-map darkening a second time. Together these changes remove
the artificial eye-socket, facial, and body-edge bands without weakening the
authored normal map.

The follow-up runtime audit found a separate final-composite error: Forge's
packed shadow RGB is the layer- and AO-resolved absolute color, but each backend
had consumed it as a multiplicative tint. The source-proven difference is now
implemented literally by interpolating from layered albedo to packed shadow
RGB. This was the remaining broad double-darkening around Machop's eye sockets
and other pale Z-A contours. A simultaneous native-geometry audit removed the
old tangent-only `(x,z,-y)` conversion: extracted normals and tangents already
share the declared `.phmodel` basis, and preserving that orthogonal frame keeps
normal relief aligned across Z-A, SV, PLA, LGPE, and Sword native imports.

The same pass closes the paired direct-specular input that the earlier runtime
work had left out of the bake. The compiled path raises
`SpecularMaskMap.r` by `SpecularMaskMapValue`, multiplies it by
`ShadowingColorMaskMap.r`, and only then applies the ordered material
intensity. Bulbasaur's body materials deliberately bind a black shadowing mask,
so the prior bridge was inventing gloss where the source requested none. The
full corpus now includes variation 1650 as well; its four Mega Gengar body
materials and `NoiseSourceMap` are retained, but animated object-space upward-
noise emission remains a named runtime gap rather than a fabricated effect.

The subsequent source-stage pass recovered the retained UI presentation
package `spl_ui_offscreen_poke.trlgt.trpak`, which is more authoritative than
trying to tune a generic studio rig by eye. Its scene record proves a fixed
directional transform of -40.5 degrees X / -36 degrees Y, an IkCharacter
category-6 direct intensity of 4.2, category-6 GI intensity of 1, and a black
category-6 scene rim. It also names and ships `probemain_diffuse.bntx` and
`probemain_specular.bntx`: 64-pixel BC6H cubes with one and seven mips,
respectively. Their lossless decoded half-float payloads are carried in the
same deterministic RGBA8 format already used for local source probes. The
Inspector's **Z-A Source Stage** profile binds both exact cubes and uses the
source-derived light/category equation on OpenGL, D3D12, and Vulkan. The current
lighting equation consumes the recovered diffuse cube; the recovered global
specular cube is transported and bound for the remaining source-specular pass,
while material specular still samples each material's authored local-reflection
cube. Runtime probe PNGs remain private generated assets; committed hashes,
topology, light values, extraction code, and regression checks make the result
reproducible.

A fixed Machop front/back Inspector audit on 2026-08-19 resolved one retained
transform boundary that the source record alone could not name. The retained
scene record and imported model shading basis use opposite Z handedness;
carrying the source Z component through unchanged backlit the character front
and made the rear uniformly bright. The Source Stage path now applies the
required Z-handedness conversion on OpenGL,
D3D12, and Vulkan. Resolved-albedo captures stayed clean through the comparison,
confirming that this correction belongs to presentation lighting rather than the
layered texture cook.

The texture census still establishes a real source-content difference from
SV. Across the 41 Kanto species retained from both titles, Z-A base color has
a median area of roughly 0.26 megapixels versus SV's roughly 1.05, Z-A ships
no corresponding per-model roughness atlases, and Z-A instead leans heavily
on normal, layer/AO, specular, rim, and scene-light data. The source-stage
recovery fixes a major Phlosion interpretation omission; it does not create
SV-resolution base detail that Z-A does not contain.

Bulbasaur is the controlled cross-title canary for that boundary. Its Z-A
package carries the same logical body normal maps as SV at 512 square instead
of 1024 square; their tangent-space X/Y amplitude remains comparable, both
materials retain `NormalHeight=1`, and hidden mapped-normal captures produce
the same correctly oriented world-space response. The cooked Z-A KTX2 base
levels also reproduce their decoded PNG sources byte-for-byte, while the
native normal/tangent frame remains orthogonal and effectively identical to
the SV mesh. A normal-disabled hidden A/B capture therefore changes the Z-A
composite only by the modest broad relief actually present in those maps.

The dense skin stippling and bulb striations visible in the SV Inspector
composite instead live predominantly in SV's dedicated 1024-square body
roughness atlases and the mode-33 SSS surface response. The raw Z-A Bulbasaur
package contains no roughness BNTX, its `IkCharacter` material exposes no
roughness sampler, and it selects `EnableHairSpecular=False`. Do not amplify
the Z-A normal, synthesize roughness, or graft the SV atlases into the Z-A
comparison entry to force visual similarity: those operations would create a
hybrid material rather than recover omitted Z-A data. Production continues to
select SV Bulbasaur; the Z-A entry remains an honest source-comparison model.

Machop is now an explicit regular/shiny canary rather than a subjective proxy
for the whole shader family. Both source manifests partition the model into
two mode-35 eyes and one mode-32 body. The body retains 1024-square base,
normal, AO, layer, specular-mask, and rim-mask inputs plus the eight-mip local
reflection cube; both 128-square eyes retain normal, layer, and parallax data.
The source selects `EnableHairSpecular=False`, supplies no `RoughnessMap`, and
binds the black `ShadowingColorMaskMap` that the compiled direct-specular
equation multiplies into the final lane. The fresh PHMT/KTX2 audit therefore
requires all six regular/shiny eye/body packed specular alpha lanes to remain
zero while retaining mode `[35, 35, 32]` and flag value 14. The runtime also
uses Machop's retained `ShadowingGIGain=0.5`, the source-exact residual shadow
coverage/emission gates, absolute shadow-color interpolation, and only the
normal-driven lighting already present in the compiled response. Together
these establish a smooth matte Z-A Machop
with live iris parallax and without artificial eye or body bands; adding PLA
eye gloss or an SV roughness/fur response would be a cross-title fabrication,
not a better Z-A decode. Hidden Low/Ultra captures pass on OpenGL, D3D12, and
Vulkan. The final Z-A LUT/
exposure boundary remains unknown, so this is a material/cook fidelity claim
rather than pixel-perfect final-frame parity.

Current emulator-free confidence is therefore intentionally split:

| Z-A interpretation area | Confidence | Remaining boundary |
| --- | ---: | --- |
| Program/material selection | 99 | No unresolved selected permutation |
| Qualified IkCharacter material-local math | 98 | Scene shadow resources; Mega Gengar upward-noise runtime branch |
| Eye/parallax material-local math | 98 | Scene shadows and final presentation |
| Shared local-reflection payload/LOD | 98 | Final source-frame exposure calibration |
| NonDirectional/Unlit broad-corpus runtime | 84 | Per-permutation output equations still need promotion-level tracing |
| Off-screen Pokemon scene lighting | 92 | Direction/exposure buffer convention, projected/cascaded shadows, and final LUT/transfer remain |
| Final framebuffer | 87 | Final LUT, screen-space passes, and output transform remain |
| Whole 212-output Z-A browser corpus | 91 | Weighted engineering assessment, not pixel similarity; broad NonDirectional/Unlit work remains |

Z-A may only replace a production source when the affected shader features
pass the source-comparison gates below. A good mesh or animation graph does
not waive material qualification.

## Comparison Matrix

Each canary needs fixed captures for:

- neutral front lighting;
- grazing directional lighting;
- a rotating directional light sequence;
- dark and bright environments;
- regular and shiny appearances;
- representative eye, visibility, and material-effect animation frames;
- Low, Medium, High, and Ultra Phlosion quality;
- OpenGL, D3D12, and Vulkan.

The Inspector's material views can isolate cooked channel/binding drift across
the three APIs, and its reproducible review profiles remove ad hoc lighting
changes from Low/Medium/High/Ultra comparisons. **Z-A Source Stage** is a
source-derived light/probe preset for the retained Z-A package; the scene
record and probe payloads are exact, while the transform-to-buffer convention
and carrier exposure remain explicit calibrations. It is not a claim that the
still-missing final LUT, screen-space shadows, and output transfer have been
recovered. Capture metadata must record the review profile,
environment, camera, exposure, quality setting, and selected material view.

## Promotion Gates

A source feature may move from experimental to qualified only when:

1. its source evidence and interpretation are documented;
2. all materials sharing the affected semantic feature are inventoried;
3. all relevant canaries pass fixed-pose and animated review;
4. regular, shiny, form, and gender identities remain correct;
5. Low through Ultra change detail without changing identity, palette, eye
   state, visibility, or material boundaries;
6. OpenGL, D3D12, and Vulkan agree within the renderer parity tolerance;
7. automated tests prevent leakage onto unrelated materials;
8. broad heuristics have been replaced with shader-option, texture-role, or
   captured-permutation evidence.

Confidence changes must cite newly acquired evidence. A visual improvement by
itself is not sufficient to raise the source score.

## Immediate Next Work

1. Promote `NonDirectional` and `Unlit` from exact selection/ABI evidence to
   the same per-equation runtime coverage currently held by IkCharacter.
2. Recover Z-A's remaining projected/cascaded shadow payloads and transforms
   around the now-exact source light/probe stage. Direct light, category
   intensity, global probes, and the category-6 scene-rim state are closed.
3. Recover the final color-grading LUT and output transfer. The source UI-light
   record proves gamma 1, tone-map scale 1, bloom disabled, and eye adaptation
   disabled, but those values do not by themselves identify the final LUT.
4. Continue emulator-free cross-family tracing of the remaining direct/
   diffuse scene constants and environment-vector construction. The middle/
   dark scalar is already proven as max direct-diffuse RGB and every declaring
   selected forward material requests ReceiveShadow; do not regress either
   boundary to guessed material semantics.
5. Run fixed-profile Inspector review on Machop, Pidgeot, Onix, Kangaskhan,
   Kakuna/Beedrill eyes, Gastly displacement/face overlays, and Staryu/Starmie
   jewels across Low through Ultra and all three rendering APIs.
6. Continue static data-flow reconstruction of the remaining SV SSS and
   EyeClearCoat scene-resource gaps; Z-A research does not reduce the already
   documented SV final-frame boundary.
7. Acquire Sword Nidoran-F and Pinsir evidence to isolate object-space normal
   and light-table behavior.
8. Add golden canary rendering only after source evidence defines the
   comparison conditions.

A Scarlet runtime capture is optional future evidence, not the current
blocking path. It is only needed for values that loose assets and offline
program analysis cannot establish: bound scene buffers, reflection/exposure,
post-processing, active mips/samplers, and final framebuffer color.
