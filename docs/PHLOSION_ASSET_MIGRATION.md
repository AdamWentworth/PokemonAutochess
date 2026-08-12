# Phlosion Asset Migration

Status: Active
Type: Runbook
Last updated: 2026-08-08

This runbook records the implemented vertical slice of
`PHLOSION_ASSET_ARCHITECTURE.md`. The architecture document owns the long-term
decision. This document owns the current cook, runtime proof, compatibility
boundaries, and next qualification steps.

## Implemented Runtime Path

`PhlosionForge` now cooks the current gameplay model representation into:

```text
<object>.phlo
  model.phmesh
  model.phskel
  model.phanim
  model.phmat
  textures/*.ktx2
```

The `.phlo` is a high-level prefab manifest. It records required typed
dependencies and their PHRC content hashes. It is not a renamed texture,
animation, or monolithic model blob. Its generated directory combines the
readable source stem with a stable hash of the normalized source identifier so
same-named assets from different folders cannot collide.

The model resources preserve all fields consumed by the current renderer:

- positions, normals, tangents, UVs, colors, joints, and weights;
- index buffers, submesh ranges, triangle-to-material mappings, bounds, and
  derived node/skin mappings;
- node hierarchy, bind transforms, skins, inverse bind matrices, and roots;
- animation clips, samplers, channels, interpolation, and durations;
- PBR factors, alpha state, double-sided state, sampler state, and base,
  normal, metallic-roughness, occlusion, and emissive texture roles.

Cooked textures are valid KTX2 files. Color textures use an sRGB Vulkan format;
data textures use an unorm format. The first desktop profile deliberately
retains decoded RGBA8 pixels without lossy recompression.

Complete canonical Route 1 is stored in
`content/phlosion/scenes/route1.phscene`. Its PHRC scene manifest indexes an
embedded, integrity-checked virtual asset store containing:

- the promoted Route 1 canonical scene;
- both encounter-grass canonical scenes;
- the ordinary-grass and both flower canonical scenes;
- the exact composition, placement, and board-layout manifests.

Gameplay mounts that virtual store and passes it through the same promoted
Route 1 preparation path. It does not read the loose provisional LGPE cache
after the `.phscene` has mounted.

## Pokemon Switch Native Import Slice

The repeatable Game Freak-to-Phlosion boundary is recipe-driven rather than a
collection of one-off species conversions:

```text
private Game Freak resources
  -> tools/assets/import_gamefreak_pokemon.ps1
  -> canonical .phmodel + .phanimset native IR
  -> PhlosionForge
  -> runtime .phlo and typed PHRC resources
```

`tools/assets/gamefreak_pokemon_imports.json` identifies source species,
form, sex, material variant, and output identity. The importer builds the
isolated Trinity decoder, stages source resources without modifying them,
applies the source rare-material payload for shiny variants, validates mesh,
materials, skeleton, animations, and material provenance, then atomically
publishes canonical imports to the private asset depot and the Git-ignored
game asset view. `-PlanOnly`, `-SpeciesId`, `-Force`, and `-Cook` provide a
reviewable batch workflow. For example:

```powershell
.\tools\assets\import_gamefreak_pokemon.ps1 -PlanOnly
.\tools\assets\import_gamefreak_pokemon.ps1 -SpeciesId 1,2,3 -Force -Cook
```

The Scarlet/Violet recipe now covers the accepted SV families through
Poliwrath, plus Pichu, with regular/shiny outputs and distinct female geometry
where the source supplies it. `KANTO_MODEL_SOURCE_AUDIT.md` owns the exact
family authority and complete sex-variant checklist. Runtime `modelVariants`
select those prefab identities while evolution preserves the active variant.
Cooked model cache identities are derived from normalized PHLO paths, so two
variants of one species cannot alias merely because they share a Pokemon name.
Proprietary source resources, canonical derived imports, and cooked objects stay
in the private asset depot; only importer code, recipes, configuration, tests,
and audit-safe metadata are committed.

The Scarlet/Violet, Sword/Shield, Let's Go, Legends: Arceus, and Legends: Z-A
recipe batches now prove the source-native path used by every configured
Pokemon and every staged family through Dodrio:

```text
TRMDL/TRMSH/TRMBF/TRSKL/TRMTR/BNTX/TRANM/TRACM
  -> isolated offline Trinity decoder
  -> 0001_Bulbasaur_SV.phmodel + binary/texture evidence
  -> PhlosionForge
  -> .phlo + .phmesh/.phskel/.phanim/.phmat + KTX2
  -> Inspector and gameplay
```

