# Starter selection: Oak's Lab

Status: Active
Type: Runbook
Last updated: 2026-09-09

The Classic and Adventure starter-selection frontend uses an editable Blender
lab with rendered opening/selection views and a short camera sequence. It has no
gameplay scene, battlefield,
board cells, benches, collision, or combat. The existing three starter cards and
number keys still choose a level-5 Pokémon and enter placement.

In the editor's **Scenarios** panel, open **Starter Selection - Classic** or
**Starter Selection - Adventure**. This is a frontend preview, so it does not
appear as an arena in Scenes. The image fills the viewport with center cropping,
preserving its proportions. The cards stay along the bottom and use the same
layout for rendering and mouse hit testing.

The larger bottom cards align with the three Poké Balls in the final camera view.
`starter_card_layout` in `scripts/states/starter.lua` stores their centers and
width in backdrop coordinates. The layout follows the image's viewport crop and
limits card height to keep the panel below the table's front edge, including in
wide editor views. The panel grows with the cards.

The screen opens with only the lab for 0.65 seconds, moves toward the
original starter table over 1.9 seconds, settles for 0.15 seconds, then fades the
title, both horizontal panels, card artwork/frames, labels and input hint in
together over 0.65 seconds. Mouse and number-key selection unlock only after the
fade completes. Early input is discarded. Re-entering the screen or reloading
its Lua script replays the sequence; resizing the viewport preserves progress.

In the editor, press **Play** to run the opening sequence. Pause/Step also control
its presentation time. The timing lives in `frontend_intro` and the atlas layout
in `frontend_backdrop_sequence` in `scripts/states/starter.lua`; press **R** in the
game viewport after a Lua edit to reload and replay it. The camera now actually
travels and lowers in Blender, ending squarely in front of the three Poké Balls.
It clears the entrance bookcases before descending. The runtime plays rendered
samples of this move rather than changing the crop of a single angled image.
Starter scripts without `frontend_intro` remain immediately interactive.

The opening and final images are 2560 × 1600. The 1.9-second move uses 116 samples
at 800 × 500, packed into fifteen 3216 × 1008 PNG atlases (4 × 2 frames with a
two-pixel extruded border). Playback uses the nearest sample at roughly 60 fps;
crossfading different perspectives caused visible ghosting and is not used.
All camera textures are prewarmed on entry. The atlas pages use approximately
186 MiB of RGBA texture memory, plus the two full-resolution endpoints. No video
decoder or live 3D lab is loaded. The runtime bounds frame counts to 128 and atlas
dimensions to 4096. Preserve the recipe/Lua layout agreement when changing packing.

## Editable source and publication

The working source is kept privately at
`EnvironmentResearch/OaksLab/authoring/OaksLab.blend`. A self-contained copy with
packed textures is published under the asset depot's
`pokemon-autochess/authoring/frontends/OaksLab/`. Named furniture parents and
collections separate the original room architecture, research machinery, desks,
whiteboard, library rows, starter table, entrance furnishings, and camera/lighting.
Move a furniture parent to move its whole assembly. A hidden, locked copy of the
complete source room provides reference and does not render.

Edit furniture, materials, lights, or the active camera animation in Blender, save, then
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

The publisher validates the source, recipe and every image hash and dimension
before copying any part of the package. It copies the editable source and render
report to the depot, then replaces the runtime images under
`assets/ui/backdrops/`. These are restored by `sync_asset_depot.ps1`.
Meshes, decoded textures and `.blend` files are never tracked in the game repo.
The camera is authored over frames 1–116. The renderer samples those frames and
packs them without applying the color transform twice; it also renders both
endpoints at full resolution. Changing furniture or the camera means saving the
Blender scene and republishing the complete sequence. Use editor content reload or relaunch after
publication to refresh cached textures.

## Source boundary and validation

Source decoding and authoring bootstrap belong to the private research companion.
The active lab uses the normal LGPE Oak's Lab field interior (`t001r0301`), with
its original furniture positions, architecture, floor, starter table, posters,
and complete machinery including glass. It replaces the earlier composition
made from battle-room furnishings. The selection camera and lighting are adapted
for this frontend. Three complete source Poké Ball props are reused on the
original empty starter pads; the room geometry is otherwise preserved.

The builder partitions complete connected pieces into furniture groups, keeping
all 29,051 source triangle records and all four UV channels. It retains 74 packed
source textures. Source vertex color and alpha participate in the Blender
materials, including the translucent machinery and lighting planes. This is a
static Blender approximation of the source shaders, not a reproduction of the
LGPE renderer or its animated effects.

The previous authored version is retained privately under
`EnvironmentResearch/OaksLab/checkpoints/before-field-interior/`.

Decorative artwork must come from LGPE. Keep the workstation's original whiteboard
posters and omit additional artwork when suitable source art is unavailable.
Avoid repeating those posters elsewhere in the room.

The game only consumes the published images. The source-neutral render recipe lives
at `config/environment/oaks_lab_backdrop.json`; `scripts/states/starter.lua`
selects the image while retaining `hide_world = true`. No Route 1 arena files are
modified by this workflow.

`backend_card_layout_model_contract` covers viewport fit, independent card hit
rectangles, undistorted backdrop cropping, every camera sample/page boundary,
safe atlas padding on resized viewports, and invalid intro time inputs.
`starter_frontend_selection_contract` opens the real frontend, changes the
embedded viewport without a window resize event, checks the hold/move/fade phases,
rejects mouse and number-key input before the fade completes, verifies matching
opacity across UI layers, and checks replay on re-entry. It then selects all three
starters by mouse in Classic and number keys in Adventure, checking level-5 placement.
Existing shop, preview catalog and end-to-end tests protect the surrounding flow.
`config/debug/editor_starter_selection.json` provides an empty-world starter
snapshot for reproducible renderer captures.
Snapshot pinning still permits the presentation timer to advance; capture at
frame 15 for the opening hold, 100 for the move, 180 for the fade, or 240 for the
completed selection view when using fixed 60 Hz updates.
`python tools/environment/test_publish_frontend_backdrop.py` checks complete
publication and rejection of missing/corrupt frames, wrong dimensions, invalid
layouts and escaping paths before any asset copy.

DirectX uses a 16,384-entry shared texture table: the previous 4,096-entry limit
was exhausted by full-content startup prewarming, preventing frontend images
from loading. The engine now also reports sprite allocation exhaustion instead
of silently omitting images. The editor only shows its unit-edit overlay when
the current viewport actually contains editable objects.
