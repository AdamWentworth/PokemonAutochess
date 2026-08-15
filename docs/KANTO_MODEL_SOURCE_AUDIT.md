# Kanto Pokemon Model Source Audit

Status: Active
Type: Reference
Last updated: 2026-08-12

This document is the current source-of-truth for choosing character-model
sources for the original 151 Pokemon. It records decisions, not a blanket rule
that the newest game always wins. A family is promoted only after its native
geometry, regular and shiny materials, animation set, and any visible sex
variants pass the Phlosion import and hidden Inspector checks.

Legends: Z-A is now a fallback source, not a preferred source. Its retained
models are frozen to an explicit catalog allowlist. New Z-A imports require a
case-specific reason and may not be promoted when a qualified Scarlet/Violet,
Sword/Shield, Let's Go, or Legends: Arceus package is available.

The complete backup remains intentionally deferred until all 151 Pokemon are
present, per the project decision. Original packages and derived imports remain
in the private asset depot throughout the work.

## Current Authoritative Sources

| Species | Family | Source | Decision |
| ---: | --- | --- | --- |
| 001-009 | Bulbasaur, Charmander, Squirtle families | Scarlet/Violet | Keep. The current eyes, layered materials, and Charmander-line fire geometry are qualified. Z-A is only a comparison candidate. |
| 010-012 | Caterpie family | Sword/Shield | Keep. Includes male and female Butterfree. |
| 013-018 | Weedle and Pidgey families | Legends: Z-A | Keep. Native Z-A geometry, materials, and animation graphs are qualified. |
| 019-022 | Rattata and Spearow families | Let's Go | Keep. Includes male and female Rattata and Raticate. |
| 023-026 | Ekans and Pikachu families | Scarlet/Violet | Keep. Pikachu and Raichu include male/female regular/shiny outputs; the repaired SV eye response is the accepted baseline. |
| 027-028 | Sandshrew family | Scarlet/Violet | Keep. The redundant GLB has been retired. |
| 029-034 | Both Nidoran families | Sword/Shield | Keep. Nidoran-F and Nidoran-M are separate species, not alternate sex meshes of one species. |
| 035-040 | Clefairy, Vulpix, and Jigglypuff families | Scarlet/Violet | Keep. Ninetales' repaired eye response is the accepted baseline. |
| 041-042 | Zubat family | Legends: Z-A | Promoted from PLA. Z-A supplies distinct male/female geometry and 58/59-clip animation sets. |
| 043-045 | Oddish family | Sword/Shield | Keep. Gloom and Vileplume now include distinct female regular/shiny geometry. |
| 046-047 | Paras family | Legends: Arceus | Keep. The translucent layered-eye treatment is specifically qualified against PLA. |
| 048-062 | Venonat through Poliwrath families | Scarlet/Violet | Keep. This includes the qualified Venomoth eyes and Golduck forehead gem. |
| 063-068 | Abra and Machop families | Legends: Z-A | Promoted from PLA. Z-A restores the closed Abra eyelids, complete Machoke belt, Machamp limb/foot detail, richer LODs, and substantially larger animation graphs. Kadabra and Alakazam include distinct female geometry. |
| 069-071 | Bellsprout family | Scarlet/Violet | Replaces Z-A with the retained native SV family. Regular/shiny materials, eye atlases, and authored animation graphs pass hidden Inspector review. No sex-specific geometry exists. |
| 072-073 | Tentacool family | Sword/Shield | Sword supplies the complete family absent from the local Z-A and SV sources. Both species are unisex because their visible appearance does not vary by sex. |
| 074-076 | Geodude family | Legends: Arceus | Native replacement for the legacy Geodude GLB; the complete family is qualified. |
| 077-078 | Ponyta family | Legends: Arceus | PLA preserves the family’s native layered-Unlit fire meshes and regular/shiny materials. Ponyta and Rapidash share the family recipe; neither source package has distinct female geometry. |
| 079-080 | Slowpoke family | Scarlet/Violet | Replaces Z-A with the retained native SV family. Body, eye, mouth, claw, and Slowbro shell partitions pass hidden Inspector review. Both species are qualified as unisex. |
| 081-082 | Magnemite family | Legends: Arceus | PLA supplies the complete family with native Eye/Standard material separation, regular/shiny palettes, and source-authored airborne placement. Magnemite's neutral eye atlas is source-qualified so its already-addressed pupil tile is not transformed a second time during layer baking. Both species are genderless. |
| 083 | Farfetch'd | Legends: Z-A | Z-A preserves the full body, eye, wing, and leek partitions, explicit shiny materials, and a 117-clip animation graph. No visible sex-specific geometry exists. |
| 084-085 | Doduo family | Let's Go | LGPE is the complete local family source and supplies distinct male/female packages. Doduo rests on the neutral round-eye column of its native mirrored-repeat atlas, while the source animation clips can select the neighboring stern expression dynamically. Male and female regular/shiny outputs are qualified; fixed-pose captures and payload hashes confirm the pairs are not aliases. |
| 086-087 | Seel family | Scarlet/Violet | Native regular/shiny family import with complete eye materials and 77/74-clip animation sets. Both species are unisex. |
| 088-089 | Grimer family | Scarlet/Violet | Native regular/shiny family import with the source-authored layered body, mouth, and eye partitions intact. Both species are unisex. |
| 090-091 | Shellder family | Scarlet/Violet | Native regular/shiny family import with complete shell, tongue, and eye material partitions. Both species are unisex. |
| 092 | Gastly | Legends: Z-A (temporary exception) | The SV mesh and materials now render correctly on all three backends, but its 44-clip payload fails the established tongue timeline: it lacks the two qualified reveal actions retained by the 63-clip Z-A package. Keep Z-A until a non-Z-A animation source or a proven compatible animation bridge preserves that contract. |
| 093-094 | Haunter and Gengar | Scarlet/Violet | Replaces Z-A with the retained native SV models. Both regular/shiny material stacks and authored animations pass hidden Inspector review; Haunter retains source-authored airborne roles. |
| 095 | Onix | Legends: Z-A | Native replacement for the legacy Onix GLB. The Z-A model has 73 clips versus 23 in the retired animation set, and its authored zero-specular stone response is preserved instead of receiving the generic glossy dielectric lobe. |
| 096-097 | Drowzee family | Scarlet/Violet | Native regular/shiny family import with complete eye and body materials. Hypno includes distinct male and female regular/shiny geometry. |
| 098-099 | Krabby family | Sword/Shield | Sword supplies the newest complete local family source, including regular/shiny materials and complete native animation sets. Neither species has distinct female geometry. |
| 100-103 | Voltorb and Exeggcute families | Scarlet/Violet | Native regular/shiny Kanto-form imports. The Hisuian Voltorb family and Alolan Exeggutor are intentionally excluded from these identities. |
| 104-105 | Cubone family | Let's Go | Provisional native regular/shiny family import from the complete local Let's Go packages. Z-A remains the preferred upgrade candidate, but its package payloads are not currently retained locally and cannot yet be reproduced. Neither species has distinct female geometry. |
| 106-107 | Hitmonlee and Hitmonchan | Scarlet/Violet | Native regular/shiny imports with the complete modern material and animation payloads. These male-only species do not have alternate female geometry. |
| 108 | Lickitung | Let's Go | Native regular/shiny import from the complete local Let's Go package. Lickitung has no distinct female geometry. |
| 109-110 | Koffing family | Scarlet/Violet | Native regular/shiny Kanto-form imports. Their airborne locomotion roles are authored, and the `SSSEffect` path preserves both controller-owned effects and each playable clip's per-puff UV/displacement overrides. Every `smokegeom_*` cloud and paired `smokemask_*` plume remains color-rendered and follows its source skeletal expand/travel/contract motion. The source 60 Hz visibility gates remove each pair before its next emission, while `UVScaleOffset3` only scrolls the displacement texture rather than being misread as opacity. Weezing's retained 241-frame `28201` controller emits four paired side-puff cycles over smoke-free idle/body clips. Koffing's one-second `28201` contains the same paired side-cloud motion/material cycle and a continuous gas event, but its `TRACM` fixes every smoke mesh hidden; the import correction restores family-standard B1 frames 10-40 and B2 frames 12-43 visibility, yielding a one-second puff-and-clear idle cycle. Action clips with their own lifecycles take precedence for both species. The same contract is used by OpenGL, D3D12, and Vulkan. Galarian Weezing is intentionally excluded from the canonical Weezing identity. Neither species has distinct female geometry. |
| 111-113 | Rhyhorn, Rhydon, and Chansey | Scarlet/Violet | Native regular/shiny imports with complete modern eye and animation graphs. Rhyhorn and Rhydon include genuinely distinct male/female geometry in all four appearance outputs. Chansey's shared SSS body atlas receives a source-qualified `EnableJewel` approximation so its neutral low-roughness egg retains the pale glossy response instead of shading gray. |
| 114 | Tangela | Legends: Arceus | Native regular/shiny import with 54 source clips. Its clean off-white/black eye expressions use a source-qualified animated-atlas transport; the PLA shader's projected eye-normal sphere is not misread as a portable tangent-space PBR normal. |
| 115 | Kangaskhan | Legends: Z-A | Native regular/shiny base-form import with 11 submeshes, 132 bones, and 63 source clips. The adult and baby geometry/material partitions are both preserved, including the pouch child's dark eye and authored white catchlight; no visible sex-specific geometry exists. |
| 116-117 | Horsea family | Scarlet/Violet | Native regular/shiny imports from the only complete retained family source. Horsea preserves 47 source clips and Seadra preserves 49; their independent left/right EyeClearCoat materials retain continuous pupil motion, skeletal eye shaping, and the authored dedicated blink. Neither species has distinct female geometry. |
| 118-119 | Goldeen family | Sword/Shield | Native regular/shiny imports from the newest complete retained family source. Goldeen preserves 22 clips and an animated eye atlas; Seaking preserves 18 clips and its source-authored static eye surface. Both species include genuinely distinct male/female geometry in all four appearance outputs. |
| 120-122 | Staryu family and Mr. Mime | Legends: Z-A (temporary exceptions) | No retained non-Z-A source package currently covers these models. Their existing jewel, eye, and animation handling remains frozen while replacement packages are acquired. |
| 123 | Scyther | Scarlet/Violet | Replaces Z-A with native SV male/female regular/shiny outputs. Independent eye materials, skeletal eye shaping, and genuinely distinct sex geometry pass hidden review and payload validation. |
| 124 | Jynx | Sword/Shield | Native regular/shiny import with 21 source clips and the authored eye-atlas animation. This female-only species has no alternate geometry. |
| 125-126 | Electabuzz and Magmar | Scarlet/Violet | Native regular/shiny imports with 49/50 source clips, independent left/right EyeClearCoat materials, and complete body material partitions. Magmar retains both source-authored Unlit fire meshes for its head and tail. Neither retained package has distinct female geometry. |
| 127 | Pinsir | Legends: Z-A (temporary exception) | The retained Sword package was imported and reviewed, but its light-table material translation renders substantially paler and loses material separation. Keep the existing Z-A model until the Sword bridge is corrected or another source passes the visual gate. No distinct female geometry exists. |
| 128 | Tauros | Scarlet/Violet | Native regular/shiny Kanto-form import with 50 source clips and complete eyes, horns, mane, and three-tail geometry. Paldean forms are intentionally excluded from the canonical Tauros identity. This male-only species has no alternate female geometry. |
| 129-130 | Magikarp family | Scarlet/Violet | Replaces the Z-A/SV hybrid with wholly native SV male/female regular/shiny outputs. Eye, mouth, fin, scale, whisker, and sex-specific geometry pass hidden review without relying on Z-A material approximations. |
| 131-132 | Lapras and Ditto | Scarlet/Violet | Native regular/shiny imports selected over Sword/Shield after a controlled source comparison. SV supplies the higher-detail meshes, larger modern rigs, and native SSS/EyeClearCoat materials: Lapras preserves 87 clips and Ditto 42. Lapras retains its animated eye atlas, while Ditto's authored skeletal eye and eyelid motion is preserved without inventing an atlas. Both species are genderless. |
| 133 | Eevee | Scarlet/Violet | Native regular/shiny male/female imports with 77 clips and genuinely distinct sex-specific geometry. SV is authoritative because its SSS body material supplies the complete 1024px base-color, normal, scalar roughness, AO, and SSS-mask stack. Static compiled-permutation analysis maps all five inputs exactly; Phlosion's dedicated soft-surface path reconstructs its extra fibre/velvet response over those inputs without tinting the coat or adding generic environmental gloss. Low retains coarsely filtered surface detail, High restores normal detail, and Ultra restores full roughness, AO, and subsurface response. |
| 134-137 | Vaporeon, Jolteon, Flareon, and Porygon | Scarlet/Violet | Replaces every Z-A hybrid with a wholly native SV model/material stack. This removes cross-game roughness and fibre grafts; the retained SV textures, regular/shiny palettes, and authored animations pass hidden review. None has sex-specific geometry. |
| 138-139 | Omanyte family | Sword/Shield | Native regular/shiny imports selected after direct comparison with Let's Go. Both sources use identical geometry, while Sword supplies the later rig and more granular material partitions; Omanyte preserves 18 clips and Omastar 20. Their animated eye atlases and body, tentacle, mouth, and shell materials are qualified. Neither species has distinct female geometry. |

