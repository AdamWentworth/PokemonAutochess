# Blender arena pilot

The current pilot is **Route 1 - Southern Entrance** (pass 10). It rebuilds the
Phlosion editor's south-entrance tile blueprint with new Blender geometry.
The recovered layout supplies cell heights and lawn/dirt assignments; Blender
owns the resulting terrain and prop placements. The original LGPE meshes stay
hidden as reference. Phlosion renders the recovered materials and animated grass.

The editor starts in the independent pilot, `routes/route1-pilot`. The original
Route 1 and Route 1.5 remain available in the scene list. The pilot is not part
of normal route progression. `tools/environment/Preview Route 1 Arena.cmd`
opens its Planning game preview directly.

## Edit the tile blueprint

1. Double-click `tools/environment/Open Route 1 in Blender.cmd`.
2. Press **N** over the viewport and open the **Autochess** tab.
3. Click **Select Blueprint Tiles**. This selects the `Tile blueprint` mesh in
   face-selection mode. Each face represents one **1 m** cell. Select one or
   several faces using Blender's normal selection tools.
4. Choose **Height level (0.5 m)**, **Ground** (Lawn/Dirt/Dark lawn), and **Shape** (Flat
   or a direction of ascent). Click **Apply to Selected Tiles**. All three
   selected values apply to those faces, and the generated terrain updates.
5. Press **Tab** to leave Edit Mode. Hide `Tile blueprint` in the Outliner when
   you want an unobstructed material preview.
6. Click **Save, Export and Update Game**, then reopen the game preview.

Raise adjacent cells to create a ledge. Give a cell a ramp shape to connect its
base elevation to the next 0.5 m level. North is Blender **+Y**, toward the back
of the game camera. Keep tile positions fixed; change their attributes using
the panel. Moving blueprint vertices directly does not change cell coordinates.

The logical map is saved in the `.blend` as face attributes on `Tile blueprint`:
`PAC_x`, `PAC_z`, `PAC_height`, `PAC_surface`, and `PAC_ramp`. The generated mesh
is separate in `PAC_EDIT_PATCH`. Its caps, ramps and ledge faces are built from
these cells; dirt transitions follow the outside of contiguous dirt cells.
Export also writes `tile-layout.json` alongside the mesh and scene outputs.

Dark lawn uses the recovered LGPE raised-lawn colour with its grass textures.
It marks enclosed banks, decorative islands and the forest border. The connected
route, including terraces reached by ramps, uses light lawn. This is saved tile
paint; height alone does not choose the colour. Dirt cells retain their paint.
Convex corners above a continuous lower floor use an 18 cm radius, or 24 cm
beside ramps. Their lower-floor pockets follow the neighbouring slope, so the
rounded wall meets both the ramp and the clearing. The cap, grassy fringe and earth
wall meet without overlapping shells. This keeps their shadow boundary closed
and gives the top and fringe the same grass colour.

**Applying tile changes regenerates the terrain mesh.** Direct sculpting of
that mesh remains exportable, but another tile update replaces those sculpted
changes. Save a separate copy before switching approaches.

## Edit brush and other props

In **PAC_PREFABS**, each grass bed, tree, plant and sign is independently
editable. Use **G** to move, **R then Z** to rotate, **S** to scale and **Shift D**
to duplicate. Terrain updates retain all prop transforms. Adjust a prop's
height manually when moving it to a different terrain level.

All four encounter areas use their correct source records. The hooked side beds
and northern square retain their source positions and full scale. The side beds
and southern threshold use **enc_grass02**; the northern square uses **enc_grass01**.
The threshold's north/south depth is shortened to match the current editor's
cleared southern rows (Blender Y=2..4.05 m). Its blade height and east/west width
are unchanged. This is a bed transform, not a per-blade crop. White fences remain removed.
There are 27 trees, 11 shrubs, four grass beds, 27 small plants and one sign.

Field rocks, stone chips, path pebbles and small field foliage live in
**PAC_EDIT_PATCH** as independent detail meshes. Their original geometry, UVs
and materials are retained. The rocks use their original vertex colours; the
painted green caps from pass 7 have been removed. Move a rock or a named field-detail group using
**G**; tile updates preserve these objects. Most planting follows source positions;
the shrubs nearest the reserve row sit just beyond it, and rocks also dress the
newly exposed rear shelf. Each remains editable.

Fine foliage is restricted to upright tufts and small ground patches. Broad
baked leaf sheets are excluded, and plants crossing unequal tile heights are
omitted. This keeps source cleanup geometry from floating over the rebuilt floor.

Pass 8 adds tree clusters and undergrowth along both edges and the rear boundary.
The new canopy bounds stay outside the board and reserve rows. Solid cliff faces
now meet the tile caps at their exact boundaries; the thin grass rim joins at
corners. This removes the gaps caused by the earlier outward-offset wall faces.
For the independent arena, ground, cliff and rock materials cast projected
shadows from the authored meshes. Original route shadow policy is retained.

The old entrance's main ledge crosses the board's northern rows. Its western
ramp spans four cells. The upper dirt turn, central dirt patch, lower entrance
strip and stepped side banks follow the recovered editor tile assignments.
The remaining adaptation is deliberate: coherent ramp strips replace isolated
slope classifications, and southern edge cells continue beneath the foreground.
The board's existing 8 x 8 registration and two reserve rows are retained.
Four cells at source X=15..18, Z=-14 are now 1.5 m high instead of 2 m. This
exposes a one-metre grass shelf between two half-metre rear ledges. The native
height contract checks all three consecutive levels, so the shelf cannot
silently turn back into a single tall wall.

