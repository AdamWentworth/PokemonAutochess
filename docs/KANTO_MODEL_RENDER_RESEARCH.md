# Kanto Model Render Research

Status: Active
Type: Roadmap
Last updated: 2026-08-12

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

The 2026-08-12 baseline contains 324 selected Kanto manifests representing
139 distinct species/name identities, 1,420 materials, 11 shader families,
and 82 material permutations. All selected manifests were present. The audit
also found 22 of 23 planned capture canaries locally available; the missing
item is the deliberately unselected Sword Pinsir review import.

The checked-in assessment and capture queue live in
`tools/assets/kanto_model_confidence_policy.json`. Generated JSON and Markdown
are written under `artifacts/` and remain untracked. Confidence values are
engineering assessments, not measured image-similarity percentages.

| Source | Species | Models | Materials | Shader families | Permutations | Current | Target |
| --- | ---: | ---: | ---: | --- | ---: | ---: | ---: |
| Scarlet/Violet | 77 | 174 | 726 | Eye, EyeClearCoat, NonDirectional, SSS, SSSEffect, Standard, Transparent, Unlit | 38 | 91 | 97 |
| Legends: Arceus | 10 | 20 | 98 | Eye, Standard, Transparent, Unlit | 12 | 88 | 95 |
| Let's Go | 9 | 26 | 72 | PokeDefaultShader | 3 | 84 | 94 |
| Sword/Shield | 21 | 52 | 290 | PokeDefaultShader | 18 | 79 | 93 |
| Legends: Z-A | 22 | 52 | 234 | Eye, FresnelEffect, IkCharacter | 11 | 70 | 92 |

Permutation counts hash the shader family, transparency state, shader-option
values, and bound texture roles/slots. They measure the implementation space;
they do not imply that every material needs a distinct runtime program.

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

The same exact option-selection workflow now covers the complete selected SV
Kanto corpus: 77 species, 174 manifests, 726 material instances, 38 distinct
permutations, eight shader families, and 19 uniquely selected BNSH programs.
All 38 permutations resolve without a material fallback. The corpus pass also
corrected the metadata decoder's two-word assumption: `Standard` has a
three-word variation table because its shader option slots wrap into a second
32-bit word; the remaining selected families use two words total. Program
identities and source hashes are promoted in
`docs/kanto/evidence/sv_kanto_shader_inventory.json`.

All 19 selected programs are also decompiled offline and summarized in
`docs/kanto/evidence/sv_kanto_selected_program_abi.json`. The hash-verified
compiled ABI spans 18 fragment sampler symbols, eight referenced fragment
constant-buffer symbols, and seven referenced vertex constant-buffer symbols.
This proves resource/interface shape per selected program, while semantic
names and runtime values remain deliberately unclaimed until differential or
data-flow evidence maps them.

Five compiled option permutations map the exact SSS program's material
bindings: base color=`tcb_8` (XYZ), normal=`tcb_C` (XY), roughness=`tcb_10`
(X), AO=`tcb_14` (X), and SSS mask=`tcb_1A` (X), plus two environment cube
resources. This corrects an earlier assumption: Eevee's RoughnessMap contains
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
above. Both exact BNSH programs have null reflection pointers, so no named
sampler or constant-buffer dictionaries survive in the shipped archives.
Remaining scene/light mappings need corroborating static resources
or runtime evidence rather than guessed reflection names.

### Stage 3: Scarlet/Violet reference implementation

Status: exact source program selected for every SV Kanto material permutation;
Eevee body bindings/constants and a first EyeClearCoat binding/constant subset
are mapped offline. Program data-flow for the other 17 selected programs plus
remaining eye and scene/light resources is pending.

Use SV as the modern baseline because its material roles translate most
cleanly. Resolve SSS diffusion, directional fibre response, EyeClearCoat,
additional lighting, local reflections, and thin transparency. The priority
canaries are Eevee, Pikachu, Golduck, Chansey, and Koffing.

For Eevee, replace “directional fibre response” with the now-proven input
contract: scalar roughness plus authored tangent-space normal detail feeding
the SSS program. Next map the anonymous SSS/EyeClearCoat scene buffers,
EyeClearCoat packed/preprocessed inputs, and environment resources before
changing Phlosion's equations again.

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

Status: pending captures; production expansion remains prohibited.

Decode `IkCharacter` from GPU evidence before changing its global material
mode again. Establish the exact order and equations for layer tints, colored
shadows, AO, half-Lambert response, masked specular, local reflections,
diffusion, rim/back-rim, fibre/feather detail, and color processing. Machop,
Pidgeot, Onix, and Kangaskhan are the core canaries. Gastly and the Staryu
family cover displaced/facial overlays and `FresnelEffect`.

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

The Inspector should eventually provide a source-reference lighting preset.
Until that exists, capture metadata must fully describe the Phlosion lights,
environment, camera, exposure, and quality settings.

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

1. Continue static data-flow reconstruction of SV SSS variation 56 and
   EyeClearCoat variation 20: resolve the retained-but-not-directly-sampled eye
   maps, then map scene buffers, array resources, cube resources, and equation
   order. Every Eevee SSS material constant is mapped; EyeClearCoat still has
   roughness, metallic, base/emission color, and layer constants to name.
2. Audit Phlosion's Eevee SSS path against the proven scalar-roughness contract;
   keep any extra fibre/velvet lobe explicitly classified as a visual
   approximation until source evidence supports it.
3. Extract and decompile the 17 selected SV Kanto programs not already covered
   by the Eevee differential, then prioritize static sampler/constant mapping
   by cross-species surface class: fur, scale/skin, metal, eye, transparent,
   unlit/effect, and Standard layered materials.
4. Acquire Sword Nidoran-F and Pinsir evidence to isolate object-space normal
   and light-table behavior.
5. Continue the existing offline Z-A Machop program analysis under the same
   source-parameter/equation discipline;
   do not tune mode 32 until the program, buffers, and draw state are recorded.
6. Add golden canary rendering only after source evidence defines the
   comparison conditions.

A Scarlet runtime capture is optional future evidence, not the current
blocking path. It is only needed for values that loose assets and offline
program analysis cannot establish: bound scene buffers, reflection/exposure,
post-processing, active mips/samplers, and final framebuffer color.
