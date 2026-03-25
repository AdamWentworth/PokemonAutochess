## Arena Backdrop Plan

This project should treat the board as the hero and the surrounding environment as a static route shell.

The target look is a `2.5D` arena:

- The board remains highly readable and gameplay-first.
- The outer space is dressed with static `3D` scenery for immersion.
- We avoid turning the combat arena into a fully explorable level.
- Route progression should change the arena mood in a visible but not distracting way.

### Core Direction

- Keep the board centered and readable.
- Use a painted or stylized board surface rather than realistic terrain tiles.
- Surround the board with a static `3D` shell that suggests the current route.
- Keep the tallest or busiest props away from the central combat space.
- Let `Viridian Forest` be the first truly dense forest/shrine-feeling arena.

### Current Route Progression

The current route chain in the repo is:

1. `route1`
2. `route1_5`
3. `route22`
4. `route2`
5. `viridian_forest`
6. `route3`

Note: the current repo uses `route22`, not `route26`.

### Route Theme Targets

#### Route 1 / Route 1.5

- Open roadside grassland between Pallet and Viridian.
- Lots of breathing room.
- Sparse trees.
- Tall grass and fence-post language.
- Soft road/path hints beyond the board.

#### Route 22

- Rougher foothill roadside.
- More stone and scrub.
- Less town-adjacent and less manicured than Route 1.
- Still open, but starting to feel wilder.

#### Route 2

- Forest edge transition.
- More tree mass at the perimeter.
- Less open than Route 1.
- Clear suggestion that the player is approaching Viridian Forest.

#### Viridian Forest

- Dense forest shell.
- Strongest tree-line and canopy framing so far.
- Mossy stones / old markers / shrine-adjacent mood.
- Still preserve combat readability inside the playable board.

#### Route 3

- Rocky route / wooded mountain pass.
- More ledges, boulders, and vertical stone shapes.
- Less pure forest than Viridian Forest.

### Implementation Strategy

Phase 1 should stay procedural and use the existing backdrop builder:

- route-aware board color theme
- raised arena ground or platform under/around the board
- outer terrain ring
- route-specific simple props
  - tree trunks and canopy blocks
  - stone outcrops
  - fence posts
  - grass clumps
  - shrine stones for Viridian Forest

This should live in the existing projected world backdrop path, not a new renderer path.

Relevant code:

- `src/game/runtime/session/SessionWorldBackdrop.cpp`
- `src/game/runtime/session/SessionProjectedWorldView.cpp`
- `src/game/runtime/session/SessionWorldRenderRuntime.cpp`
- `src/game/runtime/shared/world/SharedBoardGridBatches.cpp`

### Why This Direction

- It fills the current dead space outside the board.
- It gives each route a stronger identity without requiring a full environment art pipeline.
- It preserves the option to add authored `.glb` environment props later.
- It keeps the renderer and cache story simple because the backdrop remains static and route-scoped.

### Follow-On Work

After the first procedural route shells feel good:

1. Replace the board surface with more intentional painted tile treatments.
2. Add a small set of authored environment props where they are worth the effort.
3. Consider distant silhouette cards or skybox-style background layers for deeper atmosphere.
4. Keep the outer shell quieter than units, VFX, and combat silhouettes.
