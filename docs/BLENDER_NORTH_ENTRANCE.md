# Blender North Entrance

Status: Active
Type: Runbook
Last updated: 2026-09-09

**Route 1 - North Entrance** (`routes/route1-north-entrance`) is the fourth
independent Route 1 arena, after South Entrance, South Clearing and North
Terraces. It frames the northern route approach with two open terraces, a broad
encounter bed, the original eastern ramp and clear dirt benches.

## Preview and edit

Open `tools/environment/Preview North Entrance.cmd`, or select **Route 1 - North
Entrance** in **Scene / location**, then **Planning**, **Battle** or **Grass Test**.
Place Pokemon while stopped and press Play. The grass fixture starts Bulbasaur
inside the lower bed and Rattata on the upper terrace. F10 reveals concealed units
for visual debugging. Use OpenGL for the qualified materials.

Open `tools/environment/Open North Entrance in Blender.cmd` to edit:

`D:/ProjectData/Games/PokemonAutochess/EnvironmentResearch/Route1/authoring/north-entrance/Route1_NorthEntrance.blend`

The Autochess panel edits tile heights, surfaces and ramps. Props remain separate
in `PAC_PREFABS`; rocks and field details are in `PAC_EDIT_PATCH`. The original
reference geometry remains hidden in `LGPE_SOURCE_LOCKED`. Click **Save, Export
and Update Game**, or run:

```powershell
./tools/environment/export_route1_pilot.ps1 -Recipe config/environment/route1_north_entrance.authoring.json
./tools/environment/preview_route1_pilot.ps1 -Recipe config/environment/route1_north_entrance.authoring.json -Phase planning
```

Reopen the preview after an environment export. Terrain and prop edits do not
require a C++ rebuild. The saved blend is authoritative after initial seeding.

## Layout and fidelity

The board occupies **X=19..26, Z=-37..-30**, with reserve rows Z=-38 and Z=-29.
This is nine source tiles north and two east of North Terraces. The eastern
alignment keeps the original X=25..27 ramp inside the playable board. All 80
board/reserve centres match the rendered floor.

The upper terrace is 3 m high and the lower terrace is 2.5 m. The ledge permits
southbound jumps and blocks uphill movement; the eastern ramp remains walkable
in both directions. The narrow raised spur at X=18..20, Z=-32..-30 is flattened
into the lower lawn. Solid plants and canopies are excluded from the board and
reserves. The north bench's bank cells are lowered to the entrance lane's height;
both bench rows use dirt and remain free of encounter grass and decorative plants.

The backdrop uses original LGPE terrain and prop positions, preserving the
southern island and dirt pockets that were adapted for North Terraces gameplay.
Original geometry probes establish the eastern ramp and corner ramps farther
south. There are 750 editable metre tiles, X=6..35 and Z=-44..-20. The northern
lane and woodland continue beyond the source cutoff, with a short dirt trail at
X=19..22, Z=-44..-40 marking the route exit. Six rocks keep their source colours;
rounded closed ledges, dark inaccessible banks and perimeter foliage match the
other authored arenas. No fences are introduced.

Grass uses full-size Grass01 clumps from source records 4 and 5. Record 4 remains
at its original position in the southern backdrop. Record 5 is split into a
7 by 3 m main bed and a 1 by 1 m east return, keeping its southern edge outside
the player bench. These are separate editable props with stable IDs. Shared
rustling, concealment, attack reveals, unit memory and movement rules apply.
The other three scene sources and archives remain independent, and campaign
progression is unchanged.

## Publication and recovery

`config/environment/route1_north_entrance.authoring.json` owns all paths.
Publication qualifies the terrain, map, board and scene, then atomically activates
`content/phlosion/environment/north-entrance/arena.phscene`. Loose scene/map JSON
files are review mirrors. The private depot backs up runtime assets and immutable
source/archive revisions under `project-authored/route1-north-entrance`.

Use normal depot sync to restore runtime assets. Restore a missing source with:

```powershell
./tools/environment/restore_arena_source.ps1 -Recipe config/environment/route1_north_entrance.authoring.json
```

`tools/environment/blender/create_route1_arena.py` seeds new files from the
Entrance reference library and matching `source-layout-kit.json`. The blueprint
opts into original source placement throughout, specifies its bench clearances
and grass pieces, and records deliberate adaptations. **Do not reseed for routine
edits**. Export a newly seeded file in a fresh Blender process using the launcher.

## Qualification

`PAC_Tests.route1_north_entrance_contract` checks scene selection, all board and
reserve floor heights, dirt benches without cover, solid/plant clearance,
two-way ramp movement, one-way ledges, flying, concealment, original backdrop
terrain/grass, shadows and switching among all four arenas. The shared grass
rendering test checks rooted blades, local contacts and recovery here too.

Shared Blender checks preserve the working source:

- `verify_arena_tile_editability.py --cell 21 -35 --output <directory>` exercises
  the tile controls, independent props and grass movement export on a copy.
- `verify_arena_ledge_geometry.py --expected-rocks 6 --output <report.json>`
  checks wall joins, rounded corners, ramp junctions and source rock colours.
- `verify_arena_roundtrip.py --recipe config/environment/route1_north_entrance.authoring.json
  --output <directory>` checks identical terrain and archive bytes after export.

Use Blender's `--background --factory-startup --disable-autoexec <blend>
--python-exit-code 1 --python <script> -- <arguments>` form. For C++ changes build
the game/tests, Forge and editor/plugin pair. Also run the prior arena, bundle,
editor catalog and grass contracts, plus Python map/publication checks. Local
captures and reports are in `debug/north-entrance`.
