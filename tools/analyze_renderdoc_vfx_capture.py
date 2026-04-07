#!/usr/bin/env python3
"""Summarize RenderDoc XML+ZIP captures for VFX reverse-engineering.

This script is intentionally conservative. It focuses on answering:

* which draw families exist in the capture,
* which programs/shaders drive them,
* which textures and states they use,
* and, for line-expander families, what the per-draw uploaded geometry looks like.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import struct
import subprocess
import xml.etree.ElementTree as ET
import zipfile
from collections import Counter, defaultdict
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any


GL_TEXTURE0 = 33984


def find_renderdoccmd() -> Path | None:
    candidates = [
        Path(r"C:\Program Files\RenderDoc\renderdoccmd.exe"),
        Path(r"C:\Program Files (x86)\RenderDoc\renderdoccmd.exe"),
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


def run_renderdoc_convert(renderdoccmd: Path, capture: Path, output_zip_xml: Path) -> tuple[Path, Path]:
    output_zip_xml.parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        str(renderdoccmd),
        "convert",
        "--filename",
        str(capture),
        "--output",
        str(output_zip_xml),
        "--input-format",
        "rdc",
        "--convert-format",
        "zip.xml",
    ]
    subprocess.run(cmd, check=True)
    if output_zip_xml.suffix.lower() != ".xml":
        raise ValueError(f"expected .xml output path, got {output_zip_xml}")
    zip_path = output_zip_xml.with_suffix("")
    if not output_zip_xml.exists() or not zip_path.exists():
        raise FileNotFoundError(
            f"RenderDoc conversion did not produce both {output_zip_xml} and {zip_path}"
        )
    return output_zip_xml, zip_path


def named_child(element: ET.Element, tag: str, name: str) -> ET.Element | None:
    return element.find(f"{tag}[@name='{name}']")


def child_text(element: ET.Element, tag: str, name: str) -> str | None:
    child = named_child(element, tag, name)
    return child.text if child is not None else None


def child_int(element: ET.Element, tag: str, name: str) -> int | None:
    text = child_text(element, tag, name)
    return int(text) if text is not None else None


def child_bool(element: ET.Element, tag: str, name: str) -> bool | None:
    text = child_text(element, tag, name)
    if text is None:
        return None
    return text.lower() == "true"


def child_enum(element: ET.Element, name: str) -> tuple[int | None, str | None]:
    child = named_child(element, "enum", name)
    if child is None:
        return None, None
    return int(child.text), child.attrib.get("string")


def child_buffer_ref(element: ET.Element, name: str) -> int | None:
    text = child_text(element, "buffer", name)
    return int(text) if text is not None else None


def load_zip_blob(zf: zipfile.ZipFile, ref_id: int, cache: dict[int, bytes]) -> bytes:
    if ref_id not in cache:
        cache[ref_id] = zf.read(f"{ref_id:06d}")
    return cache[ref_id]


@dataclass
class ShaderInfo:
    shader_id: int
    shader_type: str
    label: str | None = None
    source: str | None = None

    def source_sha1(self) -> str | None:
        if self.source is None:
            return None
        return hashlib.sha1(self.source.encode("utf-8")).hexdigest()

    def is_line_expander(self) -> bool:
        if self.shader_type != "GL_GEOMETRY_SHADER" or self.source is None:
            return False
        return "layout(lines" in self.source and "triangle_strip" in self.source


@dataclass
class ProgramInfo:
    program_id: int
    attached_shaders: list[int] = field(default_factory=list)


@dataclass
class VertexAttribInfo:
    buffer: int
    index: int
    size: int
    gl_type: str
    normalized: bool
    stride: int
    offset: int


@dataclass
class VaoInfo:
    vao_id: int
    element_buffer: int | None = None
    enabled_attribs: set[int] = field(default_factory=set)
    attribs: dict[int, VertexAttribInfo] = field(default_factory=dict)


@dataclass
class BufferWrite:
    chunk_index: int
    buffer: int
    offset: int
    length: int
    ref_id: int
    source: str


@dataclass
class DrawEvent:
    chunk_index: int
    mode: str
    count: int
    index_type: str
    indices_offset: int
    basevertex: int
    program: int | None
    vao: int | None
    textures: dict[str, int]
    ubo_ranges: dict[int, dict[str, int]]
    states: dict[str, Any]
    pending_writes: list[dict[str, Any]]


def parse_sidecar_dir(sidecar_dir: Path) -> dict[int, dict[str, Any]]:
    pattern = re.compile(
        r"^(?P<prefix>.+?)_(?P<eid>\d+)_(?:(?P<stage>vertex|fragment|geometry)_shader_(?P<shader>\d+)|(?P<kind>mesh|vsblock128|psblock128))\.(?P<ext>glsl|csv)$"
    )
    out: dict[int, dict[str, Any]] = {}
    for path in sidecar_dir.iterdir():
        if not path.is_file():
            continue
        match = pattern.match(path.name)
        if not match:
            continue
        eid = int(match.group("eid"))
        entry = out.setdefault(eid, {"paths": [], "shader_ids": [], "kinds": []})
        entry["paths"].append(str(path))
        if match.group("shader"):
            entry["shader_ids"].append(int(match.group("shader")))
            entry["kinds"].append(f"{match.group('stage')}_shader")
        elif match.group("kind"):
            entry["kinds"].append(match.group("kind"))
    for entry in out.values():
        entry["shader_ids"] = sorted(set(entry["shader_ids"]))
        entry["kinds"] = sorted(set(entry["kinds"]))
    return dict(sorted(out.items()))


def parse_capture(
    xml_path: Path,
    zip_path: Path | None,
) -> dict[str, Any]:
    root = ET.parse(xml_path).getroot()
    chunk_list = root.find("chunks")
    if chunk_list is None:
        raise ValueError(f"{xml_path} does not contain <chunks>")

    shaders: dict[int, ShaderInfo] = {}
    programs: dict[int, ProgramInfo] = {}
    vaos: dict[int, VaoInfo] = {}
    draws: list[DrawEvent] = []
    buffer_writes_by_buffer: dict[int, list[BufferWrite]] = defaultdict(list)

    current_program: int | None = None
    current_vao: int | None = None
    current_active_texture = 0
    current_textures: dict[int, dict[str, int]] = defaultdict(dict)
    current_ubo_ranges: dict[int, tuple[int, int, int]] = {}
    current_states: dict[str, Any] = {
        "caps": {},
        "blend_equation": None,
        "blend_func": None,
        "depth_mask": None,
        "depth_func": None,
    }
    pending_writes: dict[int, BufferWrite] = {}

    for chunk in chunk_list.findall("chunk"):
        chunk_index = int(chunk.attrib["chunkIndex"])
        name = chunk.attrib["name"]

        if name == "glCreateShader":
            shader_id = child_int(chunk, "ResourceId", "Shader")
            _, shader_type = child_enum(chunk, "type")
            if shader_id is not None:
                shaders[shader_id] = ShaderInfo(shader_id=shader_id, shader_type=shader_type or "<unknown>")
        elif name == "glShaderSource":
            shader_id = child_int(chunk, "ResourceId", "shader")
            if shader_id is not None:
                sources = chunk.find("array[@name='sources']")
                joined = ""
                if sources is not None:
                    joined = "".join((node.text or "") for node in sources.findall("string"))
                shaders.setdefault(shader_id, ShaderInfo(shader_id=shader_id, shader_type="<unknown>")).source = joined
        elif name == "glObjectLabel":
            resource = child_int(chunk, "ResourceId", "Resource")
            label = child_text(chunk, "string", "Label")
            if resource is not None and resource in shaders and label:
                shaders[resource].label = label
        elif name == "glCreateProgram":
            program_id = child_int(chunk, "ResourceId", "Program")
            if program_id is not None:
                programs[program_id] = ProgramInfo(program_id=program_id)
        elif name == "glAttachShader":
            program_id = child_int(chunk, "ResourceId", "program")
            shader_id = child_int(chunk, "ResourceId", "shader")
            if program_id is not None and shader_id is not None:
                programs.setdefault(program_id, ProgramInfo(program_id=program_id)).attached_shaders.append(shader_id)
        elif name == "glGenVertexArrays":
            vao_id = child_int(chunk, "ResourceId", "array")
            if vao_id is not None:
                vaos.setdefault(vao_id, VaoInfo(vao_id=vao_id))
        elif name == "glVertexArrayElementBuffer":
            vao_id = child_int(chunk, "ResourceId", "vaobj")
            buffer_id = child_int(chunk, "ResourceId", "buffer")
            if vao_id is not None:
                vaos.setdefault(vao_id, VaoInfo(vao_id=vao_id)).element_buffer = buffer_id
        elif name == "glEnableVertexAttribArray":
            vao_id = child_int(chunk, "ResourceId", "vaobj")
            attrib_index = child_int(chunk, "uint", "index")
            if vao_id is not None and attrib_index is not None:
                vaos.setdefault(vao_id, VaoInfo(vao_id=vao_id)).enabled_attribs.add(attrib_index)
        elif name == "glVertexAttribPointer":
            vao_id = child_int(chunk, "ResourceId", "vaobj")
            buffer_id = child_int(chunk, "ResourceId", "buffer")
            attrib_index = child_int(chunk, "uint", "index")
            attrib_size = child_int(chunk, "int", "size")
            _, gl_type = child_enum(chunk, "type")
            normalized = child_bool(chunk, "bool", "normalized")
            stride = child_int(chunk, "int", "stride")
            offset = child_int(chunk, "uint", "offset")
            if (
                vao_id is not None
                and buffer_id is not None
                and attrib_index is not None
                and attrib_size is not None
                and stride is not None
                and offset is not None
            ):
                vao = vaos.setdefault(vao_id, VaoInfo(vao_id=vao_id))
                vao.attribs[attrib_index] = VertexAttribInfo(
                    buffer=buffer_id,
                    index=attrib_index,
                    size=attrib_size,
                    gl_type=gl_type or "<unknown>",
                    normalized=bool(normalized),
                    stride=stride,
                    offset=offset,
                )
        elif name == "glBufferStorage":
            buffer_id = child_int(chunk, "ResourceId", "buffer")
            ref_id = child_buffer_ref(chunk, "data")
            bytesize = child_int(chunk, "uint", "bytesize")
            if buffer_id is not None and ref_id is not None and bytesize is not None:
                write = BufferWrite(
                    chunk_index=chunk_index,
                    buffer=buffer_id,
                    offset=0,
                    length=bytesize,
                    ref_id=ref_id,
                    source="glBufferStorage",
                )
                buffer_writes_by_buffer[buffer_id].append(write)
        elif name == "glBufferData":
            buffer_id = child_int(chunk, "ResourceId", "buffer")
            ref_id = child_buffer_ref(chunk, "data")
            bytesize = child_int(chunk, "uint", "bytesize")
            if buffer_id is not None and ref_id is not None and bytesize is not None:
                write = BufferWrite(
                    chunk_index=chunk_index,
                    buffer=buffer_id,
                    offset=0,
                    length=bytesize,
                    ref_id=ref_id,
                    source="glBufferData",
                )
                buffer_writes_by_buffer[buffer_id].append(write)
        elif name == "Internal::Coherent Mapped Memory Write":
            buffer_id = child_int(chunk, "ResourceId", "buffer")
            offset = child_int(chunk, "uint", "offset")
            length = child_int(chunk, "uint", "length")
            ref_id = child_buffer_ref(chunk, "FlushedData")
            if (
                buffer_id is not None
                and offset is not None
                and length is not None
                and ref_id is not None
            ):
                write = BufferWrite(
                    chunk_index=chunk_index,
                    buffer=buffer_id,
                    offset=offset,
                    length=length,
                    ref_id=ref_id,
                    source=name,
                )
                buffer_writes_by_buffer[buffer_id].append(write)
                pending_writes[buffer_id] = write
        elif name == "glBindVertexArray":
            current_vao = child_int(chunk, "ResourceId", "vaobj")
        elif name == "glUseProgram":
            current_program = child_int(chunk, "ResourceId", "program")
        elif name == "glActiveTexture":
            value, _ = child_enum(chunk, "texture")
            if value is not None:
                current_active_texture = value - GL_TEXTURE0
        elif name == "glBindTexture":
            _, target = child_enum(chunk, "target")
            texture = child_int(chunk, "ResourceId", "texture")
            if target is not None and texture is not None:
                current_textures[current_active_texture][target] = texture
        elif name == "glBindBufferRange":
            _, target_name = child_enum(chunk, "target")
            bind_index = child_int(chunk, "uint", "index")
            buffer_id = child_int(chunk, "ResourceId", "buffer")
            offset = child_int(chunk, "uint", "offset")
            size = child_int(chunk, "uint", "size")
            if (
                target_name == "GL_UNIFORM_BUFFER"
                and bind_index is not None
                and buffer_id is not None
                and offset is not None
                and size is not None
            ):
                current_ubo_ranges[bind_index] = (buffer_id, offset, size)
        elif name == "glEnable":
            _, cap_name = child_enum(chunk, "cap")
            if cap_name is not None:
                current_states["caps"][cap_name] = True
        elif name == "glDisable":
            _, cap_name = child_enum(chunk, "cap")
            if cap_name is not None:
                current_states["caps"][cap_name] = False
        elif name == "glBlendEquationSeparate":
            _, rgb_name = child_enum(chunk, "modeRGB")
            _, alpha_name = child_enum(chunk, "modeAlpha")
            current_states["blend_equation"] = {"rgb": rgb_name, "alpha": alpha_name}
        elif name == "glBlendFuncSeparate":
            _, src_rgb = child_enum(chunk, "srcRGB")
            _, dst_rgb = child_enum(chunk, "dstRGB")
            _, src_alpha = child_enum(chunk, "srcAlpha")
            _, dst_alpha = child_enum(chunk, "dstAlpha")
            current_states["blend_func"] = {
                "src_rgb": src_rgb,
                "dst_rgb": dst_rgb,
                "src_alpha": src_alpha,
                "dst_alpha": dst_alpha,
            }
        elif name == "glDepthMask":
            current_states["depth_mask"] = child_bool(chunk, "bool", "flag")
        elif name == "glDepthFunc":
            _, func_name = child_enum(chunk, "func")
            current_states["depth_func"] = func_name
        elif name == "glDrawElementsBaseVertex":
            _, mode_name = child_enum(chunk, "mode")
            _, type_name = child_enum(chunk, "type")
            count = child_int(chunk, "int", "count")
            indices_offset = child_int(chunk, "uint", "indices")
            basevertex = child_int(chunk, "int", "basevertex")
            if count is None or indices_offset is None or basevertex is None:
                continue
            textures_snapshot: dict[str, int] = {}
            for unit, targets in current_textures.items():
                for target_name, tex_id in targets.items():
                    textures_snapshot[f"unit{unit}:{target_name}"] = tex_id
            ubo_snapshot = {
                index: {"buffer": rng[0], "offset": rng[1], "size": rng[2]}
                for index, rng in sorted(current_ubo_ranges.items())
            }
            draws.append(
                DrawEvent(
                    chunk_index=chunk_index,
                    mode=mode_name or "<unknown>",
                    count=count,
                    index_type=type_name or "<unknown>",
                    indices_offset=indices_offset,
                    basevertex=basevertex,
                    program=current_program,
                    vao=current_vao,
                    textures=textures_snapshot,
                    ubo_ranges=ubo_snapshot,
                    states=json.loads(json.dumps(current_states)),
                    pending_writes=[asdict(write) for write in pending_writes.values()],
                )
            )
            pending_writes = {}

    return {
        "shaders": shaders,
        "programs": programs,
        "vaos": vaos,
        "draws": draws,
        "buffer_writes_by_buffer": buffer_writes_by_buffer,
        "zip_path": zip_path,
    }


def build_program_summary(
    shaders: dict[int, ShaderInfo],
    programs: dict[int, ProgramInfo],
    draws: list[DrawEvent],
) -> dict[int, dict[str, Any]]:
    draws_by_program: dict[int, list[DrawEvent]] = defaultdict(list)
    for draw in draws:
        if draw.program is not None:
            draws_by_program[draw.program].append(draw)

    summary: dict[int, dict[str, Any]] = {}
    for program_id, program in programs.items():
        shader_ids = sorted(program.attached_shaders)
        shader_types = [shaders[sid].shader_type for sid in shader_ids if sid in shaders]
        tex_counter = Counter()
        for draw in draws_by_program.get(program_id, []):
            for binding, tex_id in draw.textures.items():
                tex_counter[(binding, tex_id)] += 1
        summary[program_id] = {
            "program_id": program_id,
            "shader_ids": shader_ids,
            "shader_types": shader_types,
            "draw_count": len(draws_by_program.get(program_id, [])),
            "first_draw_chunk": draws_by_program[program_id][0].chunk_index if draws_by_program.get(program_id) else None,
            "last_draw_chunk": draws_by_program[program_id][-1].chunk_index if draws_by_program.get(program_id) else None,
            "line_expander": any(shaders[sid].is_line_expander() for sid in shader_ids if sid in shaders),
            "top_textures": [
                {"binding": binding, "texture": texture, "count": count}
                for (binding, texture), count in tex_counter.most_common(12)
            ],
        }
    return summary


def build_draw_runs(draws: list[DrawEvent]) -> list[dict[str, Any]]:
    if not draws:
        return []
    runs: list[dict[str, Any]] = []
    current: list[DrawEvent] = [draws[0]]
    for draw in draws[1:]:
        if draw.program == current[-1].program:
            current.append(draw)
            continue
        runs.append(summarize_run(current))
        current = [draw]
    runs.append(summarize_run(current))
    return runs


def summarize_run(run: list[DrawEvent]) -> dict[str, Any]:
    tex0_sequence = [
        draw.textures.get("unit0:GL_TEXTURE_2D_ARRAY") for draw in run
    ]
    return {
        "program": run[0].program,
        "draw_count": len(run),
        "start_chunk": run[0].chunk_index,
        "end_chunk": run[-1].chunk_index,
        "mode": run[0].mode,
        "counts": [draw.count for draw in run],
        "tex0_sequence": tex0_sequence,
        "unique_tex0": sorted({tid for tid in tex0_sequence if tid is not None}),
    }


def find_covering_write(
    writes_by_buffer: dict[int, list[BufferWrite]],
    buffer_id: int,
    offset: int,
    size: int,
    max_chunk_index: int,
) -> BufferWrite | None:
    candidates = writes_by_buffer.get(buffer_id, [])
    for write in reversed(candidates):
        if write.chunk_index > max_chunk_index:
            continue
        if write.offset <= offset and offset + size <= write.offset + write.length:
            return write
    return None


def decode_streak_family(
    draws: list[DrawEvent],
    vaos: dict[int, VaoInfo],
    writes_by_buffer: dict[int, list[BufferWrite]],
    zip_path: Path | None,
    cache: dict[int, bytes],
) -> list[dict[str, Any]]:
    if zip_path is None:
        return []
    with zipfile.ZipFile(zip_path) as zf:
        decoded: list[dict[str, Any]] = []
        for draw in draws:
            if draw.program is None or draw.vao is None or draw.mode != "GL_LINES":
                continue
            vao = vaos.get(draw.vao)
            if vao is None or vao.element_buffer is None:
                continue
            pos_attrib = vao.attribs.get(0)
            color_attrib = vao.attribs.get(5)
            if pos_attrib is None or color_attrib is None:
                continue
            if (
                pos_attrib.buffer != color_attrib.buffer
                or pos_attrib.stride != color_attrib.stride
                or pos_attrib.stride != 16
                or pos_attrib.size != 3
                or color_attrib.gl_type != "GL_UNSIGNED_BYTE"
            ):
                continue

            vertex_write = None
            index_write = None
            for pending in draw.pending_writes:
                if pending["buffer"] == pos_attrib.buffer:
                    vertex_write = pending
                elif pending["buffer"] == vao.element_buffer:
                    index_write = pending
            if vertex_write is None or index_write is None:
                vertex_cover = find_covering_write(
                    writes_by_buffer,
                    pos_attrib.buffer,
                    pos_attrib.offset,
                    pos_attrib.stride * max(1, draw.count),
                    draw.chunk_index,
                )
                index_cover = find_covering_write(
                    writes_by_buffer,
                    vao.element_buffer,
                    draw.indices_offset,
                    2 * draw.count,
                    draw.chunk_index,
                )
                if vertex_cover is not None:
                    vertex_write = asdict(vertex_cover)
                if index_cover is not None:
                    index_write = asdict(index_cover)
            if vertex_write is None or index_write is None:
                continue

            vdata = load_zip_blob(zf, int(vertex_write["ref_id"]), cache)
            idata = load_zip_blob(zf, int(index_write["ref_id"]), cache)

            vertex_count = len(vdata) // pos_attrib.stride
            vertices: list[dict[str, Any]] = []
            for i in range(vertex_count):
                off = i * pos_attrib.stride
                x, y, z = struct.unpack_from("<fff", vdata, off)
                r, g, b, a = vdata[off + 12:off + 16]
                vertices.append(
                    {
                        "local_vertex": i,
                        "absolute_vertex": draw.basevertex + i,
                        "pos": [round(x, 6), round(y, 6), round(z, 6)],
                        "color_rgba8": [r, g, b, a],
                    }
                )

            indices = [
                struct.unpack_from("<H", idata, off)[0]
                for off in range(0, len(idata), 2)
            ]

            segments = []
            for i in range(0, len(indices), 2):
                if i + 1 >= len(indices):
                    break
                start = vertices[indices[i]]
                end = vertices[indices[i + 1]]
                length = math.dist(start["pos"], end["pos"])
                segments.append(
                    {
                        "local_indices": [indices[i], indices[i + 1]],
                        "absolute_indices": [
                            draw.basevertex + indices[i],
                            draw.basevertex + indices[i + 1],
                        ],
                        "start": start["pos"],
                        "end": end["pos"],
                        "length": round(length, 6),
                        "color_rgba8": start["color_rgba8"],
                    }
                )

            decoded.append(
                {
                    "chunk_index": draw.chunk_index,
                    "program": draw.program,
                    "vao": draw.vao,
                    "count": draw.count,
                    "basevertex": draw.basevertex,
                    "textures": draw.textures,
                    "vertex_write": vertex_write,
                    "index_write": index_write,
                    "vertices": vertices,
                    "indices": indices,
                    "segments": segments,
                }
            )
        return decoded


def summarize_streak_family(decoded_draws: list[dict[str, Any]]) -> dict[str, Any]:
    if not decoded_draws:
        return {}
    segment_lengths = [
        segment["length"]
        for draw in decoded_draws
        for segment in draw["segments"]
    ]
    color_counter = Counter(
        tuple(segment["color_rgba8"])
        for draw in decoded_draws
        for segment in draw["segments"]
    )
    count_hist = Counter(draw["count"] for draw in decoded_draws)
    return {
        "draw_count": len(decoded_draws),
        "segment_count": sum(len(draw["segments"]) for draw in decoded_draws),
        "draw_count_histogram": dict(sorted(count_hist.items())),
        "segment_length_min": min(segment_lengths),
        "segment_length_avg": sum(segment_lengths) / len(segment_lengths),
        "segment_length_max": max(segment_lengths),
        "top_colors": [
            {"rgba8": list(color), "count": count}
            for color, count in color_counter.most_common(8)
        ],
    }


def build_sidecar_mapping(
    sidecars: dict[int, dict[str, Any]],
    programs: dict[int, dict[str, Any]],
) -> dict[int, dict[str, Any]]:
    out: dict[int, dict[str, Any]] = {}
    for eid, entry in sidecars.items():
        shader_ids = entry.get("shader_ids", [])
        candidate_programs = []
        for program_id, program in programs.items():
            if shader_ids and set(shader_ids).issubset(program["shader_ids"]):
                candidate_programs.append(program_id)
        out[eid] = {
            "shader_ids": shader_ids,
            "kinds": entry.get("kinds", []),
            "candidate_programs": candidate_programs,
            "paths": entry.get("paths", []),
        }
    return out


def format_report(
    xml_path: Path,
    zip_path: Path | None,
    analysis: dict[str, Any],
) -> str:
    lines: list[str] = []
    lines.append(f"RenderDoc Capture: {xml_path.name}")
    lines.append(f"  xml_path              : {xml_path}")
    lines.append(f"  zip_path              : {zip_path if zip_path else '<none>'}")
    lines.append(f"  shader_count          : {len(analysis['shaders'])}")
    lines.append(f"  program_count         : {len(analysis['programs'])}")
    lines.append(f"  draw_count            : {len(analysis['draws'])}")
    lines.append("")

    lines.append("Key Program Families:")
    ordered = sorted(
        analysis["program_summary"].values(),
        key=lambda item: (item["first_draw_chunk"] or 1 << 30, item["program_id"]),
    )
    for program in ordered:
        if not program["draw_count"]:
            continue
        shaders_text = ", ".join(str(shader_id) for shader_id in program["shader_ids"])
        lines.append(
            f"  program {program['program_id']}: draws={program['draw_count']} "
            f"chunks={program['first_draw_chunk']}..{program['last_draw_chunk']} "
            f"line_expander={'yes' if program['line_expander'] else 'no'} "
            f"shaders=[{shaders_text}]"
        )
        if program["top_textures"]:
            tex_bits = ", ".join(
                f"{item['binding']}={item['texture']} x{item['count']}"
                for item in program["top_textures"][:6]
            )
            lines.append(f"    textures             : {tex_bits}")
    lines.append("")

    lines.append("Contiguous Draw Runs:")
    for run in analysis["draw_runs"]:
        if run["draw_count"] <= 0:
            continue
        tex_preview = ", ".join(str(tex) for tex in run["tex0_sequence"][:12])
        lines.append(
            f"  program {run['program']} chunks={run['start_chunk']}..{run['end_chunk']} "
            f"draws={run['draw_count']} tex0=[{tex_preview}]"
        )
    lines.append("")

    if analysis["sidecar_mapping"]:
        lines.append("Sidecar EID -> Program Candidates:")
        for eid, entry in sorted(analysis["sidecar_mapping"].items()):
            if not entry["candidate_programs"]:
                continue
            lines.append(
                f"  eid {eid}: shaders={entry['shader_ids']} kinds={entry['kinds']} "
                f"candidate_programs={entry['candidate_programs']}"
            )
        lines.append("")

    if analysis["streak_summary"]:
        streak = analysis["streak_summary"]
        lines.append("Decoded Line-Expander Family:")
        lines.append(
            f"  draws={streak['draw_count']} segments={streak['segment_count']} "
            f"count_hist={streak['draw_count_histogram']}"
        )
        lines.append(
            f"  segment_length        : min={streak['segment_length_min']:.6f} "
            f"avg={streak['segment_length_avg']:.6f} "
            f"max={streak['segment_length_max']:.6f}"
        )
        if streak["top_colors"]:
            lines.append(
                "  colors                : "
                + ", ".join(
                    f"{item['rgba8']} x{item['count']}"
                    for item in streak["top_colors"]
                )
            )
        lines.append("  first_draw_samples:")
        for draw in analysis["streak_draws"][:8]:
            seg_text = ", ".join(
                f"{segment['absolute_indices']} len={segment['length']:.3f}"
                for segment in draw["segments"]
            )
            lines.append(
                f"    chunk {draw['chunk_index']}: count={draw['count']} "
                f"basevertex={draw['basevertex']} segments={seg_text}"
            )
        lines.append("")

    return "\n".join(lines) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="RenderDoc .rdc or XML/ZIP capture")
    parser.add_argument("--zip", type=Path, default=None, help="Companion ZIP file if parsing XML directly")
    parser.add_argument("--sidecar-dir", type=Path, default=None, help="Directory of exported sidecar files such as tackle_1253_*.glsl")
    parser.add_argument("--renderdoccmd", type=Path, default=None, help="Path to renderdoccmd.exe")
    parser.add_argument("--converted-dir", type=Path, default=Path("debug"), help="Where converted XML+ZIP captures should be written")
    parser.add_argument("--report-out", type=Path, default=None, help="Write human-readable report")
    parser.add_argument("--json-out", type=Path, default=None, help="Write JSON report")
    args = parser.parse_args()

    xml_path = args.input
    zip_path = args.zip

    if args.input.suffix.lower() == ".rdc":
        renderdoccmd = args.renderdoccmd or find_renderdoccmd()
        if renderdoccmd is None:
            raise FileNotFoundError("renderdoccmd.exe not found; pass --renderdoccmd explicitly")
        output_zip_xml = args.converted_dir / f"{args.input.stem}.zip.xml"
        xml_path, zip_path = run_renderdoc_convert(renderdoccmd, args.input, output_zip_xml)
    else:
        if zip_path is None:
            if args.input.name.endswith(".zip.xml"):
                zip_path = args.input.with_suffix("")
            else:
                candidate = args.input.with_suffix(".zip")
                if candidate.exists():
                    zip_path = candidate

    capture = parse_capture(xml_path, zip_path)
    shaders = capture["shaders"]
    programs = capture["programs"]
    vaos = capture["vaos"]
    draws = capture["draws"]
    writes_by_buffer = capture["buffer_writes_by_buffer"]

    program_summary = build_program_summary(shaders, programs, draws)
    draw_runs = build_draw_runs(draws)

    zip_cache: dict[int, bytes] = {}
    streak_candidate_programs = [
        program_id
        for program_id, summary in program_summary.items()
        if summary["line_expander"]
    ]
    streak_draws = decode_streak_family(
        [draw for draw in draws if draw.program in streak_candidate_programs],
        vaos,
        writes_by_buffer,
        zip_path,
        zip_cache,
    )
    streak_summary = summarize_streak_family(streak_draws)

    sidecar_mapping: dict[int, dict[str, Any]] = {}
    if args.sidecar_dir is not None and args.sidecar_dir.exists():
        sidecars = parse_sidecar_dir(args.sidecar_dir)
        sidecar_mapping = build_sidecar_mapping(sidecars, program_summary)

    analysis = {
        "xml_path": str(xml_path),
        "zip_path": str(zip_path) if zip_path else None,
        "shaders": {
            shader_id: {
                "shader_id": info.shader_id,
                "shader_type": info.shader_type,
                "label": info.label,
                "source_sha1": info.source_sha1(),
                "line_expander": info.is_line_expander(),
            }
            for shader_id, info in sorted(shaders.items())
        },
        "programs": {
            program_id: {
                "program_id": info.program_id,
                "attached_shaders": sorted(info.attached_shaders),
            }
            for program_id, info in sorted(programs.items())
        },
        "vaos": {
            vao_id: {
                "vao_id": vao.vao_id,
                "element_buffer": vao.element_buffer,
                "enabled_attribs": sorted(vao.enabled_attribs),
                "attribs": {
                    index: asdict(info)
                    for index, info in sorted(vao.attribs.items())
                },
            }
            for vao_id, vao in sorted(vaos.items())
        },
        "draws": [asdict(draw) for draw in draws],
        "program_summary": program_summary,
        "draw_runs": draw_runs,
        "sidecar_mapping": sidecar_mapping,
        "streak_summary": streak_summary,
        "streak_draws": streak_draws,
    }

    report_text = format_report(xml_path, zip_path, analysis)
    print(report_text, end="")

    if args.report_out:
        args.report_out.parent.mkdir(parents=True, exist_ok=True)
        args.report_out.write_text(report_text, encoding="utf-8")
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(analysis, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
