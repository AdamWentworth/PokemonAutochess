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
optionally cook the result into `.phlo` runtime resources.

```powershell
$env:PHLOSION_ASSET_DEPOT = "D:\ProjectData\Games\PokemonAutochess\Assets"
.\tools\assets\import_gamefreak_pokemon.ps1 -PlanOnly
.\tools\assets\import_gamefreak_pokemon.ps1 -SpeciesId 1,2,3 -Force -Cook
```

The tracked `gamefreak_pokemon_imports.json` recipe contains only asset
identities and output names. Proprietary inputs, canonical generated models,
textures, and cooked PHLO payloads remain in the private/ignored asset roots.
