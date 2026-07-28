# LGPE Direct-Source Importer

Status: Active
Type: Tool
Last updated: 2026-07-28

This is the active direct-ingestion implementation of the contract in
`docs/LGPE_ENVIRONMENT_FIDELITY_CONTRACT.md`.

The importer reads the original Route 1 GFBMDL and its known BNTX dependencies
directly. It does not use DAE, GLB, Blender, or the current PACMDL cache as its
canonical data bridge.

## Source Manifest

`export_lgpe_source_manifest.ps1` uses the locally installed Switch Toolbox
managed readers to produce a deterministic, non-payload source manifest. This
initial dependency is an evidence bridge while the formats and required fields
are documented; Switch Toolbox is not linked into the game runtime or
redistributed by this repository.

Decoder diagnostics are retained in the manifest rather than discarded or
mixed into the command's pass/fail output.

The manifest currently records:

- source and decoder hashes;
- all 38 meshes and their transforms;
- decoded bounds and vertex counts;
- raw vertex-buffer hashes;
- numeric vertex declarations plus provisional semantic hints;
- every polygon/material group and index-buffer hash;
- explicit source topology accounting: 88,618 triangle records, of which
  88,480 are unique by mesh/material/ordered-index identity and 138 are exact
  duplicate records retained from the GFBMDL;
- all 21 complete source material metadata records and texture bindings;
- the 63-bone skeleton;
- all 39 known BNTX dependency containers;
- texture formats, dimensions, mip chains, colorspace, swizzle metadata, and
  source-payload hashes;
- exact required-texture coverage;
- structural validation against the established Route 1 evidence.

The manifest deliberately does not export source vertex or texture payloads
into this repository.

The duplicate topology is not guessed cleanup. The direct reader reports 138
exact duplicate records across three meshes; this exactly explains the
difference from the prior DAE/Blender report's 88,480 polygons. The manifest
keeps both counts so a runtime importer can preserve source data while making
the duplicate-submission policy explicit.

## Local Inputs

The defaults expect the existing sibling workspace:

```text
../PokemonAutochessEnvironment/
  Pokemon_Lets_Go_Pikachu_v0_Environment_GFPAK_Unpacked/
  Tools/Switch-Toolbox-Final/
```

Both locations can be overridden. Inputs must come from the user's own local
dump and are not repository content.

## Generate

From the PokemonAutochess repository:

```powershell
.\tools\lgpe_importer\export_lgpe_source_manifest.ps1
```

To write a promoted evidence copy:

```powershell
.\tools\lgpe_importer\export_lgpe_source_manifest.ps1 `
  -OutputPath .\docs\lgpe\evidence\route1_direct_source_manifest.json
```

To use different local source and decoder roots:

```powershell
.\tools\lgpe_importer\export_lgpe_source_manifest.ps1 `
  -UnpackedRoot D:\local\LGPE_Environment_GFPAK_Unpacked `
  -ToolboxRoot D:\local\Switch-Toolbox
```

## Cook the Canonical Scene

The next pass emits a transparent local directory:

```powershell
.\tools\lgpe_importer\cook_route1_canonical_scene.ps1
```

Default output:

```text
cache/lgpe/route1/
  source_manifest.json
  scene.json
  geometry.bin
  textures.bin
  canonical_report.json
```

This directory is explicitly
`provisional_not_a_frozen_runtime_cache`. It exists to prove lossless source
preservation and engine loading before a durable shipping cache is selected.
It is not PACMDL, PACENV, GLB, DAE, or an undocumented Blender export.

`geometry.bin` contains:

- all 106,030 fully decoded canonical vertices;
- position, normal, tangent, bitangent, four UV sets, four color sets,
  `normalW`, four joint IDs, and four weights per vertex;
- every untouched source raw vertex buffer;
- every source polygon group's original 16-bit index stream.

`textures.bin` contains 365 decoded RGBA8 subresources across all 39 required
textures, including every mip, array level, and depth level exposed by the
decoder. The source BNTX format, colorspace, container identity, mip metadata,
and untouched source-payload hashes remain in `scene.json`.

The local payloads are ignored by Git and must be regenerated from the user's
own source dump. Only the compact cook report is promoted as repository
evidence.

## Validate and Inspect

```powershell
.\tools\lgpe_importer\validate_lgpe_source_manifest.ps1 `
  -ManifestPath .\debug\lgpe_importer\route1_source_manifest.json

.\tools\lgpe_importer\validate_lgpe_canonical_scene.ps1

.\build-vs2022\Debug\PAC_LgpeInspect.exe cache/lgpe/route1
```

The compiled engine loader lives in
`src/engine/assets/lgpe/LgpeCanonicalScene.*`. Its contract test uses an
in-memory canonical scene, so safety and preservation checks run in CI without
requiring proprietary source files. The real Route 1 cook and hash validation
remain local because CI does not possess the user's dump or decoder binaries.

## Known Boundaries

- The profile lists the 39 BNTX dependencies already proven by the Route 1
  extraction evidence. Automatic GFPAK dependency discovery is later work.
- Vertex-type semantic names are decoder-convention hints and remain marked for
  independent binary-format verification. Numeric declarations and untouched
  buffer hashes are authoritative in this pass.
- Texture RGBA8 decode currently uses the Switch Toolbox bitmap decoder.
  Original BNTX payload hashes and format metadata remain authoritative while
  channel orientation and compressed-format behavior receive independent
  verification.
- General BNSH, GFBANM, GFBCOL, and anonymous auxiliary-file decoding are not
  part of the canonical cooker. Source-level BNSH audits have recovered and
  cross-checked the Route 1 `FieldGroundShader01`, `FieldCliffShader01`,
  `FieldGrassShader01`, `FieldGrassShader02`, `FieldTreeShader02`,
  `FieldTreeShader04`, and `FieldTreeShader05` programs, plus the tree-miki
  `FieldObjectShader` variant, as the first runtime material-family vertical
  slices.
- The engine now projects canonical scenes into `WorldScene`, retains all
  additional source channels in a side stream, and promotes UV1 and UV2 into
  the GPU stream for the implemented families. Seven material records remain
  intentionally on diagnostic preview rendering until their own source-backed
  passes.
