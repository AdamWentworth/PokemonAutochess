#!/usr/bin/env python3
"""Prove SV Kanto texture bindings from exact one-option program pairs."""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import pathlib
import sys
from typing import Any

from analyze_sv_kanto_selected_program_abi import stage_report


PLAN_SCHEMA = "pokemon-autochess-sv-kanto-program-differential-plan-v1"
SELECTED_SCHEMA = "pokemon-autochess-private-sv-selected-programs-v1"
COMPARISON_SCHEMA = "pokemon-autochess-private-sv-differential-programs-v1"
REPORT_SCHEMA = "pokemon-autochess-sv-kanto-program-differential-evidence-v1"


def read_json(path: pathlib.Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8-sig") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise ValueError(f"Expected a JSON object: {path}")
    return value


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def safe_program_directory(root: pathlib.Path, value: str) -> pathlib.Path:
    relative = pathlib.PurePosixPath(value)
    if relative.is_absolute() or ".." in relative.parts:
        raise ValueError(f"Unsafe program directory: {value}")
    return root.joinpath(*relative.parts)


def load_stage(
    root: pathlib.Path, record: dict[str, Any], stage: str
) -> dict[str, Any]:
    variation = int(record["variation_index"])
    stem = f"v{variation:04d}"
    directory = safe_program_directory(root, str(record["directory"]))
    kind = "fragment" if stage == "fsh" else "vertex"
    return stage_report(
        directory / f"{stem}.{stage}.maxwell.glsl",
        str(record[f"{kind}_glsl_sha256"]),
    )


def sampler_key(row: dict[str, Any]) -> tuple[str, str]:
    # The translator assigns dense GLSL binding ordinals, so disabling one
    # source sampler renumbers later declarations. The retained tcb symbol and
    # sampler dimensionality are the stable compiled identities.
    return row["name"], row["type"]


def buffer_key(row: dict[str, Any]) -> tuple[str, int]:
    return row["name"], int(row["binding"])


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", type=pathlib.Path, required=True)
    parser.add_argument("--selected-program-root", type=pathlib.Path, required=True)
    parser.add_argument("--comparison-program-root", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    plan_path = args.plan.resolve()
    selected_root = args.selected_program_root.resolve()
    comparison_root = args.comparison_program_root.resolve()
    plan = read_json(plan_path)
    selected_manifest_path = selected_root / "selected_programs_manifest.json"
    comparison_manifest_path = comparison_root / "differential_programs_manifest.json"
    selected_manifest = read_json(selected_manifest_path)
    comparison_manifest = read_json(comparison_manifest_path)
    if plan.get("schema") != PLAN_SCHEMA:
        raise ValueError("Unsupported SV Kanto differential plan")
    if selected_manifest.get("schema") != SELECTED_SCHEMA:
        raise ValueError("Unsupported selected-program manifest")
    if comparison_manifest.get("schema") != COMPARISON_SCHEMA:
        raise ValueError("Unsupported comparison-program manifest")
    if sha256(plan_path) != comparison_manifest.get("plan_sha256"):
        raise ValueError("Comparison-program manifest does not match the plan")

    selected_records = {
        (str(row["shader_family"]), int(row["variation_index"])): row
        for row in selected_manifest.get("programs", [])
    }
    comparison_records = {
        (str(row["shader_family"]), int(row["variation_index"])): row
        for row in comparison_manifest.get("programs", [])
    }
    stage_cache: dict[tuple[str, str, int, str], dict[str, Any]] = {}

    def cached_stage(
        root_kind: str,
        root: pathlib.Path,
        record: dict[str, Any],
        stage: str,
    ) -> dict[str, Any]:
        key = (root_kind, str(record["shader_family"]), int(record["variation_index"]), stage)
        if key not in stage_cache:
            stage_cache[key] = load_stage(root, record, stage)
        return stage_cache[key]

    rows: list[dict[str, Any]] = []
    mapping_evidence: dict[tuple[str, str, str, str, int], list[int]] = collections.defaultdict(list)
    for differential_index, differential in enumerate(plan.get("differentials", [])):
        family = str(differential["shader_family"])
        selected_variation = int(differential["selected_variation"])
        comparison_variation = int(differential["comparison_variation"])
        selected_key = family, selected_variation
        comparison_key = family, comparison_variation
        if selected_key not in selected_records or comparison_key not in comparison_records:
            raise ValueError(
                f"Missing compiled program pair: {family} "
                f"{selected_variation}/{comparison_variation}"
            )
        selected_record = selected_records[selected_key]
        comparison_record = comparison_records[comparison_key]
        selected_fragment = cached_stage(
            "selected", selected_root, selected_record, "fsh"
        )
        comparison_fragment = cached_stage(
            "comparison", comparison_root, comparison_record, "fsh"
        )
        selected_vertex = cached_stage("selected", selected_root, selected_record, "vsh")
        comparison_vertex = cached_stage(
            "comparison", comparison_root, comparison_record, "vsh"
        )

        selected_samplers = {
            sampler_key(row): row for row in selected_fragment["samplers"]
        }
        comparison_samplers = {
            sampler_key(row): row for row in comparison_fragment["samplers"]
        }
        added_sampler_keys = sorted(set(selected_samplers) - set(comparison_samplers))
        removed_sampler_keys = sorted(set(comparison_samplers) - set(selected_samplers))
        selected_buffers = {
            buffer_key(row) for row in selected_fragment["buffers"]
        }
        comparison_buffers = {
            buffer_key(row) for row in comparison_fragment["buffers"]
        }
        if len(added_sampler_keys) != 1 or removed_sampler_keys:
            raise ValueError(
                f"{family} {selected_variation}/{comparison_variation} "
                f"does not isolate exactly one added fragment sampler"
            )
        added = selected_samplers[added_sampler_keys[0]]
        if int(added["static_texture_call_count"]) <= 0:
            raise ValueError(
                f"{family} {selected_variation} declares but does not sample "
                f"{added['name']}"
            )
        row = {
            "differential_index": differential_index,
            "shader_family": family,
            "selected_variation": selected_variation,
            "comparison_variation": comparison_variation,
            "changed_option": str(differential["changed_option"]),
            "selected_choice": str(differential["selected_choice"]),
            "comparison_choice": str(differential["comparison_choice"]),
            "texture_role": str(differential["texture_role"]),
            "material_count": int(differential["material_count"]),
            "permutation_count": len(differential["permutations"]),
            "added_fragment_sampler": {
                "name": added["name"],
                "type": added["type"],
                "binding": int(added["binding"]),
                "static_texture_call_count": int(added["static_texture_call_count"]),
            },
            "removed_fragment_samplers": [],
            "added_fragment_buffers": [
                {"name": name, "binding": binding}
                for name, binding in sorted(selected_buffers - comparison_buffers)
            ],
            "removed_fragment_buffers": [
                {"name": name, "binding": binding}
                for name, binding in sorted(comparison_buffers - selected_buffers)
            ],
            "selected_fragment_glsl_sha256": selected_fragment["sha256"],
            "comparison_fragment_glsl_sha256": comparison_fragment["sha256"],
            "vertex_program_changed": selected_vertex["sha256"] != comparison_vertex["sha256"],
            "selected_vertex_glsl_sha256": selected_vertex["sha256"],
            "comparison_vertex_glsl_sha256": comparison_vertex["sha256"],
        }
        rows.append(row)
        mapping_key = (
            family,
            row["texture_role"],
            added["name"],
            added["type"],
        )
        mapping_evidence[mapping_key].append(differential_index)

    mappings = [
        {
            "shader_family": key[0],
            "texture_role": key[1],
            "sampler_name": key[2],
            "sampler_type": key[3],
            "differential_indices": indices,
            "differential_count": len(indices),
        }
        for key, indices in sorted(mapping_evidence.items())
    ]
    unresolved_counts = collections.Counter(
        str(row["reason"]) for row in plan.get("unresolved", [])
    )
    report = {
        "schema": REPORT_SCHEMA,
        "source_profile": "pokemon-scarlet-v3.0.1",
        "method": {
            "runtime_execution": False,
            "emulator_used": False,
            "evidence_level": "compiled_single_option_program_differential",
            "proof_rule": (
                "The compared source variations differ in one material option. "
                "Exactly one sampled fragment sampler appears in the enabled "
                "program and no fragment sampler disappears. That sampler is "
                "therefore mapped to the changed source texture role."
            ),
            "claim_boundary": plan["method"]["claim_boundary"],
        },
        "source_artifacts": {
            "plan": {"identity": plan_path.name, "sha256": sha256(plan_path)},
            "selected_program_manifest": {
                "identity": selected_manifest_path.name,
                "sha256": sha256(selected_manifest_path),
            },
            "comparison_program_manifest": {
                "identity": comparison_manifest_path.name,
                "sha256": sha256(comparison_manifest_path),
            },
        },
        "summary": {
            "differential_count": len(rows),
            "unique_comparison_programs": int(plan["summary"]["unique_comparison_programs"]),
            "proven_texture_mappings": len(mappings),
            "mapped_shader_families": len({row["shader_family"] for row in mappings}),
            "mapped_texture_roles": len({row["texture_role"] for row in mappings}),
            "unresolved_role_checks": len(plan.get("unresolved", [])),
        },
        "proven_texture_mappings": mappings,
        "differentials": rows,
        "unresolved_summary": {
            "reason_counts": dict(sorted(unresolved_counts.items())),
            "claim": (
                "Unresolved role checks remain unmapped; no semantic binding is "
                "inferred from a multi-option or unavailable comparison."
            ),
        },
    }
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(report, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    print(
        "[SvKantoProgramDifferentials] "
        f"differentials={len(rows)} mappings={len(mappings)} "
        f"families={report['summary']['mapped_shader_families']} -> {output}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FileNotFoundError, KeyError, TypeError, ValueError) as error:
        print(f"[SvKantoProgramDifferentials] ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
