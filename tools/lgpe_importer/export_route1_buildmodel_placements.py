"""Decode Route 1 vegetation placements from LGPE's shipped FlatBuffer.

This reads the original placement archive directly. It does not infer object
positions from screenshots, Blender staging, or the monolithic road model.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from collections import Counter
from pathlib import Path
from typing import Any


ARCHIVE_RELATIVE_PATH = Path("field/placement/001 C135E084B8176A95.bin")
ARCHIVE_SHA256 = (
    "56D70BBC2AD79FA01044730B171105F0F908CE4EE227F026F0B9AB4ED57F1F71"
)
ROUTE_COLLISION_PREFIX = "bin/field/model/area02/road001_"
MODEL_PATHS = {
    "grass02": "bin/field/model/buildmodel/grass02.gfbmdl",
    "flowers02": "bin/field/model/buildmodel/flowers02.gfbmdl",
    "flowers04": "bin/field/model/buildmodel/flowers04.gfbmdl",
}
EXPECTED_COUNTS = {"grass02": 9, "flowers02": 30, "flowers04": 15}


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


class FlatBuffer:
    def __init__(self, data: bytes) -> None:
        self.data = data

    def unpack(self, fmt: str, offset: int) -> Any:
        size = struct.calcsize(fmt)
        if offset < 0 or offset + size > len(self.data):
            raise ValueError(f"Read outside archive at 0x{offset:X}")
        return struct.unpack_from(fmt, self.data, offset)[0]

    def u16(self, offset: int) -> int:
        return int(self.unpack("<H", offset))

    def u32(self, offset: int) -> int:
        return int(self.unpack("<I", offset))

    def i32(self, offset: int) -> int:
        return int(self.unpack("<i", offset))

    def f32(self, offset: int) -> float:
        return float(self.unpack("<f", offset))

    def field_slot(self, table: int, field: int) -> int | None:
        vtable = table - self.i32(table)
        vtable_size = self.u16(vtable)
        entry = vtable + 4 + field * 2
        if entry + 2 > vtable + vtable_size:
            return None
        relative = self.u16(entry)
        return table + relative if relative else None

    def indirect_field(self, table: int, field: int) -> int | None:
        slot = self.field_slot(table, field)
        return slot + self.u32(slot) if slot is not None else None

    def vector(self, table: int, field: int) -> tuple[int, int] | None:
        vector = self.indirect_field(table, field)
        if vector is None:
            return None
        return vector + 4, self.u32(vector)

    def table_vector(self, table: int, field: int) -> list[int]:
        vector = self.vector(table, field)
        if vector is None:
            return []
        start, count = vector
        result = []
        for index in range(count):
            element = start + index * 4
            result.append(element + self.u32(element))
        return result

    def string_field(self, table: int, field: int) -> str | None:
        string = self.indirect_field(table, field)
        if string is None:
            return None
        length = self.u32(string)
        return self.data[string + 4 : string + 4 + length].decode("utf-8")

    def float_field(
        self, table: int, field: int, default: float = 0.0
    ) -> float:
        slot = self.field_slot(table, field)
        return default if slot is None else self.f32(slot)


def decode_entry(reader: FlatBuffer, wrapper: int, index: int) -> dict[str, Any]:
    payload = reader.indirect_field(wrapper, 0)
    if payload is None:
        raise ValueError(f"Build-model wrapper {index} has no payload")
    transform = reader.indirect_field(payload, 0)
    if transform is None:
        raise ValueError(f"Build-model payload {index} has no transform")
    return {
        "record_index": index,
        "model_path": reader.string_field(payload, 2),
        "collision_path": reader.string_field(payload, 1),
        "translation_cm": [
            reader.float_field(transform, field) for field in range(3)
        ],
        "rotation_degrees": [
            reader.float_field(transform, field) for field in range(3, 6)
        ],
        "scale": [
            reader.float_field(transform, field, 1.0)
            for field in range(6, 9)
        ],
    }


def find_route_table(
    reader: FlatBuffer,
) -> tuple[int, int, list[dict[str, Any]]]:
    root = reader.u32(0)
    candidates = []
    for table_index, table in enumerate(reader.table_vector(root, 0)):
        try:
            entries = [
                decode_entry(reader, wrapper, index)
                for index, wrapper in enumerate(reader.table_vector(table, 5))
            ]
        except (UnicodeDecodeError, ValueError):
            continue
        paths = {entry["model_path"] for entry in entries}
        collisions = {
            entry["collision_path"]
            for entry in entries
            if entry["collision_path"]
        }
        if (
            MODEL_PATHS["grass02"] in paths
            and any(path.startswith(ROUTE_COLLISION_PREFIX) for path in collisions)
        ):
            candidates.append((table_index, table, entries))
    if len(candidates) != 1:
        raise RuntimeError(
            f"Expected exactly one Route 1 placement table; found {len(candidates)}"
        )
    return candidates[0]


def parse_arguments() -> argparse.Namespace:
    repo_root = Path(__file__).resolve().parents[2]
    default_unpacked = (
        repo_root.parent
        / "PokemonAutochessEnvironment"
        / "Pokemon_Lets_Go_Pikachu_v0_Environment_GFPAK_Unpacked"
    )
    parser = argparse.ArgumentParser()
    parser.add_argument("--unpacked-root", type=Path, default=default_unpacked)
    parser.add_argument(
        "--output",
        type=Path,
        default=repo_root
        / "docs"
        / "lgpe"
        / "evidence"
        / "route1_buildmodel_placements.json",
    )
    return parser.parse_args()


def main() -> None:
    arguments = parse_arguments()
    archive = arguments.unpacked_root / ARCHIVE_RELATIVE_PATH
    if not archive.is_file():
        raise FileNotFoundError(archive)
    archive_hash = file_sha256(archive)
    if archive_hash != ARCHIVE_SHA256:
        raise RuntimeError(
            f"Placement archive hash changed: expected {ARCHIVE_SHA256}, "
            f"found {archive_hash}"
        )

    reader = FlatBuffer(archive.read_bytes())
    table_index, table_offset, entries = find_route_table(reader)
    if len(entries) != 62:
        raise RuntimeError(f"Route 1 entry count changed: {len(entries)}")

    by_model: dict[str, list[dict[str, Any]]] = {}
    reverse_paths = {path: name for name, path in MODEL_PATHS.items()}
    for entry in entries:
        logical_name = reverse_paths.get(entry["model_path"])
        if logical_name is not None:
            by_model.setdefault(logical_name, []).append(entry)
    counts = Counter({name: len(by_model.get(name, [])) for name in MODEL_PATHS})
    if dict(counts) != EXPECTED_COUNTS:
        raise RuntimeError(
            f"Route 1 vegetation counts changed: expected {EXPECTED_COUNTS}, "
            f"found {dict(counts)}"
        )
    for records in by_model.values():
        if any(record["scale"] != [1.0, 1.0, 1.0] for record in records):
            raise RuntimeError("Route 1 vegetation contains non-unit scale")

    output = {
        "schema_version": 1,
        "profile_id": "lgpe_route1_buildmodel_vegetation_placements",
        "coordinate_system": "source_centimetres_xyz_y_up",
        "source": {
            "placement_archive_relative_path": str(
                ARCHIVE_RELATIVE_PATH
            ).replace("\\", "/"),
            "placement_archive_sha256": archive_hash,
            "route_area_table_index": table_index,
            "route_table_offset_hex": f"0x{table_offset:X}",
            "route_entry_count": len(entries),
        },
        "models": {
            name: {
                "virtual_path": MODEL_PATHS[name],
                "cache_root": f"cache/lgpe/route1_{name}",
                "instance_count": EXPECTED_COUNTS[name],
                "placements": by_model[name],
            }
            for name in ("grass02", "flowers02", "flowers04")
        },
        "instance_count": sum(EXPECTED_COUNTS.values()),
    }
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(
        json.dumps(output, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(
        "LGPE_ROUTE1_BUILDMODEL_PLACEMENTS_OK "
        f"grass02={counts['grass02']} flowers02={counts['flowers02']} "
        f"flowers04={counts['flowers04']} table=0x{table_offset:X}"
    )


if __name__ == "__main__":
    main()
