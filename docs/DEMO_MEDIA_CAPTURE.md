# Demo Media Capture

Status: Active
Type: Runbook
Last updated: 2026-09-19

Use `tools/capture_demo_media.py` to produce repeatable Pokemon Autochess media
for Phlosion/readme demos.

## README Branding and Showcase

The README uses the existing red, gold and ivory Autochess identity from
[Phlosion](https://phlosion.com/?demo=autochess#demos). Its transparent header
image is an unchanged copy of the site's
[Autochess lockup](https://phlosion.com/products/pokemon-autochess/autochess-lockup-transparent.png),
stored at `docs/assets/readme/autochess-lockup.png`. Keep its aspect ratio,
transparency and original colors. Use descriptive alt text and check both light
and dark GitHub themes when changing its presentation.

The four README screenshots were captured on 2026-09-19. They are direct
captures of the game and editor material preview,
with no painted-over content or generated mockups:

| Tracked image | Capture source | Renderer |
| --- | --- | --- |
| `docs/assets/readme/route1-flat-starters.png` | Starter trio battling Pidgey and Rattata on the Flat Dirt Experiment, full game frame | Direct3D 12 |
| `docs/assets/readme/bulbasaur-material.png` | `starter-bulbasaur`, cropped model preview | Direct3D 12 |
| `docs/assets/readme/charmander-material.png` | `charmander-fire`, cropped model preview | Direct3D 12 |
| `docs/assets/readme/squirtle-material.png` | `starter-squirtle`, cropped model preview | Direct3D 12 |

The arena capture explicitly selects `routes/route1-flat-experiment`, the current
editor startup scene in `phlosion.project.json`, through
`config/debug/readme_route1_flat_starters.json`. This fixture starts an active
battle with the three starters against Pidgey and Rattata. Its Blender source and exported
bundle are recorded in `config/environment/route1_flat_experiment.authoring.json`.
Older Route 1 gameplay fixtures select the legacy layout independently of the
editor startup setting; do not use them to regenerate this image.

The model previews use the promoted Scarlet/Violet models `0001_Bulbasaur_SV`,
`0004_Charmander_SV` and `0007_Squirtle_SV`, as recorded in
`config/assets/kanto_model_promotions.json`. All three capture cases passed the
material harness's content and parity checks on OpenGL, Vulkan and Direct3D 12.
The earlier engine extraction is documented separately in
[the material verification record](CHARACTER_MATERIALS.md#boundary-verification-2026-09-19).
These are staged development scenes with diagnostic overlays, not a finished
release UI. The linked Phlosion gallery also contains older prototype captures.

Regenerate candidates on the Windows GPU workstation with the private asset
depot restored and a current Release game/editor pair:

```powershell
.\tools\environment\capture_arena_pilot.ps1 -Backend d3d12 -Snapshot config/debug/readme_route1_flat_starters.json -Frame 240 -OutputDirectory debug/readme-flat-combat/frame240
.\tools\check_character_materials.ps1 -Cases starter-bulbasaur,charmander-fire,starter-squirtle -OutputDirectory debug/readme-current-route/materials
```

Review the resulting images before copying the selected Direct3D 12 game frame
and model crops into `docs/assets/readme`. Keep generated captures, videos and
private runtime payloads out of Git. The committed images are the small,
deliberate showcase set.

Technology badges describe the checked-in CMake/vcpkg and Lua configuration;
the CI badge links to the real workflow. Update versions when those inputs
change. GitHub topics describe the game and its actual stack; the engine remains
a separate repository.

## Legacy Website Capture Workflow

The screenshot path uses the engine's built-in backend screenshot hook and works
without desktop screenshot tools. The video path records the X11 game window
with `ffmpeg` or GStreamer.

## Scenes

- `menu`: main menu, mode selection, and settings entry surface
- `starter-trio`: pinned board with Bulbasaur, Charmander, and Squirtle together
- `bulbasaur-line`: pinned Bulbasaur family board with the currently implemented
  Bulbasaur and Ivysaur models
- `charmander-line`: pinned Charmander family board with Charmander, Charmeleon,
  and Charizard tail fire
- `squirtle-line`: pinned Squirtle family board with the currently implemented
  Squirtle and Wartortle models
- `bulbasaur-route1-combat`: level-1 Bulbasaur against Route 1 Pidgey and Rattata
- `charmander-route1-combat`: level-1 Charmander against Route 1 Pidgey and Rattata
- `squirtle-route1-combat`: level-1 Squirtle against Route 1 Pidgey and Rattata
- `dense-roster`: pinned larger planning board with bench/shop context

`tail-fire` remains available as a legacy scene alias for `charmander-line`.
The family snapshots intentionally retain their original demo compositions;
they are capture fixtures, not an inventory of currently available models.

## Screenshots

```bash
./tools/capture_demo_media.py screenshots
```

Capture one scene:

```bash
./tools/capture_demo_media.py screenshots --scene starter-trio
```

Output defaults to:

```text
debug/demo_media/screenshots/
```

## Videos

Video capture requires:

- X11 display session
- `ffmpeg` with `x11grab`, or `gst-launch-1.0` with `ximagesrc` and `x264enc`
- `xwininfo`

```bash
./tools/capture_demo_media.py videos
```

Useful GPU-machine command:

```bash
./tools/capture_demo_media.py videos \
  --width 1760 \
  --height 990 \
  --fps 30 \
  --duration 12 \
  --crf 18
```

Output defaults to:

```text
debug/demo_media/videos/
```

## Placeholder Media

Placeholder media stages existing screenshots as poster images and creates
still-frame MP4s for Phlosion layout work before real motion captures are
available. It prefers `ffmpeg`, then falls back to `gst-launch-1.0` when
GStreamer has PNG, image-freeze, x264, and MP4 plugins installed.

```bash
./tools/capture_demo_media.py placeholders
```

High-quality local placeholder set for this Ubuntu laptop:

```bash
./tools/capture_demo_media.py placeholders \
  --width 1760 \
  --height 1100 \
  --fps 30 \
  --preset slow \
  --crf 12
```

Outputs default to:

```text
debug/demo_media/placeholders/screens/
debug/demo_media/placeholders/videos/
```

The placeholder path normalizes posters to the requested dimensions before
encoding so Phlosion receives consistent media even if the desktop window
manager clamps the live game window.

## Rebuild First

If CMake is not on `PATH`, the script defaults to the vcpkg-downloaded CMake
under `~/dev/vcpkg/downloads/tools/cmake-4.3.2-linux/...`.

```bash
./tools/capture_demo_media.py screenshots --rebuild
```

## Notes

- Linux capture currently uses `PAC_RENDER_BACKEND=opengl`.
- Output is written under `debug/`, which is ignored by git.
- Pinned snapshot scenes use `PAC_AUTO_LOAD_DEBUG_SNAPSHOT=1` and
  `PAC_PIN_DEBUG_SNAPSHOT_STATE=1` so captures do not drift while recording.