Pokémon and board overlays sample the actual authored terrain height. These
are visual features: camouflage, one-way north-to-south drops, uphill blocking,
jump animations and flying exceptions remain future gameplay work. Ground
units can currently cross ledges without directional restrictions.

## Source, export and preview

The editable source on this workstation is:

`D:\ProjectData\Games\PokemonAutochess\EnvironmentResearch\Route1\authoring\arena-pilot\Route1_GardenClearing.blend`

The filename is retained for existing launchers. The exporter reads that saved
file; it does not recreate the starting layout. Phlosion edits to pilot props
will be overwritten by the next Blender export, so keep permanent changes in Blender.

Run from the game repository:

```powershell
.\tools\environment\export_route1_pilot.ps1 -OpenBlender
.\tools\environment\export_route1_pilot.ps1
.\tools\environment\preview_route1_pilot.ps1 -Phase planning
.\tools\environment\preview_route1_pilot.ps1 -Phase battle -Capture
.\build\Release\PAC_Tests.exe --filter route1_arena_pilot_contract
```

The installer compiles the new terrain, installs and validates the scene, and
restores the previous installed files if native validation fails. Runtime files:

- `scenes/route1_pilot.scene.json`
- `content/phlosion/environment/arena-pilot/terrain.phpatch`

Successful installs copy the current `.blend`, scene and terrain to the private
asset depot under `pokemon-autochess/source/project-authored/route1-arena-pilot`
and `pokemon-autochess/runtime/content/phlosion/environment/arena-pilot`.
The LGPE cooked environment remains required for materials and prefab geometry.

The Blender viewport is a layout preview; Phlosion is the visual authority.
Use OpenGL for this pilot. The existing Direct3D material defect also affects
the original route and remains separate renderer work.

## Reproduce and verify

The companion research bridge contains `arena_pilot.py`, `arena_terrain.py`,
and `arena_tiles.py`. It runs ordinary Blender Python; no Blender MCP service
is needed. C++ changes require the existing paired editor/plugin build workflow;
ordinary Blender edits require no C++ build.

`PhlosionForge export-route1-authoring-kit <output.json>` exports both recovered
source and current editor tile records. `compose_route1_tiles.py` takes
`--bridge-root`, `--blueprint` and `--source-kit` inside Blender to create pass 6
from an earlier pilot source. It deliberately replaces the composition and is
only for reproducing the starting layout. Daily authoring uses the saved file.
The pass 6 seed is `config/environment/route1_pilot_pass6_blueprint.json`.
Its cell map is `config/environment/route1_pilot_tile_layout.json`; omit
`--source-kit` to reproduce that checked-in map. The source audit verifies
all 210 cells in that earlier entrance crop retain their editor heights and ground types.
See [the tile blueprint](art/route1_south_tile_blueprint.svg) for a top view.

To recreate pass 7, run `refine_route1_details.py` inside Blender with
`--bridge-root`, `--library-root` and `--blueprint`. The blueprint is
`config/environment/route1_pilot_pass7_blueprint.json`, with cells in
`route1_pilot_pass7_tile_layout.json`. This one-time composition tool replaces
the earlier approximate dressing; ordinary exports retain artist edits.
Pass 7 retains all ground-type assignments and changes only the four rear
height cells described above.

Then run `refine_route1_edges.py` with `--bridge-root` and
`--blueprint config/environment/route1_pilot_pass8_edges.json` for the current
border dressing. It keeps the existing tile map and grass beds, restores source
rock colour and regenerates the repaired ledge geometry. Daily exports preserve
manual edits; this composition script is only for recreating the pass.

Previous complete source/runtime compositions are backed up beside the source
under `versions/pass-01` through `versions/pass-07`, and copied into the depot.
Pass 8 is checkpointed under `versions/pass-08`.
Run `refine_route1_ledge_finish.py --bridge-root <bridge directory>` inside
Blender to reproduce pass 9 from pass 8: it retains all heights, ramps, dirt,
props and detail meshes, applies dark lawn, and regenerates the rounded ledges.
Pass 9 is checkpointed under `versions/pass-09`.
For pass 10, run `refine_route1_access_finish.py` inside Blender with
`--bridge-root`, `--source-kit <authoring directory>/source-layout-kit.json`,
and `--blueprint config/environment/route1_pilot_pass10_finish.json`.
This one-time paint seed follows full shared-edge connections from the entrance,
within the route corridor, and retains the source's dark decorative islands.
It does not install navigation or collision restrictions. Daily tile editing
and exports retain the saved surface choices rather than repainting by access.
The current source/runtime checkpoint is `versions/pass-10`.
Earlier freeform path and ledge tools remain available for those older files.

`verify_arena_tile_editability.py` uses a separate copy to edit one tile's height,
ground type and ramp through the real Blender operator. It checks the generated
ramp, preservation of other cells, props and detail meshes, export of a moved grass bed, and
that the original source hash is unchanged. The native pilot contract checks
all 80 board/reserve centres, interpolation on the western ramp, the ledge drop,
patch transforms and reloads, missing-ground handling and restoration of the
original route after switching back. These samples qualify the seeded layout;
intentional layout changes may require updating its expected heights.

`verify_arena_ledge_geometry.py` casts rays at actual wall triangles, including
samples near tile corners and the top fringe, and checks source rock colours.
Rounded corners are checked against their circular boundary, with additional
vertical rays verifying both the upper cap and the exposed lower-floor pocket.
Every curved wall segment is also checked at three heights, including the seven
ramp junctions and their sloped lower floors.
The native contract also verifies that authored ground reaches the shadow atlas.
