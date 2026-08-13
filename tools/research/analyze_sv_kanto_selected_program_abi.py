#!/usr/bin/env python3
"""Summarize anonymous resource ABI usage in selected SV Kanto programs."""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import pathlib
import re
import sys
from typing import Any


MANIFEST_SCHEMA = "pokemon-autochess-private-sv-selected-programs-v1"
REPORT_SCHEMA = "pokemon-autochess-sv-selected-program-abi-v1"
UNIFORM_RE = re.compile(
    r"layout\s*\(binding\s*=\s*(?P<binding>\d+)(?P<layout>[^)]*)\)\s*"
    r"uniform\s+(?:(?P<sampler>[^\s{;]+)\s+)?(?P<name>[_A-Za-z][_A-Za-z0-9]*)"
)
TEXTURE_CALL_RE = re.compile(
    r"\b(?:texture|textureLod|textureGrad|textureOffset|textureProj|texelFetch)\s*"
    r"\(\s*(?P<name>fp_t_tcb_[0-9A-F]+)"
)
BUFFER_REFERENCE_RE = re.compile(
    r"\b(?P<name>(?:fp|vp)_c[0-9]+)\.data\[(?P<index>[^\]]+)\]"
)
ATTRIBUTE_RE = re.compile(
    r"layout\s*\(location\s*=\s*(?P<location>\d+)\)\s*"
    r"(?P<direction>in|out)\s+(?P<type>\S+)\s+(?P<name>\w+)"
)


