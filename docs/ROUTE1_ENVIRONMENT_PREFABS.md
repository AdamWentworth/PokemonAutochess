# Route 1 Environment Prefabs

Status: Active first milestone  
Last updated: 2026-07-30

## Decision

Route 1 is available in two complementary forms:

1. `content/phlosion/scenes/route1.phscene` remains the promoted,
   complete world composition.
2. `content/phlosion/objects/environment/route1/` contains reusable or
   inspectable `.phlo` environment prefabs cooked from the same accepted LGPE
   evidence.

The prefab cook is additive. It does not replace, rewrite, or weaken the
promoted whole-scene restore point.

## First Prefab Library

| Prefab | Source boundary | Motion binding |
| --- | --- | --- |
| Encounter Grass 01 | Exact Game Freak build-model | Recovered LGPE encounter-grass joint wind |
| Encounter Grass 02 | Exact Game Freak build-model | Recovered LGPE encounter-grass joint wind |
| Flowers 02 | Exact Game Freak build-model | Recovered LGPE vegetation joint wind |
| Flowers 04 | Exact Game Freak build-model | Recovered LGPE vegetation joint wind |
| Small Grass 02 | Exact Game Freak build-model | Recovered LGPE vegetation joint wind |
| Route 1 Baked Foliage Collection | Exact tree and ground-foliage mesh groups flattened into the Route 1 road model | No local wind; the recovered source vertex programs are static |

The first five are true reusable archetypes with separate Game Freak model
files and authored placement records. The baked-foliage asset is deliberately
called a collection: the route source flattened its six tree groups and ten
ground-foliage groups into world-space road-model geometry. It is exact
geometry, material, and texture evidence, but it is not misrepresented as an
original per-tree prefab.

The Inspector previews read the selected cooked `.phlo` directly. They do not
parse RomFS files, Blender files, GLB files, or the whole `.phscene` as a
fallback.

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

A prefab answers, “what is this object and how is it capable of rendering?”
A scene answers, “where is it, what environment is it in, and what time-varying
inputs does this instance receive?”

## Format and Ownership

The public high-level object format is `.phlo`. The current environment PHLO
milestone is a self-contained PHRC archive whose private files preserve the
validated canonical LGPE payload. This is a compatibility implementation of
the final dependency graph, not a new public source format.

As the low-level Phlosion resource split matures, the same logical prefab will
reference:

- `.phmesh` for geometry and vertex streams;
- `.phmat` for material family, render state, parameters, and texture roles;
- KTX2 for cooked textures;
- `.phskel` and `.phanim` when authored or reconstructed joint motion exists;
- `.phcol` where a reusable collision shape belongs to the object.

The user-facing `.phlo` identity and the `.phscene` placements do not need to
change when those private payloads are split.

### Lighting

Lighting is not baked into a prefab as one frozen color.

- The prefab retains the source material family, alpha/cull/depth state,
  texture roles, vertex channels, toon/rim response, and whether it receives
  projected light or shadow.
- The `.phscene` supplies the Route 1 light rig, projected-light coordinates,
  projected shadow input, fog, and other environmental values.
- The renderer implements the material family consistently across OpenGL,
  Vulkan, and D3D12.

The Inspector creates a representative Route 1 preview environment so the
isolated asset can evaluate the same material contract.

### Wind and Motion

The prefab retains the capability and rig binding; the scene/runtime supplies
time and instance variation.

- Encounter grass binds the recovered encounter-grass joint driver.
- Flowers and Small Grass 02 bind the recovered vegetation joint driver.
- Their nominal cycle is four seconds, matching the promoted Route 1 runtime.
- Placement-owned phase belongs to the `.phscene`, so repeated instances do
  not have to sway in lockstep.
- The route-baked tree and ground-foliage groups declare no local wind because
  the recovered source vertex programs contain no local deformation. Adding
  wind to those assets would be a new interpretation and must be qualified
  separately.

Metadata alone does not make motion happen. The renderer/runtime evaluates the
declared driver against the prefab's skin data. The Inspector and gameplay
runtime both execute that implementation.

## Placement

Reusable prefab geometry stays in source-local space and previews at a
bounds-centered, floor-aligned origin. Route placement transforms remain scene
data:

- the 54 flower/small-grass transforms are exact Game Freak-authored
  build-model placements;
- encounter-grass records retain their evidence-constrained expanded grid and
  per-instance phase policy;
- baked road-model foliage remains a world-space collection until a more
  granular source-backed split is proven.

## Next Extraction Pass

The next useful pass is not a blind connected-polygon split. It is:

1. recover any remaining original tree/build-model archetypes and placement
   records from the unpacked source;
2. where the source genuinely contains only flattened road geometry, derive
   candidate instances while retaining explicit `derived_from_route_mesh`
   provenance;
3. compare reconstructed instances against the current baked-foliage
   collection;
4. only then publish individual tree/shrub `.phlo` assets;
5. migrate `route1.phscene` to reference qualified prefab IDs while keeping a
   visual and content-hash restore path to the promoted monolithic cook.

This preserves the complete world today while giving the editor useful pieces
immediately and a truthful path to finer granularity.
