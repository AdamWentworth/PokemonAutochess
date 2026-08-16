#!/usr/bin/env python3
"""Trace selected Z-A IkCharacter programs from source resources to outputs.

The Maxwell decoder emits single-assignment-style temporaries.  This tool
builds a conservative graph from every temporary assignment (including both
sides of control-flow joins), then walks backward from the fragment outputs.
It promotes only hashes, resource identities, aggregate dependency counts,
and cross-family material-buffer mappings; proprietary shader text remains in
the private shader-study directory.
"""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import pathlib
import re
import struct
import sys
from typing import Any


SCHEMA = "pokemon-autochess-za-ik-character-dataflow-evidence-v2"
SOURCE_PROFILE = "pokemon-legends-za-v2.0.0"
MANIFEST_SCHEMA = "pokemon-autochess-private-za-selected-programs-v1"
SELECTED_VARIATIONS = {514: 140, 594: 2, 682: 32, 1214: 48}
TEMP_RE = re.compile(r"\btemp_\d+\b")
ASSIGNMENT_RE = re.compile(r"^\s*(temp_\d+)\s*=\s*(.*);\s*$")
OUTPUT_RE = re.compile(
    r"^\s*((?:out_attr\d+|gl_Position)\.[xyzw])\s*=\s*(.*);\s*$")
SAMPLER_RE = re.compile(r"\b((?:fp|vp)_t_tcb_[0-9A-F]+)\b")
BUFFER_RE = re.compile(
    r"\b((?:fp|vp)_c\d+)\.data\[([^\]]+)\](?:\.([xyzw]+))?")

FRAGMENT_ROLE_BY_SAMPLER = {
    "fp_t_tcb_8": "BaseColorMap",
    "fp_t_tcb_A": "SpecularMaskMap",
    "fp_t_tcb_C": "NormalMap",
    "fp_t_tcb_E": "ParallaxMap",
    "fp_t_tcb_10": "ShadowingColorMaskMap",
    "fp_t_tcb_12": "ShadowingColorMap",
    "fp_t_tcb_14": "OcclusionMap",
    "fp_t_tcb_16": "LayerMaskMap",
    "fp_t_tcb_18": "RimLightMaskMap",
    "fp_t_tcb_1A": "HairSpecularMap",
    "fp_t_tcb_1C": "LocalReflectionMap",
    "fp_t_tcb_1E": "HighlightMaskMap",
    "fp_t_tcb_20": "EyelidShadowMaskMap",
    "fp_t_tcb_34": "DiffuseIrradianceCube",
}
VERTEX_ROLE_BY_SAMPLER = {"vp_t_tcb_24": "DisplacementMap"}


