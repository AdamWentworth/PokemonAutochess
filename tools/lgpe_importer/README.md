# LGPE Direct-Source Importer

Status: Active
Type: Tool
Last updated: 2026-07-27

This is the first implementation slice of the contract in
`docs/LGPE_ENVIRONMENT_FIDELITY_CONTRACT.md`.

The importer reads the original Route 1 GFBMDL and its known BNTX dependencies
directly. It does not use DAE, GLB, Blender, or the current PACMDL cache as its
canonical data bridge.

## Current Pass

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

It deliberately does not export source vertex or texture payloads into this
repository.

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

## Validate

```powershell
.\tools\lgpe_importer\validate_lgpe_source_manifest.ps1 `
  -ManifestPath .\debug\lgpe_importer\route1_source_manifest.json
```

The validation is intentionally local-only because CI does not possess the
user's source dump or the external decoder binaries.

## Known Boundaries

- The profile lists the 39 BNTX dependencies already proven by the Route 1
  extraction evidence. Automatic GFPAK dependency discovery is later work.
- Vertex-type semantic names are decoder-convention hints and remain marked for
  independent binary-format verification. Numeric declarations and untouched
  buffer hashes are authoritative in this pass.
- BNSH, GFBANM, GFBCOL, and anonymous auxiliary-file decoding are not part of
  this first pass.
- This pass proves source inventory and preservation. It does not yet submit
  the decoded environment to `WorldScene`.
