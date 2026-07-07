#!/usr/bin/env python3
"""Capture Pokemon Autochess demo screenshots and videos.

The screenshot path uses the engine's built-in backend screenshot hook, so it
does not require desktop screenshot tools. The video path records the game
 window through ffmpeg/x11grab or GStreamer/ximagesrc on local Linux/X11 capture rigs.
Placeholder videos are still-frame MP4s generated from screenshot posters.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import signal
import struct
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_EXE = REPO_ROOT / "build" / "debug-linux" / "PokemonAutochess"
DEFAULT_OUTPUT_DIR = REPO_ROOT / "debug" / "demo_media"
DEFAULT_CMAKE = (
    Path.home()
    / "dev"
    / "vcpkg"
    / "downloads"
    / "tools"
    / "cmake-4.3.2-linux"
    / "cmake-4.3.2-linux-x86_64"
    / "bin"
    / "cmake"
)


@dataclass(frozen=True)
class Scene:
    name: str
    description: str
    snapshot: Path | None
    screenshot_frame: int
    screenshot_auto_quit: int
    video_duration: int
    video_auto_quit_extra: int


SCENES: dict[str, Scene] = {
    "menu": Scene(
        name="menu",
        description="main menu and mode/settings entry surface",
        snapshot=None,
        screenshot_frame=90,
        screenshot_auto_quit=8,
        video_duration=12,
        video_auto_quit_extra=4,
    ),
    "starter-trio": Scene(
        name="starter-trio",
        description="Bulbasaur, Charmander, and Squirtle together on the board",
        snapshot=REPO_ROOT / "config" / "debug" / "debug_state_snapshot_starter_trio.json",
        screenshot_frame=120,
        screenshot_auto_quit=8,
        video_duration=12,
        video_auto_quit_extra=4,
    ),
    "bulbasaur-line": Scene(
        name="bulbasaur-line",
        description="Bulbasaur family demo with currently implemented Bulbasaur and Ivysaur models",
        snapshot=REPO_ROOT / "config" / "debug" / "debug_state_snapshot_bulbasaur_line.json",
        screenshot_frame=120,
        screenshot_auto_quit=8,
        video_duration=12,
        video_auto_quit_extra=4,
    ),
    "charmander-line": Scene(
        name="charmander-line",
        description="Charmander family demo with Charmander, Charmeleon, and Charizard tail fire",
        snapshot=REPO_ROOT / "config" / "debug" / "debug_state_snapshot_tail_fire_starter_line.json",
        screenshot_frame=120,
        screenshot_auto_quit=8,
        video_duration=12,
        video_auto_quit_extra=4,
    ),
    "squirtle-line": Scene(
        name="squirtle-line",
        description="Squirtle family demo with currently implemented Squirtle and Wartortle models",
        snapshot=REPO_ROOT / "config" / "debug" / "debug_state_snapshot_squirtle_line.json",
        screenshot_frame=120,
        screenshot_auto_quit=8,
        video_duration=12,
        video_auto_quit_extra=4,
    ),
    "bulbasaur-route1-combat": Scene(
        name="bulbasaur-route1-combat",
        description="level-1 Bulbasaur combat against Route 1 Pidgey and Rattata",
        snapshot=REPO_ROOT / "config" / "debug" / "debug_state_snapshot_bulbasaur_route1_combat.json",
        screenshot_frame=170,
        screenshot_auto_quit=9,
        video_duration=12,
        video_auto_quit_extra=4,
    ),
    "charmander-route1-combat": Scene(
        name="charmander-route1-combat",
        description="level-1 Charmander combat against Route 1 Pidgey and Rattata",
        snapshot=REPO_ROOT / "config" / "debug" / "debug_state_snapshot_charmander_route1_combat.json",
        screenshot_frame=170,
        screenshot_auto_quit=9,
        video_duration=12,
        video_auto_quit_extra=4,
    ),
    "squirtle-route1-combat": Scene(
        name="squirtle-route1-combat",
        description="level-1 Squirtle combat against Route 1 Pidgey and Rattata",
        snapshot=REPO_ROOT / "config" / "debug" / "debug_state_snapshot_squirtle_route1_combat.json",
        screenshot_frame=170,
        screenshot_auto_quit=9,
        video_duration=12,
        video_auto_quit_extra=4,
    ),
    "tail-fire": Scene(
        name="tail-fire",
        description="legacy alias for the Charmander-line tail-fire board",
        snapshot=REPO_ROOT / "config" / "debug" / "debug_state_snapshot_tail_fire_starter_line.json",
        screenshot_frame=120,
        screenshot_auto_quit=8,
        video_duration=12,
        video_auto_quit_extra=4,
    ),
    "dense-roster": Scene(
        name="dense-roster",
        description="larger planning board with bench and shop context",
        snapshot=REPO_ROOT / "config" / "debug" / "debug_state_snapshot_perf_dense_roster.json",
        screenshot_frame=140,
        screenshot_auto_quit=9,
        video_duration=12,
        video_auto_quit_extra=4,
    ),
}

DEFAULT_SCENE_NAMES = [
    "menu",
    "starter-trio",
    "bulbasaur-line",
    "charmander-line",
    "squirtle-line",
    "bulbasaur-route1-combat",
    "charmander-route1-combat",
    "squirtle-route1-combat",
    "dense-roster",
]


def log(message: str, *, file=sys.stdout) -> None:
    print(message, file=file, flush=True)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Capture Pokemon Autochess screenshots and ffmpeg videos."
    )
    parser.add_argument(
        "mode",
        choices=["screenshots", "videos", "placeholders", "all"],
        nargs="?",
        default="screenshots",
        help="media type to capture",
    )
    parser.add_argument(
        "--scene",
        action="append",
        choices=[*SCENES.keys(), "all"],
        default=None,
        help="scene to capture; may be passed more than once; default: all",
    )
    parser.add_argument("--backend", default="opengl", help="renderer backend token")
    parser.add_argument("--exe", type=Path, default=DEFAULT_EXE, help="game executable path")
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=DEFAULT_OUTPUT_DIR,
        help="output directory for captured media",
    )
    parser.add_argument("--width", type=int, default=1280, help="window/capture width")
    parser.add_argument("--height", type=int, default=720, help="window/capture height")
    parser.add_argument("--fps", type=int, default=30, help="video framerate")
    parser.add_argument(
        "--crf",
        type=int,
        default=18,
        help="ffmpeg libx264 CRF; lower means higher quality",
    )
    parser.add_argument(
        "--preset",
        default="veryfast",
        help="ffmpeg libx264 preset",
    )
    parser.add_argument(
        "--duration",
        type=int,
        default=None,
        help="override per-scene video duration in seconds",
    )
    parser.add_argument(
        "--screenshot-frame",
        type=int,
        default=None,
        help="override per-scene screenshot frame",
    )
    parser.add_argument(
        "--start-delay",
        type=float,
        default=0.75,
        help="seconds to wait after Game initialized before recording video",
    )
    parser.add_argument(
        "--ffmpeg",
        default=shutil.which("ffmpeg") or "",
        help="ffmpeg executable path; required for real videos and preferred for placeholders",
    )
    parser.add_argument(
        "--gst-launch",
        default=shutil.which("gst-launch-1.0") or "",
        help="gst-launch-1.0 path; fallback for placeholder MP4s when ffmpeg is unavailable",
    )
    parser.add_argument(
        "--window-title-regex",
        default=r"PokemonAutochess",
        help="xwininfo window-title regex for video capture",
    )
    parser.add_argument(
        "--rebuild",
        action="store_true",
        help="build the PokemonAutochess target before capturing",
    )
    parser.add_argument(
        "--cmake",
        type=Path,
        default=Path(shutil.which("cmake")) if shutil.which("cmake") else DEFAULT_CMAKE,
        help="cmake path used with --rebuild",
    )
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=REPO_ROOT / "build" / "debug-linux",
        help="CMake build directory used with --rebuild",
    )
    parser.add_argument(
        "--keep-going",
        action="store_true",
        help="continue capturing remaining scenes after a scene failure",
    )
    return parser.parse_args()


def selected_scenes(values: list[str] | None) -> list[Scene]:
    if not values or "all" in values:
        return [SCENES[name] for name in DEFAULT_SCENE_NAMES]
    seen: set[str] = set()
    out: list[Scene] = []
    for value in values:
        if value in seen:
            continue
        seen.add(value)
        out.append(SCENES[value])
    return out


def run_checked(cmd: list[str], cwd: Path = REPO_ROOT) -> None:
    log(f"[demo-media] $ {' '.join(cmd)}")
    subprocess.run(cmd, cwd=cwd, check=True)


def png_dimensions(path: Path) -> tuple[int, int]:
    with path.open("rb") as handle:
        header = handle.read(24)
    if len(header) < 24 or header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
        raise RuntimeError(f"not a PNG file: {path}")
    return struct.unpack(">II", header[16:24])


def normalize_png_with_gstreamer(args: argparse.Namespace, source: Path, destination: Path) -> bool:
    if not args.gst_launch:
        return False

    temp = destination.with_suffix(".tmp.png")
    temp.unlink(missing_ok=True)
    try:
        run_checked(
            [
                args.gst_launch,
                "-q",
                "filesrc",
                f"location={source}",
                "!",
                "pngdec",
                "!",
                "imagefreeze",
                "num-buffers=1",
                "!",
                "video/x-raw,framerate=1/1",
                "!",
                "videoscale",
                "method=lanczos",
                "add-borders=true",
                "!",
                f"video/x-raw,width={args.width},height={args.height},framerate=1/1",
                "!",
                "videoconvert",
                "!",
                "video/x-raw,format=RGBA",
                "!",
                "pngenc",
                "compression-level=9",
                "snapshot=true",
                "!",
                "filesink",
                f"location={temp}",
            ]
        )
        if not temp.exists():
            return False
        temp.replace(destination)
        return True
    finally:
        temp.unlink(missing_ok=True)


def ensure_png_dimensions(args: argparse.Namespace, path: Path) -> None:
    dimensions = png_dimensions(path)
    target = (args.width, args.height)
    if dimensions == target:
        return

    if normalize_png_with_gstreamer(args, path, path):
        dimensions = png_dimensions(path)
    if dimensions != target:
        raise RuntimeError(
            f"{path} is {dimensions[0]}x{dimensions[1]}, expected {target[0]}x{target[1]}"
        )


def maybe_rebuild(args: argparse.Namespace) -> None:
    if not args.rebuild:
        return
    cmake = args.cmake
    if not cmake.exists() and shutil.which(str(cmake)) is None:
        raise RuntimeError(f"cmake not found: {cmake}")
    run_checked(
        [
            str(cmake),
            "--build",
            str(args.build_dir),
            "--target",
            "PokemonAutochess",
            "-j",
            "2",
        ]
    )


def base_env(args: argparse.Namespace, scene: Scene, auto_quit_seconds: int) -> dict[str, str]:
    env = os.environ.copy()
    env.update(
        {
            "PAC_DATA_ROOT": str(REPO_ROOT),
            "PAC_ASSET_ROOT": str(REPO_ROOT / "assets"),
            "PAC_RENDER_BACKEND": args.backend,
            "PAC_RANDOM_SEED": "12345",
            "PAC_VIDEO_WIDTH": str(args.width),
            "PAC_VIDEO_HEIGHT": str(args.height),
            "PAC_VIDEO_FULLSCREEN": "0",
            "PAC_VIDEO_VSYNC": "0",
            "PAC_VIDEO_FPS_CAP": str(args.fps),
            "PAC_AUTO_QUIT_SECONDS": str(auto_quit_seconds),
        }
    )

    if scene.snapshot is not None:
        env.update(
            {
                "PAC_DEBUG_STATE_PATH": str(scene.snapshot),
                "PAC_AUTO_LOAD_DEBUG_SNAPSHOT": "1",
                "PAC_PIN_DEBUG_SNAPSHOT_STATE": "1",
            }
        )
    else:
        env.pop("PAC_DEBUG_STATE_PATH", None)
        env.pop("PAC_AUTO_LOAD_DEBUG_SNAPSHOT", None)
        env.pop("PAC_PIN_DEBUG_SNAPSHOT_STATE", None)
    return env


def ensure_scene_inputs(scene: Scene) -> None:
    if scene.snapshot is not None and not scene.snapshot.exists():
        raise RuntimeError(f"snapshot does not exist for scene '{scene.name}': {scene.snapshot}")


def ensure_exe(path: Path) -> None:
    if not path.exists():
        raise RuntimeError(f"game executable not found: {path}")
    if not os.access(path, os.X_OK):
        raise RuntimeError(f"game executable is not executable: {path}")


def screenshot_path(args: argparse.Namespace, scene: Scene) -> Path:
    out_dir = args.output_dir / "screenshots"
    return out_dir / f"pokemon-autochess-{scene.name}-{args.width}x{args.height}-{args.backend}.png"


def placeholder_poster_path(args: argparse.Namespace, scene: Scene) -> Path:
    out_dir = args.output_dir / "placeholders" / "screens"
    return out_dir / f"pokemon-autochess-{scene.name}-poster-{args.width}x{args.height}-{args.backend}.png"


def placeholder_video_path(args: argparse.Namespace, scene: Scene) -> Path:
    out_dir = args.output_dir / "placeholders" / "videos"
    return out_dir / f"pokemon-autochess-{scene.name}-placeholder-{args.width}x{args.height}-{args.backend}.mp4"


def capture_screenshot(args: argparse.Namespace, scene: Scene) -> Path:
    ensure_scene_inputs(scene)
    shot = screenshot_path(args, scene)
    shot.parent.mkdir(parents=True, exist_ok=True)
    stdout_path = shot.parent / f"{shot.stem}.stdout.log"
    stderr_path = shot.parent / f"{shot.stem}.stderr.log"

    frame = args.screenshot_frame if args.screenshot_frame is not None else scene.screenshot_frame
    env = base_env(args, scene, scene.screenshot_auto_quit)
    env["PAC_BACKEND_SCREENSHOT_PATH"] = str(shot)
    env["PAC_BACKEND_SCREENSHOT_FRAME"] = str(frame)

    for path in (shot, stdout_path, stderr_path):
        path.unlink(missing_ok=True)

    log(f"[demo-media][screenshot] {scene.name}: {scene.description}")
    with stdout_path.open("w", encoding="utf-8") as stdout, stderr_path.open(
        "w", encoding="utf-8"
    ) as stderr:
        result = subprocess.run(
            [str(args.exe)],
            cwd=REPO_ROOT,
            env=env,
            stdout=stdout,
            stderr=stderr,
            timeout=max(45, scene.screenshot_auto_quit + 40),
        )
    if result.returncode != 0:
        raise RuntimeError(f"{scene.name} screenshot process exited with {result.returncode}")
    if not shot.exists():
        raise RuntimeError(f"{scene.name} screenshot was not produced: {shot}")

    stdout_text = stdout_path.read_text(encoding="utf-8", errors="replace")
    if "[Screenshot]" not in stdout_text or "WROTE" not in stdout_text:
        raise RuntimeError(f"{scene.name} screenshot log did not confirm a write: {stdout_path}")

    ensure_png_dimensions(args, shot)
    log(f"[demo-media][screenshot] wrote {shot}")
    return shot


def iter_log_until(
    process: subprocess.Popen[str],
    markers: Iterable[str],
    timeout: float,
    sink: list[str],
) -> bool:
    deadline = time.monotonic() + timeout
    marker_tuple = tuple(markers)
    assert process.stdout is not None

    while time.monotonic() < deadline:
        line = process.stdout.readline()
        if line:
            sink.append(line)
            log(line.rstrip())
            if any(marker in line for marker in marker_tuple):
                return True
        elif process.poll() is not None:
            return False
        else:
            time.sleep(0.05)
    return False


def terminate_process(process: subprocess.Popen[str]) -> None:
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def xwininfo_tree() -> str:
    return subprocess.check_output(
        ["xwininfo", "-root", "-tree"],
        text=True,
        stderr=subprocess.STDOUT,
    )


def find_window_id(
    title_regex: str,
    timeout: float,
    preferred_size: tuple[int, int] | None = None,
) -> str:
    compiled = re.compile(title_regex, re.IGNORECASE)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            tree = xwininfo_tree()
        except (subprocess.CalledProcessError, FileNotFoundError):
            tree = ""
        fallback: str | None = None
        for line in tree.splitlines():
            if compiled.search(line):
                id_match = re.search(r"(0x[0-9a-fA-F]+)", line)
                if not id_match:
                    continue
                window_id = id_match.group(1)
                if fallback is None:
                    fallback = window_id
                size_match = re.search(r"\s(\d+)x(\d+)[+-]\d+[+-]\d+", line)
                if preferred_size is not None and size_match:
                    size = (int(size_match.group(1)), int(size_match.group(2)))
                    if size == preferred_size:
                        return window_id
        if fallback is not None:
            return fallback
        time.sleep(0.25)
    raise RuntimeError(f"could not find a window matching /{title_regex}/")


def window_geometry(window_id: str) -> tuple[int, int, int, int]:
    output = subprocess.check_output(
        ["xwininfo", "-id", window_id],
        text=True,
        stderr=subprocess.STDOUT,
    )

    def value(label: str) -> int:
        match = re.search(rf"{re.escape(label)}:\s*(-?\d+)", output)
        if not match:
            raise RuntimeError(f"xwininfo did not report '{label}' for {window_id}")
        return int(match.group(1))

    return (
        value("Absolute upper-left X"),
        value("Absolute upper-left Y"),
        value("Width"),
        value("Height"),
    )


def capture_video(args: argparse.Namespace, scene: Scene) -> Path:
    ensure_scene_inputs(scene)
    if not args.ffmpeg and not args.gst_launch:
        raise RuntimeError(
            "ffmpeg or gst-launch-1.0 is required for video capture. Install one "
            "on the capture machine or pass --ffmpeg/--gst-launch."
        )
    if not os.environ.get("DISPLAY"):
        raise RuntimeError("DISPLAY is not set; X11 video capture needs X11.")
    if not shutil.which("xwininfo"):
        raise RuntimeError("xwininfo is required for video capture window geometry.")

    out_dir = args.output_dir / "videos"
    out_dir.mkdir(parents=True, exist_ok=True)
    video = out_dir / f"pokemon-autochess-{scene.name}-{args.width}x{args.height}-{args.backend}.mp4"
    stdout_path = out_dir / f"{video.stem}.stdout.log"
    stderr_path = out_dir / f"{video.stem}.stderr.log"

    duration = args.duration if args.duration is not None else scene.video_duration
    auto_quit = duration + scene.video_auto_quit_extra
    env = base_env(args, scene, auto_quit)

    for path in (video, stdout_path, stderr_path):
        path.unlink(missing_ok=True)

    log(f"[demo-media][video] {scene.name}: {scene.description}")
    stderr_handle = stderr_path.open("w", encoding="utf-8")
    process = subprocess.Popen(
        [str(args.exe)],
        cwd=REPO_ROOT,
        env=env,
        stdout=subprocess.PIPE,
        stderr=stderr_handle,
        text=True,
        bufsize=1,
    )

    stdout_lines: list[str] = []
    try:
        ready = iter_log_until(
            process,
            ["[Init] Game initialized."],
            timeout=75,
            sink=stdout_lines,
        )
        if not ready:
            raise RuntimeError(f"{scene.name} did not reach Game initialized before timeout")
        time.sleep(max(0.0, args.start_delay))

        window_id = find_window_id(
            args.window_title_regex,
            timeout=10,
            preferred_size=(args.width, args.height),
        )
        x, y, width, height = window_geometry(window_id)
        capture_size = f"{width}x{height}"
        log(f"[demo-media][video] recording window={window_id} geom={capture_size}+{x},{y}")
        if args.ffmpeg:
            display = os.environ["DISPLAY"]
            display_input = f"{display}+{x},{y}"
            ffmpeg_cmd = [
                args.ffmpeg,
                "-hide_banner",
                "-y",
                "-f",
                "x11grab",
                "-framerate",
                str(args.fps),
                "-video_size",
                capture_size,
                "-i",
                display_input,
                "-t",
                str(duration),
                "-an",
                "-vf",
                "format=yuv420p",
                "-c:v",
                "libx264",
                "-preset",
                args.preset,
                "-crf",
                str(args.crf),
                str(video),
            ]
            run_checked(ffmpeg_cmd)
        else:
            frames = max(1, duration * args.fps)
            bitrate_kbps = max(4500, min(12000, width * height * args.fps // 4000))
            gst_cmd = [
                args.gst_launch,
                "-q",
                "ximagesrc",
                f"xid={window_id}",
                "use-damage=0",
                "show-pointer=false",
                f"num-buffers={frames}",
                "!",
                f"video/x-raw,framerate={args.fps}/1",
                "!",
                "videorate",
                "!",
                f"video/x-raw,framerate={args.fps}/1",
                "!",
                "videoscale",
                "method=lanczos",
                "add-borders=true",
                "!",
                f"video/x-raw,width={args.width},height={args.height},framerate={args.fps}/1",
                "!",
                "videoconvert",
                "!",
                "video/x-raw,format=I420",
                "!",
                "x264enc",
                "speed-preset=veryfast",
                "tune=zerolatency",
                f"bitrate={bitrate_kbps}",
                f"key-int-max={args.fps}",
                "!",
                "video/x-h264,profile=baseline",
                "!",
                "mp4mux",
                "faststart=true",
                "!",
                "filesink",
                f"location={video}",
            ]
            run_checked(gst_cmd)
    finally:
        if process.stdout is not None:
            try:
                while True:
                    line = process.stdout.readline()
                    if not line:
                        break
                    stdout_lines.append(line)
            except Exception:
                pass
        terminate_process(process)
        stderr_handle.close()
        stdout_path.write_text("".join(stdout_lines), encoding="utf-8")

    if not video.exists():
        raise RuntimeError(f"{scene.name} video was not produced: {video}")
    log(f"[demo-media][video] wrote {video}")
    return video


def make_placeholder_video_with_ffmpeg(
    args: argparse.Namespace,
    poster: Path,
    video: Path,
    duration: int,
) -> None:
    run_checked(
        [
            args.ffmpeg,
            "-hide_banner",
            "-y",
            "-loop",
            "1",
            "-framerate",
            str(args.fps),
            "-i",
            str(poster),
            "-t",
            str(duration),
            "-an",
            "-vf",
            "format=yuv420p",
            "-c:v",
            "libx264",
            "-preset",
            args.preset,
            "-crf",
            str(args.crf),
            str(video),
        ]
    )


def make_placeholder_video_with_gstreamer(
    args: argparse.Namespace,
    poster: Path,
    video: Path,
    duration: int,
) -> None:
    frames = max(1, duration * args.fps)
    quantizer = max(0, min(50, args.crf))
    run_checked(
        [
            args.gst_launch,
            "-q",
            "filesrc",
            f"location={poster}",
            "!",
            "pngdec",
            "!",
            "imagefreeze",
            f"num-buffers={frames}",
            "!",
            f"video/x-raw,framerate={args.fps}/1",
            "!",
            "videoscale",
            "method=lanczos",
            "add-borders=true",
            "!",
            f"video/x-raw,width={args.width},height={args.height},framerate={args.fps}/1",
            "!",
            "videoconvert",
            "!",
            "video/x-raw,format=I420",
            "!",
            "x264enc",
            f"speed-preset={args.preset}",
            "tune=stillimage",
            "pass=quant",
            f"quantizer={quantizer}",
            f"key-int-max={args.fps}",
            "!",
            "video/x-h264,profile=baseline",
            "!",
            "mp4mux",
            "faststart=true",
            "!",
            "filesink",
            f"location={video}",
        ]
    )


def normalize_placeholder_poster_with_gstreamer(args: argparse.Namespace, source: Path, poster: Path) -> bool:
    return normalize_png_with_gstreamer(args, source, poster)


def capture_placeholder(args: argparse.Namespace, scene: Scene) -> Path:
    poster = placeholder_poster_path(args, scene)
    video = placeholder_video_path(args, scene)
    poster.parent.mkdir(parents=True, exist_ok=True)
    video.parent.mkdir(parents=True, exist_ok=True)

    shot = screenshot_path(args, scene)
    if not shot.exists():
        log(f"[demo-media][placeholder] missing poster source for {scene.name}; capturing screenshot first")
        shot = capture_screenshot(args, scene)

    if not normalize_placeholder_poster_with_gstreamer(args, shot, poster):
        shutil.copy2(shot, poster)
    log(f"[demo-media][placeholder] wrote poster {poster}")

    duration = args.duration if args.duration is not None else scene.video_duration
    video.unlink(missing_ok=True)
    if args.ffmpeg:
        make_placeholder_video_with_ffmpeg(args, poster, video, duration)
    elif args.gst_launch:
        make_placeholder_video_with_gstreamer(args, poster, video, duration)
    else:
        raise RuntimeError(
            "placeholder video capture needs ffmpeg or gst-launch-1.0. "
            "Poster screenshots were still written."
        )

    if not video.exists():
        raise RuntimeError(f"{scene.name} placeholder video was not produced: {video}")
    log(f"[demo-media][placeholder] wrote video {video}")
    return video


def run_for_scenes(
    args: argparse.Namespace,
    scenes: list[Scene],
    action_name: str,
    action,
) -> int:
    failures = 0
    for scene in scenes:
        try:
            action(args, scene)
        except Exception as ex:
            failures += 1
            log(f"[demo-media][{action_name}] FAILED {scene.name}: {ex}", file=sys.stderr)
            if not args.keep_going:
                break
    return failures


def main() -> int:
    args = parse_args()
    args.output_dir = args.output_dir.resolve()
    args.exe = args.exe.resolve()
    args.build_dir = args.build_dir.resolve()

    maybe_rebuild(args)
    ensure_exe(args.exe)

    scenes = selected_scenes(args.scene)
    log("[demo-media] scenes: " + ", ".join(scene.name for scene in scenes))
    log(f"[demo-media] output: {args.output_dir}")

    failures = 0
    if args.mode in ("screenshots", "all"):
        failures += run_for_scenes(args, scenes, "screenshot", capture_screenshot)
    if args.mode in ("videos", "all"):
        failures += run_for_scenes(args, scenes, "video", capture_video)
    if args.mode == "placeholders":
        failures += run_for_scenes(args, scenes, "placeholder", capture_placeholder)

    if failures:
        log(f"[demo-media] completed with {failures} failure(s)", file=sys.stderr)
        return 1
    log("[demo-media] PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