def read_json(path: pathlib.Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8-sig") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise ValueError(f"Expected a JSON object: {path}")
    return value


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def stage_report(path: pathlib.Path, expected_sha256: str) -> dict[str, Any]:
    if not path.is_file():
        raise FileNotFoundError(path)
    actual_sha256 = sha256(path)
    if actual_sha256 != expected_sha256:
        raise ValueError(f"Program stage hash mismatch: {path}")
    source = path.read_text(encoding="utf-8-sig")
    uniforms: dict[str, dict[str, Any]] = {}
    for match in UNIFORM_RE.finditer(source):
        declared_name = match.group("name")
        sampler_type = match.group("sampler")
        kind = "sampler" if sampler_type and sampler_type.startswith("sampler") else "buffer"
        name = declared_name if kind == "sampler" else declared_name.removeprefix("_")
        uniforms[name] = {
            "name": name,
            "declared_name": declared_name,
            "binding": int(match.group("binding")),
            "kind": kind,
            "type": sampler_type if sampler_type and sampler_type.startswith("sampler") else "std140",
        }

    texture_call_counts = collections.Counter(
        match.group("name") for match in TEXTURE_CALL_RE.finditer(source)
    )
    samplers: list[dict[str, Any]] = []
    for name in sorted(
        (key for key, value in uniforms.items() if value["kind"] == "sampler"),
        key=lambda key: (uniforms[key]["binding"], key),
    ):
        declaration = uniforms[name]
        samplers.append(
            {
                **declaration,
                "static_texture_call_count": texture_call_counts[name],
            }
        )

    buffers: dict[str, dict[str, Any]] = {}
    for match in BUFFER_REFERENCE_RE.finditer(source):
        name = match.group("name")
        if name not in uniforms:
            continue
        row = buffers.setdefault(
            name,
            {
                "name": name,
                "binding": uniforms[name]["binding"],
                "constant_indices": set(),
                "dynamic_index_expressions": set(),
                "static_reference_count": 0,
            },
        )
        row["static_reference_count"] += 1
        expression = match.group("index").strip()
        if expression.isdecimal():
            row["constant_indices"].add(int(expression))
        else:
            row["dynamic_index_expressions"].add(expression)

    buffer_rows = []
    for name in sorted(buffers, key=lambda key: (buffers[key]["binding"], key)):
        row = buffers[name]
        buffer_rows.append(
            {
                **row,
                "constant_indices": sorted(row["constant_indices"]),
                "dynamic_index_expressions": sorted(row["dynamic_index_expressions"]),
            }
        )

    attributes = [
        {
            "location": int(match.group("location")),
            "direction": match.group("direction"),
            "type": match.group("type"),
            "name": match.group("name"),
        }
        for match in ATTRIBUTE_RE.finditer(source)
    ]
    attributes.sort(key=lambda row: (row["direction"], row["location"], row["name"]))
    return {
        "identity": path.name,
        "sha256": actual_sha256,
        "samplers": samplers,
        "buffers": buffer_rows,
        "attributes": attributes,
        "static_sampler_count": len(samplers),
        "sampled_sampler_count": sum(row["static_texture_call_count"] > 0 for row in samplers),
        "referenced_buffer_count": len(buffer_rows),
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--program-root", type=pathlib.Path, required=True)
    parser.add_argument("--manifest", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    program_root = args.program_root.resolve()
    if not program_root.is_dir():
        raise FileNotFoundError(program_root)
    manifest_path = (
        args.manifest.resolve()
        if args.manifest
        else program_root / "selected_programs_manifest.json"
    )
    manifest = read_json(manifest_path)
    if manifest.get("schema") != MANIFEST_SCHEMA:
        raise ValueError("Unsupported selected-program manifest")

    programs = []
    unique_fragment_samplers: set[str] = set()
    unique_vertex_samplers: set[str] = set()
    unique_fragment_buffers: set[str] = set()
    unique_vertex_buffers: set[str] = set()
    for record in manifest.get("programs", []):
        relative = pathlib.PurePosixPath(str(record["directory"]))
        if relative.is_absolute() or ".." in relative.parts:
            raise ValueError(f"Unsafe program directory: {relative}")
        directory = program_root.joinpath(*relative.parts)
        stem = f"v{int(record['variation_index']):04d}"
        fragment = stage_report(
            directory / f"{stem}.fsh.maxwell.glsl",
            str(record["fragment_glsl_sha256"]),
        )
        vertex = stage_report(
            directory / f"{stem}.vsh.maxwell.glsl",
            str(record["vertex_glsl_sha256"]),
        )
        unique_fragment_samplers.update(row["name"] for row in fragment["samplers"])
        unique_vertex_samplers.update(row["name"] for row in vertex["samplers"])
        unique_fragment_buffers.update(row["name"] for row in fragment["buffers"])
        unique_vertex_buffers.update(row["name"] for row in vertex["buffers"])
        programs.append(
            {
                "shader_family": str(record["shader_family"]),
                "variation_index": int(record["variation_index"]),
                "material_count": int(record["material_count"]),
                "permutation_count": int(record["permutation_count"]),
                "fragment": fragment,
                "vertex": vertex,
            }
        )

    programs.sort(key=lambda row: (row["shader_family"], row["variation_index"]))
    report = {
        "schema": REPORT_SCHEMA,
        "source_profile": "pokemon-scarlet-v3.0.1",
        "method": {
            "runtime_execution": False,
            "emulator_used": False,
            "evidence_level": "compiled_program_static_abi",
            "limits": (
                "Anonymous Maxwell-to-GLSL resource declarations and static use "
                "sites prove program ABI shape, not source semantic names or "
                "runtime-bound values."
            ),
        },
        "source_manifest": {
            "identity": manifest_path.name,
            "sha256": sha256(manifest_path),
            "program_count": int(manifest["program_count"]),
            "evidence_sha256": str(manifest["evidence_sha256"]),
        },
        "summary": {
            "program_count": len(programs),
            "shader_families": len({row["shader_family"] for row in programs}),
            "unique_fragment_sampler_symbols": len(unique_fragment_samplers),
            "unique_vertex_sampler_symbols": len(unique_vertex_samplers),
            "unique_fragment_buffer_symbols": len(unique_fragment_buffers),
            "unique_vertex_buffer_symbols": len(unique_vertex_buffers),
        },
        "programs": programs,
    }
    if len(programs) != int(manifest["program_count"]):
        raise ValueError("Selected-program manifest count does not match its records")

    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(report, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    print(
        "[SvKantoSelectedProgramAbi] "
        f"programs={len(programs)} families={report['summary']['shader_families']} "
        f"fragment_samplers={len(unique_fragment_samplers)} -> {output}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FileNotFoundError, KeyError, TypeError, ValueError) as error:
        print(f"[SvKantoSelectedProgramAbi] ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
