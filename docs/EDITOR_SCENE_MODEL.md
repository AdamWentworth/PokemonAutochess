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

World scene asset
└── Route 1
    ├── mode: Classic | Adventure
    └── phase: Planning | Battle
```

Boot is not a `.phscene`. It initializes the application and presents loading
progress. The editor's Boot preview replays that presentation over the already
initialized runtime.

Main Menu and Starter Selection are frontend states. They may eventually use
presentation scene assets, but the state itself is not a scene file.

Route 1 is one reusable world scene. Classic/Adventure rules and
Planning/Battle phases are session state layered over Route 1. Adding a second
route means adding another scene asset, not duplicating every route for every
mode and phase.

## Editor surfaces

- **Scenes** is the project scene-asset catalog. It currently contains the
  cooked Route 1 `.phscene`.
- **Scene Hierarchy** is intended to match Unity's object/component hierarchy
  for the currently open scene. The current adapter still exposes coarse,
  read-only source groups; stable per-object nodes and editable components are
  subsequent editor work.
- **Scene** in the central Viewport is the frozen/editor-camera asset view.
- **Game** in the central Viewport is the real game renderer and state.
- **Game Preview** selects a named state in that one embedded runtime.

## Runtime lifetime

Opening the project initializes and prewarms the game once. This first warm-up
can take several seconds because models, textures, shaders, VFX, and UI
resources become resident.

Afterward, selecting Main Menu, Starter Selection, Route 1 Planning, or Route 1
Battle changes scripts or restores a deterministic snapshot in the same
runtime. It does not start another process, create another window, or replay
the boot sequence unless the explicit Boot preview is selected.

This is the intended long-term pattern:

1. open the Engine editor;
2. open a game project;
3. open a world scene from Scenes for authoring;
4. choose a Game Preview for a warm gameplay state;
5. use Play, Pause, Step, and Stop without rebuilding or rebooting the game.
