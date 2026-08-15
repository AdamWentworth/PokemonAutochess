#!/usr/bin/env python3
"""Plan exact one-option counterparts for selected Z-A Kanto programs.

Unlike the texture-role planner, this walks every packed shader and global
option represented by each selected program.  It changes exactly one choice,
keeps every other effective source choice identical, and accepts the result
only when the Z-A metadata table identifies one archived variation.
"""

from __future__ import annotations

import argparse
import collections
import json
import pathlib
import sys
from typing import Any

from analyze_sv_kanto_shader_permutations import encode_options, option_word_count
from plan_sv_kanto_program_differentials import build_variation_lookup


INVENTORY_SCHEMA = "pokemon-autochess-za-kanto-shader-inventory-v1"
PLAN_SCHEMA = "pokemon-autochess-za-kanto-option-differential-plan-v1"
SOURCE_PROFILE = "pokemon-legends-za-v2.0.0"


def read_json(path: pathlib.Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8-sig") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise ValueError(f"Expected a JSON object: {path}")
    return value


def effective_choices(resolved: dict[str, Any]) -> dict[str, str]:
    rows = list(resolved.get("shader_options", [])) + list(
        resolved.get("global_options", [])
    )
    choices = {str(row["name"]): str(row["choice"]) for row in rows}
    if len(choices) != len(rows):
        raise ValueError("Resolved source options contain duplicate names")
    return choices


def packed_words(
    choices: dict[str, str], metadata: dict[str, Any]
) -> tuple[int, ...]:
    shader_keys, _ = encode_options(choices, metadata.get("shader_param", []))
    global_keys, _ = encode_options(choices, metadata.get("global_param", []))
    return tuple(shader_keys + global_keys)


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
        raise ValueError("Unsupported Z-A Kanto shader inventory")
    if inventory.get("scope", {}).get("source_profile") != SOURCE_PROFILE:
        raise ValueError("Z-A Kanto shader inventory has the wrong source profile")
    if int(inventory.get("summary", {}).get("unresolved_permutations", -1)) != 0:
        raise ValueError("Z-A Kanto shader inventory is not exactly resolved")

    planned: dict[tuple[str, int, int, str, str], dict[str, Any]] = {}
    unresolved: dict[tuple[str, int, str, str, str], dict[str, Any]] = {}
    selected_program_choices: dict[tuple[str, int], dict[str, str]] = {}
    selected_program_materials: collections.Counter[tuple[str, int]] = (
        collections.Counter()
    )
    selected_program_permutations: dict[tuple[str, int], set[str]] = (
        collections.defaultdict(set)
    )

    for family in inventory.get("families", []):
        family_name = str(family["shader_family"])
        metadata_identity = str(family["decoded_metadata"]["identity"])
        metadata = read_json(study_root / metadata_identity)
        lookup = build_variation_lookup(metadata)
        shader_slots = list(metadata.get("shader_param", []))
        global_slots = list(metadata.get("global_param", []))
        expected_stride = option_word_count(shader_slots) + option_word_count(
            global_slots
        )
        if not lookup or any(len(words) != expected_stride for words in lookup):
            raise ValueError(f"{family_name} variation table has an invalid ABI")

        for resolved in family.get("resolved_permutations", []):
            selected_variation = int(resolved["variation_index"])
            program_key = (family_name, selected_variation)
            choices = effective_choices(resolved)
            prior = selected_program_choices.setdefault(program_key, choices)
            if prior != choices:
                raise ValueError(
                    f"{family_name} variation {selected_variation} resolved from "
                    "more than one effective option set"
                )
            selected_program_materials[program_key] += int(resolved["material_count"])
            selected_program_permutations[program_key].add(
                str(resolved["permutation_sha256"])
            )

        slots = [("shader", slot) for slot in shader_slots] + [
            ("global", slot) for slot in global_slots
        ]
        for program_key, choices in sorted(selected_program_choices.items()):
            if program_key[0] != family_name:
                continue
            selected_variation = program_key[1]
            selected_words = packed_words(choices, metadata)
            selected_matches = lookup.get(selected_words, [])
            if selected_matches != [selected_variation]:
                raise ValueError(
                    f"{family_name} selected variation {selected_variation} "
                    "does not round-trip through its effective choices"
                )
            for option_scope, slot in slots:
                option_name = str(slot["slot_name"])
                selected_choice = choices[option_name]
                for value in slot.get("slot_values", []):
                    comparison_choice = str(value["string_value"])
                    if comparison_choice.lower() == selected_choice.lower():
                        continue
                    comparison_choices = dict(choices)
                    comparison_choices[option_name] = comparison_choice
                    matches = lookup.get(packed_words(comparison_choices, metadata), [])
                    if len(matches) == 1:
                        comparison_variation = matches[0]
                        if comparison_variation == selected_variation:
                            raise ValueError(
                                f"{family_name} {option_name} changed choice but "
                                "retained the selected variation"
                            )
                        key = (
                            family_name,
                            selected_variation,
                            comparison_variation,
                            option_scope,
                            option_name,
                        )
                        planned[key] = {
                            "shader_family": family_name,
                            "selected_variation": selected_variation,
                            "comparison_variation": comparison_variation,
                            "option_scope": option_scope,
                            "changed_option": option_name,
                            "selected_choice": selected_choice,
                            "comparison_choice": comparison_choice,
                            "selected_material_count": int(
                                selected_program_materials[program_key]
                            ),
                            "selected_permutations": sorted(
                                selected_program_permutations[program_key]
                            ),
                        }
                        continue
                    reason = (
                        "no_exact_archived_single_option_counterpart"
                        if not matches
                        else "ambiguous_archived_single_option_counterpart"
                    )
                    unresolved[
                        (
                            family_name,
                            selected_variation,
                            option_scope,
                            option_name,
                            comparison_choice,
                        )
                    ] = {
                        "shader_family": family_name,
                        "selected_variation": selected_variation,
                        "option_scope": option_scope,
                        "changed_option": option_name,
                        "selected_choice": selected_choice,
                        "comparison_choice": comparison_choice,
                        "reason": reason,
                        "selected_material_count": int(
                            selected_program_materials[program_key]
                        ),
                    }

    differentials = sorted(
        planned.values(),
        key=lambda row: (
            row["shader_family"],
            row["selected_variation"],
            row["option_scope"],
            row["changed_option"],
            row["comparison_choice"],
        ),
    )
    unresolved_rows = sorted(
        unresolved.values(),
        key=lambda row: (
            row["shader_family"],
            row["selected_variation"],
            row["option_scope"],
            row["changed_option"],
            row["comparison_choice"],
        ),
    )
    report = {
        "schema": PLAN_SCHEMA,
        "source_profile": SOURCE_PROFILE,
        "method": {
            "runtime_execution": False,
            "emulator_used": False,
            "selection_rule": (
                "For each selected compiled program, change exactly one effective "
                "TRSHA shader/global option choice and keep every other effective "
                "choice identical. Accept only a packed key that identifies one "
                "archived BNSH variation."
            ),
            "claim_boundary": (
                "A planned pair proves a one-option compiled-program comparison. "
                "It does not by itself identify anonymous resources or prove the "
                "runtime values supplied through constant buffers."
            ),
        },
        "source_inventory": {
            "identity": inventory_path.name,
            "selected_materials": int(inventory["summary"]["selected_materials"]),
            "selected_programs": int(
                inventory["summary"]["unique_selected_programs"]
            ),
        },
        "summary": {
            "differential_count": len(differentials),
            "unique_comparison_programs": len(
                {
                    (row["shader_family"], row["comparison_variation"])
                    for row in differentials
                }
            ),
            "covered_selected_programs": len(
                {
                    (row["shader_family"], row["selected_variation"])
                    for row in differentials
                }
            ),
            "covered_options": len(
                {
                    (row["shader_family"], row["option_scope"], row["changed_option"])
                    for row in differentials
                }
            ),
            "unresolved_option_choices": len(unresolved_rows),
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
        "[ZaKantoOptionDifferentialPlan] "
        f"exact={len(differentials)} "
        f"comparisons={report['summary']['unique_comparison_programs']} "
        f"options={report['summary']['covered_options']} "
        f"unresolved={len(unresolved_rows)} -> {output}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FileNotFoundError, KeyError, TypeError, ValueError) as error:
        print(f"[ZaKantoOptionDifferentialPlan] ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