The active Bulbasaur configuration names the `.phmodel`; it does not pass
through GLB or GLTF. The source-native import contains 4,994 vertices, 11,988
indices, six submeshes, 75 bones, four native material records, and 46 Scarlet
animation clips. Forge validates buffer bounds, contained resource paths,
material bindings, skeleton parents, animation targets, and the native UV
convention before cooking. Its PHLO is then round-tripped through the same
runtime loader used by the Inspector and gameplay.

The native animation boundary also retains `TRACM` mesh-visibility tracks.
Those tracks are step-sampled at runtime instead of deleting auxiliary meshes
by name: for example, Bulbasaur's two vine submeshes are hidden at the start of
`attack02`, visible from source frame 1, and hidden again at source frame 85.
Facial `eye01` and `mouth01` clips are overlay layers; their fixed visibility
records compose over the base idle state rather than revealing auxiliary vine
geometry. Scarlet Bulbasaur contains no morph targets--`mouth01` is a skeletal
jaw animation and its body-B mouth geometry and texture remain part of the
native mesh/material evidence.

Scarlet's `SSSEffect` smoke emitters keep their paired source construction.
Koffing and Weezing animate each `smokegeom_*` cloud together with its
`smokemask_*` plume: the skeletal tracks expand and carry the cloud away from
the vent while the paired plume contracts, then the clip's 60 Hz visibility
gate removes both before the next emission. Both meshes remain in the stable
render cache and the source gate is applied as batch alpha, avoiding geometry
cache churn without inventing intermediate opacity. `UVScaleOffset3` retains
its actual job of scrolling the displacement texture across that geometry; it
is not treated as a puff-opacity curve. Weezing's 241-frame
`28201_loop01_loop` is not material-only: it layers four paired side-puff
emissions over idle/body clips whose smoke records are fixed hidden. A roar,
attack, or other clip with its own intermittent smoke lifecycle takes
precedence instead. Koffing's one-second `28201` contains the corresponding
paired side-cloud skeletal expansion/travel/reset and displaced-material
cycle, but its shipped `TRACM` leaves all six smoke meshes fixed hidden even
while the continuous `PLAY_PM_FLOAT_GAS_RND` layer runs. The importer applies
the family's per-puff gates to Koffing's two side pairs (B1 frames 10-40, B2
frames 12-43), producing one puff-and-clear cycle per second without exposing
the unused top pair. Ordinary body, eye, mouth, vine, and accessory visibility
continues to use exact step sampling.

The decoder is an offline sidecar in the local GPL GFTool checkout. GPL code is
not linked into Phlosion Engine, the game runtime, or the reusable package
repositories. The neutral `.phmodel` reader is Forge-only and has a synthetic
CI contract that needs no proprietary asset.

The IR retains Scarlet's `SSS` and `EyeClearCoat` families, every recovered
shader option and parameter, all texture roles, and native sampler evidence.
Trinity's serialized `W, X, Y, Z` material-vector storage is normalized to
conventional `X, Y, Z, W` semantics at this boundary. This is required for
both `UVScaleOffset` (`1,1,0,0`) and RGBA layer colors.

Forge translates the renderer's established PBR subset and composes native
`LayerMaskMap` plus `BaseColorLayer1..4` evidence in linear color before the
base-color KTX2 cook. `NormalMap1` is likewise blended into the primary eye
normal through the source layer mask's green channel. This restores the
Scarlet eye palette, highlight mask, and layered surface normals without a
species-specific texture repaint. The synthetic native-IR test guards
mask-channel composition and linear/sRGB conversion. `LayerMaskScale1..4`
applies consistently to color and metallic/roughness layers; this is required
for Pikachu's authored cheek color and for its face-patch roughness to join the
surrounding fur without a rectangular material boundary. Exact dynamic Scarlet
clear-coat plus SSS/jewel response remains a later shader-parity pass; the IR
continues to retain those source parameters rather than discarding them or
replacing them with a guessed value.

Legends: Z-A's ordinary non-eye `IkCharacter` body materials select native
material mode 32 by source profile, rather than through a species allowlist.
Forge bakes each `ShadowingColorLayer*` result into RGB, the masked
`SpecularMaskMap * SpecularIntensity` response into alpha, and the restrained
`RimLightMaskMap`/back-rim response into the auxiliary map. It transports
`HalfLambertBias`, `ShadowStrength`, rim offset, rim contrast, and the authored
outer-surface roughness through the existing factor/material payloads.

