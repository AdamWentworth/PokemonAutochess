#!/usr/bin/env python3
"""Inventory and resolve selected Kanto Legends: Z-A material programs.

The selected model and native-material traversal deliberately reuses the
well-tested Scarlet/Violet inventory implementation. Only the source profile,
registry, evidence schemas, and conclusions differ. Proprietary shader bytes
remain in the private study directory; promoted reports retain identities,
hashes, packed keys, and exact variation selections only.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys

import analyze_sv_kanto_shader_permutations as trinity


SOURCE_PROFILE = "pokemon-legends-za-v2.0.0"
REPORT_SCHEMA = "pokemon-autochess-za-kanto-shader-inventory-v1"
REGISTRY_SCHEMA = "pokemon-autochess-za-shader-source-registry-v1"
EVIDENCE_SCHEMA = "pokemon-autochess-za-kanto-shader-evidence-v1"
CENSUS_SCHEMA = "pokemon-autochess-za-kanto-material-census-v1"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--game-root",
        type=pathlib.Path,
        default=pathlib.Path(__file__).resolve().parents[2],
    )
    parser.add_argument("--registry", type=pathlib.Path)
    parser.add_argument("--shader-study", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    parser.add_argument("--evidence-output", type=pathlib.Path)
    parser.add_argument("--census-output", type=pathlib.Path)
    parser.add_argument("--require-complete-source", action="store_true")
    parser.add_argument("--require-exact-resolution", action="store_true")
    return parser.parse_args()


def promoted_evidence(report: dict) -> dict:
    result = trinity.build_promoted_evidence(report)
    result["schema"] = EVIDENCE_SCHEMA
    result["conclusions"] = [
        (
            "Every selected Legends: Z-A Kanto material permutation maps to "
            "exactly one retained source BNSH variation."
        ),
        (
            "This proves program identity and packed option selection; it does "
            "not by itself prove every sampled resource, constant-buffer field, "
            "lighting input, blend state, or final-color equation."
        ),
    ]
    return result


def material_census(report: dict) -> dict:
    result = trinity.build_material_census(report)
    result["schema"] = CENSUS_SCHEMA
    return result


def write_json(path: pathlib.Path, value: dict) -> None:
    output = path.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(value, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def main() -> int:
    args = parse_args()
    game_root = args.game_root.resolve()
    registry_path = (
        args.registry.resolve()
        if args.registry
        else pathlib.Path(__file__).with_name("za_kanto_shader_families.json")
    )
    registry = trinity.read_json(registry_path)
    if (
        registry.get("schema") != REGISTRY_SCHEMA
        or registry.get("source_profile") != SOURCE_PROFILE
    ):
        raise ValueError("Unsupported Legends: Z-A shader source registry")
    study_root = args.shader_study.resolve() if args.shader_study else None
    if study_root is not None and not study_root.is_dir():
        raise FileNotFoundError(study_root)

    # The shared traversal keys its source selection and manifest validation
    # from this module constant. Point it at Z-A before invoking any traversal.
    trinity.SOURCE_PROFILE = SOURCE_PROFILE
    selections = trinity.selected_sv_models(game_root)
    permutations, summary = trinity.build_permutations(game_root, selections)
    sources, extraction_queue, coverage = trinity.build_shader_sources(
        registry, permutations, study_root
    )
    summary.update(coverage)
    summary["source_families_total"] = len(sources)
    summary["unresolved_permutations"] = (
        summary["material_permutations"]
        - coverage["exactly_resolved_permutations"]
    )
    summary["unresolved_materials"] = (
        summary["selected_materials"] - coverage["exactly_resolved_materials"]
    )

    report = {
        "schema": REPORT_SCHEMA,
        "scope": {
            "source_profile": SOURCE_PROFILE,
            "pokedex_max": 151,
            "selection_authority": "config/assets/asset_catalog.json",
            "material_authority": "selected phlosion-native-model-ir-v1 manifests",
        },
        "method": {
            "runtime_execution": False,
            "emulator_used": False,
            "selection_rule": (
                "Material and global option names present in Trinity metadata "
                "use the retained material value; omitted options use metadata "
                "bool1 as the source default choice index. Packed shader/global "
                "keys must map to exactly one param_buffer variation."
            ),
            "permutation_rule": (
                "SHA-256 of shader family, transparency, sorted shader option "
                "name/value pairs, and sorted source texture role/slot pairs."
            ),
        },
        "registry": {
            "identity": registry_path.name,
            "sha256": trinity.sha256_file(registry_path),
            "source_index": registry.get("source_index"),
        },
        "summary": summary,
        "families": sources,
        "material_permutations": permutations,
        "extraction_queue": extraction_queue,
    }

    if (
        args.require_complete_source
        and coverage["source_families_staged"] != len(sources)
    ):
        raise RuntimeError(
            f"Only {coverage['source_families_staged']} of {len(sources)} "
            "source families are staged"
        )
    if args.require_exact_resolution and summary["unresolved_permutations"]:
        raise RuntimeError(
            f"{summary['unresolved_permutations']} material permutations "
            "remain unresolved"
        )

    if args.output:
        write_json(args.output, report)
        print(f"[ZaKantoShaderInventory] wrote {args.output.resolve()}")
    else:
        sys.stdout.write(json.dumps(report, indent=2, ensure_ascii=False) + "\n")
    if args.evidence_output:
        if (
            summary["unresolved_permutations"]
            or coverage["source_families_staged"] != len(sources)
        ):
            raise RuntimeError(
                "Refusing to promote incomplete Z-A Kanto shader evidence"
            )
        write_json(args.evidence_output, promoted_evidence(report))
        print(
            "[ZaKantoShaderInventory] wrote evidence "
            f"{args.evidence_output.resolve()}"
        )
    if args.census_output:
        write_json(args.census_output, material_census(report))
        print(
            "[ZaKantoShaderInventory] wrote census "
            f"{args.census_output.resolve()}"
        )
    print(
        "[ZaKantoShaderInventory] "
        f"models={summary['selected_models']} "
        f"materials={summary['selected_materials']} "
        f"permutations={summary['material_permutations']} "
        f"resolved={summary['exactly_resolved_permutations']} "
        f"families={summary['source_families_staged']}/"
        f"{summary['source_families_total']}",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FileNotFoundError, RuntimeError, ValueError) as error:
        print(f"[ZaKantoShaderInventory] ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
