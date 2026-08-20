# Repository Architecture

Status: Active
Type: Architecture
Last updated: 2026-07-30

Phlosion development uses four separate ownership boundaries.

## 1. PhlosionEngine

Repository: `AdamWentworth/PhlosionEngine`

This public, reusable repository owns engine source, engine-owned shaders,
engine tests, build metadata, and the Phlosion runtime resource contracts. It
must not depend on Pokemon Autochess gameplay code or private source assets.

Pokemon Autochess pins an exact Phlosion Engine commit for reproducible builds.
When a sibling `PhlosionEngine` checkout exists, CMake uses it as a local
development override. Setting `PHLOSION_ENGINE_SOURCE_DIR` to an empty value
forces CMake to fetch the pinned public commit.

## 2. PhlosionVFX

Repository: `AdamWentworth/PhlosionVFX`

This reusable repository depends one-way on Phlosion Engine and owns authored
effect simulation, renderer-neutral VFX batches, runtime submission adapters,
reusable preview controllers, independent tests, and optional original stock
effects. It must not depend on Pokemon Autochess gameplay types or private
source assets.

Pokemon Autochess pins an exact Phlosion VFX commit. A sibling
`PhlosionVFX` checkout is the local development override; an empty
`PHLOSION_VFX_SOURCE_DIR` exercises the pinned remote dependency.

## 3. PokemonAutochess

This repository owns the game:

- rules, simulation, sessions, board layout, and configuration;
- Pokemon-specific presentation and VFX integration;
- asset IDs, manifests, cook recipes, and importer orchestration;
- tests and documentation for game behavior and visual parity.

It does not own a duplicate engine implementation. It also does not treat
GLB, Game Freak formats, or cooked `.phlo` files as source code merely because
they can be loaded by the game.

## 4. Private asset depot

The separately backed-up asset depot owns material that should not be
redistributed through either public code repository:

- legally acquired source dumps and proprietary formats;
- RenderDoc captures and reverse-engineering evidence;
- derived models, textures, animation payloads, and environment data when
  redistribution rights are absent or unclear;
- machine-generated cooked resources.

The intended local layout is:

```text
Projects/
  PhlosionEngine/
  PhlosionVFX/
  PokemonAutochess/
  PhlosionAssets/
    source/
    evidence/
    derived/
    cooked/
```

The depot may be a private repository only for files the owner is entitled to
store in that service. Large binary backup can instead use encrypted object
storage or another private artifact store. Changing a proprietary source file
to GLB, KTX2, or a Phlosion extension does not by itself grant redistribution
rights.

The local depot is established outside the repository. Runtime payloads can be
restored additively with `tools/assets/sync_asset_depot.ps1`. A versioned NAS
snapshot is now established through `tools/assets/backup_asset_depot.ps1`;
snapshot publication is blocked until every destination payload passes
SHA-256 verification.
The removed asset payloads also remain reachable in older Pokemon Autochess
Git commits until a separately reviewed history migration is performed;
removing them from the current branch tip is not a history purge.

## Asset flow

```text
private source/evidence
        |
        v
PokemonAutochess importer and Phlosion Forge recipes
        |
        v
private derived/cooked depot
        |
        v
local game content mount (ignored by Git)
        |
        v
Phlosion Engine runtime
```

The public game repository may contain original, licensed, or otherwise
redistributable assets. Every committed binary asset needs a known provenance
and redistribution decision.

## Dependency update rule

Shared-code updates are intentional multi-repository changes:

1. build and test the engine change independently when required;
2. push the engine commit and update the exact engine pin in Phlosion VFX;
3. build and test the VFX library independently;
4. publish the VFX commit and update both exact pins in Pokemon Autochess;
5. build the game with the fetch path as well as the local sibling paths;
6. run game tests and renderer qualification before promotion.

This keeps the game reproducible while allowing fast side-by-side engine
development.