Recipes under `tools/assets/` and the selection in
`config/assets/asset_catalog.json` are the executable authority behind this
table. If the table and catalog disagree, they must be reconciled in the same
change before another family is promoted.

## Z-A Retirement Boundary

Z-A is selected only for the explicit catalog exceptions: Weedle and Pidgey
families; sex-complete Zubat/Golbat and Kadabra/Alakazam families plus Abra
and the Machop family; Farfetch'd; Gastly; Onix; Kangaskhan; Staryu, Starmie,
Mr. Mime; and Pinsir. Z-A outputs for Bellsprout, Slowpoke, Haunter, Gengar,
Scyther, Magikarp, Gyarados, Vaporeon, Jolteon, Flareon, and Porygon remain
reproducible review sources but are no longer selected runtime assets.

The remaining exceptions fall into three actionable groups:

- no retained alternative package: Weedle/Pidgey, Farfetch'd, Onix,
  Kangaskhan, Staryu/Starmie, and Mr. Mime;
- visible female geometry unavailable in the retained PLA alternative:
  Zubat/Golbat and Kadabra/Alakazam (with their family members kept together);
- known replacement regression: Gastly's SV animation payload loses the
  qualified tongue-reveal timeline, while Sword Pinsir currently loses its
  authored light-table material response.

