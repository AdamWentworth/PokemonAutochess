#!/usr/bin/env python3
"""Trace every exact Z-A one-option program edge through final outputs."""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import pathlib
import sys
from typing import Any

import analyze_za_ik_character_dataflow as flow


SCHEMA = "pokemon-autochess-za-kanto-option-dataflow-evidence-v1"
SOURCE_PROFILE = "pokemon-legends-za-v2.0.0"
MANIFEST_SCHEMA = "pokemon-autochess-private-za-option-graph-programs-v1"
GRAPH_SCHEMA = "pokemon-autochess-za-kanto-option-graph-evidence-v1"


def read_json(path: pathlib.Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(value, dict):
        raise ValueError(f"Expected a JSON object: {path}")
    return value


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def stage_signature(path: pathlib.Path, expected_hash: str) -> dict[str, Any]:
    if sha256(path) != expected_hash:
        raise ValueError(f"Option-graph program hash changed: {path}")
    graph = flow.build_graph(path.read_text(encoding="utf-8-sig"))
    roots = set().union(*graph["outputs"].values())
    closure = flow.backward_closure(graph, roots)
    samplers, buffers = flow.references_in_closure(graph, closure)
    return {
        "sha256": expected_hash,
        "samplers": set(samplers),
        "buffers": set(buffers),
        "dependency_temporaries": len(closure),
    }


def delta(selected: dict[str, Any], comparison: dict[str, Any]) -> dict[str, Any]:
    added_samplers = sorted(comparison["samplers"] - selected["samplers"])
    removed_samplers = sorted(selected["samplers"] - comparison["samplers"])
    added_buffers = sorted(comparison["buffers"] - selected["buffers"])
    removed_buffers = sorted(selected["buffers"] - comparison["buffers"])
    compiled_changed = selected["sha256"] != comparison["sha256"]
    if not compiled_changed:
        classification = "identical_compiled_output_slice"
    elif (added_samplers or removed_samplers) and (added_buffers or removed_buffers):
        classification = "resource_and_buffer_output_change"
    elif added_samplers or removed_samplers:
        classification = "resource_output_change"
    elif added_buffers or removed_buffers:
        classification = "buffer_output_change"
    else:
        classification = "same_resources_changed_output_equations"
    return {
        "compiled_stage_changed": compiled_changed,
        "classification": classification,
        "selected_dependency_temporaries": selected["dependency_temporaries"],
        "comparison_dependency_temporaries": comparison["dependency_temporaries"],
        "added_output_samplers": added_samplers,
        "removed_output_samplers": removed_samplers,
        "added_output_buffer_fields": added_buffers,
        "removed_output_buffer_fields": removed_buffers,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-root", type=pathlib.Path, required=True)
    parser.add_argument("--shader-study", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    game_root = args.game_root.resolve()
    study_root = args.shader_study.resolve()
    program_root = study_root / "option-graph-programs"
    manifest_path = program_root / "differential_programs_manifest.json"
    graph_path = game_root / "docs" / "kanto" / "evidence" / (
        "za_kanto_option_graph.json")
    manifest = read_json(manifest_path)
    graph_report = read_json(graph_path)
    if (manifest.get("schema") != MANIFEST_SCHEMA or
            manifest.get("source_profile") != SOURCE_PROFILE):
        raise ValueError("Unsupported private Z-A option-graph manifest")
    if graph_report.get("schema") != GRAPH_SCHEMA:
        raise ValueError("Unsupported promoted Z-A option graph")

    records = {
        (str(row["shader_family"]), int(row["variation_index"])): row
        for row in manifest.get("programs", [])
    }
    if len(records) != int(manifest.get("program_count", -1)) or not records:
        raise ValueError("Private Z-A option-graph program census changed")

    cache: dict[tuple[str, int, str], dict[str, Any]] = {}

    def signature(family: str, variation: int, stage: str) -> dict[str, Any]:
        key = (family, variation)
        if key not in records:
            raise ValueError(f"Option graph lost program {family}/{variation}")
        cache_key = (family, variation, stage)
        if cache_key not in cache:
            record = records[key]
            relative = pathlib.PurePosixPath(str(record["directory"]))
            stem = f"v{variation:04d}"
            cache[cache_key] = stage_signature(
                program_root.joinpath(*relative.parts) /
                    f"{stem}.{stage}.maxwell.glsl",
                str(record[f"{stage_name(stage)}_glsl_sha256"]),
            )
        return cache[cache_key]

    edges = []
    classifications: collections.Counter[str] = collections.Counter()
    option_rows: dict[tuple[str, str], dict[str, Any]] = {}
    for row in graph_report.get("differentials", []):
        family = str(row["shader_family"])
        selected_variation = int(row["selected_variation"])
        comparison_variation = int(row["comparison_variation"])
        fragment = delta(
            signature(family, selected_variation, "fsh"),
            signature(family, comparison_variation, "fsh"),
        )
        vertex = delta(
            signature(family, selected_variation, "vsh"),
            signature(family, comparison_variation, "vsh"),
        )
        classifications[fragment["classification"]] += 1
        classifications[vertex["classification"]] += 1
        edge = {
            "differential_index": int(row["differential_index"]),
            "shader_family": family,
            "changed_option": str(row["changed_option"]),
            "selected_choice": str(row["selected_choice"]),
            "comparison_choice": str(row["comparison_choice"]),
            "selected_variation": selected_variation,
            "comparison_variation": comparison_variation,
            "distance_to_selected_program": int(row["distance_to_selected_program"]),
            "selected_program_endpoint": bool(row["selected_program_endpoint"]),
            "fragment": fragment,
            "vertex": vertex,
        }
        edges.append(edge)
        option_key = (family, edge["changed_option"])
        aggregate = option_rows.setdefault(option_key, {
            "shader_family": family,
            "changed_option": edge["changed_option"],
            "edge_count": 0,
            "selected_program_endpoint_edges": 0,
            "fragment_classifications": collections.Counter(),
            "vertex_classifications": collections.Counter(),
            "added_output_samplers": set(),
            "removed_output_samplers": set(),
            "added_output_buffer_fields": set(),
            "removed_output_buffer_fields": set(),
        })
        aggregate["edge_count"] += 1
        aggregate["selected_program_endpoint_edges"] += int(
            edge["selected_program_endpoint"])
        for stage_name_value, stage_value in (
                ("fragment", fragment), ("vertex", vertex)):
            aggregate[f"{stage_name_value}_classifications"][
                stage_value["classification"]] += 1
            for field in (
                    "added_output_samplers", "removed_output_samplers",
                    "added_output_buffer_fields", "removed_output_buffer_fields"):
                aggregate[field].update(stage_value[field])

    expected_edges = int(graph_report.get("summary", {}).get(
        "differential_count", -1))
    expected_signatures = {
        (str(row["shader_family"]), int(row[endpoint]), stage)
        for row in graph_report.get("differentials", [])
        for endpoint in ("selected_variation", "comparison_variation")
        for stage in ("fsh", "vsh")
    }
    if (len(edges) != expected_edges or
            set(cache) != expected_signatures):
        raise ValueError("Z-A option dataflow coverage is incomplete")
    option_impacts = []
    for key in sorted(option_rows):
        row = option_rows[key]
        option_impacts.append({
            **row,
            "fragment_classifications": dict(sorted(
                row["fragment_classifications"].items())),
            "vertex_classifications": dict(sorted(
                row["vertex_classifications"].items())),
            "added_output_samplers": sorted(row["added_output_samplers"]),
            "removed_output_samplers": sorted(row["removed_output_samplers"]),
            "added_output_buffer_fields": sorted(
                row["added_output_buffer_fields"]),
            "removed_output_buffer_fields": sorted(
                row["removed_output_buffer_fields"]),
        })

    report = {
        "schema": SCHEMA,
        "source_profile": SOURCE_PROFILE,
        "method": {
            "runtime_execution": False,
            "emulator_used": False,
            "evidence_level": (
                "complete_exact_single_option_graph_plus_conservative_"
                "compiled_output_dependency_differentials"),
            "claim_boundary": (
                "This proves which sampled resources and anonymous constant-"
                "buffer fields enter or leave final output dependency cones "
                "for each archived one-option transition. It does not assign "
                "semantic names to anonymous scene fields or recover source "
                "runtime values."),
        },
        "summary": {
            "programs_analyzed": len(records),
            "stages_analyzed": len(cache),
            "one_option_edges": len(edges),
            "shader_families": len({row["shader_family"] for row in edges}),
            "covered_options": len(option_impacts),
            "stage_classifications": dict(sorted(classifications.items())),
        },
        "option_impacts": option_impacts,
        "differentials": edges,
        "source_sha256": {
            "program_manifest": sha256(manifest_path),
            "promoted_option_graph": sha256(graph_path),
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(json.dumps(report["summary"], indent=2))
    return 0


def stage_name(short_name: str) -> str:
    return "fragment" if short_name == "fsh" else "vertex"


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FileNotFoundError, KeyError, TypeError, ValueError) as error:
        print(f"[ZaKantoOptionDataflow] ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
