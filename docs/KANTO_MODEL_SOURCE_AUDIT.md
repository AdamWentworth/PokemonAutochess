# Kanto Pokemon Model Source Audit

Status: Active
Type: Reference
Last updated: 2026-08-11

This document is the current source-of-truth for choosing character-model
sources for the original 151 Pokemon. It records decisions, not a blanket rule
that the newest game always wins. A family is promoted only after its native
geometry, regular and shiny materials, animation set, and any visible sex
variants pass the Phlosion import and hidden Inspector checks.

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
| 069-071 | Bellsprout family | Legends: Z-A | Native Z-A family import, including regular/shiny materials and animation graphs. No sex-specific geometry exists for this family. |
| 072-073 | Tentacool family | Sword/Shield | Sword supplies the complete family absent from the local Z-A and SV sources. Both species are unisex because their visible appearance does not vary by sex. |
| 074-076 | Geodude family | Legends: Arceus | Native replacement for the legacy Geodude GLB; the complete family is qualified. |
| 077-078 | Ponyta family | Legends: Arceus | PLA preserves the family’s native layered-Unlit fire meshes and regular/shiny materials. Ponyta and Rapidash share the family recipe; neither source package has distinct female geometry. |
| 079-080 | Slowpoke family | Legends: Z-A | Z-A supplies explicit regular/rare material graphs, complete layered eyes and shell materials, and 120/116-clip animation sets. Both species are qualified as unisex. |
| 081-082 | Magnemite family | Legends: Arceus | PLA supplies the complete family with native Eye/Standard material separation, regular/shiny palettes, and source-authored airborne placement. Magnemite's neutral eye atlas is source-qualified so its already-addressed pupil tile is not transformed a second time during layer baking. Both species are genderless. |
| 083 | Farfetch'd | Legends: Z-A | Z-A preserves the full body, eye, wing, and leek partitions, explicit shiny materials, and a 117-clip animation graph. No visible sex-specific geometry exists. |
| 084-085 | Doduo family | Let's Go | LGPE is the complete local family source and supplies distinct male/female packages. Doduo rests on the neutral round-eye column of its native mirrored-repeat atlas, while the source animation clips can select the neighboring stern expression dynamically. Male and female regular/shiny outputs are qualified; fixed-pose captures and payload hashes confirm the pairs are not aliases. |
| 086-087 | Seel family | Scarlet/Violet | Native regular/shiny family import with complete eye materials and 77/74-clip animation sets. Both species are unisex. |
| 088-089 | Grimer family | Scarlet/Violet | Native regular/shiny family import with the source-authored layered body, mouth, and eye partitions intact. Both species are unisex. |
| 090-091 | Shellder family | Scarlet/Violet | Native regular/shiny family import with complete shell, tongue, and eye material partitions. Both species are unisex. |
| 092-094 | Gastly family | Legends: Z-A | Native regular/shiny family import. Gastly and Haunter retain source-authored airborne roles. Gastly's body and eye surfaces use narrowly source-qualified clip-depth ordering so the face remains visible through its opaque smoke shell without moving any geometry. |
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
| 120-123 | Staryu family, Mr. Mime, and Scyther | Legends: Z-A | Native regular/shiny imports with 120, 119, 58, and 119 source clips respectively. Staryu and Starmie's FresnelEffect jewels use a source-qualified portable PBR approximation that retains the regular red/pink and shiny blue BaseColor constants instead of exporting the neutral white carrier texture. Mr. Mime and Scyther preserve independent left/right animated eye materials and skeletal eye shaping. Scyther includes genuinely distinct male/female geometry in all four appearance outputs; the others have no sex-specific geometry. |
| 124 | Jynx | Sword/Shield | Native regular/shiny import with 21 source clips and the authored eye-atlas animation. This female-only species has no alternate geometry. |
| 125-126 | Electabuzz and Magmar | Scarlet/Violet | Native regular/shiny imports with 49/50 source clips, independent left/right EyeClearCoat materials, and complete body material partitions. Magmar retains both source-authored Unlit fire meshes for its head and tail. Neither retained package has distinct female geometry. |
| 127 | Pinsir | Legends: Z-A | Native regular/shiny import with 65 source clips and complete body, horn, mouth, and independent eye partitions. No distinct female geometry exists in the source package. |
| 128 | Tauros | Scarlet/Violet | Native regular/shiny Kanto-form import with 50 source clips and complete eyes, horns, mane, and three-tail geometry. Paldean forms are intentionally excluded from the canonical Tauros identity. This male-only species has no alternate female geometry. |
| 129-130 | Magikarp family | Legends: Z-A | Native imports with 118/117 source clips, complete eye, mouth, fin, scale, and whisker partitions, and genuinely distinct male/female geometry. Gyarados keeps the richer Z-A rig/material partition while using SV's authored roughness maps; matching base-color hashes prove the male and female Z-A/SV UV layouts are identical. Male and female regular/shiny outputs are all qualified. |
| 131-132 | Lapras and Ditto | Scarlet/Violet | Native regular/shiny imports selected over Sword/Shield after a controlled source comparison. SV supplies the higher-detail meshes, larger modern rigs, and native SSS/EyeClearCoat materials: Lapras preserves 87 clips and Ditto 42. Lapras retains its animated eye atlas, while Ditto's authored skeletal eye and eyelid motion is preserved without inventing an atlas. Both species are genderless. |
| 133-137 | Eevee family and Porygon | Legends: Z-A | Native regular/shiny imports selected over SV for the richer Z-A rigs, masks, and animation graphs: Eevee, Vaporeon, and Jolteon preserve 113 clips, Flareon 57 distinct behaviors, and Porygon 58 clips. Z-A's IkCharacter eye layer-5 masks are baked as authored white catchlights. Eevee through Flareon use the dedicated soft-coat path: layer-resolved shadow tint, source half-Lambert response, restrained rim/back-rim, normal detail, AO, and near-zero masked specular replace the plastic generic-PBR lobe. Porygon retains PBR lighting with SV's UV-identical authored roughness. Male and female Eevee regular/shiny geometry is genuinely distinct; the evolutions and Porygon have no sex-specific geometry. Porygon's static eye surface remains static as authored. |
| 138-139 | Omanyte family | Sword/Shield | Native regular/shiny imports selected after direct comparison with Let's Go. Both sources use identical geometry, while Sword supplies the later rig and more granular material partitions; Omanyte preserves 18 clips and Omastar 20. Their animated eye atlases and body, tentacle, mouth, and shell materials are qualified. Neither species has distinct female geometry. |