The source material is a two-stage response: its colored half-Lambert,
shadow-tint, AO, and rim composition becomes the base color of a second
normal-mapped dielectric lighting stage. OpenGL, D3D12, and Vulkan now retain
both stages. This exposes the authored coat, feather, skin, scale, and stone
normal relief instead of returning nearly flat front-facing albedo, while the
source specular mask prevents a uniform generic glTF highlight. Low keeps the
foundational shadow/specular payload at the strongest texture LOD bias; Medium
reduces that bias, High restores normal detail, and Ultra restores AO and rim
response. `EnableEyeOptions`, displaced effects, Gastly's dedicated face/smoke
ordering, and the qualified Gyarados/Porygon SV-roughness hybrids remain on
their specialized paths. Synthetic native-IR tests and hidden Low-through-
Ultra Inspector captures cover all three rendering APIs.

Eevee instead uses its complete Scarlet/Violet SSS material and native mode
33. Forge preserves its directional-fur `RoughnessMap`, normal, AO,
`SSSMaskMap`, and `SubsurfaceColor`; the backends reconstruct a restrained
fibre/velvet lobe from that authored signal. World textures receive complete,
color-space-correct mip chains on OpenGL, D3D12, and Vulkan so Inspector
quality tiers operate on real filtered detail. Low retains a coarse version of
the foundational fur atlas, High restores normal detail, and Ultra restores
the sharp fibre, AO, and SSS response. Male/female and regular/shiny variants
all use this same SV contract.

For hard-surface Z-A selections with byte-identical SV base-color atlases,
Forge may use an authored SV roughness texture without changing the chosen
mesh, rig, materials, or animations. The current qualified set is male/female
Gyarados and Porygon. `tools/housekeeping/stage_za_sv_surface_maps.ps1` stages
the five source maps from the retained SV comparison imports; this remains a
local source-asset operation and does not publish to the deferred backup depot.

Native `COLOR_0` values are likewise preserved losslessly, but Forge only feeds
them into albedo when the source material explicitly enables
`EnableVertexColor`. Scarlet's Bulbasaur SSS materials do not; multiplying that
auxiliary channel into base color washed out and spatially distorted the
authored body atlas.

Native animation translation is also retained unchanged in `.phmodel` and the
cooked `.phanim`. Pose evaluation applies a runtime root-motion policy instead
of rewriting source clips. Gameplay and the standard Inspector preview use
`InPlaceHorizontal`: Game Freak's named `origin` joint keeps its authored
vertical component while horizontal X/Z travel is restored to the bind pose,
then the game-owned Pokemon instance transform supplies the allowed world
movement. `PreserveAuthored` remains available for source-evidence inspection,
and `InPlaceAll` is available for contexts that must suppress every root
translation component. Descendant pose motion such as `waist` bobbing is never
classified as root motion.

Charmander extends that proof to source-authored animated materials. Its
Scarlet payload contains a distinct 123-vertex, 648-index `fire_mesh` skinned
to the tail feeler joints, an `Unlit` material, `LayerMaskMap`, and
`DisplacementMap`. Forge preserves that mesh inside the same native PHLO and
marks the material for Phlosion's generic layered-unlit/displacement path.
The source skeleton drives the flame silhouette while the source displacement
texture drives the internal surface motion. The old Charmander flipbook,
procedural tail emitter, special preview routing, and D3D12 skinning exception
are disabled; they remain only for Charmeleon and Charizard until those models
are migrated. This is intentionally a reusable material capability rather than
a Charmander-specific VFX override, so the same import/runtime boundary can be
qualified against Ponyta's larger authored fire system next.

Ponyta qualifies the same boundary against the closely related Legends:
Arceus Trinity generation without translating through GLB. The imported
normal and shiny prefabs each retain 3,945 vertices, 17,928 indices, seven
submeshes/materials, 87 bones, and 54 source-named 60 fps animation clips.
Four independently skinned `Unlit` fire meshes carry their own base-color,
layer-mask, and displacement maps. The normal materials retain the source
orange/yellow layers; the rare materials retain their source cyan/blue layers.

Those meshes now play the complete two-second
`pm0077_00_00_08201_loop01_loop` contract instead of reducing it to guessed
scroll speeds. Forge retains every `UVScaleOffset` and `UVScaleOffset3`
component key, its source frame time, its static defaults, and each material's
authored U/V axis and 60 Hz source rate. Runtime samples those curves on the
shared continuous clock and transports the resulting transforms unchanged to
OpenGL, D3D12, and Vulkan. Adjacent source-frame UV reset keys cross the
periodic texture seam instead of being linearly swept through the middle of
the texture, which prevents a corrupt in-between fire frame on displays that
render between the source's 60 Hz samples. The six source skeletal tracks from
the same clip are retained as an always-on overlay over the selected body
animation; their 121 samples are static in this particular clip, so the
visible flame motion comes from the material curves rather than inferred bone
motion. Regression tests cover axis-specific material sampling, periodic reset
semantics, duration wrapping, skeletal loop continuity, and PHLO cook/load
round trips.

