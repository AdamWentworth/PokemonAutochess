#!/usr/bin/env python3
"""Promote emulator-free Z-A scene/color boundary evidence.

This analyzer deliberately separates equations recoverable from compiled
programs from values owned by the missing source runtime. It records hashes,
operation identities, and cross-family coverage without promoting proprietary
shader text from the private study directory.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import sys
from typing import Any

import analyze_za_ik_character_dataflow as flow


SCHEMA = "pokemon-autochess-za-scene-color-boundary-evidence-v1"
SOURCE_PROFILE = "pokemon-legends-za-v2.0.0"
SELECTED_MANIFEST_SCHEMA = "pokemon-autochess-private-za-selected-programs-v1"
OPTION_DATAFLOW_SCHEMA = "pokemon-autochess-za-kanto-option-dataflow-evidence-v1"


def read_json(path: pathlib.Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(value, dict):
        raise ValueError(f"Expected a JSON object: {path}")
    return value


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require_fragments(source: str, fragments: list[str], label: str) -> None:
    missing = [fragment for fragment in fragments if fragment not in source]
    if missing:
        raise ValueError(f"{label} operation signature changed: {missing}")


def final_scene_fade(source: str, label: str) -> dict[str, Any]:
    report = flow.final_scene_fade_boundary(source, label)
    return {
        "operation": report["operation"],
        "proof": report["proof"],
        "boundary": report["boundary"],
    }


def camera_relative_vector(source: str, label: str) -> dict[str, Any]:
    pattern = re.compile(
        r"temp_(\d+) = 0\.0 - temp_(\d+);\s*"
        r"temp_(\d+) = temp_\1 \+ fp_c5\.data\[19\]\.([xyz]);")
    channels = {channel for _, _, _, channel in pattern.findall(source)}
    if channels != {"x", "y", "z"}:
        raise ValueError(f"{label} camera-relative vector changed: {channels}")
    require_fragments(
        source,
        ["fp_c5.data[19].x", "fp_c5.data[19].y", "fp_c5.data[19].z",
         "inversesqrt"],
        label,
    )
    return {
        "field": "fp_c5[19].xyz",
        "operation": (
            "subtract interpolated world-position xyz, normalize the result, "
            "and consume it in view/reflection lighting paths"),
        "proof": "cross_program_compiled_operation_identity",
        "semantic_classification": "camera_world_position",
        "classification_strength": (
            "strong structural inference; stripped reflection omits the "
            "source field name"),
    }


def material_program_rows(study_root: pathlib.Path) -> list[dict[str, Any]]:
    selected_root = study_root / "selected-programs"
    manifest_path = selected_root / "selected_programs_manifest.json"
    manifest = read_json(manifest_path)
    if (manifest.get("schema") != SELECTED_MANIFEST_SCHEMA or
            manifest.get("source_profile") != SOURCE_PROFILE):
        raise ValueError("Unsupported private Z-A selected-program manifest")

    rows: list[dict[str, Any]] = []
    selected = [
        row for row in manifest.get("programs", [])
        if (row.get("shader_family") == "IkCharacter" or
            row.get("shader_family") == "FresnelEffect")
    ]
    for record in selected:
        relative = pathlib.PurePosixPath(str(record["directory"]))
        variation = int(record["variation_index"])
        path = selected_root.joinpath(*relative.parts) / (
            f"v{variation:04d}.fsh.maxwell.glsl")
        expected = str(record["fragment_glsl_sha256"])
        if sha256(path) != expected:
            raise ValueError(f"Selected program hash changed: {path}")
        source = path.read_text(encoding="utf-8-sig")
        rows.append({
            "shader_family": str(record["shader_family"]),
            "variation_index": variation,
            "role": "selected_kanto_material_program",
            "fragment_sha256": expected,
            "final_scene_fade": final_scene_fade(
                source, f"{record['shader_family']} variation {variation}"),
            "camera_relative_vector": camera_relative_vector(
                source, f"{record['shader_family']} variation {variation}"),
        })

    adjacent = [
        ("Hair", "hair"),
        ("IkStandard", "ik_standard"),
    ]
    for family, directory in adjacent:
        path = (study_root / "adjacent-programs" / directory / "v0000" /
                "v0000.fsh.maxwell.glsl")
        source = path.read_text(encoding="utf-8-sig")
        rows.append({
            "shader_family": family,
            "variation_index": 0,
            "role": "adjacent_cross_family_control",
            "fragment_sha256": sha256(path),
            "final_scene_fade": final_scene_fade(source, family),
            "camera_relative_vector": camera_relative_vector(source, family),
        })
    if len(rows) != 7:
        raise ValueError(f"Z-A material scene-boundary coverage changed: {len(rows)}")
    return rows


def receive_shadow_boundary(game_root: pathlib.Path) -> dict[str, Any]:
    path = game_root / "docs" / "kanto" / "evidence" / (
        "za_kanto_option_dataflow.json")
    report = read_json(path)
    if report.get("schema") != OPTION_DATAFLOW_SCHEMA:
        raise ValueError("Unsupported promoted Z-A option-dataflow report")
    rows = [
        row for row in report.get("differentials", [])
        if row.get("shader_family") == "IkCharacter" and
        row.get("changed_option") == "ReceiveShadow"
    ]
    if len(rows) != 3 or any(
            row.get("fragment", {}).get("classification") !=
            "identical_compiled_output_slice" for row in rows):
        raise ValueError("ReceiveShadow no longer has three identical fragments")
    return {
        "one_option_edges": len(rows),
        "selected_variations": sorted(
            int(row["selected_variation"]) for row in rows),
        "comparison_variations": sorted(
            int(row["comparison_variation"]) for row in rows),
        "fragment_classification": "identical_compiled_output_slice",
        "conclusion": (
            "ReceiveShadow is not a material-program equation switch in these "
            "selected permutations; its effect is supplied by shared scene "
            "state or draw setup."),
        "proof": "complete_exact_one_option_graph",
    }


def tonemap_boundary(study_root: pathlib.Path) -> dict[str, Any]:
    directory = study_root / "adjacent-programs" / "post_effects_tonemap" / "v0000"
    path = directory / "v0000.fsh.maxwell.glsl"
    descriptor_path = directory / "v0000.json"
    descriptor = read_json(descriptor_path)
    if (int(descriptor.get("variation_index", -1)) != 0 or
            int(descriptor.get("fragment_bytes", -1)) != 1792):
        raise ValueError("Z-A tone-map program descriptor changed")
    source = path.read_text(encoding="utf-8-sig")
    require_fragments(source, [
        "uniform sampler2D fp_t_tcb_8;",
        "uniform sampler3D fp_t_tcb_A;",
        "temp_31 = fp_c3.data[114].w * 1.44269502;",
        "temp_51 = exp2(temp_31);",
        "temp_66 = fma(temp_63, fp_c1.data[0].z, 0.0479959995);",
        "temp_72 = fma(temp_68, fp_c1.data[0].w, 0.386036009);",
        "temp_82 = texture(fp_t_tcb_A, vec3(temp_79, temp_80, temp_81),",
        "temp_101 = temp_83 * 12.9200001;",
        "temp_111 = fma(temp_109, fp_c1.data[1].y, -0.0549999997);",
        "temp_134 = fma(temp_127, fp_c3.data[25].x, temp_131);",
        "temp_137 = fma(temp_130, fp_c3.data[26].x, temp_134);",
        "temp_140 = temp_137 + fp_c3.data[27].x;",
    ], "PostEffectsToneMap variation 0")
    return {
        "shader_family": "PostEffectsToneMap",
        "variation_index": 0,
        "fragment_sha256": sha256(path),
        "descriptor_sha256": sha256(descriptor_path),
        "input_roles": {
            "fp_t_tcb_8": "rendered_scene_color",
            "fp_t_tcb_A": "three_dimensional_color_lut",
        },
        "ordered_operations": [
            "sample rendered scene color",
            "apply screen-position-dependent source color blend",
            "apply exponential exposure from fp_c3[114].w",
            "encode and inset logarithmic three-dimensional LUT coordinates",
            "sample the three-dimensional color LUT",
            "apply the piecewise sRGB output transfer",
            "apply a final three-by-three color transform and RGB offset",
        ],
        "exact_equations": {
            "exposure_multiplier": "exp(fp_c3[114].w)",
            "output_transfer": "piecewise_srgb_oetf",
            "final_transform": "mat3(fp_c3[24..26].rgb) * rgb + fp_c3[27].rgb",
        },
        "proof": "compiled_operation_identity",
        "unavailable_runtime_values": [
            "fp_c3[114].w exposure",
            "screen-position blend parameters fp_c3[50..52]",
            "the bound three-dimensional color LUT payload",
            "final color-transform rows fp_c3[24..27]",
            "render-target format and presentation state",
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-root", type=pathlib.Path, required=True)
    parser.add_argument("--shader-study", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    game_root = args.game_root.resolve()
    study_root = args.shader_study.resolve()
    program_rows = material_program_rows(study_root)
    receive_shadow = receive_shadow_boundary(game_root)
    tonemap = tonemap_boundary(study_root)
    report = {
        "schema": SCHEMA,
        "source_profile": SOURCE_PROFILE,
        "method": {
            "runtime_execution": False,
            "emulator_used": False,
            "evidence_level": (
                "cross_family_compiled_operation_identity_plus_complete_"
                "one_option_graph"),
            "claim_boundary": (
                "This proves scene-facing equation boundaries and final color "
                "operation order. It does not recover values supplied by the "
                "source runtime, its bound LUT, or final presentation state."),
        },
        "summary": {
            "material_fragment_programs": len(program_rows),
            "material_shader_families": len({
                row["shader_family"] for row in program_rows}),
            "programs_with_exact_final_scene_fade": sum(
                "final_scene_fade" in row for row in program_rows),
            "programs_with_camera_position_classification": sum(
                "camera_relative_vector" in row for row in program_rows),
            "receive_shadow_identical_fragment_edges": receive_shadow[
                "one_option_edges"],
            "tonemap_programs": 1,
            "runtime_changes_authorized_by_this_report": 1,
        },
        "shared_scene_fields": {
            "camera_world_position": "fp_c5[19].xyz",
            "final_scene_color": "fp_c10[12].rgb",
            "final_scene_fade": "fp_c10[12].w",
        },
        "material_programs": program_rows,
        "receive_shadow": receive_shadow,
        "post_effect_tonemap": tonemap,
        "implementation_decision": {
            "authorized": (
                "replace Phlosion's power-law Z-A rim domain with the exact "
                "compiled smoothstep plus symmetric contrast remap"),
            "kept_neutral": [
                "ReceiveShadow scene state",
                "fp_c10[12] final scene fade",
                "source exposure",
                "source three-dimensional color LUT",
                "source final color matrix and framebuffer state",
            ],
        },
        "source_sha256": {
            "selected_program_manifest": sha256(
                study_root / "selected-programs" /
                "selected_programs_manifest.json"),
            "promoted_option_dataflow": sha256(
                game_root / "docs" / "kanto" / "evidence" /
                "za_kanto_option_dataflow.json"),
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
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
