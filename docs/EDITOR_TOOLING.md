# PokemonAutochess Editor Tooling

Status: Active
Last updated: 2026-08-02

PokemonAutochess extends the reusable Phlosion Editor through
`PokemonAutochessEditorProject`. The game repository owns every tool whose
meaning depends on PokemonAutochess, LGPE evidence, Route 1, or the Autochess
board.

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

- A Scene or environment root exposes project-wide layout guides, the collapsed
  terrain editor, and PokemonAutochess project commands.
- Enabling tile mode expands the terrain editor and activates projected cell
  selection in Scene view.
- Selecting an environment object shows only the actions that object supports.
- Selecting a gameplay-preview Pokemon in Game view shows starting position,
  rotation, runtime-resolved scale, and reset. Route 1 terrain tools are hidden.
- Terrain-following Pokemon retain ordinary world-position controls; only the
  Autochess board opts into the integer terrain-grid position editor.
- Runtime-owned scale is read-only. Board and bench positions are snapped by
  PokemonAutochess, not by title-specific Phlosion code.

## Persistence

- `scenes/route1.scene.json` stores Route 1 authored environment and board
  layout changes.
- `config/editor/game_preview_layouts.json` stores per-preview Pokemon starting
  position and rotation overrides.
- Cooked/private source assets remain outside Git according to the project
  asset policy.

Opening or selecting an item must not create an override. Only a completed
edit or confirmed project command may write these files.
