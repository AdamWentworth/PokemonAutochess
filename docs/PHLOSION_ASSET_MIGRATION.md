# Phlosion Asset Migration

Status: Active
Type: Runbook
Last updated: 2026-08-02

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

## Pokemon Scarlet Native Import Slice

Bulbasaur now proves a source-native path alongside the legacy GLTF inputs used
by the remaining configured Pokemon:

```text
TRMDL/TRMSH/TRMBF/TRSKL/TRMTR/BNTX/TRANM
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

The decoder is an offline sidecar in the local GPL GFTool checkout. GPL code is
not linked into Phlosion Engine, the game runtime, or the reusable package
repositories. The neutral `.phmodel` reader is Forge-only and has a synthetic
CI contract that needs no proprietary asset.

The IR retains Scarlet's `SSS` and `EyeClearCoat` families, every recovered
shader option and parameter, all texture roles, and native sampler evidence.
The current `.phmat` cook translates only the renderer's established PBR
subset. Exact Scarlet SSS/jewel and layered-eye material-family reconstruction
is therefore the next material-parity pass; the importer deliberately retains
the unresolved evidence instead of baking a guessed appearance.

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

- Bulbasaur uses the native Scarlet `.phmodel` evidence path. The other 20
  configured Pokemon remain GLB compatibility inputs until they receive the
  same source-native migration. Runtime resolves all of them to `.phlo` and
  never invokes either importer when cooked resources are present.
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
