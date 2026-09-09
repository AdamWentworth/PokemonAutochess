# Blender North Terraces

Status: Active
Type: Runbook
Last updated: 2026-09-09

**Route 1 - North Terraces** (`routes/route1-north-terraces`) is the third
Blender-authored Route 1 arena. Entrance, South Clearing and North Terraces are
successive route landmarks, approximately one quarter, halfway and three quarters
along the route. These are composition landmarks, not measured route distances.
South Entrance remains the startup map; each arena has an independent source.
[North Entrance](BLENDER_NORTH_ENTRANCE.md) completes the sequence nine tiles
farther north, with an eastern board alignment that includes the source ramp.

## Preview and edit

Open `tools/environment/Preview North Terraces.cmd`, or select **Route 1 - North
Terraces** in **Scene / location**, then **Planning**, **Battle** or **Grass Test**
in **Scenario / starting setup**. Use OpenGL for the qualified materials.
The grass test starts allied Bulbasaur in the east grass and enemy Rattata in the
open. Place Pokemon while stopped, then press Play.

Open `tools/environment/Open North Terraces in Blender.cmd` to edit the source:

`D:/ProjectData/Games/PokemonAutochess/EnvironmentResearch/Route1/authoring/north-terraces/Route1_NorthTerraces.blend`

Use the Autochess panel for tile heights, surfaces, straight ramps and corner
ramps. Props remain separate in `PAC_PREFABS`; rocks and field details are separate
meshes in `PAC_EDIT_PATCH`. Original reference meshes stay hidden in
`LGPE_SOURCE_LOCKED`. Click **Save, Export and Update Game**, or run:

```powershell
./tools/environment/export_route1_pilot.ps1 -Recipe config/environment/route1_north_terraces.authoring.json
./tools/environment/preview_route1_pilot.ps1 -Recipe config/environment/route1_north_terraces.authoring.json -Phase planning
```

Reopen the preview after an environment export. No C++ rebuild is needed for
subsequent terrain or prop edits. The shared scripts use the recipe to choose
this arena's source, board registration, scene and archive.

## Layout and fidelity

The board advances **nine source tiles north** of South Clearing, matching the
Entrance-to-Clearing increment. It occupies **X=17..24, Z=-28..-21**; reserve rows
are Z=-29 and Z=-20. The backdrop contains 840 editable metre tiles, X=6..35 and
Z=-38..-11. The northern terrain preserves the approved route terraces. The
southern backdrop, Z=-19..-11, uses the original LGPE layout rather than South
Clearing's arena adaptations. This regional override also restores source prop
positions and leaves the separate South Clearing scene unchanged.

The six-cell island at X=21..23, Z=-25..-24 is flattened to the surrounding 2 m
light lawn, leaving an open board. Both eight-cell reserve rows are dirt, with
their small decorative plants cleared away. The broad northern encounter bed,
upper dirt pockets and east ramp frame the
enemy reserve row. Source geometry probes establish the NE corner foot at
(20,-28) and the upper ramp strip X=25..27, Z=-33. Other terraces retain their
terrain, with solid shrubs and canopies cleared out of the board and reserve rows.
This also excludes the tall leafy `small_grass_02` shrub at (20,-27.2). The
blueprint records these deliberate gameplay adaptations.

Encounter grass uses full-size Grass01 clumps from source records 3, 4 and 5.
The northern bed is split into two editable pieces, leaving a four-tile notch
for the enemy bench while retaining grass beyond its east end. Its main footprint
is X=21..28, Z=-32..-29; the east return is X=25..28, Z=-29..-28.
Record 3 returns to its original X=2350 cm pivot in this scene.
The southern backdrop restores its smaller dirt pockets, two-level western bank,
source shrub at (19.655,-13.393), and narrower southern ramp X=22..25, Z=-13.
Six source
rocks retain their original colours. The scene has rounded closed ledges, dark
inaccessible lawn, source vegetation and additional woodland around the edges.
It uses the shared directional jumps, uphill blocking, flying, grass visibility,
rustling, attack reveals and unit memory. Campaign progression is unchanged.

## Publication and recovery

`config/environment/route1_north_terraces.authoring.json` owns all paths.
Publication qualifies the terrain, board, map and scene, then atomically activates
`content/phlosion/environment/north-terraces/arena.phscene`. Loose JSON files are
review mirrors. Runtime assets and immutable Blender/archive revisions are backed
up in the private depot under `project-authored/route1-north-terraces`.

Use the normal asset-depot sync to restore runtime assets. Restore a missing source:

```powershell
./tools/environment/restore_arena_source.ps1 -Recipe config/environment/route1_north_terraces.authoring.json
```

`tools/environment/blender/create_route1_arena.py` records the initial seed from
the Entrance reference library, its matching `source-layout-kit.json`, and the
explicit blueprint. It refuses an existing output. **Do not reseed for routine
edits**: the saved North Terraces blend is authoritative after creation.
After seeding, use the normal export launcher in a fresh Blender process.

## Qualification

`PAC_Tests.route1_north_terraces_contract` checks all 80 board/reserve centres
against the visible floor, dirt benches, level movement across the removed island,
terrace jumps, ramps, flying, grass sight, prop clearance, source suppression,
shadows and switching among all three arenas. It also checks the restored southern
terrain and prop positions. The shared grass
rendering test checks North Terraces contacts, rooted blades and recovery too.

The shared Blender checks preserve the working source:

- `verify_arena_tile_editability.py --cell 18 -26 --output <directory>` exercises
  terrain controls, independent props and grass movement export on a copy.
- `verify_arena_ledge_geometry.py --expected-rocks 6 --output <report.json>` checks
  walls, rounded corners, ramp joins and source rock colours.
- `verify_arena_roundtrip.py --recipe config/environment/route1_north_terraces.authoring.json
  --output <directory>` checks identical exported terrain and archive bytes.

Use Blender's `--background --factory-startup --disable-autoexec <blend>
--python-exit-code 1 --python <script> -- <arguments>` form. Build the game/tests,
Forge and editor/plugin pair for C++ changes. Local captures and reports are in
`debug/north-terraces`. Also run both previous arena contracts, editor catalog,
arena bundle, encounter grass and Python map/publication checks.
