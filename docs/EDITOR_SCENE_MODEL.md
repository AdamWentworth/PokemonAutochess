# Pokemon Autochess Editor Scene Model

Status: Active
Type: Architecture
Last updated: 2026-09-19

## Scenes and scenarios

A **scene** is a location. A **scenario** is a starting setup at that location:
Pokemon placements, game phase, or a prepared mechanic test. Selecting another
scenario does not create another environment asset.

The editor scene catalog contains the four Blender-authored Route 1 arenas,
followed by the separate flat-board experiment:

| Location | Stable scene ID |
| --- | --- |
| South Entrance | `routes/route1-pilot` |
| South Clearing | `routes/route1-south-clearing` |
| North Terraces | `routes/route1-north-terraces` |
| North Entrance | `routes/route1-north-entrance` |
| Flat Dirt Experiment (startup) | `routes/route1-flat-experiment` |

`phlosion.project.json` selects the Flat Dirt Experiment at startup. Legacy
gameplay and parity fixtures can explicitly select the older imported Route 1
layout; those fixtures do not follow the editor's startup selection. README
captures explicitly select the flat arena through their own starter snapshot.

The Scenes panel and **Scene / location** selector expose the same catalog.
One click opens a location. The open location is highlighted. Archived imported
maps, the published source reference, and unbuilt route placeholders are absent
from this working catalog. Their runtime scripts and private assets have not
been deleted; future locations can return when they have an authored environment.

The Scenarios panel and **Scenario / starting setup** selector expose the same
setups for the open location. **Starting setups** contains Planning, Battle and,
where provided, Crowded Battle. **Tests** contains the location's grass, ledge,
travel or Earthquake fixtures. Planning is first and is the default when opening
a location. Only scenarios supported by that location appear.

Clicking a scenario loads it in Game view with simulation stopped. **Play** runs
it, **Pause / Step** inspects it, and **Stop** restores the starting setup.
**Reload Starting Setup** reloads the chosen fixture with saved unit placements.
Scenario IDs remain stable for the retained setups.

## Environment authoring and preview

Blender owns scenery, terrain, vegetation, ledges, material assignment and the
board registration. The authoring recipe publishes the terrain, scene, gameplay
map and board registration into one validated arena archive. See
[the Blender pilot workflow](BLENDER_ARENA_PILOT.md) and the individual arena
runbooks for authoring and recovery.

**Scene** view inspects the published environment with the editor camera and
optional board guides. It has no imported-prefab markers, source suppression
handles, scenery transform gizmos or tile-repair tools. The hierarchy retains
read-only **Autochess Board + Benches** information. The old hidden-source tree
and one-to-one source-prefab asset aliases are not exposed for Blender arenas.

**Game** view shows the real game camera and runtime. While stopped, Pokemon
starting positions and rotations remain editable. Legal board/bench snapping,
terrain grounding, runtime-owned scale and per-scenario saved placements still
apply. Removing scenery handles must not remove or renumber units incorrectly.

All five arenas currently reuse the cooked Route 1 asset package for materials
and prefab geometry. This is an asset dependency, not an instruction to display
or edit the original imported layout. Keep those cooked dependencies available
when publishing or syncing the private asset depot.

## Frontend

**Frontend / menus** is separate from route scenarios. It contains Boot Sequence,
Main Menu, and the Classic and Adventure starter-selection setups under
**Oak's Lab**. The lab is a Blender-authored frontend backdrop. It has no board,
benches or battle. See [Starter Lab Backdrop](STARTER_LAB_BACKDROP.md).

Boot replays the loading presentation over the warm runtime. Classic/Adventure
are game rules and frontend modes, not duplicate environment assets.

## Ownership and persistence

- `phlosion.project.json` owns the active location catalog.
- `PokemonAutochessEditorPreviewCatalog` owns scenario definitions and groups.
- Each `config/environment/*.authoring.json` recipe identifies its Blender
  source, board registration, scene document, gameplay map and arena archive.
- `config/editor/game_preview_layouts.json` owns saved Pokemon starting-position
  and rotation overrides. Opening a location does not rewrite those placements.
- Reusable Phlosion UI draws the catalogs and available object capabilities;
  the game plugin decides which objects and operations it exposes.

The source-terrain mutation modules remain available for compatibility tests and
historical content. They are outside the current editor authoring workflow. The
project no longer loads the legacy tile-tools editor package.

OpenGL, Vulkan and D3D12 must expose the same scene/scenario content, unit editing
and scenery presentation. Follow the [renderer parity contract](RENDERER_PARITY_CONTRACT.md).
