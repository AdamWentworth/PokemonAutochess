#!/usr/bin/env python3
"""Plan exact one-option shader differentials for selected SV Kanto programs."""

from __future__ import annotations

import argparse
import collections
import json
import pathlib
import sys
from typing import Any

from analyze_sv_kanto_shader_permutations import encode_options, option_word_count


INVENTORY_SCHEMA = "pokemon-autochess-sv-kanto-shader-inventory-v1"
PLAN_SCHEMA = "pokemon-autochess-sv-kanto-program-differential-plan-v1"


def read_json(path: pathlib.Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8-sig") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise ValueError(f"Expected a JSON object: {path}")
    return value


def disabled_choice(slot: dict[str, Any]) -> str | None:
    for value in slot.get("slot_values", []):
        if str(value["string_value"]).lower() == "false" or int(value["u_int_value"]) == 0:
            return str(value["string_value"])
    return None


def build_variation_lookup(metadata: dict[str, Any]) -> dict[tuple[int, ...], list[int]]:
    shader_words = option_word_count(metadata.get("shader_param", []))
    global_words = option_word_count(metadata.get("global_param", []))
    stride = shader_words + global_words
    values = metadata.get("param_buffer", [])
    if not values or len(values) % stride:
        raise ValueError(f"Metadata variation table is not divisible by {stride}")
    result: dict[tuple[int, ...], list[int]] = collections.defaultdict(list)
    for variation, offset in enumerate(range(0, len(values), stride)):
        result[tuple(int(value) for value in values[offset : offset + stride])].append(variation)
    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inventory", type=pathlib.Path, required=True)
    parser.add_argument("--shader-study", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    inventory_path = args.inventory.resolve()
    study_root = args.shader_study.resolve()
    if not study_root.is_dir():
        raise FileNotFoundError(study_root)
    inventory = read_json(inventory_path)
    if inventory.get("schema") != INVENTORY_SCHEMA:
        raise ValueError("Unsupported SV Kanto shader inventory")
    if int(inventory.get("summary", {}).get("unresolved_permutations", -1)) != 0:
        raise ValueError("SV Kanto shader inventory is not exactly resolved")

    permutations = {
        row["permutation_sha256"]: row
        for row in inventory.get("material_permutations", [])
    }
    planned: dict[tuple[str, int, int, str, str], dict[str, Any]] = {}
    unresolved: dict[tuple[str, int, str, str, str], dict[str, Any]] = {}

    for family in inventory.get("families", []):
        family_name = str(family["shader_family"])
        metadata_identity = str(family["decoded_metadata"]["identity"])
        metadata = read_json(study_root / metadata_identity)
        lookup = build_variation_lookup(metadata)
        slots = {
            str(slot["slot_name"]): slot for slot in metadata.get("shader_param", [])
        }
        for resolved in family.get("resolved_permutations", []):
            permutation = permutations[resolved["permutation_sha256"]]
            options = dict(permutation["shader_options"])
            selected_variation = int(resolved["variation_index"])
            texture_roles = sorted({row["role"] for row in permutation["texture_bindings"]})
            for role in texture_roles:
                option_name = f"Enable{role}"
                reason: str | None = None
                if option_name not in slots:
                    reason = "no_direct_enable_slot"
                elif option_name not in options:
                    reason = "material_omits_toggle_option"
                else:
                    disabled = disabled_choice(slots[option_name])
                    if disabled is None:
                        reason = "no_disabled_choice"
                    elif str(options[option_name]).lower() == disabled.lower():
                        reason = "source_option_is_disabled"
                    else:
                        comparison_options = dict(options)
                        comparison_options[option_name] = disabled
                        shader_keys, _ = encode_options(
                            comparison_options, metadata.get("shader_param", [])
                        )
                        global_keys, _ = encode_options(
                            comparison_options, metadata.get("global_param", [])
                        )
                        matches = lookup.get(tuple(shader_keys + global_keys), [])
                        if len(matches) == 1:
                            comparison_variation = matches[0]
                            key = (
                                family_name,
                                selected_variation,
                                comparison_variation,
                                option_name,
                                role,
                            )
                            row = planned.setdefault(
                                key,
                                {
                                    "shader_family": family_name,
                                    "selected_variation": selected_variation,
                                    "comparison_variation": comparison_variation,
                                    "changed_option": option_name,
                                    "selected_choice": str(options[option_name]),
                                    "comparison_choice": disabled,
                                    "texture_role": role,
                                    "material_count": 0,
                                    "permutations": [],
                                },
                            )
                            if resolved["permutation_sha256"] not in row["permutations"]:
                                row["permutations"].append(resolved["permutation_sha256"])
                                row["material_count"] += int(resolved["material_count"])
                            continue
                        reason = (
                            "no_exact_archived_single_option_counterpart"
                            if not matches
                            else "ambiguous_archived_single_option_counterpart"
                        )
                key = (family_name, selected_variation, option_name, role, reason)
                row = unresolved.setdefault(
                    key,
                    {
                        "shader_family": family_name,
                        "selected_variation": selected_variation,
                        "changed_option": option_name,
                        "texture_role": role,
                        "reason": reason,
                        "material_count": 0,
                        "permutations": [],
                    },
                )
                if resolved["permutation_sha256"] not in row["permutations"]:
                    row["permutations"].append(resolved["permutation_sha256"])
                    row["material_count"] += int(resolved["material_count"])

    differentials = sorted(
        planned.values(),
        key=lambda row: (
            row["shader_family"],
            row["selected_variation"],
            row["comparison_variation"],
            row["changed_option"],
        ),
    )
    unresolved_rows = sorted(
        unresolved.values(),
        key=lambda row: (
            row["shader_family"],
            row["selected_variation"],
            row["changed_option"],
            row["reason"],
        ),
    )
    for row in differentials + unresolved_rows:
        row["permutations"].sort()

    report = {
        "schema": PLAN_SCHEMA,
        "source_profile": "pokemon-scarlet-v3.0.1",
        "method": {
            "runtime_execution": False,
            "emulator_used": False,
            "selection_rule": (
                "Only a source texture role with a matching Enable<Role> slot is "
                "eligible. The source choice is changed to its disabled choice; "
                "every other material and metadata-default option remains identical. "
                "The packed words must identify exactly one archived variation."
            ),
            "claim_boundary": (
                "Only exact one-option counterparts are planned. Unavailable or "
                "multi-option comparisons remain unresolved and cannot name a "
                "compiled resource by exclusion."
            ),
        },
        "source_inventory": {
            "identity": inventory_path.name,
            "selected_materials": int(inventory["summary"]["selected_materials"]),
            "material_permutations": int(inventory["summary"]["material_permutations"]),
        },
        "summary": {
            "differential_count": len(differentials),
            "unique_comparison_programs": len(
                {(row["shader_family"], row["comparison_variation"]) for row in differentials}
            ),
            "covered_texture_roles": len({row["texture_role"] for row in differentials}),
            "unresolved_role_checks": len(unresolved_rows),
        },
        "differentials": differentials,
        "unresolved": unresolved_rows,
    }
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(report, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    print(
        "[SvKantoDifferentialPlan] "
        f"exact={len(differentials)} comparisons={report['summary']['unique_comparison_programs']} "
        f"roles={report['summary']['covered_texture_roles']} unresolved={len(unresolved_rows)} "
        f"-> {output}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FileNotFoundError, KeyError, TypeError, ValueError) as error:
        print(f"[SvKantoDifferentialPlan] ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
