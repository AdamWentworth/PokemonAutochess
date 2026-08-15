#!/usr/bin/env python3
"""Select representative one-option edges from the full Z-A shader graph."""

from __future__ import annotations

import argparse
import collections
import json
import pathlib
import sys
from typing import Any

from analyze_sv_kanto_shader_permutations import option_word_count, trailing_zeroes
from plan_sv_kanto_program_differentials import build_variation_lookup


INVENTORY_SCHEMA = "pokemon-autochess-za-kanto-shader-inventory-v1"
PLAN_SCHEMA = "pokemon-autochess-za-kanto-option-graph-plan-v1"
SOURCE_PROFILE = "pokemon-legends-za-v2.0.0"


def read_json(path: pathlib.Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8-sig") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise ValueError(f"Expected a JSON object: {path}")
    return value


def slot_layout(
    metadata: dict[str, Any]
) -> tuple[list[dict[str, Any]], int, int]:
    rows: list[dict[str, Any]] = []
    shader_slots = list(metadata.get("shader_param", []))
    global_slots = list(metadata.get("global_param", []))
    shader_words = option_word_count(shader_slots)
    for scope, slots, word_base in (
        ("shader", shader_slots, 0),
        ("global", global_slots, shader_words),
    ):
        word_index = word_base
        previous_slot = -1
        for slot in slots:
            slot_index = int(slot["slot_index"])
            if previous_slot >= 0 and slot_index <= previous_slot:
                word_index += 1
            previous_slot = slot_index
            rows.append(
                {
                    "scope": scope,
                    "name": str(slot["slot_name"]),
                    "word_index": word_index,
                    "mask": int(slot["offset"]),
                    "values": list(slot["slot_values"]),
                }
            )
    return rows, shader_words, option_word_count(global_slots)


def decode_variations(
    metadata: dict[str, Any], slots: list[dict[str, Any]], stride: int
) -> list[tuple[int, ...]]:
    values = [int(value) for value in metadata.get("param_buffer", [])]
    if not values or len(values) % stride:
        raise ValueError("Metadata variation table has an invalid stride")
    result = []
    for offset in range(0, len(values), stride):
        words = values[offset : offset + stride]
        choices = []
        for slot in slots:
            mask = int(slot["mask"])
            choice = (words[int(slot["word_index"])] & mask) >> trailing_zeroes(mask)
            if choice < 0 or choice >= len(slot["values"]):
                raise ValueError(
                    f"Packed choice exceeds {slot['name']} value table"
                )
            choices.append(choice)
        result.append(tuple(choices))
    return result


def selected_choice_vectors(
    family: dict[str, Any], slots: list[dict[str, Any]]
) -> dict[int, tuple[int, ...]]:
    slot_indices = {
        (str(slot["name"]), str(value["string_value"]).lower()): index
        for slot in slots
        for index, value in enumerate(slot["values"])
    }
    result: dict[int, tuple[int, ...]] = {}
    for resolved in family.get("resolved_permutations", []):
        named_choices = {
            str(row["name"]): str(row["choice"])
            for row in list(resolved.get("shader_options", []))
            + list(resolved.get("global_options", []))
        }
        vector = tuple(
            slot_indices[(slot["name"], named_choices[slot["name"]].lower())]
            for slot in slots
        )
        variation = int(resolved["variation_index"])
        previous = result.setdefault(variation, vector)
        if previous != vector:
            raise ValueError(
                f"Selected variation {variation} has inconsistent effective choices"
            )
    return result


def hamming(left: tuple[int, ...], right: tuple[int, ...]) -> int:
    return sum(a != b for a, b in zip(left, right, strict=True))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inventory", type=pathlib.Path, required=True)
    parser.add_argument("--shader-study", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--representatives-per-transition", type=int, default=3)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.representatives_per_transition <= 0:
        raise ValueError("representatives-per-transition must be positive")
    inventory_path = args.inventory.resolve()
    study_root = args.shader_study.resolve()
    inventory = read_json(inventory_path)
    if inventory.get("schema") != INVENTORY_SCHEMA:
        raise ValueError("Unsupported Z-A Kanto shader inventory")
    if inventory.get("scope", {}).get("source_profile") != SOURCE_PROFILE:
        raise ValueError("Z-A Kanto shader inventory has the wrong source profile")

    planned_rows = []
    family_summaries = []
    for family in inventory.get("families", []):
        family_name = str(family["shader_family"])
        metadata = read_json(
            study_root / str(family["decoded_metadata"]["identity"])
        )
        slots, shader_words, global_words = slot_layout(metadata)
        stride = shader_words + global_words
        vectors = decode_variations(metadata, slots, stride)
        lookup = build_variation_lookup(metadata)
        selected_vectors = selected_choice_vectors(family, slots)
        if not selected_vectors:
            raise ValueError(f"{family_name} has no selected program")
        for variation, vector in selected_vectors.items():
            if vectors[variation] != vector:
                raise ValueError(
                    f"{family_name} selected variation {variation} does not "
                    "match decoded metadata choices"
                )

        transition_edges: dict[
            tuple[int, int, int], list[dict[str, Any]]
        ] = collections.defaultdict(list)
        param_values = [int(value) for value in metadata["param_buffer"]]
        for variation, vector in enumerate(vectors):
            words = tuple(
                param_values[variation * stride : (variation + 1) * stride]
            )
            for slot_index, slot in enumerate(slots):
                selected_choice_index = vector[slot_index]
                for comparison_choice_index in range(len(slot["values"])):
                    if comparison_choice_index == selected_choice_index:
                        continue
                    changed_words = list(words)
                    mask = int(slot["mask"])
                    word_index = int(slot["word_index"])
                    changed_words[word_index] &= ~mask
                    changed_words[word_index] |= (
                        comparison_choice_index << trailing_zeroes(mask)
                    )
                    matches = lookup.get(tuple(changed_words), [])
                    if len(matches) != 1:
                        continue
                    comparison = matches[0]
                    if comparison <= variation:
                        continue
                    lower_choice = min(selected_choice_index, comparison_choice_index)
                    upper_choice = max(selected_choice_index, comparison_choice_index)
                    transition_key = (slot_index, lower_choice, upper_choice)
                    endpoint_is_selected = (
                        variation in selected_vectors
                        or comparison in selected_vectors
                    )
                    distance = min(
                        min(hamming(vector, selected) for selected in selected_vectors.values()),
                        min(
                            hamming(vectors[comparison], selected)
                            for selected in selected_vectors.values()
                        ),
                    )
                    transition_edges[transition_key].append(
                        {
                            "shader_family": family_name,
                            "selected_variation": variation,
                            "comparison_variation": comparison,
                            "option_scope": slot["scope"],
                            "changed_option": slot["name"],
                            "selected_choice": str(
                                slot["values"][selected_choice_index]["string_value"]
                            ),
                            "comparison_choice": str(
                                slot["values"][comparison_choice_index]["string_value"]
                            ),
                            "distance_to_selected_program": distance,
                            "selected_program_endpoint": endpoint_is_selected,
                        }
                    )

        selected_edge_count = 0
        for key in sorted(transition_edges):
            candidates = sorted(
                transition_edges[key],
                key=lambda row: (
                    not row["selected_program_endpoint"],
                    row["distance_to_selected_program"],
                    row["selected_variation"],
                    row["comparison_variation"],
                ),
            )
            selected = candidates[: args.representatives_per_transition]
            for rank, row in enumerate(selected):
                planned_rows.append(
                    {
                        **row,
                        "transition_representative_rank": rank,
                        "available_transition_edges": len(candidates),
                    }
                )
            selected_edge_count += len(selected)
        family_summaries.append(
            {
                "shader_family": family_name,
                "archive_variations": len(vectors),
                "packed_options": len(slots),
                "selected_programs": len(selected_vectors),
                "exact_one_option_edges": sum(
                    len(edges) for edges in transition_edges.values()
                ),
                "covered_option_transitions": len(transition_edges),
                "selected_representative_edges": selected_edge_count,
            }
        )

    planned_rows.sort(
        key=lambda row: (
            row["shader_family"],
            row["option_scope"],
            row["changed_option"],
            row["selected_choice"],
            row["comparison_choice"],
            row["transition_representative_rank"],
        )
    )
    report = {
        "schema": PLAN_SCHEMA,
        "source_profile": SOURCE_PROFILE,
        "runtime_execution": False,
        "emulator_used": False,
        "method": {
            "selection_rule": (
                "Enumerate the complete decoded TRSHA variation graph. An edge "
                "exists only when two archived programs differ in exactly one "
                "packed option choice. Retain up to the requested number of "
                "edges per option transition, preferring a selected-program "
                "endpoint and then minimum Hamming distance to the selected corpus."
            ),
            "claim_boundary": (
                "Edges prove controlled compiled-program comparisons. Repeated "
                "contexts test whether an option's resource delta is stable; "
                "anonymous stable resources still require data-flow evidence."
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
            "differential_count": len(planned_rows),
            "unique_comparison_programs": len(
                {
                    (row["shader_family"], variation)
                    for row in planned_rows
                    for variation in (
                        row["selected_variation"],
                        row["comparison_variation"],
                    )
                }
            ),
            "covered_options": len(
                {
                    (row["shader_family"], row["option_scope"], row["changed_option"])
                    for row in planned_rows
                }
            ),
            "covered_option_transitions": len(
                {
                    (
                        row["shader_family"],
                        row["option_scope"],
                        row["changed_option"],
                        row["selected_choice"],
                        row["comparison_choice"],
                    )
                    for row in planned_rows
                }
            ),
            "representatives_per_transition": args.representatives_per_transition,
        },
        "families": family_summaries,
        "differentials": planned_rows,
    }
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(report, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    print(
        "[ZaKantoOptionGraphPlan] "
        f"edges={len(planned_rows)} "
        f"programs={report['summary']['unique_comparison_programs']} "
        f"options={report['summary']['covered_options']} -> {output}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FileNotFoundError, KeyError, TypeError, ValueError) as error:
        print(f"[ZaKantoOptionGraphPlan] ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