Corpus-wide Z-A shader research is authorized because the same interpretation
must support comparison imports and future games. That research does not widen
the production allowlist: work on a selected exception must still fix its
narrow contract or replace its source, and no additional Z-A model is promoted
without passing the normal source-comparison gate. When the allowlist becomes
empty, remove production-only Z-A compatibility code that is no longer needed;
keep source-agnostic research/import support in the comparison workspace.

## Legacy Z-A Material Response Audit

This section documents the compatibility renderer still required by the
temporary exceptions above. It is not evidence that Z-A is preferred for new
imports.

The selected Z-A species' ordinary non-eye `IkCharacter` body materials now
select material mode 32 by Z-A source profile. Forge preserves the source
layer-resolved shadow color, masked specular strength, AO, rim/back-rim masks,
half-Lambert/shadow parameters, authored AO strength, specular offset/contrast,
metallic response, reflection blur, and diffusion.
All three backends evaluate the source's colored half-Lambert response,
normal-mapped direct specular, authored local-reflection cube, and mask-gated
reflection response. Forge block-linear deswizzles and BC6H-decodes every face
and mip of the shared 128px cube; Phlosion losslessly reconstructs its RGBA16F
radiance and samples the source `ReflectionsBlur` LOD. Low-value dielectric strength is squared so
broad masks do not turn Haunter and other soft bodies into uniformly glossy
objects. Source AO strengths are clamped as blend weights; extrapolating the
values above one had clipped mid-gray facial AO into the dark halos previously
visible around some eyes. Coat sheen now requires an explicit compatible
soft-surface detail atlas. Feather relief is likewise qualified only for the exact Pidgey,
Pidgeotto, Pidgeot, and Farfetch'd body atlases whose authored normal fields
contain plumage strokes; generic body names and specular values never select
it. Both paths add only soft, positive, source-tinted relief, so they cannot
draw a dark facial seam or coat unrelated Haunter, shell, stone, or metal
materials. EyeOptions materials, displaced effects, and Gastly's custom
face/smoke stack remain explicitly outside this path.
The retired Jolteon and Flareon Z-A comparison outputs may still use their
qualified SV scalar roughness atlases during source comparison, but canonical
gameplay now selects wholly native SV models for both species. That evidence is
carried in a neutral-by-default payload lane and cannot leak onto selected
Haunter, shell, stone, or metal materials.
Phlosion's texture uploader generates the mip chain from the cooked KTX2 base
level. The surface programs compare deliberately sharp and coarse filtered
samples of those real chains: the former preserves strand/feather direction in
the Inspector thumbnail, while the latter supplies local relief without a
broad dirty tint. D3D12 packs quality LOD, diffusion, reflection blur, and the
exact surface qualifier together; its generic debug-view override explicitly
leaves that native mode-32 payload intact.
Decoded two-channel normal maps reconstruct tangent-space Z for both blue=0
and blue=255 container sentinels, preserving that relief consistently on
OpenGL, D3D12, and Vulkan.

