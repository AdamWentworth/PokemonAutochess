# PokemonAutochess Editor Tooling

Status: Active
Type: Architecture
Last updated: 2026-08-27

PokemonAutochess extends the reusable Phlosion Editor through
`PokemonAutochessEditorProject`. The game repository owns every tool whose
meaning depends on PokemonAutochess, LGPE evidence, Route 1, or the Autochess
board.

It also declares `phlosion.tile-tools` 0.1.0. That reusable package owns the
generic grid UI and interaction; this plugin supplies Route 1 catalogs,
projected cells, edit semantics, persistence, and undo transactions.

## Project-owned features

- the game scene and warm Game Preview catalogs;
- Route 1 cooked-environment mounting and authored-scene persistence;
- Pokemon, VFX, and Route 1 environment prefab previews;
- Route 1 hierarchy categories and source-backed layout records;
- Route 1 terrain surfaces, ramps, platforms, connected dirt-path variants,
  swatches, source references, and edit interpretation;
- the Autochess board/bench registration, exact terrain regions, snapping, and
  board-clearing workflow;
- gameplay-preview Pokemon enumeration, runtime-resolved scale, legal
  board/bench placement, terrain grounding, and per-preview starting-position
  overrides.

The board-clearing and imported-scene-reset workflows are project commands.
Phlosion renders their declared controls and confirmation; this plugin owns
their Route 1 logic, undo transaction, save, and status message.

## Inspector behavior

- A Scene or environment root exposes project-wide layout guides, the optional
  Tile Tools panel, and PokemonAutochess project commands.
- Enabling tile mode activates projected cell
  selection in Scene view.
- The Tile Tools **Projected Terrain Shadow** controls can disable or restore
  projected-shadow receiving on the selected terrain tops without disabling
  the trees, props, or other objects that cast those shadows.
- The **Imported Source Tint** controls normalize or restore source vertex
  color on selected terrain. Normalize removes the blue-green ground paint
  left behind when an imported encounter-grass overlay is removed; it is
  independent of dynamic lighting and projected shadows.
- Terrain previews and commits run the same compatible-neighbor seam resolver.
  It rebuilds one continuous material field through connected authored cells
  with matching surfaces and shared height profiles, while retaining exact
  untouched source fields. The project-level **Terrain Seam Diagnostics**
  command optionally draws resolved component boundaries in cyan and
  projected-shadow mismatches in magenta; it starts hidden and does not alter
  the scene.
- The **Exact Source Piece Catalog** inventories every occupied imported
  terrain cell, classifies its ledge/ramp/boundary topology, and records all
  four height-and-surface edge sockets. Its carousel ranks exact LGPE donor
  geometry against one selected target cell. Applying a donor persists the
  existing lossless `source_reference` path, so positions, normals, UVs,
  vertex colors, materials, and topology come from the imported scene rather
  than another generic ledge strip. See
  [LGPE_TERRAIN_PIECE_WORKFLOW.md](LGPE_TERRAIN_PIECE_WORKFLOW.md).
- The **Encounter Grass** controls remove or restore only the blade clusters
  whose source-weighted rendered geometry is centered in each selected terrain
  cell. Removal filters the owning source triangles instead of moving skin
  joints below the map, and atomically enables source-tint cleanup; neighboring
  cell clusters and authored grass-prefab copies remain untouched even though
  source modules straddle tile boundaries.
- Selecting an environment object shows only the actions that object supports.
- Selecting a gameplay-preview Pokemon in Game view shows starting position,
  rotation, runtime-resolved scale, and reset. Route 1 terrain tools are hidden.
- Terrain-following Pokemon retain ordinary world-position controls; only the
  Autochess board opts into the integer terrain-grid position editor.
- Runtime-owned scale is read-only. Board and bench positions are snapped by
  PokemonAutochess, not by title-specific Phlosion code.

## Persistence

- `scenes/route1.scene.json` and
  `config/environment/route1_board_layout.json` own the Route 1 source
  baseline and its proposed entry-board registration. The authored scene
  starts with zero overrides so customization begins from the imported route.
- `scenes/route1_5.scene.json` and
  `config/environment/route1_5_board_layout.json` independently own the pinned
  Route 1.5 board.
- `config/editor/game_preview_layouts.json` stores per-preview Pokemon starting
  position and rotation overrides.
- Cooked/private source assets remain outside Git according to the project
  asset policy.

Opening or selecting an item must not create an override. Only a completed
edit or confirmed project command may write these files.

The built package module is generated beneath
`.phlosion/packages/<configuration>/phlosion.tile-tools/` and remains ignored.
Its source is versioned only in the sibling `PhlosionPackages` repository.
