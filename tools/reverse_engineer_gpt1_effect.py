#!/usr/bin/env python3
"""Reverse-engineer Pokemon Colosseum/XD move-effect FSYS payloads.

This tool is intentionally conservative: it reports structure candidates,
pointer tables, float-rich records, and GPT1-relative blocks without claiming
that a field is definitively a texture, timeline, or particle control unless
the evidence is strong.
"""

from __future__ import annotations

import argparse
import json
import math
import struct
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any, Iterable


FSYS_MAGIC = b"FSYS"
LZSS_MAGIC = b"LZSS"
GPT1_MAGIC = b"GPT1"
LZSS_HEADER_SIZE = 16


def u8(data: bytes, offset: int) -> int:
    return data[offset]


def u16be(data: bytes, offset: int) -> int:
    return struct.unpack_from(">H", data, offset)[0]


def s16be(data: bytes, offset: int) -> int:
    return struct.unpack_from(">h", data, offset)[0]


def u32be(data: bytes, offset: int) -> int:
    return struct.unpack_from(">I", data, offset)[0]


def s32be(data: bytes, offset: int) -> int:
    return struct.unpack_from(">i", data, offset)[0]


def f32be(data: bytes, offset: int) -> float:
    return struct.unpack_from(">f", data, offset)[0]


def align_up(value: int, alignment: int) -> int:
    if alignment <= 0:
        return value
    return (value + alignment - 1) // alignment * alignment


def safe_ascii(data: bytes) -> str:
    return "".join(chr(b) if 32 <= b < 127 else "." for b in data)


def read_c_string(data: bytes, offset: int) -> str:
    if offset < 0 or offset >= len(data):
        return f"<bad:0x{offset:X}>"
    end = data.find(b"\x00", offset)
    if end < 0:
        end = len(data)
    return data[offset:end].decode("ascii", errors="replace")


def lzss_decompress(data: bytes, offset: int) -> bytes:
    if data[offset:offset + 4] != LZSS_MAGIC:
        raise ValueError(f"LZSS magic not found at 0x{offset:X}")

    compressed_size = u32be(data, offset + 8)
    src = data[offset + LZSS_HEADER_SIZE:offset + compressed_size]

    ei, ej, p = 12, 4, 2
    n = 1 << ei
    f = 1 << ej
    rless = 2

    ring = bytearray(n)
    out = bytearray()
    r = (n - f) - rless
    pos = 0
    flags = 0

    n -= 1
    f -= 1

    while pos < len(src):
        if (flags & 0x100) == 0:
            flags = src[pos] | 0xFF00
            pos += 1

        if pos >= len(src):
            break

        if flags & 1:
            c = src[pos]
            pos += 1
            out.append(c)
            ring[r] = c
            r = (r + 1) & n
        else:
            if pos + 1 >= len(src):
                break
            i = src[pos]
            j = src[pos + 1]
            pos += 2
            i |= (j >> ej) << 8
            j = (j & f) + p

            for k in range(j + 1):
                c = ring[(i + k) & n]
                out.append(c)
                ring[r] = c
                r = (r + 1) & n

        flags >>= 1

    return bytes(out)


@dataclass
class FsysEntry:
    index: int
    entry_ptr: int
    file_type: int
    data_address: int
    file_size: int
    flags: int
    short_name: str
    full_name: str | None
    is_compressed: bool
    raw_slice_len: int
    payload_len: int


def parse_fsys_entries(raw: bytes) -> list[FsysEntry]:
    if raw[:4] != FSYS_MAGIC:
        raise ValueError("Not an FSYS archive")

    entry_count = u32be(raw, 0x0C)
    metadata_list_ptr = u32be(raw, 0x40)
    entries: list[FsysEntry] = []

    for index in range(entry_count):
        entry_ptr = u32be(raw, metadata_list_ptr + index * 4)
        file_type = u8(raw, entry_ptr + 0x02)
        data_address = u32be(raw, entry_ptr + 0x04)
        flags = u32be(raw, entry_ptr + 0x0C)
        file_size = u32be(raw, entry_ptr + 0x14)
        full_ptr = u32be(raw, entry_ptr + 0x1C)
        short_ptr = u32be(raw, entry_ptr + 0x24)
        full_name = None if full_ptr == 0 else read_c_string(raw, full_ptr)
        short_name = read_c_string(raw, short_ptr) if short_ptr else f"entry_{index}"
        entry_raw = raw[data_address:data_address + file_size]
        is_compressed = bool(flags & 0x80000000)
        payload_len = len(lzss_decompress(entry_raw, 0)) if is_compressed and entry_raw[:4] == LZSS_MAGIC else len(entry_raw)
        entries.append(
            FsysEntry(
                index=index,
                entry_ptr=entry_ptr,
                file_type=file_type,
                data_address=data_address,
                file_size=file_size,
                flags=flags,
                short_name=short_name,
                full_name=full_name,
                is_compressed=is_compressed,
                raw_slice_len=len(entry_raw),
                payload_len=payload_len,
            )
        )
    return entries


