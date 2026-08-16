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
import sys
from typing import Any


SCHEMA = "pokemon-autochess-za-ik-character-dataflow-evidence-v1"
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


def material_option_census(game_root: pathlib.Path) -> dict[str, Any]:
    counts: collections.Counter[str] = collections.Counter()
    materials = 0
    for stem in selected_za_stems(game_root):
        model = read_json(game_root / "assets" / "models" / f"{stem}.phmodel")
        for material in model.get("materials", []):
            if material.get("shader_family") != "IkCharacter":
                continue
            materials += 1
            choice = str(material.get("shader_options", {}).get(
                "EnableHairSpecular", "<missing>"))
            counts[choice] += 1
    if materials != 222 or counts != {"False": 222}:
        raise ValueError(
            f"Selected IkCharacter HairSpecular census changed: {dict(counts)}")
    return {"materials": materials, "choices": dict(sorted(counts.items()))}


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
            "fragment": fragment,
            "vertex": vertex,
        })

    hair_census = material_option_census(game_root)
    body = next(row for row in programs if row["variation_index"] == 514)
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

    report = {
        "schema": SCHEMA,
        "source_profile": SOURCE_PROFILE,
        "method": {
            "runtime_execution": False,
            "emulator_used": False,
            "evidence_level": (
                "conservative_compiled_ssa_output_slice_plus_exact_selected_"
                "material_option_census_plus_single_option_differential"),
            "claim_boundary": (
                "Output reachability, source resource participation, and the "
                "absence of the optional hair-specular branch are proven. "
                "Anonymous scene-buffer semantics, bound scene values, and "
                "a literal high-level BRDF reconstruction remain unresolved."),
        },
        "summary": {
            "selected_programs": len(programs),
            "selected_materials": hair_census["materials"],
            "output_reachable_body_resources": sum(
                row["output_reachable"] for row in body["fragment"]["resources"]),
            "hair_specular_enabled_materials": 0,
            "hair_specular_single_option_differentials": len(hair_edges),
            "mapped_eye_material_fields": 7,
            "runtime_changes_authorized_by_this_report": 0,
        },
        "shared_material_buffer_mappings": {
            "UVScaleOffset": "fp_c8[1].xyzw",
            "NormalHeight": "fp_c7[4].z",
            "LayerMaskScale1": "fp_c7[8].y",
            "LayerMaskScale2": "fp_c7[8].z",
            "LayerMaskScale3": "fp_c7[8].w",
            "LayerMaskScale4": "fp_c7[9].x",
            "BaseColor": "fp_c8[9].xyzw",
            "BaseColorLayer1": "fp_c8[10].xyzw",
            "BaseColorLayer2": "fp_c8[11].xyzw",
            "BaseColorLayer3": "fp_c8[12].xyzw",
            "BaseColorLayer4": "fp_c8[13].xyzw",
        },
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
            "selected_material_choices": hair_census["choices"],
            "selected_program_sampler": "absent",
            "optional_branch_sampler": "fp_t_tcb_1A",
            "status": "source_proven_disabled_for_selected_kanto_corpus",
            "runtime_implication": (
                "Any Phlosion fibre/feather sheen is a bounded presentation "
                "reconstruction, not the source EnableHairSpecular branch."),
        },
        "body_resource_dependencies": body["fragment"]["resources"],
        "programs": programs,
        "remaining_equation_gaps": [
            "anonymous_scene_light_and_shadow_buffers",
            "literal_direct_diffuse_specular_composition_order",
            "literal_rim_and_color_process_equations",
            "source_framebuffer_exposure_and_post_process",
        ],
        "source_sha256": {
            "selected_program_manifest": sha256(manifest_path),
            "option_graph": sha256(option_graph_path),
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
