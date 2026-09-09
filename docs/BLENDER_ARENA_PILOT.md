# Blender environment workflow

Status: Active
Type: Runbook
Last updated: 2026-09-08

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
The editor does not expose source-tile painting for this authored mesh arena;
those controls remain available only on the original source-terrain scenes.

Run from the game repository:

```powershell
.\tools\environment\export_route1_pilot.ps1 -OpenBlender
.\tools\environment\export_route1_pilot.ps1
.\tools\environment\preview_route1_pilot.ps1 -Phase planning
.\tools\environment\preview_route1_pilot.ps1 -Phase battle -Capture
.\build\Release\PAC_Tests.exe --filter route1_arena_pilot_contract
```

The installer compiles the new terrain, validates the map data, installs the scene,
terrain and gameplay map together, and restores the previous installed files if
native validation fails. Runtime files:

- `scenes/route1_pilot.scene.json`
- `content/phlosion/environment/arena-pilot/terrain.phpatch`
- `config/environment/route1_pilot_gameplay.json`

Successful installs copy the current `.blend`, scene and terrain to the private
asset depot under `pokemon-autochess/source/project-authored/route1-arena-pilot`
and `pokemon-autochess/runtime/content/phlosion/environment/arena-pilot`.
The LGPE cooked environment remains required for materials and prefab geometry.

The Blender viewport is a layout preview; Phlosion is the visual authority.
Use OpenGL for this pilot. The existing Direct3D material defect also affects
the original route and remains separate renderer work.

## Current ownership and recovery

The maintained Blender tool lives in `tools/environment/blender`. Routine edits
and exports use only this game checkout, Blender, Forge, and the restored asset
library. The research repository is needed for extracting or publishing a new
reference library, not for editing the existing environment.

`config/environment/route1_south_entrance.authoring.json` records the current
scene, board, runtime outputs, working source, and depot backup paths. Stable
`route1-pilot` IDs and the existing `.blend` filename are retained so saved
placements, launchers, checkpoints, and scene references remain valid.

To restore runtime assets on a fresh workstation, set `PHLOSION_ASSET_DEPOT` and
run `tools/assets/sync_asset_depot.ps1`. Restore a missing working source with:

```powershell
./tools/environment/restore_arena_source.ps1
```

Restoration verifies the copied source hash and refuses to overwrite an existing
working file. Use `-Destination` for a separate review copy. Configure the build,
build Forge and the editor/plugin pair, then use the normal Blender launcher.
The engine revision pinned in CMake must be available in the engine repository;
when publishing coordinated changes, publish that engine revision before the game.

C++ gameplay saves use [Auto Reload](EDITOR_GAMEPLAY_RELOAD.md). Engine/editor
C++ changes require the paired build and an editor restart. Blender exports need
no C++ rebuild; reopen the game preview to load the updated environment.

## Gameplay data

Export also writes `arena-map.json`, installed as
`config/environment/route1_pilot_gameplay.json`. It records 840 authored terrain
cells, the 64 board cells, 16 reserve cells, four encounter-grass regions, and
height changes across directed shared edges. Playability follows board
registration; dark grass paint does not determine movement permission.

Grass footprints use the published core cells transformed with each grass
prefab. This preserves hooked footprints and prop movement instead of treating
an entire grass bed's rectangular bounding box as cover. The decorative outer
blade ring is excluded. Cover behavior and directional traversal are not enabled
by this data; see [the map contract](AUTHORED_ARENA_MAP.md).

## Verify the south entrance

```powershell
./tools/environment/check_south_entrance.ps1
./tools/environment/check_south_entrance.ps1 -IncludeBlender -Capture
```

The default command builds the game/tests/Forge and the editor/plugin pair,
checks documentation and map data, validates the authored scene, and runs the
focused arena, movement, and model-visibility regressions. Missing private assets
are failures with restore instructions. `-NoBuild` is only for already current
binaries; the report records that compilation and paired-build proof were omitted.

`-IncludeBlender` exercises tile height, paint, and ramp edits on a copy, checks
rounded ledge walls/caps and ramp junctions, and verifies that an unedited export
reproduces the installed scene, gameplay data, and terrain bytes. `-Capture`
creates a hidden OpenGL fixed-camera screenshot for visual review. Results are
written to `debug/south-entrance-check`. The capture and round-trip cook use the
standard Release build. No check changes the working Blender source.

The native arena contract samples all board/reserve centres, ramp interpolation,
the ledge drop, mesh transforms and reloads, missing ground, source-scene
restoration, and shadow participation. Expected heights qualify the current
layout; intentional terrain edits may require updating those expectations.

## Historical compositions

One-time composition, dressing, and repair recipes are retained under
`tools/environment/archive/route1`. Their pass-specific JSON seeds remain in
`config/environment`, and source/runtime checkpoints remain in the private
asset depot. These recipes explain how the approved map was developed; daily
editing never reruns them. The original source-repair plan is archived as
[historical context](archive/ARENA_BACKDROP_PLAN.md).

The original entrance and south-clearing scenes still depend on the legacy
source-terrain repair path. Their data and behavior remain available as reference
for the future clearing. New authored mesh preparation and ground sampling live
in `AuthoredEnvironmentPatch` and `AuthoredGroundSurface`; source-preservation
predicates live under the runtime scene directory's `legacy` folder.
