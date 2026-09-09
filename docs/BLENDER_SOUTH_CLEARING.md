# Blender South Clearing

Status: Active
Type: Runbook
Last updated: 2026-09-09

**Route 1 - South Clearing** (`routes/route1-south-clearing`) is a separate
Blender-authored arena. South Entrance remains the startup map. The imported
clearing remains **Route 1 - South Clearing (Legacy)** for comparison.

## Preview and edit

Open `tools/environment/Preview South Clearing.cmd`, or select **Route 1 - South
Clearing** in **Scene / location**, then **Planning** or **Battle** in
**Scenario / starting setup**. Place Pokemon while stopped, then press Play.
Use OpenGL for the qualified materials.

Open `tools/environment/Open South Clearing in Blender.cmd` to edit. The Autochess
panel provides the same tile height, surface and ramp controls as
[South Entrance](BLENDER_ARENA_PILOT.md). Props are separate in `PAC_PREFABS`;
rocks and field details are separate meshes in `PAC_EDIT_PATCH`. Original LGPE
meshes remain hidden in `LGPE_SOURCE_LOCKED` as references.

The working source is:

`D:/ProjectData/Games/PokemonAutochess/EnvironmentResearch/Route1/authoring/south-clearing/Route1_SouthClearing.blend`

Click **Save, Export and Update Game**, or run:

```powershell
./tools/environment/export_route1_pilot.ps1 -Recipe config/environment/route1_south_clearing.authoring.json
./tools/environment/preview_route1_pilot.ps1 -Recipe config/environment/route1_south_clearing.authoring.json -Phase planning
```

Reopen the preview after an environment export; no C++ rebuild is needed. Shared
scripts retain their historical filenames; the recipe selects the correct source,
registration, archive, and scene. `-Capture` also follows the selected recipe.

## Layout and fidelity

The blueprint starts from recovered LGPE source tiles and applies the saved
Phlosion edits in `scenes/route1_5.scene.json`. Source terrace elevations are retained; the northern corner crest uses a lower
base level plus its ramp shape to reproduce the original diagonal surface. The board occupies **X=17..24, Z=-19..-12**; reserve rows
are Z=-20 and Z=-11. There are 840 editable 1 m terrain cells overall.

The left dirt square occupies X=17..20, Z=-17..-14. The right grass bed uses
grass01 record 3 with the editor's adjusted X pivot of 2242.21167 cm, retaining
its blade height, shape and density. The northern bed and the two hooked beds
farther south retain their own source records.

The northern board edge has an eastern half-metre drop and a western ramp. The
southern ramp spans the board. Terraces and side banks use the entrance's rounded
corners and closed wall/fringe/cap construction. Connected route lawn is light;
inaccessible banks and woodland are dark. Terrain and rocks cast projected shadows.

Above the enemy reserve row, the northeast-rising corner ramp uses source cells
(22, -22) and (23, -21) as **NE corner foot**, with (23, -22) as **NE corner crest**,
all at base height 3 (1.5 m). These three tiles reproduce the original LGPE
diagonal turn into the straight northern ramp. Their split triangular surfaces
match continuous runtime height sampling; the board and reserve rows are unchanged.
Use the Autochess **Shape** menu to edit corner feet and crests in any of the
four directions. A foot has one high corner; a crest has three.

Source trees, plants and rock colours are retained, with extra perimeter foliage.
The east-bank boulder sits 18 cm south of its source position to clear the rebuilt
bank. Small leaf/grass details are excluded from dirt and uneven surfaces. No
fences are introduced. Coherent ramp strips replace isolated source slope
classifications; outer terrain continues beneath the woodland. These choices are
recorded in `config/environment/route1_south_clearing_blueprint.json`.

Directional jumps, uphill blocking, flying, encounter-grass visibility, rustling,
attack reveals and unit memory use the existing gameplay systems. This independent
editor arena does not replace campaign progression.

## Publication and recovery

The recipe `config/environment/route1_south_clearing.authoring.json` owns all paths.
Publication qualifies the scene, terrain, map, board and composition, then atomically
replaces `content/phlosion/environment/south-clearing/arena.phscene`. Loose scene/map
JSON files are review mirrors; the archive activates them together. Runtime assets
and immutable Blender/archive revisions are backed up in the private depot under
`project-authored/route1-south-clearing`.

Restore runtime assets with the normal asset-depot sync. Restore a missing source:

```powershell
./tools/environment/restore_arena_source.ps1 -Recipe config/environment/route1_south_clearing.authoring.json
```

The bootstrap `tools/environment/blender/create_route1_south_clearing.py` records
the initial composition from the entrance's reference library and matching
`source-layout-kit.json`. **Do not rerun it for routine edits**: the saved clearing
blend becomes authoritative. Bootstrap output must differ from its input blend.

## Qualification

Build Forge, the game/tests, and the paired editor/plugin before testing C++ changes.
`PAC_Tests.route1_south_clearing_contract` checks all 80 rendered board/reserve floor
heights, the corner ramp surface, ramps, ledges, flying, grass sight, source suppression, prop clearance,
shadows and switching back to South Entrance. Also run the existing entrance,
editor catalog, arena bundle, and Python map/publication contracts.

The shared Blender checks apply with these arguments:

- `verify_arena_tile_editability.py --cell 19 -16 --output <directory>` edits a
  copy and verifies terrain/prop independence and grass movement export.
- `verify_arena_ledge_geometry.py --expected-rocks 7 --output <report.json>` checks
  wall/corner intersections, rounded ramp joins and original rock colours.
- `verify_arena_roundtrip.py --recipe config/environment/route1_south_clearing.authoring.json
  --output <directory>` verifies unchanged source exports to identical terrain and
  archive bytes.

Use Blender's `--background --factory-startup --disable-autoexec <blend>
--python-exit-code 1 --python <script> -- <arguments>` form. These checks preserve
the working source. Local visual comparisons and reports are in `debug/south-clearing`.
