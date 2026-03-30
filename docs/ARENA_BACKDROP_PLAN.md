# Arena Backdrop Plan

Status: Active
Type: Roadmap
Last updated: 2026-03-31

This is a living visual-direction and implementation plan for the projected
arena backdrop path. The procedural route shell is already in the repo, but the
theme direction and follow-on work should stay active until the backdrops feel
finished.

## Current Role
- Keep the board as the hero and the surrounding space as a static route shell.
- Preserve gameplay readability first.
- Use the backdrop system to give each route a stronger identity without
  turning the arena into a fully explorable level.

## Current Implemented Baseline
- The current projected backdrop path already lives in:
  - `src/game/runtime/session/SessionWorldBackdrop.cpp`
  - `src/game/runtime/session/SessionProjectedWorldView.cpp`
  - `src/game/runtime/session/SessionWorldRenderRuntime.cpp`
- Route-aware shell styles already exist for:
  - `Route1`
  - `Route22Foothills`
  - `Route2ForestEdge`
  - `ViridianForestShrine`
  - `Route3MountainPass`
- The backdrop plan is therefore no longer a blank-slate idea. It is now the
  active direction guide for continuing to improve that implemented path.

## Core Direction
- Keep the board centered and readable.
- Use a painted or stylized board surface rather than realistic terrain tiles.
- Surround the board with a static `3D` shell that suggests the current route.
- Keep the tallest or busiest props away from the central combat space.
- Let `Viridian Forest` be the first truly dense forest/shrine-feeling arena.

## Current Route Progression
1. `route1`
2. `route1_5`
3. `route22`
4. `route2`
5. `viridian_forest`
6. `route3`

Note: the current repo uses `route22`, not `route26`.

## Route Theme Targets

### Route 1 / Route 1.5
- Open roadside grassland between Pallet and Viridian.
- Lots of breathing room.
- Sparse trees.
- Tall grass and fence-post language.
- Soft road/path hints beyond the board.

### Route 22
- Rougher foothill roadside.
- More stone and scrub.
- Less town-adjacent and less manicured than Route 1.
- Still open, but starting to feel wilder.

### Route 2
- Forest edge transition.
- More tree mass at the perimeter.
- Less open than Route 1.
- Clear suggestion that the player is approaching Viridian Forest.

### Viridian Forest
- Dense forest shell.
- Strongest tree-line and canopy framing so far.
- Mossy stones / old markers / shrine-adjacent mood.
- Still preserve combat readability inside the playable board.

### Route 3
- Rocky route / wooded mountain pass.
- More ledges, boulders, and vertical stone shapes.
- Less pure forest than Viridian Forest.

## Current Implementation Strategy
- Keep using the existing projected world backdrop path rather than inventing a
  separate renderer path.
- Keep the backdrop mostly static and route-scoped so renderer/cache behavior
  stays simple.
- Continue iterating through:
  - route-aware board color theme
  - raised arena ground or platform under/around the board
  - outer terrain ring
  - route-specific simple props
  - authored props only where they are worth the cost

## Why This Direction Still Matters
- It fills dead space outside the board.
- It gives each route a stronger identity without requiring a full environment
  art pipeline.
- It preserves the option to add authored `.glb` environment props later.
- It keeps the backdrop quieter than units, VFX, and combat silhouettes.

## Follow-On Work
1. Make the board surface itself feel more intentional and route-aware.
2. Improve the strongest route-specific prop silhouettes:
   - treelines
   - stones
   - shrine markers
   - fence/grass language
3. Add a small curated set of authored props where procedural shapes stop being
   enough.
4. Consider distant silhouette layers or sky-card treatment only if they deepen
   atmosphere without competing with gameplay readability.
5. Keep testing the shell against active combat, VFX, and UI readability rather
   than judging it only in empty scenes.