The retired Z-A Gyarados and Porygon comparison outputs likewise retain their
historical compatible SV roughness bridge for controlled review only.
Canonical gameplay selects the wholly native SV models documented above.

The emulator-free Z-A census resolves all 234 selected materials to 11 exact
permutations and all 144 single-option graph edges without ambiguity. This
substantially raises confidence in resource transport and program selection,
but it does not turn mode 32 into a literal source shader: complete
direct/diffuse/specular/color-process ordering, scene shadow/irradiance
resources, the final rim composite domain, and fur/feather response remain
explicit research gaps.

Eevee is the cross-backend canary because SV's directional roughness and SSS
maps expose both missing fur and unwanted gloss immediately. A fixed hidden Inspector pass validates Low,
Medium, High, and Ultra on OpenGL, D3D12, and Vulkan. Onix, Flareon, Vaporeon,
Gyarados, Porygon, Staryu, and Gastly cover stone, body-layer color separation,
reflective, jewel, and specialized-material regressions.

## Dynamic Eye Expression Audit

The 2026-08-12 audit covered all 326 selected native-model manifests,
including their regular, shiny, and female variants and the two currently
published Pichu variants. These counts describe manifests, not unique species:

| Source mechanism | Variants | Runtime result |
| --- | ---: | --- |
| Authored eye-atlas material animation | 266 | Converted to clip-bound four-component eye UV tracks and verified in the local cook. |
| Authored eye/eyelid skeletal animation without an atlas track | 18 | Already follows the selected skeletal clip through the normal model-pose path. This covers Clefairy, Clefable, Vulpix, Ninetales, Paras, Venomoth, Electrode, Exeggcute, and Ditto regular/shiny variants. |
| Static or embedded eye surface | 42 | No eye-atlas parameter, changing eye-mesh visibility, animated eye/eyelid bone, or morph metadata is authored; these remain static instead of receiving invented expressions. |

