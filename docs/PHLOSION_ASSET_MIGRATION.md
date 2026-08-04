# Phlosion Asset Migration

Status: Active
Type: Runbook
Last updated: 2026-08-03

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

Bulbasaur and Charmander from Scarlet/Violet, plus Ponyta from Legends:
Arceus, now prove a source-native path alongside the legacy GLTF inputs used
by the remaining configured Pokemon:

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
mask-channel composition and linear/sRGB conversion. Exact dynamic Scarlet
clear-coat plus SSS/jewel response remains a later shader-parity pass; the IR
continues to retain those source parameters rather than discarding them or
replacing them with a guessed value.

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

Ponyta's Standard body material enables `EnableLerpBaseColorEmission`. In that
mode its layer mask is not an ordinary albedo-layer selector, so Forge keeps
the authored pale body map instead of baking the olive `BaseColorLayer1` into
it. The IR continues to retain all layer values for the eventual exact
base-color/emission shader response.

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
.\build\Debug\PhlosionForge.exe cook-pokemon
.\build\Debug\PhlosionForge.exe cook-runtime
.\build\Debug\PhlosionForge.exe cook-route1
.\build\Debug\PhlosionForge.exe cook-model assets/models/0077_Ponyta_PLA.phmodel
```

`cook-all` writes `content/phlosion/cook_manifest.json`. It records each source
path, source hash, `.phlo` path, cooked object hash, render counts, texture
count, and cooked byte count. The Route 1 record includes its archive hash and
runtime composition counts. The entire `content/phlosion/` tree is generated
source-derived data and is intentionally excluded from Git.

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

The 2026-07-30 D3D12 qualification used the pinned Route 1 combat snapshot at
1280 by 720 and frame 170. It reported:

- 21 configured Pokemon `.phlo` prefabs;
- 12 reusable runtime auxiliary `.phlo` prefabs, including the Poke Ball,
  compatibility environment objects, and authored Growl meshes;
- 33 successful PHLO loads in the gameplay process;
- one Route 1 PHSC mount with 33 virtual files;
- `loaded=24 failed=0` for startup model prewarming;
- no missing cooked resource or render-model warning;
- a successful backend screenshot write.

The real Route 1 PHSC validation reports:

| Measure | Result |
| --- | ---: |
| Canonical scene layers | 6 |
| Materials | 27 |
| Draw classes | 64 |
| Visible triangles | 112,153 |
| Projected-shadow triangles | 45,760 |
| Encounter-grass instances | 164 |
| Authored vegetation instances | 54 |
| Virtual files | 33 |
| PHSC bytes | 91,395,848 |
| PHSC FNV-1a-64 | `4522633e54103ecf` |

The complete cook currently contains 33 PHLO prefabs, 213 KTX2 textures, one
PHSC scene, and the four typed low-level resources for every prefab.
Two consecutive full cooks produced the identical cook-manifest SHA-256
`8DE8715D31E819FC5924D3DFB3FDEC8AA33F1FD539131BF1996B925EA40BF193`.

## Compatibility Boundaries

These boundaries are deliberate and must not be described as already removed:

- Bulbasaur and Charmander use the native Scarlet `.phmodel` evidence path.
  Ponyta has normal and shiny native PLA Inspector prefabs but is not yet an
  active configured gameplay species. The remaining configured Pokemon stay
  on GLB compatibility inputs until they receive the same source-native
  migration. Runtime resolves configured models to `.phlo` and never invokes
  either importer when cooked resources are present.
- Forge still obtains legacy model IR through the validated version-9
  `.pacmdl` compatibility reader for those remaining GLB inputs. That reader
  may rebuild during an offline cook; it is not part of the shipping path.
- Route 1 PHSC embeds the exact promoted canonical scene IR as private scene
  payloads. A later deduplication pass may promote reusable vegetation to
  separately addressable `.phlo` dependencies, but it must produce identical
  runtime hashes and images before replacing this restore point.
- PHRC version 1 uses bounded little-endian records and FNV-1a-64 integrity
  fingerprints. Cryptographic distribution signing and SHA-256 vault
  manifests belong to the `.phv` shipping pass.
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
6. confirm 21 configured Pokemon PHLO loads and one PHSC load;
7. confirm there are no missing cooked assets or render failures;
8. visually compare terrain, vegetation, shadows, Pokemon materials, and
   animation against the promoted baseline.

The next architecture milestone is `.phv`: pack the exact loose cooked
resources into a mountable vault, then prove loose and vault-mounted runtime
hashes and captures are identical.
