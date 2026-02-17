# VFX Configs

This folder now tracks only runtime configs that are actively consumed.

## Live Runtime Configs

- `moves/<move>_draw_passes.json`
  - Used by move-specific VFX code to map draw passes to mesh/texture/shader inputs.
  - Growl example: `moves/growl_draw_passes.json`.

## Scope

- Keep configs aligned with assets that currently ship and render in game.
- Do not keep placeholder recipe/atlas/preset configs here unless they are wired into runtime.
