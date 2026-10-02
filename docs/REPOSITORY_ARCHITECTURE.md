# Repository Architecture

Status: Active
Type: Architecture
Last updated: 2026-10-02

Phlosion development separates engine services, optional adapters/tools, games and private content.

## 1. PhlosionEngine

Repository: `AdamWentworth/PhlosionEngine`

This public, reusable repository owns engine source, engine-owned shaders,
engine tests, build metadata, and the Phlosion runtime resource contracts. It
must not depend on Pokemon Autochess gameplay code or private source assets.

Pokemon Autochess pins an exact Phlosion Engine commit for reproducible builds.
When a sibling `PhlosionEngine` checkout exists, CMake uses it as a local
development override. Setting `PHLOSION_ENGINE_SOURCE_DIR` to an empty value
forces CMake to fetch the pinned public commit.

## 2. Optional adapters and tools

Reusable VFX is part of PhlosionEngine: effect hosts, authored batches, submission
and shared preview helpers. `Phlosion::VfxLibraryBridge` is an explicit optional
adapter to the independent `D:/Projects/VFX/VFXLibrary`. Autochess does not enable
that bridge. Its move presets and lab live in `src/game/vfx`.

PokemonModelSupport owns Pokemon source/material interpretation. PhlosionPackages
currently holds the shelved Tile Tools; `PAC_BUILD_TILE_TOOLS=OFF` keeps them out
of normal builds. Engine model support remains generic and built in.

The former PhlosionVFX checkout is being retired; no active game target consumes it.

## 3. PokemonAutochess

This repository owns the game:

- rules, simulation, sessions, board layout, and configuration;
- Pokemon-specific presentation and VFX integration;
- asset IDs, source-neutral manifests, publication inputs, and game cooks;
- tests and documentation for game behavior and visual parity.

It does not own a duplicate engine implementation. It also does not treat
GLB, Game Freak formats, or cooked `.phlo` files as source code merely because
they can be loaded by the game.

## 4. Private asset research

The private `PokemonSwitchAssetResearch` companion repository owns
source-game extraction, reverse engineering, source-specific qualification,
evidence, and publication recipes. Pokemon Autochess must configure, build,
test, and run without it. A deliberate publisher copies only source-neutral
manifests and canonical payloads into the ignored game asset view.

## 5. Private asset depot

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
  Phlosion/
    PhlosionEngine/
    PokemonModelSupport/
    PhlosionPackages/
  VFX/
    VFXLibrary/
  Games/
    PokemonAutochess/
    PokemonCyberpunk/
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
private PokemonSwitchAssetResearch recipes
        |
        v
private derived/cooked depot
        |
        v
source-neutral PokemonAutochess publication + game cook
        |
        v
Phlosion Engine runtime
```

Canonical Route 1 runtime hydration crosses that boundary through one cooked
PHSC file. Once mounted, its isolated virtual store supplies all canonical
published environment dependencies; runtime code has no environment GLB or
loose research-cache fallback. Project-owned board and authored-scene deltas
remain a separate game layer so editor changes do not mutate the preserved
source environment.

The public game repository may contain original, licensed, or otherwise
redistributable assets. Every committed binary asset needs a known provenance
and redistribution decision.

## Dependency update rule

Shared-code updates are intentional multi-repository changes:

1. build and test the engine change independently;
2. commit/publish the engine and update the game's exact engine pin;
3. publish and pin any separately changed adapter dependencies;
4. build the game with the fetch path as well as local checkouts;
5. run game tests and renderer qualification before promotion.

The VFX migration is committed locally and the engine pin includes it. Publishing
that revision and providing a remote distribution path for the model adapter
remain outstanding. Local builds are independently verified; clean remote builds
and eventual deletion of the former VFX repository await these publication steps.

This keeps the game reproducible while allowing fast side-by-side engine
development.
