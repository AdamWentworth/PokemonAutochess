#!/usr/bin/env python3
"""
Generate initial VFX sprite textures for growl-like effects.

Outputs:
- assets/vfx/textures/moves/growl/growl_cone_line.png
- assets/vfx/textures/moves/growl/growl_ring_soft.png
- assets/vfx/textures/common/star_glint_01.png
- assets/vfx/textures/common/soft_glow_01.png
"""

from __future__ import annotations

import math
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]


def clamp01(v: float) -> float:
    return 0.0 if v < 0.0 else 1.0 if v > 1.0 else v


def smoothstep(edge0: float, edge1: float, x: float) -> float:
    if edge1 == edge0:
        return 0.0
    t = clamp01((x - edge0) / (edge1 - edge0))
    return t * t * (3.0 - 2.0 * t)


def make_soft_glow(path: Path, size: int = 256) -> None:
    img = Image.new("RGBA", (size, size))
    px = img.load()
    c = (size - 1) * 0.5
    for y in range(size):
        for x in range(size):
            dx = (x - c) / c
            dy = (y - c) / c
            r = math.sqrt(dx * dx + dy * dy)
            a = (1.0 - smoothstep(0.1, 1.0, r)) ** 1.8
            rgb = int(255 * (0.92 + 0.08 * (1.0 - r)))
            px[x, y] = (rgb, rgb, rgb, int(255 * clamp01(a)))
    path.parent.mkdir(parents=True, exist_ok=True)
    img.save(path)


def make_growl_ring(path: Path, size: int = 512) -> None:
    img = Image.new("RGBA", (size, size))
    px = img.load()
    c = (size - 1) * 0.5
    for y in range(size):
        for x in range(size):
            dx = (x - c) / c
            dy = (y - c) / c
            r = math.sqrt(dx * dx + dy * dy)

            outer = 1.0 - smoothstep(0.80, 1.00, r)
            inner_hole = smoothstep(0.58, 0.72, r)
            ring = clamp01(outer * inner_hole)

            soft_fill = (1.0 - smoothstep(0.0, 0.95, r)) * 0.08
            a = clamp01(ring * 0.92 + soft_fill)

            # Slight warm/pink hue in texture; shader can tint further.
            rr = int(255 * (0.95 + 0.05 * (1.0 - r)))
            gg = int(255 * (0.78 + 0.18 * (1.0 - r)))
            bb = int(255 * (0.88 + 0.10 * (1.0 - r)))
            px[x, y] = (rr, gg, bb, int(255 * a))
    path.parent.mkdir(parents=True, exist_ok=True)
    img.save(path)


def make_growl_cone_line(path: Path, w: int = 512, h: int = 128) -> None:
    img = Image.new("RGBA", (w, h))
    px = img.load()
    cy = (h - 1) * 0.5

    for y in range(h):
        for x in range(w):
            u = x / (w - 1)  # 0..1 left->right
            v = (y - cy) / cy  # -1..1 top->bottom

            # Tapered cone width: tight at source, wider toward target.
            half_w = 0.06 + u * 0.55
            cone = 1.0 - smoothstep(half_w, half_w + 0.10, abs(v))

            # Three core linear streaks (center + side lanes).
            d0 = abs(v - 0.0)
            d1 = abs(v - half_w * 0.34)
            d2 = abs(v + half_w * 0.34)
            l0 = 1.0 - smoothstep(0.04, 0.09, d0)
            l1 = 1.0 - smoothstep(0.035, 0.085, d1)
            l2 = 1.0 - smoothstep(0.035, 0.085, d2)
            lines = l0 * 1.0 + l1 * 0.8 + l2 * 0.8

            # Longitudinal fade and subtle grain.
            length_fade = smoothstep(0.0, 0.07, u) * (1.0 - smoothstep(0.90, 1.0, u))
            grain = 0.90 + 0.10 * math.sin((u * 48.0 + v * 9.0) * math.pi)

            a = clamp01(cone * lines * length_fade * grain)

            # Mostly white so move shader can tint.
            rr = int(255 * 0.98)
            gg = int(255 * 0.97)
            bb = int(255 * 0.99)
            px[x, y] = (rr, gg, bb, int(255 * a))

    path.parent.mkdir(parents=True, exist_ok=True)
    img.save(path)


def make_star_glint(path: Path, size: int = 256) -> None:
    img = Image.new("RGBA", (size, size))
    px = img.load()
    c = (size - 1) * 0.5

    for y in range(size):
        for x in range(size):
            dx = (x - c) / c
            dy = (y - c) / c
            r = math.sqrt(dx * dx + dy * dy)

            # 8-point star via angular ray maxima.
            a = math.atan2(dy, dx)
            ray4 = abs(math.cos(a * 4.0)) ** 20
            ray8 = abs(math.cos(a * 8.0)) ** 32
            rays = max(ray4 * 0.85, ray8 * 0.35)

            core = (1.0 - smoothstep(0.0, 0.28, r)) ** 2.0
            halo = (1.0 - smoothstep(0.0, 1.0, r)) * 0.22
            alpha = clamp01(core + rays * (1.0 - smoothstep(0.0, 1.0, r)) + halo)

            # Warm white glint.
            rr = int(255 * 1.00)
            gg = int(255 * 0.97)
            bb = int(255 * 0.95)
            px[x, y] = (rr, gg, bb, int(255 * alpha))

    path.parent.mkdir(parents=True, exist_ok=True)
    img.save(path)


def main() -> None:
    cone = ROOT / "assets/vfx/textures/moves/growl/growl_cone_line.png"
    ring = ROOT / "assets/vfx/textures/moves/growl/growl_ring_soft.png"
    star = ROOT / "assets/vfx/textures/common/star_glint_01.png"
    glow = ROOT / "assets/vfx/textures/common/soft_glow_01.png"

    make_growl_cone_line(cone)
    make_growl_ring(ring)
    make_star_glint(star)
    make_soft_glow(glow)

    print("Generated:")
    print(f"- {cone}")
    print(f"- {ring}")
    print(f"- {star}")
    print(f"- {glow}")


if __name__ == "__main__":
    main()

