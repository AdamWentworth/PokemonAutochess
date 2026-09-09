# Starter selection: Oak's Lab

Status: Active
Type: Runbook
Last updated: 2026-09-09

The Classic and Adventure starter-selection frontend uses an editable Blender
lab rendered as a fixed 2560 × 1600 backdrop. It has no gameplay scene, battlefield,
board cells, benches, collision, or combat. The existing three starter cards and
number keys still choose a level-5 Pokémon and enter placement.

In the editor's **Scenarios** panel, open **Starter Selection - Classic** or
**Starter Selection - Adventure**. This is a frontend preview, so it does not
appear as an arena in Scenes. The image fills the viewport with center cropping,
preserving its proportions. The cards stay along the bottom and use the same
layout for rendering and mouse hit testing.

## Editable source and publication

The working source is kept privately at
`EnvironmentResearch/OaksLab/authoring/OaksLab.blend`. A self-contained copy with
packed textures is published under the asset depot's
`pokemon-autochess/authoring/frontends/OaksLab/`. Its collections separate the
room and starter table, reusable furnishings, camera/lighting, and hidden locked
source reference. The reference collection does not render.

Edit furniture, materials, lights, or the active camera in Blender, save, then
render and publish from the game repository:

```powershell
& 'C:/Program Files/Blender Foundation/Blender 4.5/blender.exe' `
  --background '<private path>/OaksLab.blend' --python-exit-code 1 `
  --python tools/environment/blender/frontend_backdrop.py -- `
  --recipe config/environment/oaks_lab_backdrop.json --output debug/oaks-lab/render

python tools/environment/publish_frontend_backdrop.py `
  --recipe config/environment/oaks_lab_backdrop.json `
  --blend '<private path>/OaksLab.blend' --render debug/oaks-lab/render `
  --depot '<asset depot root>'
```

Rendering an existing `.blend` preserves manual edits. The research companion's
`build_oaks_lab.py` (in its LGPE Blender bridge tools) bootstraps the initial scene; running
that builder again replaces its output, so it is not the normal edit/export step.

The publisher validates the source, recipe and image hashes plus image dimensions;
copies the editable source and render report to the depot; then replaces the
runtime PNG. The PNG uses the existing UI sprite asset path,
`assets/ui/backdrops/oaks_lab.png`. It is restored by `sync_asset_depot.ps1`.
Meshes, decoded textures and `.blend` files are never tracked in the game repo.
The authored camera render is static; changing furniture means saving the Blender
scene and republishing the image. Use editor content reload or relaunch after
publication to refresh cached textures.

## Source boundary and validation

Source decoding and the initial furnishing selection belong to the private
research companion. The lab uses LGPE laboratory furnishing meshes and texture
atlases, rearranged into a rectangular interior with a new starter display table.
The original circular battle-room layout is only hidden reference data in Blender;
it is not part of the rendered lab or any gameplay environment.

The game only consumes the published image. The source-neutral render recipe lives
at `config/environment/oaks_lab_backdrop.json`; `scripts/states/starter.lua`
selects the image while retaining `hide_world = true`. No Route 1 arena files are
modified by this workflow.

`backend_card_layout_model_contract` covers viewport fit, independent card hit
rectangles and undistorted centered backdrop cropping.
`starter_frontend_selection_contract` opens the real frontend, changes the
embedded viewport without a window resize event, and selects all three starters
by mouse in Classic and number keys in Adventure, checking level-5 placement.
Existing shop, preview catalog and end-to-end tests protect the surrounding flow.
`config/debug/editor_starter_selection.json` provides an empty-world starter
snapshot for reproducible renderer captures.

DirectX uses a 16,384-entry shared texture table: the previous 4,096-entry limit
was exhausted by full-content startup prewarming, preventing frontend images
from loading. The engine now also reports sprite allocation exhaustion instead
of silently omitting images. The editor only shows its unit-edit overlay when
the current viewport actually contains editable objects.
