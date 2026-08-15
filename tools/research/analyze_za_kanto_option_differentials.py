#!/usr/bin/env python3
"""Summarize exact one-option structural differentials in Z-A programs."""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import pathlib
import sys
from typing import Any

from analyze_sv_kanto_selected_program_abi import stage_report


PLAN_SCHEMA = "pokemon-autochess-za-kanto-option-differential-plan-v1"
SELECTED_SCHEMA = "pokemon-autochess-private-za-selected-programs-v1"
COMPARISON_SCHEMA = (
    "pokemon-autochess-private-za-option-differential-programs-v1"
)
REPORT_SCHEMA = "pokemon-autochess-za-kanto-option-differential-evidence-v1"
GRAPH_PLAN_SCHEMA = "pokemon-autochess-za-kanto-option-graph-plan-v1"
GRAPH_PROGRAM_SCHEMA = "pokemon-autochess-private-za-option-graph-programs-v1"
GRAPH_REPORT_SCHEMA = "pokemon-autochess-za-kanto-option-graph-evidence-v1"
SOURCE_PROFILE = "pokemon-legends-za-v2.0.0"


def read_json(path: pathlib.Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8-sig") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise ValueError(f"Expected a JSON object: {path}")
    return value


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def safe_program_directory(root: pathlib.Path, relative_value: str) -> pathlib.Path:
    relative = pathlib.PurePosixPath(relative_value)
    if relative.is_absolute() or ".." in relative.parts:
        raise ValueError(f"Unsafe program directory: {relative}")
    return root.joinpath(*relative.parts)


def program_stages(root: pathlib.Path, record: dict[str, Any]) -> dict[str, Any]:
    variation = int(record["variation_index"])
    stem = f"v{variation:04d}"
    directory = safe_program_directory(root, str(record["directory"]))
    return {
        "fragment": stage_report(
            directory / f"{stem}.fsh.maxwell.glsl",
            str(record["fragment_glsl_sha256"]),
        ),
        "vertex": stage_report(
            directory / f"{stem}.vsh.maxwell.glsl",
            str(record["vertex_glsl_sha256"]),
        ),
    }


def sampler_key(row: dict[str, Any]) -> tuple[str, str]:
    # The Maxwell translator assigns dense GLSL binding ordinals. Adding or
    # removing one source sampler can renumber later declarations; the retained
    # tcb symbol and dimensionality are the stable compiled identities.
    return str(row["name"]), str(row["type"])


def sampler_summary(row: dict[str, Any]) -> dict[str, Any]:
    return {
        "name": str(row["name"]),
        "type": str(row["type"]),
        "binding": int(row["binding"]),
        "static_texture_call_count": int(row["static_texture_call_count"]),
    }


def buffer_key(row: dict[str, Any]) -> tuple[str, int]:
    return str(row["name"]), int(row["binding"])


def stage_differential(
    selected: dict[str, Any], comparison: dict[str, Any]
) -> dict[str, Any]:
    selected_samplers = {sampler_key(row): row for row in selected["samplers"]}
    comparison_samplers = {
        sampler_key(row): row for row in comparison["samplers"]
    }
    added_sampler_keys = sorted(set(selected_samplers) - set(comparison_samplers))
    removed_sampler_keys = sorted(set(comparison_samplers) - set(selected_samplers))
    changed_sampler_calls = []
    for key in sorted(set(selected_samplers) & set(comparison_samplers)):
        selected_count = int(selected_samplers[key]["static_texture_call_count"])
        comparison_count = int(
            comparison_samplers[key]["static_texture_call_count"]
        )
        if selected_count != comparison_count:
            changed_sampler_calls.append(
                {
                    "name": key[0],
                    "type": key[1],
                    "selected_binding": int(selected_samplers[key]["binding"]),
                    "comparison_binding": int(
                        comparison_samplers[key]["binding"]
                    ),
                    "selected_static_texture_call_count": selected_count,
                    "comparison_static_texture_call_count": comparison_count,
                }
            )

    selected_buffers = {buffer_key(row): row for row in selected["buffers"]}
    comparison_buffers = {buffer_key(row): row for row in comparison["buffers"]}
    added_buffer_keys = sorted(set(selected_buffers) - set(comparison_buffers))
    removed_buffer_keys = sorted(set(comparison_buffers) - set(selected_buffers))
    changed_buffer_uses = []
    for key in sorted(set(selected_buffers) & set(comparison_buffers)):
        selected_indices = set(selected_buffers[key]["constant_indices"])
        comparison_indices = set(comparison_buffers[key]["constant_indices"])
        selected_dynamic = set(selected_buffers[key]["dynamic_index_expressions"])
        comparison_dynamic = set(
            comparison_buffers[key]["dynamic_index_expressions"]
        )
        selected_references = int(selected_buffers[key]["static_reference_count"])
        comparison_references = int(
            comparison_buffers[key]["static_reference_count"]
        )
        if (
            selected_indices != comparison_indices
            or selected_dynamic != comparison_dynamic
            or selected_references != comparison_references
        ):
            changed_buffer_uses.append(
                {
                    "name": key[0],
                    "binding": key[1],
                    "added_constant_indices": sorted(
                        selected_indices - comparison_indices
                    ),
                    "removed_constant_indices": sorted(
                        comparison_indices - selected_indices
                    ),
                    "added_dynamic_indices": sorted(
                        selected_dynamic - comparison_dynamic
                    ),
                    "removed_dynamic_indices": sorted(
                        comparison_dynamic - selected_dynamic
                    ),
                    "selected_static_reference_count": selected_references,
                    "comparison_static_reference_count": comparison_references,
                }
            )

    source_changed = selected["sha256"] != comparison["sha256"]
    resource_delta_count = (
        len(added_sampler_keys)
        + len(removed_sampler_keys)
        + len(added_buffer_keys)
        + len(removed_buffer_keys)
    )
    data_flow_delta_count = len(changed_sampler_calls) + len(changed_buffer_uses)
    if not source_changed:
        classification = "identical_compiled_stage"
    elif resource_delta_count:
        classification = "resource_abi_and_data_flow_change"
    elif data_flow_delta_count:
        classification = "data_flow_change_with_stable_resource_abi"
    else:
        classification = "compiled_code_change_with_stable_static_abi"
    return {
        "selected_sha256": str(selected["sha256"]),
        "comparison_sha256": str(comparison["sha256"]),
        "compiled_stage_changed": source_changed,
        "classification": classification,
        "added_samplers": [
            sampler_summary(selected_samplers[key]) for key in added_sampler_keys
        ],
        "removed_samplers": [
            sampler_summary(comparison_samplers[key]) for key in removed_sampler_keys
        ],
        "changed_sampler_call_counts": changed_sampler_calls,
        "added_buffers": [
            {"name": key[0], "binding": key[1]} for key in added_buffer_keys
        ],
        "removed_buffers": [
            {"name": key[0], "binding": key[1]} for key in removed_buffer_keys
        ],
        "changed_buffer_uses": changed_buffer_uses,
        "resource_delta_count": resource_delta_count,
        "data_flow_delta_count": data_flow_delta_count,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", type=pathlib.Path, required=True)
    parser.add_argument("--selected-program-root", type=pathlib.Path, required=True)
    parser.add_argument("--comparison-program-root", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument(
        "--differential-kind",
        choices=("SelectedCorpus", "FullGraph"),
        default="SelectedCorpus",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    plan_path = args.plan.resolve()
    selected_root = args.selected_program_root.resolve()
    comparison_root = args.comparison_program_root.resolve()
    plan = read_json(plan_path)
    comparison_manifest_path = (
        comparison_root / "differential_programs_manifest.json"
    )
    comparison_manifest = read_json(comparison_manifest_path)
    if args.differential_kind == "FullGraph":
        plan_schema = GRAPH_PLAN_SCHEMA
        selected_schema = GRAPH_PROGRAM_SCHEMA
        comparison_schema = GRAPH_PROGRAM_SCHEMA
        report_schema = GRAPH_REPORT_SCHEMA
        label = "ZaKantoOptionGraph"
        selected_manifest_path = comparison_manifest_path
        selected_manifest = comparison_manifest
        selected_root = comparison_root
    else:
        plan_schema = PLAN_SCHEMA
        selected_schema = SELECTED_SCHEMA
        comparison_schema = COMPARISON_SCHEMA
        report_schema = REPORT_SCHEMA
        label = "ZaKantoOptionDifferentials"
        selected_manifest_path = selected_root / "selected_programs_manifest.json"
        selected_manifest = read_json(selected_manifest_path)
    if plan.get("schema") != plan_schema:
        raise ValueError("Unsupported Z-A Kanto option-differential plan")
    if selected_manifest.get("schema") != selected_schema:
        raise ValueError("Unsupported Z-A selected-program manifest")
    if comparison_manifest.get("schema") != comparison_schema:
        raise ValueError("Unsupported Z-A option-differential manifest")
    for document in (plan, selected_manifest, comparison_manifest):
        if document.get("source_profile") != SOURCE_PROFILE:
            raise ValueError("Z-A option-differential source profile mismatch")
        if bool(document.get("runtime_execution", False)) or bool(
            document.get("emulator_used", False)
        ):
            raise ValueError("Runtime or emulator evidence is outside this workflow")
    if sha256(plan_path) != comparison_manifest.get("plan_sha256"):
        raise ValueError("Option-differential manifest does not match its plan")

    selected_records = {
        (str(row["shader_family"]), int(row["variation_index"])): row
        for row in selected_manifest.get("programs", [])
    }
    comparison_records = {
        (str(row["shader_family"]), int(row["variation_index"])): row
        for row in comparison_manifest.get("programs", [])
    }
    selected_cache: dict[tuple[str, int], dict[str, Any]] = {}
    comparison_cache: dict[tuple[str, int], dict[str, Any]] = {}
    rows = []
    impact_groups: dict[tuple[str, str, str], list[dict[str, Any]]] = (
        collections.defaultdict(list)
    )
    for index, differential in enumerate(plan.get("differentials", [])):
        family = str(differential["shader_family"])
        selected_variation = int(differential["selected_variation"])
        comparison_variation = int(differential["comparison_variation"])
        selected_key = (family, selected_variation)
        comparison_key = (family, comparison_variation)
        if selected_key not in selected_records:
            raise ValueError(f"Selected program is missing: {selected_key}")
        if comparison_key not in comparison_records:
            raise ValueError(f"Comparison program is missing: {comparison_key}")
        selected_stages = selected_cache.setdefault(
            selected_key,
            program_stages(selected_root, selected_records[selected_key]),
        )
        comparison_stages = comparison_cache.setdefault(
            comparison_key,
            program_stages(comparison_root, comparison_records[comparison_key]),
        )
        fragment = stage_differential(
            selected_stages["fragment"], comparison_stages["fragment"]
        )
        vertex = stage_differential(
            selected_stages["vertex"], comparison_stages["vertex"]
        )
        row = {
            "differential_index": index,
            "shader_family": family,
            "selected_variation": selected_variation,
            "comparison_variation": comparison_variation,
            "option_scope": str(differential["option_scope"]),
            "changed_option": str(differential["changed_option"]),
            "selected_choice": str(differential["selected_choice"]),
            "comparison_choice": str(differential["comparison_choice"]),
            "selected_material_count": int(
                differential.get("selected_material_count", 0)
            ),
            "fragment": fragment,
            "vertex": vertex,
        }
        for optional_key in (
            "distance_to_selected_program",
            "selected_program_endpoint",
            "transition_representative_rank",
            "available_transition_edges",
        ):
            if optional_key in differential:
                row[optional_key] = differential[optional_key]
        rows.append(row)
        impact_groups[
            (family, row["option_scope"], row["changed_option"])
        ].append(row)

    impacts = []
    for key, group in sorted(impact_groups.items()):
        impacts.append(
            {
                "shader_family": key[0],
                "option_scope": key[1],
                "changed_option": key[2],
                "differential_count": len(group),
                "selected_variations": sorted(
                    {int(row["selected_variation"]) for row in group}
                ),
                "fragment_changed_count": sum(
                    bool(row["fragment"]["compiled_stage_changed"])
                    for row in group
                ),
                "vertex_changed_count": sum(
                    bool(row["vertex"]["compiled_stage_changed"]) for row in group
                ),
                "fragment_classifications": sorted(
                    {str(row["fragment"]["classification"]) for row in group}
                ),
                "vertex_classifications": sorted(
                    {str(row["vertex"]["classification"]) for row in group}
                ),
                "resource_delta_count": sum(
                    int(row[stage]["resource_delta_count"])
                    for row in group
                    for stage in ("fragment", "vertex")
                ),
                "data_flow_delta_count": sum(
                    int(row[stage]["data_flow_delta_count"])
                    for row in group
                    for stage in ("fragment", "vertex")
                ),
            }
        )

    report = {
        "schema": report_schema,
        "source_profile": SOURCE_PROFILE,
        "method": {
            "runtime_execution": False,
            "emulator_used": False,
            "evidence_level": "compiled_single_option_program_structural_differential",
            "proof_rule": (
                "Each program pair differs in exactly one packed TRSHA choice. "
                "Static resource declarations, texture-call counts, constant-buffer "
                "use sites, and stage hashes are compared without executing code."
            ),
            "claim_boundary": (
                "The report proves which compiled stages and anonymous resources "
                "change with an option. It does not name stable resources absent a "
                "resource delta and does not prove runtime constant-buffer values."
            ),
        },
        "sources": {
            "plan_identity": plan_path.name,
            "plan_sha256": sha256(plan_path),
            "selected_manifest_identity": selected_manifest_path.name,
            "selected_manifest_sha256": sha256(selected_manifest_path),
            "comparison_manifest_identity": comparison_manifest_path.name,
            "comparison_manifest_sha256": sha256(comparison_manifest_path),
        },
        "summary": {
            "differential_count": len(rows),
            "covered_options": len(impacts),
            "shader_families": len({row["shader_family"] for row in rows}),
            "fragment_changed_differentials": sum(
                bool(row["fragment"]["compiled_stage_changed"]) for row in rows
            ),
            "vertex_changed_differentials": sum(
                bool(row["vertex"]["compiled_stage_changed"]) for row in rows
            ),
            "resource_changing_differentials": sum(
                int(row["fragment"]["resource_delta_count"])
                + int(row["vertex"]["resource_delta_count"])
                > 0
                for row in rows
            ),
            "unresolved_option_choices": int(
                plan["summary"].get("unresolved_option_choices", 0)
            ),
        },
        "option_impacts": impacts,
        "differentials": rows,
        "unresolved": plan.get("unresolved", []),
    }
    if len(rows) != int(plan["summary"]["differential_count"]):
        raise ValueError("Analyzed differential count does not match its plan")
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(report, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    print(
        f"[{label}] "
        f"differentials={len(rows)} options={len(impacts)} "
        f"fragment_changed={report['summary']['fragment_changed_differentials']} "
        f"vertex_changed={report['summary']['vertex_changed_differentials']} "
        f"-> {output}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FileNotFoundError, KeyError, TypeError, ValueError) as error:
        print(f"[ZaKantoOptionDifferentials] ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
