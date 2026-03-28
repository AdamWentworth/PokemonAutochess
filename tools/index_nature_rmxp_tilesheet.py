from __future__ import annotations

import csv
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

from PIL import Image, ImageColor, ImageDraw, ImageOps


CELL_SIZE = 16
DEFAULT_INPUT = Path(r"assets/textures/environment/source_atlas.png")
DEFAULT_OUTPUT = Path(r"assets/textures/environment/generated_index")

# Dominant layout colors in this sheet. These are not tile content and can be
# ignored when we look for logical tileset groupings.
BACKGROUND_COLORS = {
    (255, 245, 104),  # yellow layout background
    (240, 91, 161),   # magenta layout background
    (255, 255, 255),  # white separators / notes
    (248, 248, 248),  # white antialiasing
    (30, 29, 31),     # dotted separator lines / labels
}


@dataclass(frozen=True)
class Rect:
    x0: int
    y0: int
    x1: int
    y1: int

    @property
    def width(self) -> int:
        return self.x1 - self.x0

    @property
    def height(self) -> int:
        return self.y1 - self.y0

    @property
    def tile_x(self) -> int:
        return self.x0 // CELL_SIZE

    @property
    def tile_y(self) -> int:
        return self.y0 // CELL_SIZE

    @property
    def tile_w(self) -> int:
        return self.width // CELL_SIZE

    @property
    def tile_h(self) -> int:
        return self.height // CELL_SIZE

    def to_dict(self) -> dict[str, int]:
        return {
            "x0": self.x0,
            "y0": self.y0,
            "x1": self.x1,
            "y1": self.y1,
            "width": self.width,
            "height": self.height,
            "tile_x": self.tile_x,
            "tile_y": self.tile_y,
            "tile_w": self.tile_w,
            "tile_h": self.tile_h,
        }


def non_background_pixel(pixel: tuple[int, int, int]) -> bool:
    return pixel not in BACKGROUND_COLORS


def column_content_counts(img: Image.Image) -> list[int]:
    width, height = img.size
    counts: list[int] = []
    for x in range(width):
        total = 0
        for y in range(height):
            if non_background_pixel(img.getpixel((x, y))):
                total += 1
        counts.append(total)
    return counts


def tile_activity_grid(img: Image.Image, rect: Rect) -> list[list[int]]:
    cols = rect.width // CELL_SIZE
    rows = rect.height // CELL_SIZE
    grid = [[0 for _ in range(cols)] for _ in range(rows)]

    for ty in range(rows):
        for tx in range(cols):
            total = 0
            x0 = rect.x0 + tx * CELL_SIZE
            y0 = rect.y0 + ty * CELL_SIZE
            for y in range(y0, y0 + CELL_SIZE):
                for x in range(x0, x0 + CELL_SIZE):
                    if non_background_pixel(img.getpixel((x, y))):
                        total += 1
            grid[ty][tx] = total
    return grid


def find_content_bands(img: Image.Image) -> list[Rect]:
    counts = column_content_counts(img)
    height = img.size[1]
    bands: list[Rect] = []
    in_band = False
    start = 0

    for x, count in enumerate(counts):
        active = count > 40
        if active and not in_band:
            start = x
            in_band = True
        elif not active and in_band:
            bands.append(Rect(start, 0, x, height))
            in_band = False
    if in_band:
        bands.append(Rect(start, 0, len(counts), height))

    filtered: list[Rect] = []
    for band in bands:
        tile_w = band.width / CELL_SIZE
        if tile_w < 2.0:
            continue
        filtered.append(snap_rect_to_tile_grid(band, img.size[0], img.size[1]))
    return filtered