def extract_single_payload_from_fsys(raw: bytes, entry_index: int = 0) -> tuple[FsysEntry, bytes]:
    entries = parse_fsys_entries(raw)
    if entry_index < 0 or entry_index >= len(entries):
        raise IndexError(f"entry_index {entry_index} out of range for {len(entries)} FSYS entries")
    entry = entries[entry_index]
    entry_raw = raw[entry.data_address:entry.data_address + entry.file_size]
    payload = lzss_decompress(entry_raw, 0) if entry.is_compressed else entry_raw
    return entry, payload


def floatish(value: float) -> bool:
    return math.isfinite(value) and abs(value) <= 100000.0


def fixed_16_16(value: int) -> float:
    return float(value) / 65536.0


def fixed_20_12(value: int) -> float:
    return float(value) / 4096.0


def analyze_words(data: bytes, offset: int, word_count: int = 8) -> dict[str, Any]:
    words = []
    for i in range(word_count):
        off = offset + i * 4
        if off + 4 > len(data):
            break
        u = u32be(data, off)
        s = s32be(data, off)
        f = f32be(data, off)
        words.append(
            {
                "offset": off,
                "u32_be": u,
                "s32_be": s,
                "f32_be": f if floatish(f) else None,
                "fixed16_16": fixed_16_16(s),
                "fixed20_12": fixed_20_12(s),
                "u16_be": [u16be(data, off), u16be(data, off + 2)],
                "s16_be": [s16be(data, off), s16be(data, off + 2)],
            }
        )
    return {"offset": offset, "raw_hex": data[offset:offset + word_count * 4].hex(), "words": words}


def detect_pointer_tables(
    data: bytes,
    candidate_bases: dict[str, int],
    search_start: int = 0,
    search_end: int | None = None,
    min_run: int = 3,
) -> list[dict[str, Any]]:
    if search_end is None:
        search_end = len(data)
    hits: list[dict[str, Any]] = []
    for off in range(search_start, max(search_start, search_end - min_run * 4) + 1, 4):
        vals = [u32be(data, off + i * 4) for i in range(min_run)]
        for base_name, base in candidate_bases.items():
            abs_vals = [base + v for v in vals]
            if all(0 <= v < len(data) for v in abs_vals) and vals == sorted(vals) and len(set(vals)) == len(vals):
                hits.append(
                    {
                        "table_offset": off,
                        "relative_base": base_name,
                        "relative_values": vals,
                        "absolute_values": abs_vals,
                    }
                )
    return hits


def detect_float_rich_regions(data: bytes, start: int, end: int, stride: int = 0x20) -> list[dict[str, Any]]:
    out: list[dict[str, Any]] = []
    for off in range(start, max(start, end - stride) + 1, 4):
        chunk = data[off:off + stride]
        if len(chunk) < stride:
            break
        fvals = [f32be(chunk, i) for i in range(0, stride, 4)]
        useful = [f for f in fvals if floatish(f) and abs(f) > 1.0e-6]
        if len(useful) >= 4:
            out.append(
                {
                    "offset": off,
                    "float_count": len(useful),
                    "floats": [round(f, 6) if floatish(f) else None for f in fvals],
                    "hex": chunk.hex(),
                }
            )
    return out


