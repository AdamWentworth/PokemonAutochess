# Route 1 Environment Prefabs

Status: Active
Type: Architecture
Last updated: 2026-08-21

## Decision

Route 1 is available in two complementary forms:

1. `content/phlosion/scenes/route1.phscene` remains the promoted, complete
   world composition.
2. `content/phlosion/objects/environment/route1/` contains reusable or
   inspectable `.phlo` environment prefabs cooked from the same accepted
   published environment package.

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
| Autochess Board Ground Patch | Generated 100 cm quad carrying exact source ground attributes and the recovered lawn-cap material | Static source vertex program |
| 25 source mesh groups | Exact non-tree/non-terrain canonical mesh boundary | Declared source vertex behavior |
| 23 terrain assemblies | Connected Game Freak cliff body paired with its exact cap/transition/fringe component | Static source vertex program |

The first five are true reusable archetypes with separate Game Freak model
files. The six tree types require a qualified derivation because the Route 1
model flattened their instances into world-space meshes. They are not blind
connected-polygon fragments:

- connected trunk topology proves each tree family's source instance count;
- every material stream stores those instances as repeated, contiguous source
  vertex blocks in the same order;
- the cooker selects the matching canopy, projected-shadow, and trunk block
  for one complete representative, then recenters it for the prefab;
- Tree 006's one-vertex foliage block variations are resolved from the
  inter-instance discontinuities in source order;
- no nearest-centre triangle assignment is used, so a nearby canopy cannot
  leak into the representative tree;
- the selected vertex count is retained and checked when the preview is built;
- all 47 discovered source centres are retained in PHLO metadata.

The source distribution is 11, 11, 12, 2, 2, and 9 instances for Tree 001
through Tree 006. Their selected representatives contain 1,340, 1,340, 2,541,
1,057, 1,045, and 1,027 source vertices respectively. The cook or preview
fails if those evidence counts change.

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
| `PublishedEnvironment` | Environment Prefabs | Trees, flowers, grass |
| Project/plugin asset | VFX Prefabs | Tackle, Growl |

The hidden Growl helper meshes remain excluded from the top-level browser.

## Format and Ownership

The public high-level object format is `.phlo`. Exact independent source
build-model prefabs currently use a self-contained PHRC archive whose private
files preserve the validated canonical published payload. Older cooked PHLOs
with the `LgpeEnvironment` kind remain readable through a narrow compatibility
alias; all new cooks emit `PublishedEnvironment`.

Derived Route 1 tree PHLOs are intentionally lightweight (roughly 2-4 KiB).
They declare the complete Route 1 `.phscene` as a hashed required
dependency and store the selector/provenance needed to isolate their geometry.
They do not duplicate the roughly 87 MiB scene payload six times.

Source-mesh and terrain-assembly PHLOs use the same lightweight dependency
model. Terrain selectors retain mesh index, assembly index, expected assembly
count, source pivot, bounds, profile role, and the connectivity/pairing proof;
they do not copy the complete Route 1 archive for every editor entry.

The board-ground PHLO uses that lightweight model as well. Its runtime
prototype is hidden in the untouched scene. The board-clearance tool creates a
project-owned, scaled instance only when requested, so the original Route 1
composition and its whole-scene restore point remain unchanged.

The gameplay board itself is not baked into the environment PHSCENE. It is a
virtual editor layout object backed by the active scene's schema-6 board
manifest. The proposed entry board uses
`config/environment/route1_board_layout.json` at `(17,-10)` over a deliberately
untouched source scene; the pinned second board uses
`config/environment/route1_5_board_layout.json` at `(17,-19)`.
Each registration stores the integer terrain-grid origin, elevation, board
extent, bench-slot count, integer bench gap, and enabled north/south bench
rows; source anchor and cell size derive from the recovered Route 1 metre grid.
Moving the object updates the editor overlay immediately and persists only the
active scene's values. Both layouts use zero bench-gap cells, and the one-click
board-clearance pass covers the board and both bench footprints.

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