def snap_rect_to_tile_grid(rect: Rect, max_width: int, max_height: int) -> Rect:
    x0 = max(0, (rect.x0 // CELL_SIZE) * CELL_SIZE)
    y0 = max(0, (rect.y0 // CELL_SIZE) * CELL_SIZE)
    x1 = min(max_width, ((rect.x1 + CELL_SIZE - 1) // CELL_SIZE) * CELL_SIZE)
    y1 = min(max_height, ((rect.y1 + CELL_SIZE - 1) // CELL_SIZE) * CELL_SIZE)
    return Rect(x0, y0, x1, y1)


def merge_ranges(ranges: list[tuple[int, int]], max_gap: int) -> list[tuple[int, int]]:
    if not ranges:
        return []
    ranges = sorted(ranges)
    merged = [ranges[0]]
    for start, end in ranges[1:]:
        prev_start, prev_end = merged[-1]
        if start - prev_end <= max_gap:
            merged[-1] = (prev_start, max(prev_end, end))
        else:
            merged.append((start, end))
    return merged


def detect_band_groups(img: Image.Image, band: Rect) -> list[Rect]:
    grid = tile_activity_grid(img, band)
    cols = band.tile_w
    rows = band.tile_h
    row_active_cols = [
        sum(1 for value in row if value >= 18)
        for row in grid
    ]

    def build_ranges(threshold: int, max_gap: int) -> list[tuple[int, int]]:
        ranges: list[tuple[int, int]] = []
        in_group = False
        start = 0
        for row, active_cols in enumerate(row_active_cols):
            active = active_cols >= threshold
            if active and not in_group:
                start = row
                in_group = True
            elif not active and in_group:
                ranges.append((start, row))
                in_group = False
        if in_group:
            ranges.append((start, rows))
        ranges = merge_ranges(ranges, max_gap=max_gap)
        return [r for r in ranges if r[1] - r[0] >= 2]

    strict_ranges = build_ranges(max(cols - 1, 2), max_gap=0)
    strict_coverage = sum(row1 - row0 for row0, row1 in strict_ranges)
    if len(strict_ranges) >= 3 and strict_coverage >= max(8, rows // 6):
        ranges = strict_ranges
    else:
        ranges = build_ranges(max(2, cols // 2), max_gap=1)

    groups: list[Rect] = []
    for row0, row1 in ranges:
        groups.append(
            Rect(
                band.x0,
                band.y0 + row0 * CELL_SIZE,
                band.x1,
                band.y0 + row1 * CELL_SIZE,
            )
        )
    return groups


def save_labeled_overview(img: Image.Image,
                          bands: list[Rect],
                          groups_by_band: list[list[Rect]],
                          out_path: Path) -> None:
    overview = img.convert("RGBA")
    draw = ImageDraw.Draw(overview)
    palette = [
        "#ff5c8a",
        "#2aa1ff",
        "#00b894",
        "#ffb000",
        "#9b59b6",
        "#00cec9",
    ]

    for band_index, band in enumerate(bands):
        color = ImageColor.getrgb(palette[band_index % len(palette)])
        draw.rectangle((band.x0, band.y0, band.x1 - 1, band.y1 - 1),
                       outline=color,
                       width=3)
        draw.text((band.x0 + 4, 4), f"B{band_index:02d}", fill=color)

        for group_index, group in enumerate(groups_by_band[band_index]):
            gx0 = group.x0 + 4
            gy0 = group.y0 + 4
            draw.rectangle((group.x0, group.y0, group.x1 - 1, group.y1 - 1),
                           outline=color,
                           width=2)
            draw.text((gx0, gy0), f"G{band_index:02d}-{group_index:02d}", fill=color)

    overview.save(out_path)


def save_group_previews(img: Image.Image,
                        groups_by_band: list[list[Rect]],
                        previews_dir: Path) -> list[dict[str, object]]:
    previews_dir.mkdir(parents=True, exist_ok=True)
    manifest_rows: list[dict[str, object]] = []

    for band_index, groups in enumerate(groups_by_band):
        for group_index, rect in enumerate(groups):
            crop = img.crop((rect.x0, rect.y0, rect.x1, rect.y1))
            filename = f"band_{band_index:02d}_group_{group_index:02d}.png"
            crop.save(previews_dir / filename)
            manifest_rows.append({
                "id": f"B{band_index:02d}_G{group_index:02d}",
                "band_index": band_index,
                "group_index": group_index,
                "preview": filename,
                **rect.to_dict(),
            })
    return manifest_rows


def save_contact_sheet(previews_dir: Path,
                       manifest_rows: list[dict[str, object]],
                       out_path: Path) -> None:
    if not manifest_rows:
        return

    thumbs: list[Image.Image] = []
    labels: list[str] = []
    max_w = 0
    max_h = 0
    for row in manifest_rows:
        img = Image.open(previews_dir / str(row["preview"])).convert("RGBA")
        thumb = ImageOps.contain(img, (192, 192))
        thumbs.append(thumb)
        labels.append(
            f'{row["id"]}  tx={row["tile_x"]} ty={row["tile_y"]} '
            f'tw={row["tile_w"]} th={row["tile_h"]}'
        )
        max_w = max(max_w, thumb.size[0])
        max_h = max(max_h, thumb.size[1])

    cols = 4
    rows = (len(thumbs) + cols - 1) // cols
    label_h = 34
    pad = 12
    sheet = Image.new(
        "RGBA",
        (cols * (max_w + pad) + pad, rows * (max_h + label_h + pad) + pad),
        (20, 22, 28, 255),
    )
    draw = ImageDraw.Draw(sheet)

    for index, thumb in enumerate(thumbs):
        col = index % cols
        row = index // cols
        x = pad + col * (max_w + pad)
        y = pad + row * (max_h + label_h + pad)
        sheet.alpha_composite(thumb, (x, y))
        draw.rectangle((x - 1, y - 1, x + thumb.size[0], y + thumb.size[1]),
                       outline=(230, 230, 235, 255),
                       width=1)
        draw.text((x, y + max_h + 6), labels[index], fill=(240, 240, 245, 255))

    sheet.save(out_path)


def write_manifest_csv(rows: Iterable[dict[str, object]], out_path: Path) -> None:
    rows = list(rows)
    if not rows:
        return
    fieldnames = [
        "id",
        "band_index",
        "group_index",
        "preview",
        "x0",
        "y0",
        "x1",
        "y1",
        "width",
        "height",
        "tile_x",
        "tile_y",
        "tile_w",
        "tile_h",
    ]
    with out_path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def main() -> None:
    input_path = DEFAULT_INPUT
    output_dir = DEFAULT_OUTPUT
    output_dir.mkdir(parents=True, exist_ok=True)

    img = Image.open(input_path).convert("RGB")
    bands = find_content_bands(img)
    groups_by_band = [detect_band_groups(img, band) for band in bands]

    previews_dir = output_dir / "groups"
    manifest_rows = save_group_previews(img, groups_by_band, previews_dir)
    save_labeled_overview(
        img,
        bands,
        groups_by_band,
        output_dir / "NatureRMXP_labeled_overview.png",
    )
    save_contact_sheet(
        previews_dir,
        manifest_rows,
        output_dir / "NatureRMXP_groups_contact_sheet.png",
    )
    write_manifest_csv(manifest_rows, output_dir / "manifest.csv")

    summary = {
        "source": str(input_path),
        "cell_size": CELL_SIZE,
        "background_colors": [list(color) for color in sorted(BACKGROUND_COLORS)],
        "bands": [
            {
                "band_index": band_index,
                **band.to_dict(),
                "groups": [group.to_dict() for group in groups_by_band[band_index]],
            }
            for band_index, band in enumerate(bands)
        ],
    }
    with (output_dir / "manifest.json").open("w", encoding="utf-8") as handle:
        json.dump(summary, handle, indent=2)

    print(f"Indexed {len(manifest_rows)} candidate tileset groups.")
    print(output_dir)


if __name__ == "__main__":
    main()
