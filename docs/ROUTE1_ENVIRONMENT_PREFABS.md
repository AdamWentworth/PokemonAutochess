# Route 1 Environment Prefabs

Status: Active
Last updated: 2026-07-30

## Decision

Route 1 is available in two complementary forms:

1. `content/phlosion/scenes/route1.phscene` remains the promoted, complete
   world composition.
2. `content/phlosion/objects/environment/route1/` contains reusable or
   inspectable `.phlo` environment prefabs cooked from the same accepted LGPE
   evidence.

The prefab cook is additive. It does not replace, rewrite, or weaken the
promoted whole-scene restore point.

## Current Prefab Library

| Prefab | Source boundary | Motion binding |
| --- | --- | --- |
| Encounter Grass 01 | Exact Game Freak build-model | Recovered LGPE encounter-grass joint wind |
| Encounter Grass 02 | Exact Game Freak build-model | Recovered LGPE encounter-grass joint wind |
| Flowers 02 | Exact Game Freak build-model | Recovered LGPE vegetation joint wind |
| Flowers 04 | Exact Game Freak build-model | Recovered LGPE vegetation joint wind |
| Small Grass 02 | Exact Game Freak build-model | Recovered LGPE vegetation joint wind |
| Tree 001 | Representative archetype derived from exact Route 1 `tree001` topology | No local wind in the recovered source vertex program |
| Tree 002 | Representative archetype derived from exact Route 1 `tree002` topology | No local wind in the recovered source vertex program |
| Tree 003 | Representative archetype derived from exact Route 1 `tree003` topology | No local wind in the recovered source vertex program |
| Tree 004 | Representative archetype derived from exact Route 1 `tree004` topology | No local wind in the recovered source vertex program |
| Tree 005 | Representative archetype derived from exact Route 1 `tree005` topology | No local wind in the recovered source vertex program |
| Tree 006 | Representative archetype derived from exact Route 1 `tree006` topology | No local wind in the recovered source vertex program |

The first five are true reusable archetypes with separate Game Freak model
files. The six tree types require a qualified derivation because the Route 1
model flattened their instances into world-space meshes. They are not blind
connected-polygon fragments:

- the cooker identifies the trunk material topology;
- it finds the largest connected trunk component;
- nearby trunk components are clustered within 100 source centimetres;
- every triangle is assigned to its nearest proven tree centre in XZ;
- one complete representative is isolated and recentered for the prefab;
- all 47 discovered source centres are retained in PHLO metadata.

The source distribution is 11, 11, 12, 2, 2, and 9 instances for Tree 001
through Tree 006. The cook fails if those evidence counts change.

The former `Route 1 Baked Foliage Collection` was removed. It was effectively
the scene without its floor and was not a useful reusable object boundary.

The Inspector previews read the selected cooked `.phlo` directly. They do not
parse RomFS files, Blender files, GLB files, or the whole `.phscene` as an
unqualified fallback.

## How the Pieces Fit

```text
.phlo environment prefab
  geometry and source vertex channels
  material-family and texture bindings
  skeleton/skin contract where authored
  supported motion-driver binding
  pivot, bounds, and semantic tags

             referenced by
                  |
                  v

.phscene world composition
  prefab asset IDs
  instance transforms and hierarchy
  per-instance wind phase/seed
  directional light and environment settings
  projected light/cloud inputs
  projected shadow atlas/input
  fog and route-level overrides
```

A prefab answers, "what is this object and how is it capable of rendering?"
A scene answers, "where is it, what environment is it in, and what
time-varying inputs does this instance receive?"

## Editor Categories

The cooked PHLO manifest owns its `prefab_kind`; the editor does not infer
category from a Pokémon-specific directory name.

| Prefab kind | Editor category | Examples |
| --- | --- | --- |
| `Character` | Character Prefabs | Bulbasaur, Ivysaur |
| `Object` | Object Prefabs | Poké Ball and future gameplay props |
| `LgpeEnvironment` | Environment Prefabs | Trees, flowers, grass |
| Project/plugin asset | VFX Prefabs | Tackle, Growl |