Route 1 now cooks `route1/terrain_tileset` as the semantic PHLO dependency for
its authoring grid. The tile records themselves live in the project-owned scene
document: integer X/Z coordinates, 50 cm elevation levels, light/dark lawn
roles, and flat or directional-ramp shapes. Runtime-derived neighbor seams
create exposed ledge walls, so top, ramp, and cliff pieces cannot drift apart.
Each shared side retains both corner heights. Ramp-side junctions can therefore
taper the decoded cliff profile and material-13 leafy carrier to zero at a
matching corner instead of collapsing the ramp side into a misplaced rectangular
wall.

The dirt/lawn join is also derived from the shared terrain topology. A
matching-profile lawn donates its continuous world-space target-family
`Color0` at the seam for both flat dirt and dirt ramps. Flat dirt resolves back
to its normal field and a ramp resolves back to the recovered sign-ramp field
across the source 30 cm material-19 ribbon. The outer 5 cm row first joins the
repeat-equivalent clean-lawn UV2 endpoint to the boundary-lawn endpoint,
keeping the leafy atlas contour organic without a straight color line or a
protruding ledge-like patch. Lawn-to-lawn neighbors sample the same target
field at their shared world coordinates, so edited cells cannot form square
tone islands.

Encounter-grass source paint follows the encounter record rather than becoming
permanent terrain color. When a canonical encounter record is disabled, its
collision-core cells and the manifest's full eight-neighbor fringe are
qualified for cleanup. Any
still-visible light-lawn cell keeps its original mesh and texture coordinates
but receives the neutral source lawn `Color0`; adjoining dirt ribbons blend to
that same value. This removes the orphaned east-side tint at Route 1 cells
`(25,-18)` through `(25,-14)`, including the diagonal corner cells, without
flattening the route's other authored ground-color variation.

When a flat Dirt Path prefab replaces lawn, its reusable vertex paint is the
modal clean level-2 material-19 control recovered from 47 of 55 clean-soil
samples: `(0.905882, 0.815686, 0.631373, 1.0)`. This deliberately discards
blue/green route paint that belonged beneath the former encounter-grass
footprint while retaining the canonical UV0/UV1 fields, dirt texture blend,
UV2 edge ribbon, and projected lighting. The directly affected one-cell dirt
dependency at an authored join uses the same clean control so it cannot
reintroduce a square seam. Unrelated source dirt keeps its source-family paint,
and a dirt ramp keeps the sign-ramp field.

The tile-set contract also supports an exact source-cell reference for
canonical terrain that cannot be represented faithfully by the flat/ramp
vocabulary. A referenced patch clips and reuses the donors' original LGPE
ground, cliff, leafy-fringe, and cleanup triangles with their original material
and vertex streams. Adjacent references sharing one translation are unioned
before clipping, so source triangles crossing an internal cell boundary occur
once rather than once per tile. Route 1 maps source
`(19..21,-13..-15)` to target `(14..16,-13..-15)` as one coherent patch. At
its outer boundary, donor cleanup carriers may spill into a neighboring cell
when the destination edge's endpoint heights differ. When both sides have the
same complete tile profile, canonical and donor crossing vertices are trimmed
to their shared source-grid plane. Exact references use their donors' recovered elevation, surface, and ramp profile for that test; the
front row remains three `ramp_south` cells. Along the continuous west edge,
source `(19,-13)` is clipped at `x=1900 cm` before translation, while canonical
target `(13,-13)` is clipped at `x=1400 cm`. Source `(18,-13)` is not imported,
so its incompatible west-facing side and lower dirt neighbor `(18,-12)` cannot
reverse the ledge beside target `(12,-13)`. The true north-facing drop retains
its required donor cliff and leafy overhang. Referenced-cell perimeter masks
use triangle-centroid ownership before plane trimming. This preserves the irregular
multi-tier platform end without a rectangular split, a top gap, or an
incompatible westward cliff continuation. No heuristic source-profile label
becomes a synthesized dirt or lawn ramp.
At a height-changing boundary, the exact source patch owns its complete cliff
and crossing underside strip. At `(15,-12)/(15,-13)`, donor material-18
triangle 191 is retained with donor cliff triangles 448-451, while the
adjacent triangle 192 completes its lower band from within source `(20,-12)`.
The competing canonical carriers centered in target `(15,-12)` are removed.
Plane trimming remains reserved for matching-height outer boundaries.
For ordinary authored terrain, a changed endpoint profile also invalidates the
old canonical ledge's complete decoded 25 cm companion band. Cleanup therefore
removes paired cliff/fringe triangles just inside the neighboring cell even
when none of their vertices crosses the edited cell plane. Matching profiles
retain their source carriers, and exact-reference patches continue to use the
donor ownership rules above.
The editor presents this dependency as three semantic prefab groups: ground,
directional ramps, and raised ledge/platform tiles. Platform mode also exposes
an exact footprint builder: independently toggled source-grid `(X,Z)`, flat
`L#`, and sloped `L#-L#+1` labels; a persistent hovered-cell readout; a
non-destructive per-corner cyan ghost, connected/bounds/grow/shrink selection,
source-level sampling, and one atomic profiled-platform command. The profile
policy can preserve each cell's current flat/ramp shape, restore each recovered
LGPE source shape, force flat, or apply an explicit direction. Compatible
source ledge/fringe geometry is retained. Changed boundaries reconstruct both
the material-18 bowed cliff and the exact three-row material-13 leafy carrier
decoded from mesh 32 group 2. Adjacent rebuilt edges form one directed contour,
so both materials advance their source UV fields continuously across long side
walls instead of restarting or mirroring per tile. The cliff preserves the
source's independent band vertices, lower green control color, UV2 switch, and
48 cm crown; the fringe preserves its dark-green crown and two sloped carrier
rows. Light lawn, dark lawn, and dirt path retain their `+1` cards as quick
actions.

