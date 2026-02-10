# VFX Configs

This folder holds data-driven VFX definitions.

- `blend_presets.json`: named blending + depth-write defaults.
- `sprite_atlases.json`: atlas IDs mapped to texture paths and frame grids.
- `emitter_presets.json`: reusable emitter defaults (spawn, motion, curves).
- `recipes/*.recipe.json`: move/effect timelines composed from presets.

These files are scaffolding for the upcoming recipe runtime.
Current move VFX still run through existing C++ implementations.

