# Phlosion Asset Migration

Status: Active
Type: Runbook
Last updated: 2026-08-19

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

Native `.phmodel` geometry already declares and stores normals and tangents in
one right-handed, positive-Y basis. Forge therefore preserves tangent XYZ and
only normalizes it while retaining handedness. The retired importer converted
tangent `(x,y,z)` to `(x,z,-y)` without making the same change to normals; that
destroyed the tangent frame and rotated normal-map relief into false facial and
body bands. The native-IR unit contract now requires the preserved tangent to
remain orthogonal to its normal.

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

## External Native Model Package

Source-game extraction, reverse engineering, import recipes, static analysis,
and visual evidence are owned by the private companion research repository.
Pokemon Autochess consumes only the source-neutral package declared by:

- `config/assets/kanto_native_model_package.json`;
- `config/assets/kanto_model_promotions.json`;
- `config/assets/asset_catalog.json`.

The package enumerates canonical `.phmodel` and `.animset.json` identities that
must exist in the ignored private asset view. It deliberately contains no
source archive paths, extraction commands, shader programs, decoded textures,
or research-repository dependencies. Established artifact stems remain stable
so existing private depot and cooked-object identities do not churn during the
repository split.

Validate the published package, active Pokemon configuration, and current cook
manifest together with:

```powershell
.\tools\assets\validate_kanto_model_promotions.ps1
```

The complete ownership and promotion protocol is documented in
`EXTERNAL_ASSET_RESEARCH.md`.

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
