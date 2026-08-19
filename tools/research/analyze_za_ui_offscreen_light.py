#!/usr/bin/env python3
"""Decode Z-A's source off-screen Pokemon-light FlatBuffer evidence.

The source ``.trlgt`` schema is not public.  This reader deliberately exposes
only the stable table shape used by the retained light asset: component name,
component type, transform, scalar/vector/string properties, and source hash.
It never writes or embeds the proprietary source payload.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import struct
from typing import Any


class FlatBuffer:
    def __init__(self, payload: bytes) -> None:
        self.payload = payload

    def _bounds(self, offset: int, size: int) -> None:
        if offset < 0 or size < 0 or offset + size > len(self.payload):
            raise ValueError(
                f"FlatBuffer access [{offset}, {offset + size}) is out of bounds")

    def u16(self, offset: int) -> int:
        self._bounds(offset, 2)
        return struct.unpack_from("<H", self.payload, offset)[0]

    def u32(self, offset: int) -> int:
        self._bounds(offset, 4)
        return struct.unpack_from("<I", self.payload, offset)[0]

    def i32(self, offset: int) -> int:
        self._bounds(offset, 4)
        return struct.unpack_from("<i", self.payload, offset)[0]

    def f32(self, offset: int) -> float:
        self._bounds(offset, 4)
        return struct.unpack_from("<f", self.payload, offset)[0]

    def field(self, table: int, index: int) -> int | None:
        vtable = table - self.i32(table)
        self._bounds(vtable, 4)
        field_count = self.u16(vtable) // 2 - 2
        if index >= field_count:
            return None
        relative = self.u16(vtable + 4 + index * 2)
        return table + relative if relative else None

    def indirect(self, offset: int) -> int:
        return offset + self.u32(offset)

    def string(self, field: int) -> str:
        start = self.indirect(field)
        length = self.u32(start)
        self._bounds(start + 4, length)
        return self.payload[start + 4:start + 4 + length].decode("utf-8")

    def vector(self, field: int) -> tuple[int, int]:
        start = self.indirect(field)
        count = self.u32(start)
        return start + 4, count


def parse_property_vector(
        source: FlatBuffer,
        component: int,
        field_index: int,
        kind: str) -> dict[str, Any]:
    vector_field = source.field(component, field_index)
    if vector_field is None:
        return {}
    entries, count = source.vector(vector_field)
    result: dict[str, Any] = {}
    for index in range(count):
        entry = source.indirect(entries + index * 4)
        name_field = source.field(entry, 0)
        if name_field is None:
            raise ValueError(f"{kind} property {index} has no name")
        name = source.string(name_field)
        value_field = source.field(entry, 1)
        if kind == "scalar":
            value: Any = source.f32(value_field) if value_field is not None else 0.0
        elif kind == "vector":
            value = ([source.f32(value_field + lane * 4) for lane in range(4)]
                     if value_field is not None else [0.0, 0.0, 0.0, 0.0])
        elif kind == "string":
            value = source.string(value_field) if value_field is not None else ""
        else:
            raise ValueError(f"unsupported property kind: {kind}")
        result[name] = value
    return result


def parse_transform(source: FlatBuffer, component: int) -> dict[str, list[float]] | None:
    transform_field = source.field(component, 2)
    if transform_field is None:
        return None
    transform = source.indirect(transform_field)
    # Trinity stores three inline float3 structs.  The table vtable order is
    # scale, rotation, translation while their physical object offsets are
    # translation (+4), Euler rotation (+16), and scale (+28).
    return {
        "translation": [source.f32(transform + 4 + lane * 4) for lane in range(3)],
        "rotation_euler_radians": [
            source.f32(transform + 16 + lane * 4) for lane in range(3)],
        "scale": [source.f32(transform + 28 + lane * 4) for lane in range(3)],
    }


def parse_light(path: pathlib.Path) -> dict[str, Any]:
    payload = path.read_bytes()
    source = FlatBuffer(payload)
    root = source.u32(0)
    component_field = source.field(root, 1)
    if component_field is None:
        raise ValueError("root has no component vector")
    entries, count = source.vector(component_field)
    components: list[dict[str, Any]] = []
    for index in range(count):
        component = source.indirect(entries + index * 4)
        name_field = source.field(component, 0)
        type_field = source.field(component, 1)
        if name_field is None or type_field is None:
            raise ValueError(f"component {index} is missing its identity")
        record: dict[str, Any] = {
            "index": index,
            "name": source.string(name_field),
            "type": source.string(type_field),
            "scalars": parse_property_vector(source, component, 3, "scalar"),
            "vectors": parse_property_vector(source, component, 4, "vector"),
            "strings": parse_property_vector(source, component, 5, "string"),
        }
        transform = parse_transform(source, component)
        if transform is not None:
            record["transform"] = transform
        components.append(record)
    return {
        "schema": "phlosion-za-ui-offscreen-light-evidence-v1",
        "source_file": path.name,
        "source_size": len(payload),
        "source_sha256": hashlib.sha256(payload).hexdigest(),
        "component_count": len(components),
        "components": components,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    report = parse_light(args.source.resolve())
    encoded = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded, encoding="utf-8")
    else:
        print(encoded, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