The hidden Growl helper meshes remain excluded from the top-level browser.

## Format and Ownership

The public high-level object format is `.phlo`. Exact independent Game Freak
build-model prefabs currently use a self-contained PHRC archive whose private
files preserve the validated canonical LGPE payload.

Derived Route 1 tree PHLOs are intentionally lightweight (roughly 1.5-2.1
KiB). They declare the complete Route 1 `.phscene` as a hashed required
dependency and store the selector/provenance needed to isolate their geometry.
They do not duplicate the roughly 87 MiB scene payload six times.

As the low-level Phlosion resource split matures, a logical prefab references:

- `.phmesh` for geometry and vertex streams;
- `.phmat` for material family, render state, parameters, and texture roles;
- KTX2 for cooked textures;
- `.phskel` and `.phanim` when authored or reconstructed joint motion exists;
- `.phcol` where a reusable collision shape belongs to the object.

The user-facing `.phlo` identity and `.phscene` placements do not need to
change when private payloads are split.

### Floor, Ledge, and Platform Semantics

A raw floor image is not a prefab:

- KTX2 stores the cooked image;
- `.phmat` stores the material family, texture roles, blending, and parameters;
- `.phmesh` stores the geometry and vertex streams;
- a `.phlo` floor tile, ledge module, or platform module composes those
  resources into a reusable loadable object;
- `.phscene` places those modules and supplies route-level lighting, projected
  shadow, fog, and wind inputs.

This keeps the asset browser semantic. A texture can still receive its own
Inspector preview without pretending it is a placeable object.

### Lighting

Lighting is not baked into a prefab as one frozen color.

- The prefab retains source material family, alpha/cull/depth state, texture
  roles, vertex channels, toon/rim response, and projected-light/shadow
  participation.
- The `.phscene` supplies the Route 1 light rig, projected-light coordinates,
  projected shadow input, fog, and other environmental values.
- The renderer implements the material family consistently across OpenGL,
  Vulkan, and D3D12.

The Inspector creates a representative Route 1 preview environment so the
isolated asset evaluates the same material contract.

### Wind and Motion

The prefab retains capability and rig binding; the scene/runtime supplies time
and instance variation.

- Encounter grass binds the recovered encounter-grass joint driver.
- Flowers and Small Grass 02 bind the recovered vegetation joint driver.
- Their nominal cycle is four seconds, matching the promoted Route 1 runtime.
- Placement-owned phase belongs to `.phscene`, so repeated instances do not
  have to sway in lockstep.
- Route-baked tree groups declare no local wind because the recovered source
  vertex programs contain no local deformation. Adding wind would be a new
  interpretation and must be qualified separately.

Metadata alone does not make motion happen. Inspector and gameplay runtime
both execute the declared driver against the prefab skin data.

## Placement

Reusable prefab geometry previews at a bounds-centered, floor-aligned origin.
Route placement remains scene data:

- 54 flower/small-grass transforms are exact Game Freak-authored build-model
  placements;
- encounter-grass records retain their evidence-constrained expanded grid and
  per-instance phase policy;
- tree PHLO metadata retains 47 topology-derived source centres;
- reconstructed per-tree rotation/scale and migration of the complete
  `.phscene` to prefab instances remain separate proof steps.

## Next Extraction Pass

Extraction proceeds one evidence boundary at a time:

1. split the ten remaining route-baked grass/shrub mesh families into useful
   qualified archetypes;
2. preview and validate each family against the promoted complete scene;
3. define floor material resources without classifying raw textures as
   prefabs;
4. derive reusable light-ground and dark-platform lawn modules;
5. derive ledge/cliff modules while preserving the lawn-to-stripe-to-overhang
   material layering;
6. publish complete platform modules only where mesh and material boundaries
   support reusable placement;
7. migrate `route1.phscene` to prefab references incrementally, retaining the
   promoted monolithic scene as the visual and content-hash restore path.

This preserves the complete world while building a truthful, useful prefab
library instead of accumulating progressively smaller scene collections.