def parse_texture_directory(data: bytes, directory_abs: int) -> dict[str, Any] | None:
    if directory_abs < 0 or directory_abs + 4 > len(data):
        return None
    count = u32be(data, directory_abs)
    if count <= 0 or count > 64:
        return None
    offsets = []
    for i in range(count):
        off = directory_abs + 4 + i * 4
        if off + 4 > len(data):
            return None
        rel = u32be(data, off)
        offsets.append(rel)
    if not all(0 < rel < len(data) for rel in offsets):
        return None

    blocks = []
    for index, rel in enumerate(offsets):
        abs_off = directory_abs + rel
        next_abs = directory_abs + offsets[index + 1] if index + 1 < len(offsets) else len(data)
        header = [u32be(data, abs_off + i * 4) for i in range(8) if abs_off + (i + 1) * 4 <= len(data)]
        blocks.append(
            {
                "index": index,
                "relative_offset": rel,
                "absolute_offset": abs_off,
                "next_absolute_offset": next_abs,
                "span": next_abs - abs_off,
                "u32_header": header,
                "header_interpretation": analyze_words(data, abs_off, 8),
            }
        )
    return {"directory_abs": directory_abs, "count": count, "relative_offsets": offsets, "blocks": blocks}


@dataclass
class Gpt1Info:
    start: int
    header_size: int
    raw_data_offsets: list[int]
    subentry_count: int
    subentry_relative_offsets: list[int]
    subentry_absolute_offsets: list[int]
    subentries: list[dict[str, Any]] = field(default_factory=list)
    texture_directory: dict[str, Any] | None = None
    pointer_tables: list[dict[str, Any]] = field(default_factory=list)
    float_rich_regions: list[dict[str, Any]] = field(default_factory=list)


def parse_gpt1(payload: bytes, start: int) -> Gpt1Info:
    header_size = u32be(payload, start + 4)
    raw_data_offsets = [u32be(payload, start + 8), u32be(payload, start + 12), u32be(payload, start + 16)]
    subentry_count = u32be(payload, start + 0x28)
    subentry_relative_offsets = [u32be(payload, start + 0x2C + i * 4) for i in range(subentry_count)]

    # Strongest evidence: subentry offsets are relative to GPT1 + header_size.
    subentry_base = start + header_size
    subentry_absolute_offsets = [subentry_base + rel for rel in subentry_relative_offsets]

    # Strongest evidence for the bulk directory split: raw data offsets are relative to GPT1 start.
    section_relative_data_abs = [start + rel for rel in raw_data_offsets]

    subentries: list[dict[str, Any]] = []
    data_start_abs = section_relative_data_abs[0]
    for index, abs_off in enumerate(subentry_absolute_offsets):
        next_abs = subentry_absolute_offsets[index + 1] if index + 1 < len(subentry_absolute_offsets) else data_start_abs
        if next_abs < abs_off:
            next_abs = abs_off
        subentries.append(
            {
                "index": index,
                "relative_offset": subentry_relative_offsets[index],
                "absolute_offset": abs_off,
                "end_offset": next_abs,
                "length": next_abs - abs_off,
                "analysis_0x20": analyze_words(payload, abs_off, 8),
                "analysis_0x30": analyze_words(payload, abs_off, 12),
                "ascii_preview": safe_ascii(payload[abs_off: min(next_abs, abs_off + 0x40)]),
                "hex_preview": payload[abs_off: min(next_abs, abs_off + 0x40)].hex(),
            }
        )

    candidate_bases = {
        "payload_start": 0,
        "gpt1_start": start,
        "gpt1_plus_header": subentry_base,
        "gpt1_data_start_section_relative": data_start_abs,
    }
    pointer_tables = detect_pointer_tables(payload, candidate_bases, start, min(len(payload), data_start_abs))
    float_rich_regions = detect_float_rich_regions(payload, start, min(len(payload), data_start_abs), 0x20)
    texture_directory = parse_texture_directory(payload, data_start_abs)

    return Gpt1Info(
        start=start,
        header_size=header_size,
        raw_data_offsets=raw_data_offsets,
        subentry_count=subentry_count,
        subentry_relative_offsets=subentry_relative_offsets,
        subentry_absolute_offsets=subentry_absolute_offsets,
        subentries=subentries,
        texture_directory=texture_directory,
        pointer_tables=pointer_tables,
        float_rich_regions=float_rich_regions,
    )