The editor tile clipboard treats any selected footprint as a temporary stamp
of this same tile-set PHLO. It copies surface, shape, visual variant, optional
source reference, and both exact and anchor-relative elevation without creating
another asset or duplicating material payloads. Exact paste preserves
source-grid levels and donor references for repairs; relative paste maps one
real copied cell to the destination and keeps all signed tier offsets. Both
reject out-of-route targets atomically and regenerate all ground, ramp,
dirt-boundary, and ledge neighbor relationships at the destination.

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
- the editor/runtime adapter now partitions those same exact material-stream
  vertex blocks into 47 independently transformable source instances with
  floor-aligned pivots;
- the source distribution remains 11/11/12/2/2/9, and the split is rejected if
  any triangle crosses an instance boundary or the partition fails to preserve
  a complete polygon group;
- reconstructed source-authored per-tree rotation/scale and migration of the
  complete `.phscene` to native prefab references remain separate proof steps.
- seven coarse terrain source meshes are decomposed into 23 independently
  editable body/cap assemblies; the split rejects topology drift and validates
  that every polygon-group index is preserved exactly once;
- Assets mirrors every editable hierarchy object one-to-one as a source-bound
  prefab entry. Repeated entries share their PHLO prototype rather than
  duplicating cooked geometry. It also exposes the non-placeable Route 1
  terrain-tile-set PHLO used by cell authoring.

## Next Extraction Pass

Extraction proceeds one evidence boundary at a time:

1. split the ten remaining route-baked grass/shrub mesh families into useful
   qualified archetypes;
2. preview and validate each family against the promoted complete scene;
3. define floor material resources without classifying raw textures as
   prefabs;
4. **Complete first slice:** qualify reusable light/dark lawn cells with exact
   source attributes, directional ramps, and derived ledge walls;
5. add optional footprint/spline controls above the tile layer and exact 23
   source terrain assemblies without weakening their
   lawn-to-stripe-to-overhang layering;
6. **Complete first slice:** publish project-authored raised-platform prefab
   actions for light lawn, dark lawn, and dirt path on the qualified tile-set
   PHLO; keep their ledge geometry neighbor-derived rather than duplicating
   brittle directional wall meshes;
7. migrate `route1.phscene` to prefab references incrementally, retaining the
   promoted monolithic scene as the visual and content-hash restore path.

This preserves the complete world while building a truthful, useful prefab
library instead of accumulating progressively smaller scene collections.
