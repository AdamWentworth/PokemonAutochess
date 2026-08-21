# Demo Media Capture

Status: Active
Type: Runbook
Last updated: 2026-07-08

Use `tools/capture_demo_media.py` to produce repeatable Pokemon Autochess media
for Phlosion/readme demos.

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
