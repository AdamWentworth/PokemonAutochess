# External Asset Research Boundary

Status: Active
Type: Architecture
Last updated: 2026-08-20

Pokemon Autochess consumes source-neutral, versioned Phlosion model and
environment packages. Source-game extraction, reverse engineering, shader
analysis, import recipes, evidence reports, and source-specific qualification
programs are owned by the private companion workspace at
`D:/Projects/Research/PokemonSwitchAssetResearch`.

The baseline extracted with this boundary is companion commit
`f292c57e29d18d28a43d63ebb0ae031e13074117`. Later package publications should
record their companion commit in the game-side change or release evidence.
The current source-neutral Route 1 environment package and publication workflow
are qualified against companion commit
`cd2edbac9c58a3e9b5185486685286a0f4966c5a`.

The game repository must configure, build, test, cook already-published native
assets, and run without that companion workspace. The integration boundary is:

```text
private research workspace + private source corpus
  -> canonical model/environment package
  -> private Phlosion asset depot
  -> config/assets/asset_catalog.json
  -> PhlosionForge
  -> ignored content/phlosion runtime objects
  -> Pokemon Autochess
```

## Tracked game-side authority

- `config/assets/kanto_native_model_package.json` enumerates canonical model
  and animation-set identities published to the game workspace.
- `config/assets/kanto_model_promotions.json` selects the accepted Kanto
  gameplay variants without depending on source-game recipes.
- `config/assets/asset_catalog.json` owns source-neutral cook inputs and runtime
  resources.
- `config/environment/route1_environment_package.json` and
  `config/environment/route1_buildmodel_placements.json`, plus
  `config/environment/route1_board_layout.json`, contain the minimal published
  Route 1 composition required by the runtime and editor.

These files may retain established artifact stems for compatibility, but they
must not contain source archive locations, extracted shader programs, decoded
textures, capture paths, or research-workspace-relative dependencies.

Existing private Route 1 PHSC archives predate this split and retain their
original embedded virtual manifest path. The runtime accepts that name only
after the PHSC has been mounted; it never falls back to a loose research file.
Future source-authorized recooks publish the new `config/environment` and
`cache/environment` paths. The companion command
`tools/publish_pokemon_autochess_environment.ps1` verifies those tracked
documents by default and only writes them with an explicit `-Publish` switch.

## Private material

Raw source files, captures, decoded textures, canonical imported models, and
cooked objects are excluded from Git. They remain in the independently backed
up private asset depot. Moving research code does not authorize publishing any
of those payloads.

## Promotion protocol

1. Qualify a candidate in the private research workspace.
2. Publish canonical assets to the private depot.
3. Update the external model/environment package manifest.
4. Restore or sync the game asset view.
5. Run package validation, Forge validation, CTest, and renderer qualification.
6. Commit the source-neutral package change in Pokemon Autochess separately
   from research evidence.

Pokemon Autochess CI validates only the published package contract. Research
workflow contracts run in the companion repository.

The two optional C++ environment inspection tools can be built against the
game runtime without returning their sources to this repository:

```powershell
cmake -S . -B build-research `
  -DPAC_BUILD_EXTERNAL_ENVIRONMENT_TOOLS=ON `
  -DPAC_ENVIRONMENT_TOOL_ROOT=D:/Projects/Research/PokemonSwitchAssetResearch
cmake --build build-research --config Debug `
  --target PAC_EnvironmentInspect PAC_EnvironmentQualification
```