The native material contract also protects those values from unrelated
graphics-quality packing. `materialFlipbook1Frames` is a legacy transport slot
that carries `BaseColorLayer2.b` for native layered-Unlit materials and
clear-coat roughness for native eye materials. Generic texture LOD policy must
not overwrite it or remove their displacement/layer-mask maps. Synthetic tests
cover both the Inspector world-material path and the gameplay batch path.

Ponyta and Rapidash's Standard body materials enable
`EnableLerpBaseColorEmission`. Their qualified body atlases use red as
base-map coverage, so Forge keeps the pale body maps instead of baking the
olive `BaseColorLayer1` into them. Shader options, UVs, and texture-resolution
ratios do not uniquely identify that response: Machamp's smaller body mask
uses red as the literal blue-gray Layer1 tint for its arms and feet. Forge
therefore keys this family exception to Ponyta's exact body texture and
Rapidash's exact body_a/body_b textures, preserving literal Layer1 for other
PLA bodies. The IR continues to retain all layer values for the eventual exact
base-color/emission shader response.

PLA Standard layers tint rather than replace their authored BaseColorMap. The
original Abra/Machop qualification exposed this importer rule because the atlas
carries closed eyelids and fine limb, toe, and foot definition independently of
flat body-color selectors. Forge continues to multiply those atlas samples by
the selected linear layer color, while the production Abra and Machop families
have since been promoted to their visually qualified Z-A imports.

The PLA source also supplies two normal and two rare resident `PTCL` effects:
`fire00_s_loop` for four tail attachments (`left_tail_b_02`,
`right_tail_b_02`, `tail_e_01`, and `tail_c_01`) and `fire02_loop` for
`hair_02`. The offline decoder validates the Switch `VFXB` structure, preserves
every raw emitter and embedded shader block, extracts the embedded BNTX, and
records the recovered texture bindings and color/alpha curves. These particles
are supplemental to the four fire meshes; exact runtime PTCL simulation is
still an explicit qualification gate and must not be replaced by a hand-made
Ponyta flame. Until that gate passes, the Ponyta entries are Inspector
qualification prefabs rather than the active gameplay species configuration.

The animset's source FPS remains authoritative even when a native PHLO unit has
no legacy `Model` object. GameWorld spawn metadata and backend hydration both
propagate that rate into the runtime unit. Hit-frame markers are converted with
the native FPS before attack-window scaling, keeping animation poses, damage,
projectile release, and impact VFX on the same normalized moment of the clip.

Game Freak UVs remain losslessly preserved in `.phmodel`. At runtime the
importer flips V inside each integer UV tile instead of applying one global
`1 - v`. Bulbasaur's `body_a` islands occupy tile 0 while `body_b` occupies tile
1. This distinction keeps the skin markings aligned while mapping the bulb,
mouth/tongue, teeth, claws, and vine geometry to their intended `body_b`
regions.

## Build and Cook

From the repository root:

```powershell
cmake --preset vs2026
cmake --build --preset debug --target PhlosionForge PokemonAutochess PAC_Tests
.\build\Debug\PhlosionForge.exe cook-all
.\build\Debug\PhlosionForge.exe validate
```

Useful narrower commands are:

```powershell
.\build\Debug\PhlosionForge.exe validate-catalog
.\build\Debug\PhlosionForge.exe cook-pokemon
.\build\Debug\PhlosionForge.exe cook-staged
.\build\Debug\PhlosionForge.exe cook-runtime
.\build\Debug\PhlosionForge.exe cook-route1
.\build\Debug\PhlosionForge.exe cook-model assets/models/0077_Ponyta_PLA.phmodel
.\build\Debug\PhlosionForge.exe finalize-cook
```

`config/assets/asset_catalog.json` is the tracked ownership boundary for active
gameplay models, staged native imports, authored runtime sources, Route 1, and
retained legacy-review inputs. `cook-all` writes schema-2
`content/phlosion/cook_manifest.json` transactionally: it records each source
path and hash, deterministic `.phlo` path and hash, render counts, texture
count, cooked byte count, catalog provenance, and retained-review identities.
The Route 1 record includes its archive and authored-scene hashes plus runtime
composition counts. Failed cooking or validation leaves the previous manifest
in place.