Recipes under `tools/assets/` and the selection in
`config/assets/asset_catalog.json` are the executable authority behind this
table. If the table and catalog disagree, they must be reconciled in the same
change before another family is promoted.

## Z-A Material Response Audit

The 37 selected Z-A species comprise 90 regular, shiny, and required female
variants. Their ordinary `IkCharacter` body materials preserve both the source
`SpecularMaskMap` and `SpecularIntensity` instead of falling back to a uniform
generic-PBR highlight. Near-black masks remain matte, while stronger authored
surfaces retain their own spatial response. EyeOptions materials and Gastly's
custom face/smoke stack remain explicitly outside this path.

The Eevee family needs more than the generic specular correction. Its Z-A
materials have a soft-coat lighting contract, so Forge additionally bakes the
layer-resolved shadow color and rim/back-rim masks, transports half-Lambert and
shadow-strength parameters, and selects material mode 32. The backends multiply
the shadow tint into albedo rather than replacing albedo with it; this keeps
Eevee matte without bleaching Flareon's orange or Vaporeon's blue. Hard-surface
Z-A bodies such as Onix, Staryu, Gyarados, and Porygon stay on their established
PBR path. Gyarados and Porygon supplement that path with SV roughness maps only
where identical base-color hashes prove exact Z-A/SV UV compatibility.

Eevee is the cross-backend canary because its low-valued fur mask makes the old
gloss immediately visible. A fixed hidden Inspector pass validates Low,
Medium, High, and Ultra on OpenGL, D3D12, and Vulkan. Onix, Flareon, Vaporeon,
Gyarados, Porygon, Staryu, and Gastly cover stone, soft-coat color separation,
reflective, jewel, and specialized-material regressions.

## Dynamic Eye Expression Audit

The 2026-08-11 audit covered all 326 native-model manifests currently present,
including their regular, shiny, and female variants and the two currently
published Pichu variants. These counts describe manifests, not unique species:

| Source mechanism | Variants | Runtime result |
| --- | ---: | --- |
| Authored eye-atlas material animation | 264 | Converted to clip-bound four-component eye UV tracks and verified in both the local cook and private depot. |
| Authored eye/eyelid skeletal animation without an atlas track | 18 | Already follows the selected skeletal clip through the normal model-pose path. This covers Clefairy, Clefable, Vulpix, Ninetales, Paras, Venomoth, Electrode, Exeggcute, and Ditto regular/shiny variants. |
| Static eye surface in the imported source | 28 | No eye-atlas parameter, changing eye-mesh visibility, animated eye/eyelid bone, or morph metadata is authored; these remain static instead of receiving invented expressions. |
| Embedded or no separate eye surface | 16 | The source has no independently animated eye surface to transport, as with Staryu and Starmie; the importer preserves the embedded/static result. |

The 264 atlas-driven variants break down as 26 LGPE, 16 PLA, 120 SV, 28
Sword/Shield, and 74 Z-A manifests. Their cooked PHAN objects contain 30,560
eye tracks. The fractional-frame audit divides those into 1,792 discrete
atlas selectors, 16,164 continuous curves, and 12,604 static tracks. Discrete
selectors carry `hold_source_frame` in PHAN; continuous pupil/gaze curves stay
`linear`. Every local PHAN had a matching published depot object and a
non-empty `uv_scale_offset` track set.

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

## Z-A Upgrade Review

The local Z-A source contains additional Kanto models. Existing qualified SV
models for 001-009, 023-026, 035-036, 039-040, 052-053, and 056-057 are
comparison candidates, not automatic upgrades. Those families have already
needed exact eye, layered-material, translucency, or special-surface repairs.
Replacing them solely because Z-A is newer would risk a visual regression while
often reusing similar top-level geometry.

For not-yet-imported families, the local Z-A source offers these candidates:

- 104-105 Cubone and Marowak (preferred once the missing local package
  payloads are restored; the current Let's Go imports are provisional);
- 142 Aerodactyl;
- 147-150 Dratini, Dragonair, Dragonite, and Mewtwo.

Each is preferred for the first trial import where it covers the whole family
or clearly improves a legacy source. Gaps still require a family-level choice
among Scarlet/Violet, Sword/Shield, Let's Go, and Legends: Arceus.

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
