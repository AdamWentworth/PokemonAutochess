# VFX Pipeline (Colosseum-Style Direction)

This project already has solid low-level particle primitives. The next step is to author effects as **data-driven recipes** so we can iterate quickly without adding a new C++ class per move.

## Design Targets

1. `Billboard sprite particles` are the primary system.
2. `Flipbook atlases` are optional, used when one sprite is not enough.
3. `Special meshes` are reserved for arcs/rings/shockwaves that need cleaner silhouettes.
4. `Distortion` is opt-in and localized, not a default for every move.
5. `Recipes` define timing/choreography: emitters, bursts, curves, blend, and lifetime windows.

## Folder Layout

```text
assets/
  vfx/
    textures/
      common/
      moves/
        growl/
    meshes/
      common/
      moves/

config/
  vfx/
    blend_presets.json
    sprite_atlases.json
    emitter_presets.json
    recipes/
      _template.recipe.json
      growl.recipe.json
```

## Why This Split

- `assets/vfx/**` = actual art content.
- `config/vfx/**` = effect behavior and timing.
- Current C++ VFX classes can gradually migrate to recipe execution, one move at a time.

## Growl Recipe Direction

For growl, use a single timeline with one primary billboard cone emitter:

- One cone from tight to wide toward target direction.
- Fuzzy linear streaks inside that cone.
- Additive or premultiplied blend.
- Size/alpha curves define onset and decay.
- No flipbook/mesh/distortion required for this move.

## Migration Plan

1. Add a lightweight recipe loader (`config/vfx/recipes/*.json`).
2. Build a generic recipe runner on top of existing `ParticleSystem`.
3. Move growl to recipe first.
4. Move tackle/leaf/scratch next.
5. Keep specialized C++ VFX for outliers (fire tail, distortion-heavy effects).

