# Asset depot sync

`sync_asset_depot.ps1` copies local runtime payloads from the private sibling
asset depot into this ignored game workspace. It never mirrors or deletes
destination files.

Default layout:

```text
../PhlosionAssets/
  pokemon-autochess/runtime/assets/
  pokemon-autochess/runtime/content/phlosion/
```

Use a different depot location with:

```powershell
$env:PHLOSION_ASSET_DEPOT = "E:\Private\PhlosionAssets"
.\tools\assets\sync_asset_depot.ps1
```

Use `-VerifyOnly` to validate paths and report file counts without copying.

## Game Freak Pokemon importer

`import_gamefreak_pokemon.ps1` is the repeatable offline boundary from a
catalogued Game Freak resource set to Phlosion's canonical native model IR.
It resolves species/form/gender from `pokemon-variants.json`, stages rare
materials without modifying the private extraction, runs the isolated
headless decoder, validates mesh/material/skeleton/animation output, publishes
the `.phmodel` package to the private depot and ignored game workspace, and can
optionally cook the result into `.phlo` runtime resources. A successful cook is
also published back to the private depot, keeping it as the reproducible source
for later workspace syncs.

```powershell
$env:PHLOSION_ASSET_DEPOT = "D:\ProjectData\Games\PokemonAutochess\Assets"
.\tools\assets\import_gamefreak_pokemon.ps1 -PlanOnly
.\tools\assets\import_gamefreak_pokemon.ps1 -SpeciesId 1,2,3 -Force -Cook
```

The tracked `gamefreak_pokemon_imports.json` recipe contains only asset
identities and output names. Proprietary inputs, canonical generated models,
textures, and cooked PHLO payloads remain in the private/ignored asset roots.

### Extracted TRPAK sources (Legends: Z-A)

Legends: Z-A merged-game-file dumps may expose each TRPAK as a directory of
hash-named payloads. `stage_gamefreak_trpak_sources.ps1` resolves those names
with the versioned Z-A hash list in the private depot, preserves the full
native dependency graph, and emits the catalog consumed by
`import_gamefreak_pokemon.ps1`:

```powershell
.\tools\assets\stage_gamefreak_trpak_sources.ps1 `
  -RecipePath .\tools\assets\gamefreak_pokemon_imports_za.json

.\tools\assets\import_gamefreak_pokemon.ps1 `
  -RecipePath .\tools\assets\gamefreak_pokemon_imports_za.json `
  -Force -Cook
```

The tracked Z-A recipe records exact package identities and the expected hash
list digest. The hash list, native resources, canonical imports, cooked model
data, and decoded textures remain private asset-depot content.

### Legacy Game Freak GFPAK importer

`import_gamefreak_gfpak_pokemon.ps1` performs the equivalent offline import
for the LGPE and Sword/Shield GFPAK/GFBMDL/GFBANM resource family. It extracts
the selected packages, preserves source material UV transforms and every
animation clip, exports regular/shiny and gender variants to canonical
`.phmodel` resources, validates them, and optionally cooks their `.phlo`
runtime objects. Source identity, depot layout, and animation provenance are
driven by the selected recipe. Sword recipes preserve their object-space
normal maps as native evidence without binding them to Phlosion's currently
tangent-space-only runtime normal slot. Native GFLX ambient masks are also
preserved and translated into grayscale runtime occlusion with Game Freak's
packed-channel formula; they must not be bound directly as red-channel glTF
occlusion maps.

```powershell
# Validate the default LGPE recipe.
.\tools\assets\import_gamefreak_gfpak_pokemon.ps1 -PlanOnly

# Import the Sword/Shield Caterpie family.
.\tools\assets\import_gamefreak_gfpak_pokemon.ps1 `
  -RecipePath .\tools\assets\gamefreak_pokemon_imports_sword.json `
  -RomFsRoot "\\TNAS-98B9\pokemon\Game Files\Switch\Pokemon_Sword_v1.3.2_Merged_RomFS" `
  -SpeciesId 10,11,12 -Force -Cook
```

The tracked `gamefreak_pokemon_imports_lgpe.json` and
`gamefreak_pokemon_imports_sword.json` recipes describe reproducible source
and output identities. Original GFPAKs, extracted files, generated textures,
canonical models, and cooked resources remain private and ignored.
