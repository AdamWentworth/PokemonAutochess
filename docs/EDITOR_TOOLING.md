# PokemonAutochess Editor Tooling

Status: Active
Type: Architecture
Last updated: 2026-09-12

PokemonAutochess extends the reusable Phlosion Editor through
`PokemonAutochessEditorProject`. Blender owns environment authoring; the editor
inspects published scenes, configures Pokemon starting positions and runs tests.

## Daily workflow

1. Choose a location in **Scenes** or **Scene / location** above the viewport.
   The four Route 1 arenas are grouped together; Flat Dirt Experiment is separate.
2. Choose **Planning**, **Battle** or **Crowded Battle** under **Starting setups**.
   Prepared grass, ledge, travel and Earthquake fixtures appear under **Tests**
   only where the location supports them. One click loads a scenario.
3. In **Game** view, place Pokemon while stopped, then press **Play**. Use
   **Pause / Step** to inspect and **Stop** to restore the starting setup.

**Scene** view shows the published environment with the editor camera. Scenery
and board registration are read-only. Edit them in Blender and export the arena.
The read-only board entry remains in the hierarchy, alongside editable Pokemon
when a game scenario is loaded. Imported scenery markers, hidden-source trees,
source-prefab aliases and old terrain-repair tools are absent.

Frontend previews live under **Frontend / menus**. Oak's Lab starter selection
remains a backdrop without a battlefield. See [the scene model](EDITOR_SCENE_MODEL.md)
for the complete catalog and the scene/scenario distinction.

## Boundaries

- The project descriptor owns available locations. The dedicated preview
  catalog owns retained scenario IDs, grouping and activation metadata.
- The game plugin exposes gameplay board information and preview units. Blender
  arenas never publish source scenery records as editable layout objects.
- Pokemon movement/rotation handles appear only in Game view while stopped.
  Placement snaps to legal board or bench slots; height follows the terrain and
  species scale is owned by the runtime.
- Pokemon, VFX and cooked prefab inspectors remain available through Assets.
  Reused cooked geometry and materials remain required runtime dependencies.
- Phlosion draws common controls from the plugin's capabilities. Read-only
  environments do not show transform tools. Viewport handles are clipped to the
  image and cannot paint over scene/scenario controls.
- Historical source-scene mutation helpers remain for compatibility; current
  arenas reject those mutations and do not load the tile-tools editor package.

## Persistence and builds

Each arena recipe under `config/environment` publishes its Blender source into
an atomic arena archive. See [the Blender pilot workflow](BLENDER_ARENA_PILOT.md),
[South Clearing](BLENDER_SOUTH_CLEARING.md), [North Terraces](BLENDER_NORTH_TERRACES.md),
[North Entrance](BLENDER_NORTH_ENTRANCE.md), and [Flat Experiment](FLAT_ARENA_EXPERIMENT.md).

`config/editor/game_preview_layouts.json` stores per-scenario Pokemon starting
position and rotation overrides. Only a completed placement edit/reset writes
those overrides. Retiring a scene from the catalog does not delete saved data.
Cooked/private assets remain outside Git according to the asset policy.

Game-code saves use the existing gameplay reload workflow. Changes to the
Phlosion editor UI require an editor restart. Build matching editor/plugin pairs:

```powershell
./tools/housekeeping/build_editor_pair.ps1 -Configuration All
```

Projected environment shadows use a cache identity derived from the generated
depth image and its dimensions. Arenas sharing a board position still receive
their own shadows, and revisiting unchanged content reuses the existing texture.
The current projection matrix is rebound with the image on each scene load.

Catalog and hierarchy CPU contracts guard scene ownership, default Planning
setups, frontend separation and unit addressing. Editor captures on OpenGL,
Vulkan and D3D12 verify both Scene and Game views with expected-content checks;
see the [renderer parity contract](RENDERER_PARITY_CONTRACT.md).