def read_json(path: pathlib.Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(value, dict):
        raise ValueError(f"Expected a JSON object: {path}")
    return value


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require_source_fragments(
        source: str, fragments: list[str], label: str) -> None:
    missing = [fragment for fragment in fragments if fragment not in source]
    if missing:
        raise ValueError(f"{label} data-flow signature changed: {missing}")


def bnsh_reflection_report(
        path: pathlib.Path, variation_index: int) -> dict[str, Any]:
    """Record whether the selected binary program retained reflection data."""
    payload = path.read_bytes()

    def u32(offset: int) -> int:
        if offset < 0 or offset + 4 > len(payload):
            raise ValueError(f"BNSH u32 offset is out of range: 0x{offset:X}")
        return struct.unpack_from("<I", payload, offset)[0]

    def u64(offset: int) -> int:
        if offset < 0 or offset + 8 > len(payload):
            raise ValueError(f"BNSH u64 offset is out of range: 0x{offset:X}")
        return struct.unpack_from("<Q", payload, offset)[0]

    grsc_offset = payload.find(b"grsc", 0, min(len(payload), 0x100))
    if grsc_offset < 0:
        raise ValueError(f"BNSH grsc block was not found: {path}")
    variation_count = u32(grsc_offset + 0x1C)
    if variation_index < 0 or variation_index >= variation_count:
        raise ValueError(
            f"BNSH variation {variation_index} is outside "
            f"0..{variation_count - 1}")
    variation_array_offset = u64(grsc_offset + 0x20)
    entry_offset = variation_array_offset + variation_index * 0x40
    program_offset = u64(entry_offset) + u64(entry_offset + 0x10)
    reflection_offset = u64(program_offset + 0x78)
    return {
        "variation_index": variation_index,
        "program_offset_hex": f"0x{program_offset:X}",
        "reflection_pointer_hex": f"0x{reflection_offset:X}",
        "status": "absent_or_stripped" if reflection_offset == 0 else "retained",
    }


def build_graph(source: str) -> dict[str, Any]:
    definitions: dict[str, list[dict[str, Any]]] = collections.defaultdict(list)
    outputs: dict[str, set[str]] = {}
    reverse: dict[str, set[str]] = collections.defaultdict(set)
    sampler_seeds: dict[str, set[str]] = collections.defaultdict(set)
    for line_number, line in enumerate(source.splitlines(), 1):
        assignment = ASSIGNMENT_RE.match(line)
        if assignment:
            target, expression = assignment.groups()
            dependencies = set(TEMP_RE.findall(expression))
            definitions[target].append({
                "line": line_number,
                "expression": expression,
                "dependencies": dependencies,
            })
            for dependency in dependencies:
                reverse[dependency].add(target)
            if "texture" in expression:
                for sampler in SAMPLER_RE.findall(expression):
                    sampler_seeds[sampler].add(target)
            continue
        output = OUTPUT_RE.match(line)
        if output:
            target, expression = output.groups()
            # Constant initialization is followed by the real assignment in
            # decoded vertex stages. Unioning both is conservative and leaves
            # the dependency roots unchanged.
            outputs.setdefault(target, set()).update(TEMP_RE.findall(expression))
    if not outputs or not any(outputs.values()):
        raise ValueError("Decoded stage has no temporary-backed outputs")
    return {
        "definitions": definitions,
        "reverse": reverse,
        "sampler_seeds": sampler_seeds,
        "outputs": outputs,
    }


def backward_closure(graph: dict[str, Any], roots: set[str]) -> set[str]:
    closure = set(roots)
    pending = list(roots)
    while pending:
        target = pending.pop()
        for row in graph["definitions"].get(target, []):
            for dependency in row["dependencies"]:
                if dependency not in closure:
                    closure.add(dependency)
                    pending.append(dependency)
    return closure


def forward_closure(graph: dict[str, Any], roots: set[str]) -> set[str]:
    closure = set(roots)
    pending = list(roots)
    while pending:
        dependency = pending.pop()
        for target in graph["reverse"].get(dependency, set()):
            if target not in closure:
                closure.add(target)
                pending.append(target)
    return closure


def references_in_closure(
        graph: dict[str, Any], closure: set[str]) -> tuple[dict[str, int], list[str]]:
    samplers: collections.Counter[str] = collections.Counter()
    buffers: set[str] = set()
    for target in closure:
        for row in graph["definitions"].get(target, []):
            expression = row["expression"]
            samplers.update(SAMPLER_RE.findall(expression))
            for buffer_name, index, components in BUFFER_RE.findall(expression):
                if TEMP_RE.search(index):
                    index = "dynamic"
                suffix = f".{components}" if components else ""
                buffers.add(f"{buffer_name}[{index}]{suffix}")
    return dict(sorted(samplers.items())), sorted(buffers)


def stage_report(
        path: pathlib.Path,
        expected_sha256: str,
        role_by_sampler: dict[str, str]) -> dict[str, Any]:
    if sha256(path) != expected_sha256:
        raise ValueError(f"Selected program hash changed: {path}")
    source = path.read_text(encoding="utf-8-sig")
    graph = build_graph(source)
    roots = set().union(*graph["outputs"].values())
    output_closure = backward_closure(graph, roots)
    samplers, buffers = references_in_closure(graph, output_closure)
    resource_rows = []
    for sampler in sorted(graph["sampler_seeds"]):
        seeds = graph["sampler_seeds"][sampler]
        influenced = forward_closure(graph, seeds) & output_closure
        input_closure = backward_closure(graph, seeds)
        input_samplers, input_buffers = references_in_closure(
            graph, input_closure)
        resource_rows.append({
            "sampler": sampler,
            "role": role_by_sampler.get(sampler, "anonymous_scene_resource"),
            "sample_assignments": len(seeds),
            "output_reachable": bool(influenced),
            "output_reachable_temporaries": len(influenced),
            "input_sampler_references": input_samplers,
            "input_material_buffer_fields": [
                field for field in input_buffers
                if field.startswith(("fp_c7[", "fp_c8[", "vp_c7[", "vp_c8["))
            ],
        })
    return {
        "identity": path.name,
        "sha256": expected_sha256,
        "temporary_definitions": len(graph["definitions"]),
        "output_dependency_temporaries": len(output_closure),
        "output_roots": {
            target: sorted(values)
            for target, values in sorted(graph["outputs"].items())
        },
        "output_sampler_references": samplers,
        "output_buffer_references": buffers,
        "resources": resource_rows,
    }


def selected_za_stems(game_root: pathlib.Path) -> list[str]:
    catalog = read_json(game_root / "config" / "assets" / "asset_catalog.json")
    rows = [
        row for row in catalog.get("native_import_sets", [])
        if row.get("recipe") == "tools/assets/gamefreak_pokemon_imports_za.json"
    ]
    if len(rows) != 1 or rows[0].get("selection") != "include_stems":
        raise ValueError("Canonical Z-A catalog selection changed")
    return [str(value) for value in rows[0].get("stems", [])]


class PhmatReader:
    def __init__(self, payload: bytes) -> None:
        self.payload = payload
        self.offset = 0

    def skip(self, count: int) -> None:
        if count < 0 or self.offset + count > len(self.payload):
            raise ValueError("PHMAT DATA chunk is truncated")
        self.offset += count

    def u32(self) -> int:
        if self.offset + 4 > len(self.payload):
            raise ValueError("PHMAT DATA chunk is truncated")
        value = struct.unpack_from("<I", self.payload, self.offset)[0]
        self.offset += 4
        return value

    def string(self) -> str:
        count = self.u32()
        if self.offset + count > len(self.payload):
            raise ValueError("PHMAT string is truncated")
        value = self.payload[self.offset:self.offset + count].decode("utf-8")
        self.offset += count
        return value


def phrc_data_chunk(path: pathlib.Path) -> bytes:
    payload = path.read_bytes()
    if len(payload) < 96 or payload[:4] != b"PHMT":
        raise ValueError(f"Invalid PHMT container: {path}")
    chunk_count = struct.unpack_from("<I", payload, 20)[0]
    chunk_table_offset = struct.unpack_from("<Q", payload, 56)[0]
    for index in range(chunk_count):
        record_offset = chunk_table_offset + index * 48
        if record_offset + 48 > len(payload):
            raise ValueError(f"Truncated PHMT chunk table: {path}")
        if payload[record_offset:record_offset + 4] != b"DATA":
            continue
        data_offset = struct.unpack_from("<Q", payload, record_offset + 16)[0]
        data_size = struct.unpack_from("<Q", payload, record_offset + 24)[0]
        if data_offset + data_size > len(payload):
            raise ValueError(f"Invalid PHMT DATA range: {path}")
        return payload[data_offset:data_offset + data_size]
    raise ValueError(f"PHMT has no DATA chunk: {path}")


def phmat_mode_emissive_and_native_parameters(
        path: pathlib.Path) -> tuple[
            list[int], list[dict[str, Any]], list[list[tuple[float, ...]]]]:
    reader = PhmatReader(phrc_data_chunk(path))

    def skip_vector(stride: int) -> int:
        count = reader.u32()
        reader.skip(count * stride)
        return count

    def texture_set() -> list[dict[str, Any]]:
        result = []
        for _ in range(reader.u32()):
            values = struct.unpack_from(
                "<6i", reader.payload, reader.offset)
            reader.skip(24)
            result.append({
                "width": values[0],
                "height": values[1],
                "path": reader.string(),
            })
        return result

    material_count = skip_vector(16)
    for _ in range(4):
        references = texture_set()
        if len(references) != material_count:
            raise ValueError(f"PHMAT texture-vector count changed: {path}")
    emissive = texture_set()
    if len(emissive) != material_count:
        raise ValueError(f"PHMAT emissive-vector count changed: {path}")
    skip_vector(1)
    for _ in range(5):
        skip_vector(4)
    skip_vector(12)
    mode_count = reader.u32()
    if mode_count != material_count:
        raise ValueError(f"PHMAT material-mode count changed: {path}")
    if reader.offset + mode_count > len(reader.payload):
        raise ValueError(f"PHMAT material-mode vector is truncated: {path}")
    modes = list(reader.payload[reader.offset:reader.offset + mode_count])
    reader.skip(mode_count)
    if skip_vector(4) != material_count:
        raise ValueError(f"PHMAT material-flag count changed: {path}")
    parameter_vectors: list[list[tuple[float, ...]]] = []
    for parameter_index in range(4):
        count = reader.u32()
        if count != material_count:
            raise ValueError(
                f"PHMAT params{parameter_index} count changed: {path}")
        values = []
        for _ in range(count):
            if reader.offset + 16 > len(reader.payload):
                raise ValueError(
                    f"PHMAT params{parameter_index} vector is truncated: "
                    f"{path}")
            values.append(struct.unpack_from(
                "<4f", reader.payload, reader.offset))
            reader.skip(16)
        parameter_vectors.append(values)
    return modes, emissive, parameter_vectors


def ktx2_blue_channel_stats(path: pathlib.Path) -> dict[str, Any]:
    payload = path.read_bytes()
    if payload[:12] != bytes.fromhex("AB4B5458203230BB0D0A1A0A"):
        raise ValueError(f"Invalid KTX2 dependency: {path}")
    vk_format, type_size, width, height = struct.unpack_from(
        "<4I", payload, 12)
    level_count, supercompression = struct.unpack_from("<2I", payload, 40)
    if (vk_format != 43 or type_size != 1 or width <= 0 or height <= 0 or
            level_count < 1 or supercompression != 0):
        raise ValueError(f"Unexpected packed-control KTX2 layout: {path}")
    level_offset, level_size, uncompressed_size = struct.unpack_from(
        "<3Q", payload, 80)
    if (level_size != width * height * 4 or
            uncompressed_size != level_size or
            level_offset + level_size > len(payload)):
        raise ValueError(f"Unexpected KTX2 base-level range: {path}")
    blue = payload[level_offset + 2:level_offset + level_size:4]
    alpha = payload[level_offset + 3:level_offset + level_size:4]
    counts = collections.Counter(blue)
    alpha_counts = collections.Counter(alpha)
    return {
        "vk_format": "VK_FORMAT_R8G8B8A8_SRGB",
        "width": width,
        "height": height,
        "minimum_blue": min(counts),
        "maximum_blue": max(counts),
        "nonzero_blue_pixels": sum(
            count for value, count in counts.items() if value != 0),
        "half_linear_srgb_byte_pixels": counts[188],
        "minimum_alpha": min(alpha_counts),
        "maximum_alpha": max(alpha_counts),
        "sha256": sha256(path),
    }


def cooked_body_emission_verification(
        game_root: pathlib.Path) -> dict[str, Any]:
    cooked_root = game_root / "content" / "phlosion" / "objects"
    dependency_root = game_root / "content" / "phlosion"
    phmat_hashes: dict[str, str] = {}
    emission_records = []
    mode32_records = 0
    mode32_native_parameter_records = 0
    neutral_mode32_records = 0
    neutral_hair_auxiliary_records = 0
    texture_stats_cache: dict[pathlib.Path, dict[str, Any]] = {}
    for stem in selected_za_stems(game_root):
        manifest = read_json(
            game_root / "assets" / "models" / f"{stem}.phmodel")
        candidates = sorted(cooked_root.glob(f"{stem}-*/model.phmat"))
        if len(candidates) != 1:
            raise ValueError(
                f"Expected one cooked PHMAT for {stem}, found {len(candidates)}")
        phmat_path = candidates[0]
        phmat_hashes[stem] = sha256(phmat_path)
        (modes,
         emissive_references,
         native_parameters) = phmat_mode_emissive_and_native_parameters(
             phmat_path)
        submeshes = manifest.get("model", {}).get("submeshes", [])
        materials = manifest.get("materials", [])
        if len(modes) != len(submeshes):
            raise ValueError(f"Cooked/source submesh count changed: {stem}")
        for index, (submesh, mode) in enumerate(zip(submeshes, modes)):
            if mode != 32:
                continue
            mode32_records += 1
            material = materials[int(submesh["material"])]
            values = material.get("float_parameters", {})
            expected_native_parameters = (
                (
                    max(0.0, float(values["ReflectionsBlur"])),
                    max(0.0, float(values["DiffusionLevels"])),
                    0.0,
                    min(max(float(values["ShadowingGIGain"]), 0.0), 1.0),
                ),
                (
                    float(values["ShadowingBias"]),
                    float(values["ShadowingShift"]),
                    float(values["ShadowingContrast"]),
                    float(values["HueShiftBias"]),
                ),
                (
                    float(values["MidAreaShift"]),
                    float(values["MidAreaContrast"]),
                    float(values["MidAreaHueOffset"]) / 360.0,
                    float(values["DarkAreaShift"]),
                ),
                (
                    float(values["DarkAreaContrast"]),
                    float(values["DarkAreaHueOffset"]) / 360.0,
                    0.0,
                    float(values["HueShiftAreaValue"]),
                ),
            )
            actual_native_parameters = tuple(
                parameter_set[index] for parameter_set in native_parameters)
            if any(
                    abs(actual - expected) > 1e-5
                    for actual_vector, expected_vector in zip(
                        actual_native_parameters, expected_native_parameters)
                    for actual, expected in zip(
                        actual_vector, expected_vector)):
                raise ValueError(
                    f"Cooked Z-A native parameter transport changed: "
                    f"{stem}/{material.get('name')}: "
                    f"actual={actual_native_parameters}, "
                    f"expected={expected_native_parameters}")
            mode32_native_parameter_records += 1
            intensities = [float(values.get("EmissionIntensity", 0.0))]
            intensities.extend(float(values.get(
                f"EmissionIntensityLayer{layer}", 0.0))
                for layer in range(1, 5))
            authored_emission = max(intensities) > 0.0
            reference = emissive_references[index]
            dependency_path = dependency_root / reference["path"]
            if dependency_path not in texture_stats_cache:
                texture_stats_cache[dependency_path] = (
                    ktx2_blue_channel_stats(dependency_path))
            stats = texture_stats_cache[dependency_path]
            if (stats["width"] != reference["width"] or
                    stats["height"] != reference["height"]):
                raise ValueError(
                    f"PHMAT/KTX2 control dimensions changed: {stem}/{index}")
            if stats["minimum_alpha"] != 255 or stats["maximum_alpha"] != 255:
                raise ValueError(
                    f"Selected mode-32 record retained a fabricated hair "
                    f"auxiliary: {stem}/{material.get('name')}")
            neutral_hair_auxiliary_records += 1
            if not authored_emission:
                if stats["maximum_blue"] != 0:
                    raise ValueError(
                        f"Neutral mode-32 emission lane is nonzero: "
                        f"{stem}/{material.get('name')}")
                neutral_mode32_records += 1
                continue
            color = material.get("vec4_parameters", {}).get(
                "EmissionColorLayer3")
            if (stem not in {"0120_Staryu_ZA", "0120_Staryu_ZA_Shiny"} or
                    material.get("name") != "body_00" or
                    intensities != [0.0, 0.0, 0.0, 0.5, 0.0] or
                    color is None or
                    [float(value) for value in color[:3]] != [1.0, 1.0, 1.0] or
                    stats["maximum_blue"] != 188 or
                    stats["half_linear_srgb_byte_pixels"] <= 0 or
                    stats["nonzero_blue_pixels"] <= 0):
                raise ValueError(
                    f"Selected Z-A body-emission contract changed: "
                    f"{stem}/{material.get('name')}")
            emission_records.append({
                "stem": stem,
                "submesh_index": index,
                "material": material.get("name"),
                "material_mode": mode,
                "source_emission_intensity_layer3": 0.5,
                "source_emission_color_layer3": [1.0, 1.0, 1.0],
                "packed_emissive_reference": reference["path"],
                "packed_blue_channel": stats,
            })
    if (len(phmat_hashes) != 52 or mode32_records != 184 or
            mode32_native_parameter_records != 184 or
            neutral_hair_auxiliary_records != 184 or
            neutral_mode32_records != 182 or len(emission_records) != 2):
        raise ValueError(
            "Cooked selected-body emission census changed: "
            f"files={len(phmat_hashes)}, mode32={mode32_records}, "
            f"neutral={neutral_mode32_records}, emission={len(emission_records)}")
    digest_source = "\n".join(
        f"{stem}:{value}" for stem, value in sorted(phmat_hashes.items()))
    return {
        "cooked_phmat_files_verified": len(phmat_hashes),
        "mode32_submesh_records_verified": mode32_records,
        "mode32_native_parameter_records_verified":
            mode32_native_parameter_records,
        "neutral_mode32_emission_lanes_verified": neutral_mode32_records,
        "neutral_hair_auxiliary_records_verified":
            neutral_hair_auxiliary_records,
        "source_authored_emission_records_verified": len(emission_records),
        "cooked_phmat_set_sha256": hashlib.sha256(
            digest_source.encode("utf-8")).hexdigest(),
        "emission_records": emission_records,
    }


def material_census(game_root: pathlib.Path) -> dict[str, Any]:
    counts: collections.Counter[str] = collections.Counter()
    materials = 0
    body_materials = 0
    body_parameters: dict[str, collections.Counter[str]] = {
        name: collections.Counter()
        for name in (
            "HalfLambertBias", "ShadowStrength", "ShadowingGIGain",
            "RimLightOffset", "RimLightContrast", "RimLightIntensity",
            "BackRimLightIntensity", "ReflectionsBlur", "DiffusionLevels",
            "ShadowingBias", "ShadowingShift", "ShadowingContrast",
            "HueShiftBias", "MidAreaShift", "MidAreaContrast",
            "MidAreaHueOffset", "DarkAreaShift", "DarkAreaContrast",
            "DarkAreaHueOffset", "HueShiftAreaValue", "SpecularIntensity",
            "SpecularOffset", "SpecularContrast", "Metallic",
            "MetallicLayer1", "MetallicLayer2", "MetallicLayer3",
            "MetallicLayer4",
            "OcclusionStrength", "EmissionIntensity",
            "EmissionIntensityLayer1", "EmissionIntensityLayer2",
            "EmissionIntensityLayer3", "EmissionIntensityLayer4",
        )
    }

    def stable_number(value: Any) -> str:
        number = float(value)
        return f"{number:.9g}"

    for stem in selected_za_stems(game_root):
        model = read_json(game_root / "assets" / "models" / f"{stem}.phmodel")
        for material in model.get("materials", []):
            if material.get("shader_family") != "IkCharacter":
                continue
            materials += 1
            choice = str(material.get("shader_options", {}).get(
                "EnableHairSpecular", "<missing>"))
            counts[choice] += 1
            options = material.get("shader_options", {})
            if (options.get("EnableEyeOptions") == "True" or
                    options.get("EnableDisplacementMap") == "True"):
                continue
            body_materials += 1
            values = material.get("float_parameters", {})
            for name, census in body_parameters.items():
                if name not in values:
                    raise ValueError(
                        f"Selected body material lost {name}: {stem}/"
                        f"{material.get('name', '<unnamed>')}")
                census[stable_number(values[name])] += 1
    if materials != 222 or counts != {"False": 222}:
        raise ValueError(
            f"Selected IkCharacter HairSpecular census changed: {dict(counts)}")
    if body_materials != 140:
        raise ValueError(
            f"Selected ordinary-body material census changed: {body_materials}")
    return {
        "materials": materials,
        "choices": dict(sorted(counts.items())),
        "ordinary_body_materials": body_materials,
        "ordinary_body_parameter_distributions": {
            name: dict(sorted(values.items(), key=lambda row: float(row[0])))
            for name, values in body_parameters.items()
        },
    }


def body_constant_buffer_data_flow(source: str) -> dict[str, Any]:
    """Pin literal body-program motifs before assigning material semantics."""
    graph = build_graph(source)
    signatures = [
        "temp_158 = temp_82 * fp_c7.data[4].z;",
        "temp_161 = temp_83 * fp_c7.data[4].z;",
        "temp_199 = temp_90 * fp_c7.data[10].y;",
        "temp_214 = temp_91 * fp_c7.data[10].z;",
        "temp_168 = temp_92 * fp_c7.data[10].w;",
        "temp_209 = temp_93 * fp_c7.data[11].x;",
        "temp_194 = fp_c7.data[8].y * fp_c8.data[19].x;",
        "temp_202 = fma(fp_c7.data[8].z, fp_c8.data[20].x, temp_201);",
        "temp_218 = fma(fp_c7.data[8].w, fp_c8.data[21].x, temp_217);",
        "temp_252 = fma(fp_c7.data[9].x, fp_c8.data[22].x, temp_251);",
        "temp_286 = fma(fp_c7.data[9].y, fp_c8.data[23].x, temp_285);",
        "temp_192 = temp_88 * fp_c7.data[99].y;",
        "temp_195 = fma(temp_163, temp_192, fp_c8.data[127].y);",
        "temp_196 = fma(temp_165, temp_192, fp_c8.data[127].z);",
        "temp_200 = fma(temp_167, temp_192, fp_c8.data[127].x);",
        "temp_235 = 0.0 - fp_c7.data[1].w;",
        "temp_236 = temp_235 + fp_c7.data[2].x;",
        "temp_443 = fma(temp_209, temp_433, temp_347);",
        "temp_241 = 0.0 - fp_c7.data[92].w;",
        "temp_242 = fp_c7.data[93].x + temp_241;",
        "temp_310 = fma(temp_209, temp_303, temp_284);",
        "temp_239 = 0.0 - fp_c7.data[94].y;",
        "temp_240 = fp_c7.data[94].z + temp_239;",
        "temp_456 = fma(temp_209, temp_445, temp_281);",
        "temp_237 = 0.0 - fp_c7.data[95].w;",
        "temp_238 = fp_c7.data[96].x + temp_237;",
        "temp_328 = fma(temp_209, temp_314, temp_280);",
        "temp_1605 = 0.0 - temp_310;",
        "temp_1606 = temp_1605 + temp_1550;",
        "temp_1607 = clamp(temp_1606, 0.0, 1.0);",
        "temp_1635 = fma(temp_1607, -2.0, 3.0);",
        "temp_1636 = temp_1607 * temp_1607;",
        "temp_1644 = temp_1635 * temp_1636;",
        "temp_1652 = temp_328 + temp_328;",
        "temp_1660 = fma(temp_1644, temp_1652, temp_1644);",
        "temp_1669 = 0.0 - temp_328;",
        "temp_1670 = temp_1669 + temp_1660;",
        "temp_1671 = clamp(temp_1670, 0.0, 1.0);",
        "temp_1684 = temp_456 * temp_1671;",
        "temp_1139 = texture(fp_t_tcb_18, vec2(temp_35, temp_37), fp_c3.data[0x11B].x).x;",
        "temp_1318 = textureLod(fp_t_tcb_1C, vec3(temp_1317, temp_1315, temp_1316), fp_c7.data[101].w).xyz;",
        "temp_1169 = temp_443 > 0.0;",
        "temp_1372 = max(temp_1369, fp_c7.data[102].x);",
        "temp_1373 = max(temp_1370, fp_c7.data[102].x);",
        "temp_1374 = max(temp_1371, fp_c7.data[102].x);",
        "temp_1665 = temp_1197 * fp_c7.data[101].x;",
        "temp_1666 = temp_1653 * fp_c7.data[101].y;",
        "temp_1705 = temp_1139 * temp_1696;",
        "temp_1719 = fma(temp_1697, temp_1139, temp_1718);",
        "temp_1198 = fp_c7.data[99].x * fp_c7.data[99].x;",
        "temp_1205 = fma(temp_1198, -0.5, 0.5);",
        "temp_1206 = fma(temp_1198, 0.5, 0.5);",
        "temp_1227 = fma(temp_1217, fp_c7.data[100].x, temp_1207);",
        "temp_1235 = temp_1205 * fp_c7.data[99].w;",
        "temp_1246 = fma(temp_1206, fp_c7.data[99].w, temp_1245);",
        "temp_1431 = 0.0 - fp_c7.data[100].z;",
        "temp_1434 = 1.0 / temp_1432;",
        "temp_1514 = fma(temp_1434, temp_1505, temp_1434);",
        "temp_1529 = temp_1515 * temp_1515;",
        "temp_1530 = fma(temp_1515, -2.0, 3.0);",
        "temp_1538 = temp_1530 * temp_1529;",
        "temp_1547 = fp_c7.data[100].w + fp_c7.data[100].w;",
        "temp_1561 = fma(temp_1538, temp_1547, temp_1538);",
        "temp_1622 = 0.0 - fp_c7.data[100].w;",
        "temp_1624 = clamp(temp_1623, 0.0, 1.0);",
        "temp_1717 = temp_1695 * fp_c7.data[99].z;",
        "temp_1774 = temp_1761 * fp_c7.data[100].y;",
        "temp_1446 = fma(fp_c7.data[103].z, fp_c1.data[9].y, temp_1436);",
        "temp_1447 = fma(fp_c7.data[102].w, fp_c1.data[9].y, temp_1438);",
        "temp_1510 = 0.0 - fp_c7.data[102].y;",
        "temp_1512 = 0.0 - fp_c7.data[103].x;",
        "temp_1557 = fp_c7.data[102].z + fp_c7.data[102].z;",
        "temp_1498 = fp_c7.data[103].y + fp_c7.data[103].y;",
        "temp_1641 = temp_1604 * fp_c7.data[103].w;",
        "temp_1638 = fma(temp_1604, temp_1631, temp_1163);",
        "temp_1639 = fma(temp_1604, temp_1627, temp_1165);",
        "temp_1640 = fma(temp_1604, temp_1634, temp_1166);",
        "temp_1655 = fma(temp_1620, temp_1654, temp_1628);",
        "temp_1656 = fma(temp_1620, temp_1646, temp_1638);",
        "temp_1664 = temp_1655 * temp_1651;",
        "temp_1677 = fma(temp_1651, temp_1656, temp_1676);",
        "temp_1686 = fma(temp_1677, temp_1663, temp_1664);",
        "temp_1236 = 0.0 - fp_c7.data[104].x;",
        "temp_1433 = fp_c7.data[104].y + fp_c7.data[104].y;",
        "temp_1583 = 0.0 - temp_1197;",
        "temp_1584 = 0.0 - temp_1418;",
        "temp_1594 = temp_1585 + 0.400000006;",
        "temp_1608 = temp_1594 * 2.5;",
        "temp_1629 = fma(temp_1609, -2.0, 3.0);",
        "temp_1632 = temp_1609 * temp_1609;",
        "temp_1642 = temp_1629 * temp_1632;",
        "temp_1653 = temp_1624 * temp_1643;",
        "temp_1666 = temp_1653 * fp_c7.data[101].y;",
        "temp_1684 = temp_456 * temp_1671;",
        "temp_1774 = temp_1761 * fp_c7.data[100].y;",
        "temp_1780 = fma(temp_1774, 2.0, temp_1761);",
    ]
    require_source_fragments(source, signatures, "IkCharacter variation 514")

    hue_dependencies = {}
    for name, roots, expected in (
            ("middle", {"temp_1598", "temp_1621", "temp_1625"},
             "fp_c7[102].w"),
            ("dark", {"temp_1612", "temp_1617", "temp_1628"},
             "fp_c7[103].z")):
        closure = backward_closure(graph, roots)
        _, buffer_references = references_in_closure(graph, closure)
        material_hue_references = [
            value for value in buffer_references
            if value in {"fp_c7[102].w", "fp_c7[103].z"}
        ]
        if material_hue_references != [expected]:
            raise ValueError(
                f"IkCharacter {name} hue target changed: "
                f"{material_hue_references}")
        hue_dependencies[name] = {
            "output_temporaries": sorted(roots),
            "exclusive_authored_hue_dependency": expected,
            "proof": "compiled_backward_dependency_closure",
        }
    return {
        "normal_height": {
            "register": "fp_c7[4].z",
            "operation": "multiplies both unpacked NormalMap XY channels",
            "proof": "compiled_operation_identity",
        },
        "layer_mask_scales": {
            "LayerMaskScale1": "fp_c7[10].y",
            "LayerMaskScale2": "fp_c7[10].z",
            "LayerMaskScale3": "fp_c7[10].w",
            "LayerMaskScale4": "fp_c7[11].x",
            "operation": "multiply sampled LayerMaskMap RGBA before ordered layers",
            "proof": "compiled_operation_identity",
        },
        "emission_intensities": {
            "EmissionIntensity": "fp_c7[8].y",
            "EmissionIntensityLayer1": "fp_c7[8].z",
            "EmissionIntensityLayer2": "fp_c7[8].w",
            "EmissionIntensityLayer3": "fp_c7[9].x",
            "EmissionIntensityLayer4": "fp_c7[9].y",
            "operation": "scale five emission-color vectors before layer mixing",
            "proof": "compiled_operation_identity",
        },
        "occlusion_shadow_color": {
            "OcclusionStrength": "fp_c7[99].y",
            "ShadowingColor": "fp_c8[127].xyz",
            "ShadowingColorLayer1": "fp_c8[128].xyz",
            "ShadowingColorLayer2": "fp_c8[129].xyz",
            "ShadowingColorLayer3": "fp_c8[130].xyz",
            "ShadowingColorLayer4": "fp_c8[131].xyz",
            "operation": (
                "OcclusionMap.r * OcclusionStrength is the interpolation "
                "weight from ShadowingColor to ShadowingColorMap before "
                "the ordered layer-shadow colors"),
            "proof": "compiled_operation_identity",
        },
        "half_lambert_shadow_band": {
            "HalfLambertBias": "fp_c7[99].x",
            "ShadowStrength": "fp_c7[99].w",
            "operation": (
                "square HalfLambertBias, form symmetric 0.5 +/- 0.5*bias^2 "
                "band endpoints, then scale both endpoints by ShadowStrength"),
            "proof": "compiled_operation_identity",
        },
        "shadowing_bias_response": {
            "ShadowingBias": "fp_c7[100].x",
            "operation": "clamp(x + ShadowingBias * (x^2 - x), 0, 1)",
            "proof": "compiled_operation_identity",
        },
        "ambient_diffusion": {
            "ShadowingGIGain": "fp_c7[99].z",
            "DiffusionLevels": "fp_c7[100].y",
            "operation": (
                "ShadowingGIGain scales the three-channel GI difference; "
                "DiffusionLevels scales the final three-channel diffuse term"),
            "proof": "compiled_operation_identity",
            "local_operation": (
                "each source diffuse channel is multiplied by "
                "1 + 2 * DiffusionLevels before the anonymous scene-light "
                "mix"),
            "boundary": (
                "The local diffusion scale is exact; the source scene-light "
                "mix value is unavailable in the loose model archive."),
        },
        "layered_metallic": {
            "Metallic": "fp_c7[1].w",
            "MetallicLayer1": "fp_c7[2].x",
            "MetallicLayer2": "fp_c7[2].y",
            "MetallicLayer3": "fp_c7[2].z",
            "MetallicLayer4": "fp_c7[2].w",
            "operation": "ordered LayerMaskMap RGBA interpolation",
            "proof": "compiled_operation_identity_plus_metal_branch_use",
        },
        "layered_specular": {
            "offset": {
                "SpecularOffset": "fp_c7[92].w",
                "SpecularLayer1Offset": "fp_c7[93].x",
                "SpecularLayer2Offset": "fp_c7[93].y",
                "SpecularLayer3Offset": "fp_c7[93].z",
                "SpecularLayer4Offset": "fp_c7[93].w",
            },
            "intensity": {
                "SpecularIntensity": "fp_c7[94].y",
                "SpecularLayer1Intensity": "fp_c7[94].z",
                "SpecularLayer2Intensity": "fp_c7[94].w",
                "SpecularLayer3Intensity": "fp_c7[95].x",
                "SpecularLayer4Intensity": "fp_c7[95].y",
            },
            "contrast": {
                "SpecularContrast": "fp_c7[95].w",
                "SpecularLayer1Contrast": "fp_c7[96].x",
                "SpecularLayer2Contrast": "fp_c7[96].y",
                "SpecularLayer3Contrast": "fp_c7[96].z",
                "SpecularLayer4Contrast": "fp_c7[96].w",
            },
            "operation": (
                "subtract ordered offset, smoothstep the clamped domain, "
                "apply clamp(x * (1 + 2 * contrast) - contrast), then "
                "multiply ordered intensity"),
            "proof": "compiled_operation_identity",
        },
        "local_reflection": {
            "ReflectionsBlur": "fp_c7[101].w",
            "HueShiftBias": "fp_c7[102].x",
            "operation": (
                "execute only when the ordered layer-resolved Metallic value "
                "is greater than zero; sample LocalReflectionMap with the "
                "literal ReflectionsBlur LOD and floor the locally shaped "
                "probe channels by HueShiftBias"),
            "proof": (
                "compiled_metallic_branch_plus_lod_plus_floor_identity"),
            "boundary": (
                "The metal gate, LOD, and HueShiftBias floor are exact. "
                "Anonymous scene constants in the reflection vector and its "
                "later lighting composite remain unresolved."),
        },
        "rim_mask": {
            "sampled_channel": "RimLightMaskMap.r",
            "intensity_register": "fp_c7[101].x",
            "back_intensity_register": "fp_c7[101].y",
            "operation": "paired scalar path reaches the rim-mask composite",
            "proof": (
                "compiled_output_slice_plus_adjacent_authored_rim_scalar_pair"),
            "boundary": (
                "The material scalar pair and mask path are mapped; anonymous "
                "scene terms and the final exposure/composite scale are not."),
        },
        "rim_shape": {
            "RimLightOffset": "fp_c7[100].z",
            "RimLightContrast": "fp_c7[100].w",
            "operation": (
                "domain = clamp((1 - NdotV - offset) / (1 - offset)); "
                "smooth = domain^2 * (3 - 2*domain); "
                "shape = clamp(smooth * (1 + 2*contrast) - contrast)"),
            "proof": "compiled_operation_identity",
            "boundary": (
                "The local view-domain shape is exact. Its later scene-light, "
                "back-rim, mask, and exposure composite remains anonymous."),
        },
        "back_rim_gate": {
            "operation": (
                "back_domain = clamp((0.4 - NdotL - NdotV) * 2.5); "
                "back_gate = smoothstep(back_domain); "
                "back rim reuses the contrast-remapped front rim shape and "
                "multiplies BackRimLightIntensity"),
            "proof": "compiled_operation_identity",
            "boundary": (
                "The local view/light gate is exact; later scene-light, mask, "
                "exposure, and presentation terms remain anonymous."),
        },
        "color_process_layout": {
            "HueShiftBias": "fp_c7[102].x",
            "MidAreaShift": "fp_c7[102].y",
            "MidAreaContrast": "fp_c7[102].z",
            "MidAreaHueOffset": "fp_c7[102].w",
            "DarkAreaShift": "fp_c7[103].x",
            "DarkAreaContrast": "fp_c7[103].y",
            "DarkAreaHueOffset": "fp_c7[103].z",
            "HueShiftAreaValue": "fp_c7[103].w",
            "ShadowingShift": "fp_c7[104].x",
            "ShadowingContrast": "fp_c7[104].y",
            "operation": (
                "middle and dark domains each apply clamp, cubic smoothstep, "
                "and clamp(x * (1 + 2 * contrast) - contrast). Build "
                "B=mix(base,middleHue,middleArea), A=mix(darkHue,B,darkArea), "
                "C=mix(B,darkHue,darkArea), then output "
                "mix(A,C,shadowProcessArea) * "
                "(1 - 0.5*middleArea*HueShiftAreaValue)"),
            "hue_target_dependencies": hue_dependencies,
            "proof": (
                "compiled_register_group_plus_backward_dependency_closure_"
                "plus_operation_identity"),
            "boundary": (
                "The ordered local composite is exact. Its middle/dark domain "
                "input light scalar and ReceiveShadow scene state are absent "
                "from the loose material archive."),
        },
        "direct_specular_boundary": {
            "operation": (
                "the direct specular response multiplies the ordered "
                "layer-resolved SpecularIntensity path; Metallic separately "
                "gates the local-reflection branch"),
            "proof": "compiled_operation_and_branch_identity",
            "boundary": (
                "Material-path participation is exact; anonymous scene-light "
                "constants and the complete final BRDF order are unresolved."),
        },
    }


def final_scene_fade_boundary(source: str, label: str) -> dict[str, Any]:
    """Prove the final RGB operation without assigning scene-buffer semantics."""
    delta_pattern = re.compile(
        r"temp_(\d+) = 0\.0 - temp_(\d+);\s*"
        r"temp_(\d+) = temp_\1 \+ fp_c10\.data\[12\]\.([xyz]);")
    rows: dict[str, dict[str, str]] = {}
    for negative, source_color, delta, channel in delta_pattern.findall(source):
        final_pattern = re.compile(
            rf"temp_(\d+) = fma\(temp_{delta}, "
            rf"fp_c10\.data\[12\]\.w, temp_{source_color}\);")
        final = final_pattern.search(source)
        if final is None:
            continue
        result = final.group(1)
        if f"out_attr0.{channel} = temp_{result};" not in source:
            continue
        rows[channel] = {
            "source_composite_temporary": f"temp_{source_color}",
            "scene_color_field": f"fp_c10[12].{channel}",
            "scene_fade_field": "fp_c10[12].w",
            "output_temporary": f"temp_{result}",
        }
    if set(rows) != {"x", "y", "z"}:
        raise ValueError(f"{label} final scene-fade boundary changed: {rows}")
    return {
        "operation": (
            "mix(source_material_composite, fp_c10[12].rgb, "
            "fp_c10[12].w)"),
        "proof": "compiled_final_output_operation_identity",
        "channels": rows,
        "boundary": (
            "The final material-to-scene fade is exact; the runtime meaning "
            "and bound values of fp_c10[12] remain unavailable."),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-root", type=pathlib.Path, required=True)
    parser.add_argument("--shader-study", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    game_root = args.game_root.resolve()
    study_root = args.shader_study.resolve()
    manifest_path = study_root / "selected-programs" / (
        "selected_programs_manifest.json")
    manifest = read_json(manifest_path)
    if (manifest.get("schema") != MANIFEST_SCHEMA or
            manifest.get("source_profile") != SOURCE_PROFILE):
        raise ValueError("Unsupported selected Z-A program manifest")
    records = {
        int(row["variation_index"]): row
        for row in manifest.get("programs", [])
        if row.get("shader_family") == "IkCharacter"
    }
    if {index: int(row["material_count"]) for index, row in records.items()} != (
            SELECTED_VARIATIONS):
        raise ValueError("Selected IkCharacter variation census changed")

    archive_path = study_root / "ik_character.bnsh"
    if not archive_path.is_file():
        raise FileNotFoundError(archive_path)
    programs = []
    for variation, record in sorted(records.items()):
        directory = study_root / "selected-programs" / "ik_character" / (
            f"v{variation:04d}")
        fragment = stage_report(
            directory / f"v{variation:04d}.fsh.maxwell.glsl",
            str(record["fragment_glsl_sha256"]),
            FRAGMENT_ROLE_BY_SAMPLER,
        )
        vertex = stage_report(
            directory / f"v{variation:04d}.vsh.maxwell.glsl",
            str(record["vertex_glsl_sha256"]),
            VERTEX_ROLE_BY_SAMPLER,
        )
        programs.append({
            "variation_index": variation,
            "material_count": int(record["material_count"]),
            "reflection": bnsh_reflection_report(archive_path, variation),
            "fragment": fragment,
            "vertex": vertex,
            "final_scene_fade": final_scene_fade_boundary(
                (directory / f"v{variation:04d}.fsh.maxwell.glsl").read_text(
                    encoding="utf-8-sig"),
                f"IkCharacter variation {variation}"),
        })

    material_evidence = material_census(game_root)
    body = next(row for row in programs if row["variation_index"] == 514)
    displaced_body = next(
        row for row in programs if row["variation_index"] == 594)
    if body["fragment"]["sha256"] != displaced_body["fragment"]["sha256"]:
        raise ValueError(
            "IkCharacter ordinary/displaced body fragment identity changed")
    body_source_path = (
        study_root / "selected-programs" / "ik_character" / "v0514" /
        "v0514.fsh.maxwell.glsl")
    body_data_flow = body_constant_buffer_data_flow(
        body_source_path.read_text(encoding="utf-8-sig"))
    body_resources = {
        row["role"]: row for row in body["fragment"]["resources"]
    }
    for required in (
            "BaseColorMap", "NormalMap", "OcclusionMap", "SpecularMaskMap",
            "ShadowingColorMap", "ShadowingColorMaskMap", "LayerMaskMap",
            "RimLightMaskMap", "LocalReflectionMap", "DiffuseIrradianceCube"):
        if not body_resources.get(required, {}).get("output_reachable"):
            raise ValueError(f"Body output dependency lost source role: {required}")
    if "HairSpecularMap" in body_resources:
        raise ValueError("Selected body program unexpectedly samples HairSpecularMap")

    eye_without_shadow = next(
        row for row in programs if row["variation_index"] == 682)
    eye_with_shadow = next(
        row for row in programs if row["variation_index"] == 1214)
    eye_resources = {
        row["role"]: row for row in eye_without_shadow["fragment"]["resources"]
    }
    shadow_eye_resources = {
        row["role"]: row for row in eye_with_shadow["fragment"]["resources"]
    }
    expected_eye_inputs = {
        "ParallaxMap": {
            "fp_c7[5].y", "fp_c7[5].z",
            "fp_c8[1].x", "fp_c8[1].y", "fp_c8[1].z", "fp_c8[1].w",
            "fp_c8[139].x", "fp_c8[139].y",
        },
        "HighlightMaskMap": {
            "fp_c8[2].x", "fp_c8[2].y", "fp_c8[2].z", "fp_c8[2].w",
            "fp_c8[140].x", "fp_c8[140].y",
        },
        "EyelidShadowMaskMap": {
            "fp_c7[21].y",
            "fp_c8[3].x", "fp_c8[3].y", "fp_c8[3].z", "fp_c8[3].w",
        },
    }
    for role in ("ParallaxMap", "HighlightMaskMap"):
        actual = set(eye_resources[role]["input_material_buffer_fields"])
        if actual != expected_eye_inputs[role]:
            raise ValueError(
                f"Selected IkCharacter {role} input subgraph changed: {actual}")
    actual_shadow_inputs = set(
        shadow_eye_resources["EyelidShadowMaskMap"]
        ["input_material_buffer_fields"])
    if actual_shadow_inputs != expected_eye_inputs["EyelidShadowMaskMap"]:
        raise ValueError(
            "Selected IkCharacter eyelid-shadow input subgraph changed: "
            f"{actual_shadow_inputs}")

    option_graph_path = game_root / "docs" / "kanto" / "evidence" / (
        "za_kanto_option_graph.json")
    option_graph = read_json(option_graph_path)
    hair_edges = [
        row for row in option_graph.get("differentials", [])
        if row.get("shader_family") == "IkCharacter" and
        row.get("changed_option") == "EnableHairSpecular"
    ]
    if len(hair_edges) != 4 and len(hair_edges) != 3:
        raise ValueError("HairSpecular option differential coverage changed")
    # At least one exact archived one-option differential proves that the
    # optional branch owns tcb_1A while every selected material disables it.
    if not all(
            any(row.get("name") == "fp_t_tcb_1A" for row in
                edge.get("fragment", {}).get("removed_samplers", []) +
                edge.get("fragment", {}).get("added_samplers", []))
            for edge in hair_edges):
        raise ValueError("HairSpecular differentials lost their tcb_1A delta")

    loader_path = game_root / "tools" / "PhlosionNativeModelIr.cpp"
    loader_source = loader_path.read_text(encoding="utf-8-sig")
    for token in (
            "emissionLuminance", "EmissionIntensityLayer",
            "linearToSrgb(emissionLuminance)",
            "pre-composite rim scalars"):
        if token not in loader_source:
            raise ValueError(
                f"Z-A body-emission cooker contract lost token: {token}")
    for forbidden in (
            "kNativeRimCompositeScale",
            "nativeIkCharacterSurfaceProfile",
            "loadSupplementalScarletSurfaceDetail"):
        if forbidden in loader_source:
            raise ValueError(
                f"Z-A cooker restored an unsupported presentation bake: {forbidden}")
    cooked_emission = cooked_body_emission_verification(game_root)

    report = {
        "schema": SCHEMA,
        "source_profile": SOURCE_PROFILE,
        "method": {
            "runtime_execution": False,
            "emulator_used": False,
            "evidence_level": (
                "conservative_compiled_ssa_output_slice_plus_exact_selected_"
                "material_parameter_census_plus_literal_operation_signatures_"
                "plus_single_option_differential_plus_reflection_header_audit_"
                "plus_cooked_phmat_ktx2_channel_verification"),
            "claim_boundary": (
                "Output reachability, source resource participation, and the "
                "ordinary-body layer scales, emission intensities, normal "
                "height, local-reflection LOD, the AO/shadow-color blend, "
                "layered metallic/specular registers and shaping order, the "
                "exact half-Lambert band and ShadowingBias response, the "
                "front/back rim domains, the ordered middle/dark hue "
                "composite, the local DiffusionLevels scale, the metallic "
                "local-reflection gate, the rim-mask scalar path, and "
                "the absence of the optional hair-specular branch are proven. "
                "All four selected fragments share an exact final scene-fade "
                "boundary, and ordinary/displaced bodies share one identical "
                "fragment program. Raw authored rim values now remain in the "
                "asset while Phlosion's unresolved exposure calibration stays "
                "explicitly presentation-side. "
                "All 184 cooked mode-32 records were decoded: 182 carry a "
                "zero body-emission lane and regular/shiny Staryu alone carry "
                "their exact achromatic 0.5 layer-3 term. All 184 also retain "
                "the fourteen source-controlled scalar lanes in native "
                "params0-3 exactly; params0.z is the deliberately neutral "
                "runtime-only surface qualifier. "
                "The stripped reflection dictionaries, anonymous scene-buffer "
                "semantics, bound scene-light values entering the proven "
                "local blocks, final rim exposure, and complete scene-level "
                "BRDF/post-process composition remain unresolved."),
        },
        "summary": {
            "selected_programs": len(programs),
            "selected_materials": material_evidence["materials"],
            "ordinary_body_materials": material_evidence[
                "ordinary_body_materials"],
            "output_reachable_body_resources": sum(
                row["output_reachable"] for row in body["fragment"]["resources"]),
            "hair_specular_enabled_materials": 0,
            "hair_specular_single_option_differentials": len(hair_edges),
            "mapped_body_material_fields": 62,
            "mapped_eye_material_fields": 7,
            "selected_programs_with_stripped_reflection": sum(
                row["reflection"]["status"] == "absent_or_stripped"
                for row in programs),
            "selected_programs_with_exact_final_scene_fade": sum(
                "final_scene_fade" in row for row in programs),
            "ordinary_displaced_body_fragment_identity": "identical",
            "cooked_phmat_files_verified": cooked_emission[
                "cooked_phmat_files_verified"],
            "cooked_mode32_submesh_records_verified": cooked_emission[
                "mode32_submesh_records_verified"],
            "cooked_mode32_native_parameter_records_verified":
                cooked_emission[
                    "mode32_native_parameter_records_verified"],
            "cooked_body_emission_records_verified": cooked_emission[
                "source_authored_emission_records_verified"],
            "cooked_neutral_hair_auxiliary_records_verified": cooked_emission[
                "neutral_hair_auxiliary_records_verified"],
            "runtime_changes_authorized_by_this_report": 6,
        },
        "shared_material_buffer_mappings": {
            "UVScaleOffset": "fp_c8[1].xyzw",
            "NormalHeight": "fp_c7[4].z",
            "LayerMaskScale1": "fp_c7[10].y",
            "LayerMaskScale2": "fp_c7[10].z",
            "LayerMaskScale3": "fp_c7[10].w",
            "LayerMaskScale4": "fp_c7[11].x",
            "BaseColor": "fp_c8[9].xyzw",
            "BaseColorLayer1": "fp_c8[10].xyzw",
            "BaseColorLayer2": "fp_c8[11].xyzw",
            "BaseColorLayer3": "fp_c8[12].xyzw",
            "BaseColorLayer4": "fp_c8[13].xyzw",
            "EmissionIntensity": "fp_c7[8].y",
            "EmissionIntensityLayer1": "fp_c7[8].z",
            "EmissionIntensityLayer2": "fp_c7[8].w",
            "EmissionIntensityLayer3": "fp_c7[9].x",
            "EmissionIntensityLayer4": "fp_c7[9].y",
            "ReflectionsBlur": "fp_c7[101].w",
            "RimLightIntensity": "fp_c7[101].x",
            "BackRimLightIntensity": "fp_c7[101].y",
            "OcclusionStrength": "fp_c7[99].y",
            "HalfLambertBias": "fp_c7[99].x",
            "ShadowingGIGain": "fp_c7[99].z",
            "ShadowStrength": "fp_c7[99].w",
            "ShadowingBias": "fp_c7[100].x",
            "DiffusionLevels": "fp_c7[100].y",
            "RimLightOffset": "fp_c7[100].z",
            "RimLightContrast": "fp_c7[100].w",
            "HueShiftBias": "fp_c7[102].x",
            "MidAreaShift": "fp_c7[102].y",
            "MidAreaContrast": "fp_c7[102].z",
            "MidAreaHueOffset": "fp_c7[102].w",
            "DarkAreaShift": "fp_c7[103].x",
            "DarkAreaContrast": "fp_c7[103].y",
            "DarkAreaHueOffset": "fp_c7[103].z",
            "HueShiftAreaValue": "fp_c7[103].w",
            "ShadowingShift": "fp_c7[104].x",
            "ShadowingContrast": "fp_c7[104].y",
            "ShadowingColor": "fp_c8[127].xyz",
            "ShadowingColorLayer1": "fp_c8[128].xyz",
            "ShadowingColorLayer2": "fp_c8[129].xyz",
            "ShadowingColorLayer3": "fp_c8[130].xyz",
            "ShadowingColorLayer4": "fp_c8[131].xyz",
            "Metallic": "fp_c7[1].w",
            "MetallicLayer1": "fp_c7[2].x",
            "MetallicLayer2": "fp_c7[2].y",
            "MetallicLayer3": "fp_c7[2].z",
            "MetallicLayer4": "fp_c7[2].w",
            "SpecularOffset": "fp_c7[92].w",
            "SpecularLayer1Offset": "fp_c7[93].x",
            "SpecularLayer2Offset": "fp_c7[93].y",
            "SpecularLayer3Offset": "fp_c7[93].z",
            "SpecularLayer4Offset": "fp_c7[93].w",
            "SpecularIntensity": "fp_c7[94].y",
            "SpecularLayer1Intensity": "fp_c7[94].z",
            "SpecularLayer2Intensity": "fp_c7[94].w",
            "SpecularLayer3Intensity": "fp_c7[95].x",
            "SpecularLayer4Intensity": "fp_c7[95].y",
            "SpecularContrast": "fp_c7[95].w",
            "SpecularLayer1Contrast": "fp_c7[96].x",
            "SpecularLayer2Contrast": "fp_c7[96].y",
            "SpecularLayer3Contrast": "fp_c7[96].z",
            "SpecularLayer4Contrast": "fp_c7[96].w",
        },
        "body_constant_buffer_data_flow": body_data_flow,
        "ordinary_body_parameter_census": material_evidence[
            "ordinary_body_parameter_distributions"],
        "cooked_body_emission_verification": cooked_emission,
        "eye_material_buffer_mappings": {
            "ParallaxHeight": "fp_c7[5].y",
            "ParallaxIOR": "fp_c7[5].z",
            "UVScaleOffset1": "fp_c8[2].xyzw",
            "UVScaleOffset2": "fp_c8[3].xyzw",
            "UVRotation2": "fp_c7[21].y",
            "UVCenter0": "fp_c8[139].xy",
            "UVCenter1": "fp_c8[140].xy",
        },
        "eye_resource_subgraphs": {
            "variation_682": {
                role: eye_resources[role]
                for role in ("ParallaxMap", "HighlightMaskMap")
            },
            "variation_1214": {
                role: shadow_eye_resources[role]
                for role in (
                    "ParallaxMap", "HighlightMaskMap", "EyelidShadowMaskMap")
            },
        },
        "hair_specular": {
            "selected_material_choices": material_evidence["choices"],
            "selected_program_sampler": "absent",
            "optional_branch_sampler": "fp_t_tcb_1A",
            "status": "source_proven_disabled_for_selected_kanto_corpus",
            "runtime_implication": (
                "The selected mode-32 runtime must not execute a fabricated "
                "fibre/feather sheen. Visible soft-surface relief must come "
                "from the selected program's real base, normal, shadow, "
                "specular, and rim inputs."),
        },
        "body_resource_dependencies": body["fragment"]["resources"],
        "programs": programs,
        "remaining_equation_gaps": [
            "anonymous_scene_light_and_shadow_buffers",
            "source_middle_dark_domain_light_scalar",
            "source_receive_shadow_scene_state",
            "remaining_scene_dependent_direct_diffuse_specular_composition",
            "source_rim_exposure_and_composite_scale",
            "source_framebuffer_exposure_and_post_process",
            "chromatic_body_emission_transport_if_a_future_selected_record_"
            "uses_non_achromatic_emission",
        ],
        "source_sha256": {
            "ik_character_archive": sha256(archive_path),
            "selected_program_manifest": sha256(manifest_path),
            "option_graph": sha256(option_graph_path),
            "game_loader": sha256(loader_path),
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(json.dumps(report["summary"], indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FileNotFoundError, KeyError, TypeError, ValueError) as error:
        print(f"[ZaIkCharacterDataflow] ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
