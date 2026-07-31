# Pokemon Autochess Editor Scene Model

Status: Active
Last updated: 2026-07-30

## Semantic model

Pokemon Autochess keeps assets, UI flow, and session state distinct:

```text
Application lifecycle
└── Boot / loading presentation

Frontend runtime state
├── Main Menu
└── Starter Selection

Cooked world scene
└── Route 1
    ├── mode: Classic | Adventure
    └── phase: Planning | Battle

Runtime route stages
├── Route 1.5
├── Route 22
├── Route 2
├── Viridian Forest
└── Route 3
```

Boot is not a `.phscene`. It initializes the application and presents loading
progress. The editor's Boot preview replays that presentation over the already
initialized runtime.

Main Menu and Starter Selection are frontend states. They may eventually use
presentation scene assets, but the state itself is not a scene file.

Route 1 is currently the only standalone cooked `.phscene`.
Classic/Adventure rules and Planning/Battle phases are session state layered
over it.

The game also has real combat scripts and procedural backdrop themes for Route
1.5, Route 22, Route 2, Viridian Forest, and Route 3. The editor catalogs these
as **runtime stages** so they can be opened in the already-warm Game view. A
runtime stage is deliberately not presented as a source-faithful world scene:
it points at the existing game script and theme, but it has no independently
authored/cooked `.phscene` yet. When another route environment is authored, it
should become a cooked world scene rather than a copy of Route 1.

## Editor surfaces

- **Scenes** is the project scene catalog. It separates the cooked Route 1
  world from the existing playable runtime route stages.
- **Scene Hierarchy** is intended to match Unity's object/component hierarchy
  for the currently open cooked scene. The current adapter exposes coarse,
  read-only source groups with selection-specific properties; stable
  per-object nodes and editable components are subsequent editor work.
- **Inspector** reports the properties of the selected hierarchy object,
  scene, or asset. This milestone is read-only, so it explains what the
  selection is without pretending that edits can already be saved.
- **Assets** is the cooked runtime asset registry. It discovers `.phscene`
  worlds and `.phlo` prefabs as the useful top-level entries. A prefab's mesh,
  skeleton, animations, materials, and textures remain owned dependencies
  within that prefab rather than separate peer rows. Route 1 keeps its complete
  world alongside inspector-previewable environment prefabs; their ownership
  and evidence boundaries are defined in
  `docs/ROUTE1_ENVIRONMENT_PREFABS.md`.
- **Scene** in the central Viewport is the frozen/editor-camera asset view.
- **Game** in the central Viewport is the real game renderer and state.
- **Game Preview** selects a named state in that one embedded runtime.

Selecting a Pokemon `.phlo` opens an embedded 3D Inspector preview decoded
from that cooked prefab. It supports orbit, pan, zoom, animation playback,
material and texture isolation, wireframe, and a cooked-skeleton overlay.
Move-only auxiliary geometry remains in the cooked object but is omitted from
the ordinary idle preview until a matching move-state preview exists. The
viewer is deliberately read-only; Blender remains the source inspection and
authoring tool.

Selecting a Route 1 environment `.phlo` opens the same Inspector surface with
its exact LGPE material families and declared motion driver. These previews are
decoded from the cooked prefab itself and centered at a floor-aligned local
origin; Route 1 placement remains scene-owned.

## Runtime lifetime

Opening the project initializes and prewarms the game once. This first warm-up
can take several seconds because models, textures, shaders, VFX, and UI
resources become resident.

Afterward, selecting Main Menu, Starter Selection, a Route 1 planning/battle
snapshot, or one of the runtime route stages changes scripts or restores a
deterministic snapshot in the same runtime. It does not start another process,
create another window, or replay the boot sequence unless the explicit Boot
preview is selected.

This is the intended long-term pattern:

1. open the Engine editor;
2. open a game project;
3. open a world scene from Scenes for authoring;
4. choose a Game Preview for a warm gameplay state;
5. use Play, Pause, Step, and Stop without rebuilding or rebooting the game.