`finalize-cook` is an interrupted-cook recovery path. It validates and hashes
the exact existing catalogued model objects, rejects any object older than its
source, recooks Route 1, and publishes only after full strict validation. It is
not a substitute for `cook-all` after changing source models. The entire
`content/phlosion/` tree is generated source-derived data and is intentionally
excluded from Git.

## Strict Runtime Proof

Set both variables before launching a qualification run:

```powershell
$env:PHLOSION_REQUIRE_COOKED_ASSETS = "1"
$env:PHLOSION_TRACE_ASSET_LOADS = "1"
.\build\Debug\PokemonAutochess.exe
```

Strict mode makes a missing cooked object or scene a hard load failure. Trace
mode emits `[Phlosion][PHLO]` and `[Phlosion][PHSC]` records. A proof is not
accepted if it contains a required-asset failure, a model-render failure, or
an unintended fallback.

The headless 2026-08-08 authority qualification validates 54 configured native
Pokemon objects, 148 staged native objects, 10 authored runtime objects, 61
Route 1 environment prefabs, and one Route 1 PHSC. It verifies every recorded
source/object FNV-1a-64 hash and exact source-derived object identity.

The current Route 1 PHSC validation reports:

| Measure | Result |
| --- | ---: |
| Canonical scene layers | 6 |
| Materials | 27 |
| Draw classes | 210 |
| Visible triangles | 174,749 |
| Projected-shadow triangles | 118,794 |
| Encounter-grass instances | 164 |
| Authored vegetation instances | 54 |
| Virtual files | 33 |
| PHSC bytes | 91,396,088 |
| PHSC FNV-1a-64 | `2d7765ba4a46ce29` |

The authoritative model generation contains 212 model/object PHLO prefabs and
1,815 logical model KTX2 references backed by 994 immutable shared payloads
(1,801,392,796 bytes), plus 61 Route 1 environment prefabs and one PHSC scene.
No authoritative object directory contains a private KTX2 copy; the cook
manifest owns every shared payload and reports no missing or orphan entry.
The schema-2 manifest SHA-256 for this qualification is
`5253EB7FAF0DF04548ED2317A82D8896B88CA6A5B1944DA0EE1FD36BBCB830DA`.

## Compatibility Boundaries

These boundaries are deliberate and must not be described as already removed:

- All 54 configured Pokemon variants use native Game Freak `.phmodel` inputs.
  The catalogued but unconfigured families through Dodrio are staged imports.
  Runtime resolves configured models to `.phlo`; proprietary
  decoding remains an offline Forge/importer responsibility.
- Forge still supports GLB authoring inputs for the Poke Ball and Growl meshes,
  and the runtime retains compatibility paths that Phase 4 will remove after
  strict gameplay/Inspector proof. The `.pacmdl` compatibility reader remains
  for those offline GLB cook paths, not as the authoritative Pokemon source.
- Route 1 PHSC embeds the exact promoted canonical scene IR as private scene
  payloads. A later deduplication pass may promote reusable vegetation to
  separately addressable `.phlo` dependencies, but it must produce identical
  runtime hashes and images before replacing this restore point.
- PHRC version 1 uses bounded little-endian records and FNV-1a-64 integrity
  fingerprints. Cryptographic distribution signing and SHA-256 vault
  manifests belong to the `.phv` shipping pass.
- Loose development KTX2 dependencies are already content addressed. Their
  two-part identity separates encoded bytes from role/color-space/sampler and
  material interpretation. Ten exact-byte groups (22,219,484 redundant bytes)
  intentionally remain as semantic partitions; the unexpected duplicate-byte
  budget is zero.
- `.phcol` and `.phv` are specified but are not required by this visual
  vertical slice. They remain the next resource and packaging milestones.

The development fallback remains available when strict mode is off so source
iteration is not blocked. It must never hide a failed qualification run.

## Qualification Gates

Before promoting changes to this path:

1. build `PhlosionForge`, `PokemonAutochess`, and `PAC_Tests`;
2. run `PhlosionForge cook-all`;
3. run `PhlosionForge validate`;
4. run the complete `PAC_Tests` suite;
5. capture a fixed Route 1 gameplay frame in strict mode;
6. confirm 54 configured Pokemon PHLO identities and one PHSC load;
7. confirm there are no missing cooked assets or render failures;
8. visually compare terrain, vegetation, shadows, Pokemon materials, and
   animation against the promoted baseline.

The next architecture milestone is `.phv`: pack the exact loose cooked
resources into a mountable vault, then prove loose and vault-mounted runtime
hashes and captures are identical.