The selected 266 atlas-driven variants contain 15,528 non-static eye tracks:
1,352 discrete atlas selectors and 14,176 continuous curves, plus 10,470
static atlas tracks. Discrete selectors carry `hold_source_frame` in PHAN;
continuous pupil/gaze curves stay `linear`. Every audited selected model had a
matching cooked object, and the audit reported zero flags.

The discrete selectors occur in Metapod, Rattata, Raticate, Spearow, Fearow,
Nidorina, Nidoqueen, Oddish, Vileplume, Weepinbell, Tentacruel, Slowpoke,
Magnemite, Magneton, Doduo, Dodrio, Gastly, Gengar, Onix, Cubone, Marowak,
Lickitung, Tangela, Jynx, Magikarp, Omanyte, and Omastar. This is
track-level policy rather than a species allowlist: high-precision curves
remain interpolated even in a model that also contains a discrete selector.
Rattata's `hate01` transition and Dodrio's rounded twelfth-cell EyeB
coordinates are explicit regression cases. Pidgeotto and Sandshrew remain
continuous and protect against an over-broad rational-coordinate tolerance.

Twenty variants require a numbered color-atlas fallback on at least one eye
material: Weedle, Pikachu, Sandslash, Diglett, Dugtrio, Bellsprout, Ponyta,
Chansey, and Pichu forms. Selection is per material: an unnumbered fire or other-surface
channel cannot mask a numbered eye channel. Normal-map UV channels are
explicitly excluded. Doduo's source-qualified neutral transform is
`(2, 1, 2, 0)`, and its authored expression clips retain their alternate atlas
states.