def summarize_gpt1(gpt1: Gpt1Info) -> list[str]:
    lines = []
    lines.append(f"GPT1 @ 0x{gpt1.start:X}")
    lines.append(f"  header_size            : 0x{gpt1.header_size:X}")
    lines.append(
        "  raw_data_offsets       : "
        + ", ".join(f"0x{v:X}" for v in gpt1.raw_data_offsets)
        + " (interpreted section-relative)"
    )
    lines.append(f"  subentry_count         : {gpt1.subentry_count}")
    lines.append(
        "  subentry_rel_offsets   : " + ", ".join(f"0x{v:X}" for v in gpt1.subentry_relative_offsets)
    )
    lines.append(
        "  subentry_abs_offsets   : " + ", ".join(f"0x{v:X}" for v in gpt1.subentry_absolute_offsets)
    )
    if gpt1.texture_directory:
        td = gpt1.texture_directory
        lines.append(f"  texture_directory_abs  : 0x{td['directory_abs']:X}")
        lines.append(f"  texture_block_count    : {td['count']}")
        for block in td["blocks"]:
            lines.append(
                f"    block[{block['index']}] rel=0x{block['relative_offset']:X} "
                f"abs=0x{block['absolute_offset']:X} span=0x{block['span']:X} "
                f"u32={', '.join(f'0x{x:X}' for x in block['u32_header'])}"
            )
    lines.append(f"  pointer_table_hits     : {len(gpt1.pointer_tables)}")
    lines.append(f"  float_rich_region_hits : {len(gpt1.float_rich_regions)}")
    return lines


def summarize_payload(name: str, payload: bytes, gpt1: Gpt1Info | None) -> list[str]:
    lines = []
    lines.append(f"Payload: {name}")
    lines.append(f"  length                 : 0x{len(payload):X} ({len(payload)} bytes)")
    lines.append(f"  GPT1 present           : {'yes' if gpt1 else 'no'}")
    if gpt1:
        lines.extend(summarize_gpt1(gpt1))
    return lines


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="FSYS archive or decompressed payload")
    parser.add_argument("--mode", choices=["auto", "fsys", "payload"], default="auto")
    parser.add_argument("--entry-index", type=int, default=0, help="FSYS entry index to inspect")
    parser.add_argument("--report-out", type=Path, default=None, help="Write human-readable report")
    parser.add_argument("--json-out", type=Path, default=None, help="Write JSON report")
    args = parser.parse_args()

    raw = args.input.read_bytes()
    mode = args.mode
    if mode == "auto":
        if raw[:4] == FSYS_MAGIC:
            mode = "fsys"
        else:
            mode = "payload"

    report: dict[str, Any] = {"input": str(args.input), "mode": mode}
    payload_name = args.input.name
    payload = raw

    if mode == "fsys":
        entries = parse_fsys_entries(raw)
        report["fsys"] = {
            "entry_count": len(entries),
            "entries": [asdict(entry) for entry in entries],
        }
        entry, payload = extract_single_payload_from_fsys(raw, args.entry_index)
        payload_name = entry.full_name or entry.short_name
        report["selected_entry"] = asdict(entry)

    report["payload"] = {
        "name": payload_name,
        "length": len(payload),
        "ascii_hits": [
            {"offset": i, "text": payload[i:i + 4].decode("ascii", errors="replace")}
            for i in range(len(payload) - 3)
            if payload[i:i + 4] in {GPT1_MAGIC, b"TEX1", b"JPC1", b"JPA1", b"BCK1", b"BTK1", b"BRK1"}
        ],
    }

    gpt1 = None
    gpt1_offset = payload.find(GPT1_MAGIC)
    if gpt1_offset >= 0:
        gpt1 = parse_gpt1(payload, gpt1_offset)
        report["gpt1"] = asdict(gpt1)

    lines = summarize_payload(payload_name, payload, gpt1)
    if gpt1:
        lines.append("")
        lines.append("Suspicious control candidates:")
        for entry in gpt1.subentries:
            off = entry["absolute_offset"]
            length = entry["length"]
            if off >= gpt1.start + 0x170 or off >= 0x490:
                lines.append(
                    f"  subentry[{entry['index']}] @ 0x{off:X}, len=0x{length:X}, "
                    f"first_words="
                    + ", ".join(f"0x{w['u32_be']:X}" for w in entry["analysis_0x20"]["words"][:4])
                )
        lines.append("")
        lines.append("Top float-rich regions before the texture directory:")
        for region in gpt1.float_rich_regions[:12]:
            lines.append(
                f"  0x{region['offset']:X}: float_count={region['float_count']} "
                f"floats={region['floats'][:8]}"
            )

    report_text = "\n".join(lines) + "\n"
    print(report_text, end="")

    if args.report_out:
        args.report_out.write_text(report_text, encoding="utf-8")
    if args.json_out:
        args.json_out.write_text(json.dumps(report, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
