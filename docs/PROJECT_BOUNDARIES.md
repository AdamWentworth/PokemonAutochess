# Pokemon Autochess project boundaries

Pokemon Autochess is a Phlosion game project, not a fork of the engine. Its
repository owns everything that would be nonsensical in a racing game or
shooter.

## Project-owned runtime and content

- Pokemon, moves, cards, shops, rounds, autochess simulation, board, benches,
  planning/battle modes, UI, saves, and game configuration;
- Route environments, scene overlays, terrain rules, encounter grass, project
  VFX bindings, Pokemon model conventions, and all cooked project content;
- LGPE import/qualification tools, canonical-scene decoder, Route 1 adapter,
  recovered CPU material-reference implementations, and source evidence.

## Project-owned editor extension

`PokemonAutochessEditorProject` supplies scene/game-preview catalogs, Pokemon
and VFX previews, Route 1 prefab previews, terrain catalogs and hotswap rules,
board/bench snapping, project commands, and authored-scene editing. Phlosion
Editor only hosts these capabilities through its generic project-plugin ABI.

The reusable projected-grid UI, tile selection, multi-level stamps, prefab
palette presentation, ramps, and platform controls come from the declared
`phlosion.tile-tools` package in the sibling `PhlosionPackages` monorepo. This
game supplies the Route 1 data and executes its edit requests; it does not own
the generic package implementation.

Opening a different project must load a different plugin and expose none of
the Pokemon-specific tools. It sees Tile Tools only if it independently
declares that package.

## Engine dependencies

The game consumes Phlosion Engine for its runtime/render/editor contracts,
Phlosion Packages for optional editor features, and Phlosion VFX for reusable
effect primitives. It must not add game headers or game vocabulary back to
any dependency.

`PAC_ProjectOwnershipBoundary`, `PAC_EngineSemanticBoundary`, and the compiled
layering tests enforce both directions of this rule.

## Generated and cached data

Cooked `.phlo`/`.phscene` content is project data, while `build/`, `.phlosion/`
plugin outputs, and `cache/` are generated. Caches may accelerate decoding,
shader compilation, or derived atlases, but a clean rebuild from authoritative
inputs must always work and cache keys must cover source hashes plus schema or
implementation versions.
