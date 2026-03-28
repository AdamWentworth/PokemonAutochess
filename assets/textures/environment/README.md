## Nature RMXP Reference Kit

This folder is the canonical home for the small curated set of usable `2D` / `2.5D`
environment textures we extracted from the imported `NatureRMXP` atlas.

Kept files:

- `source_atlas.png`
  - The full original atlas used for future extraction work.
- `board_dirt_grass_border_4x4.png`
  - The dirt board kit with grassy border.
  - Runtime board texture.
- `grass_fill_2x2.png`
  - Clean grass fill tiles for repeating ground or bench use.
  - Runtime bench / outer-ground texture.
- `ledge_front_wall_4x3.png`
  - Front-facing ledge / retaining-wall reference kit.
  - Runtime ledge texture when that path is enabled.

Disposable/generated output:

- If `tools/index_nature_rmxp_tilesheet.py` is rerun, it now writes to `generated_index/`.
- That folder is ignored by git and should be treated as disposable scratch output.

Intent:

- Keep this folder human-readable.
- Keep only broadly reusable extracted tile kits here.
- Do not keep debug grids, probe crops, PDF page exports, or contact sheets unless they become directly useful references again.