Eye shapes intentionally use the selected source animation clip and playback
time in the Inspector, game, and VFX preview. There is no independent facial
override API yet; add one only when gameplay needs an expression to be forced
separately from its authored animation.

Re-run the complete source/variant audit with:

```powershell
.\tools\assets\audit_kanto_eye_handling.ps1 `
  -OutputDirectory .\artifacts\eye-audit
```

`tools/full_check.ps1` also runs this guard.

## Non-Z-A Replacement Queue

Do not use the staged Z-A candidates for future Kanto imports by default.
Trial sources are chosen in this order: retained Scarlet/Violet when present;
otherwise the newest complete retained Sword/Shield, Let's Go, or Legends:
Arceus package that preserves the required family and female variants. Z-A is
considered only when none of those sources can meet the promotion gate.

Acquire or stage non-Z-A packages for the retirement exceptions before adding
new Z-A assets. Highest-priority gaps are the Weedle and Pidgey families,
Farfetch'd, Onix, Kangaskhan, Staryu/Starmie, Mr. Mime, and sex-complete
Zubat/Golbat and Kadabra/Alakazam packages. Gastly and Pinsir additionally need
their known animation/material regressions resolved before a source switch.

## Female Model Contract

The Kanto species with visible male/female model differences are:

Venusaur, Butterfree, Rattata, Raticate, Pikachu, Raichu, Zubat, Golbat,
Gloom, Vileplume, Kadabra, Alakazam, Doduo, Dodrio, Hypno, Rhyhorn, Rhydon,
Goldeen, Seaking, Scyther, Magikarp, Gyarados, and Eevee.

All twenty-three are qualified in
`tools/assets/kanto_gender_model_policy.json`. Once any of those species enters
the selected asset catalog, validation requires exactly
one male and one female import, each with regular and shiny outputs. It also
requires the female identities to use the canonical `_Female` stem.

Source encodings are not uniform. Z-A can encode a female difference as form 1
with gender 0, while another game may use a gender field. Recipe
`genderLabel` is therefore the semantic contract; numeric form/gender values
remain source provenance rather than cross-game truth.

All twenty-three qualified regular male/female pairs were also checked
against their native payload hashes during this pass; every pair is genuinely
distinct rather than two labels pointing at one geometry payload.

Run the guard directly with:

```powershell
.\tools\assets\validate_kanto_gender_models.ps1
```

It is also part of `tools/full_check.ps1` and CTest.

## Promotion Gate

A source becomes authoritative only when all of these are true:

1. exact source package and version are recorded in a tracked recipe;
2. regular, shiny, and required female variants import successfully;
3. geometry, materials, textures, skeleton, and animation provenance validate;
4. female and male payloads are proven distinct when the source claims distinct geometry;
5. a hidden Inspector capture confirms the important eyes, markings, limbs, accessories, transparency, and grounding;
6. the cooked catalog validates without unowned or duplicate model sources;
7. legacy test coverage is migrated before any replaced GLB is retired.

Current cleanup result: no Pokemon GLBs remain under `assets/models/`.
`pokeball.glb` and the authored Growl meshes remain intentionally required until
their separate replacement decisions are made.
