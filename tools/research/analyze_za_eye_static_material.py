#!/usr/bin/env python3
"""Audit the retained Kanto Z-A dedicated Eye shader materials."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
from typing import Any


SCHEMA = "pokemon-autochess-za-eye-static-material-evidence-v1"
SOURCE_PROFILE = "pokemon-legends-za-v2.0.0"
MODELS = {
    "0014_Kakuna_ZA": {"l_eye", "r_eye"},
    "0014_Kakuna_ZA_Shiny": {"l_eye", "r_eye"},
    "0015_Beedrill_ZA": {"l_eye", "r_eye"},
    "0015_Beedrill_ZA_Shiny": {"l_eye", "r_eye"},
}
ROLE_MAPPINGS = {
    "BaseColorMap": "fp_t_tcb_8",
    "LayerMaskMap": "fp_t_tcb_16",
    "NormalMap": "fp_t_tcb_C",
    "HighlightMaskMap": "fp_t_tcb_1E",
}


def read_json(path: pathlib.Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--game-root", type=pathlib.Path, required=True)
    parser.add_argument("--shader-study", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    game_root = args.game_root.resolve()
    shader_study = args.shader_study.resolve()

    program_path = (
        shader_study / "selected-programs" / "eye" / "v0146" /
        "v0146.fsh.maxwell.glsl")
    source = program_path.read_text(encoding="utf-8")
    for marker in (
            "texture(fp_t_tcb_8", "texture(fp_t_tcb_16",
            "texture(fp_t_tcb_C", "texture(fp_t_tcb_1E"):
        if marker not in source:
            raise ValueError(f"Z-A Eye variation 146 lost marker: {marker}")

    material_rows: list[dict[str, Any]] = []
    for stem, expected_names in MODELS.items():
        path = game_root / "assets" / "models" / f"{stem}.phmodel"
        manifest = read_json(path)
        if manifest.get("source", {}).get("profile") != SOURCE_PROFILE:
            raise ValueError(f"{stem} source profile changed")
        materials = [
            row for row in manifest.get("materials", [])
            if row.get("shader_family") == "Eye"
        ]
        if {str(row.get("name")) for row in materials} != expected_names:
            raise ValueError(f"{stem} dedicated Eye material set changed")
        for material in materials:
            textures = {
                str(row.get("role")): row
                for row in material.get("textures", [])
            }
            if set(textures) != set(ROLE_MAPPINGS):
                raise ValueError(
                    f"{stem}/{material.get('name')} Eye roles changed")
            if any(row.get("decoded") is not True for row in textures.values()):
                raise ValueError(
                    f"{stem}/{material.get('name')} Eye texture is undecoded")
            options = material.get("shader_options", {})
            required_options = {
                "EnableNormalMap": "True",
                "EnableParallaxMap": "False",
                "EnableHighlight": "True",
                "EyelidType": "None",
                "NumMaterialLayer": "5",
            }
            if any(options.get(key) != value
                   for key, value in required_options.items()):
                raise ValueError(
                    f"{stem}/{material.get('name')} Eye option key changed")
            material_rows.append({
                "stem": stem,
                "material": material["name"],
                "manifest_sha256": sha256(path),
                "texture_source_sha256": {
                    role: textures[role].get("source_sha256")
                    for role in ROLE_MAPPINGS
                },
            })
    if len(material_rows) != 8:
        raise ValueError("Retained dedicated Eye material count changed")

    abi_path = (
        game_root / "docs" / "kanto" / "evidence" /
        "za_kanto_selected_program_abi.json")
    graph_path = (
        game_root / "docs" / "kanto" / "evidence" /
        "za_kanto_option_graph.json")
    abi = read_json(abi_path)
    selected = [
        row for row in abi.get("programs", [])
        if row.get("shader_family") == "Eye"
        and row.get("variation_index") == 146
    ]
    if len(selected) != 1 or selected[0].get("material_count") != 8:
        raise ValueError("Promoted ABI lost Eye variation 146")
    if selected[0]["fragment"].get("sha256") != sha256(program_path):
        raise ValueError("Retained Eye program differs from promoted ABI")
    graph = read_json(graph_path)
    eye_edges = [
        row for row in graph.get("differentials", [])
        if row.get("shader_family") == "Eye"
    ]
    if not eye_edges or graph.get("summary", {}).get(
            "unresolved_option_choices") != 0:
        raise ValueError("Promoted exact Eye option graph is incomplete")

    loader_path = game_root / "tools" / "PhlosionNativeModelIr.cpp"
    loader = loader_path.read_text(encoding="utf-8-sig")
    for token in (
            "bakeEyeHighlightEmission", "bakeLayeredBaseColor",
            "bakeLayeredNormal", "EnableHighlight"):
        if token not in loader:
            raise ValueError(f"Eye runtime bridge lost token: {token}")

    report = {
        "schema": SCHEMA,
        "source_profile": SOURCE_PROFILE,
        "method": {
            "runtime_execution": False,
            "emulator_used": False,
            "evidence_level": (
                "exact_material_options_plus_compiled_program_abi_plus_"
                "complete_option_graph"),
            "claim_boundary": (
                "The retained dedicated Eye materials, selected variation, "
                "sampler roles, layer/normal/highlight transport, and option "
                "graph are proven. Anonymous source lighting buffers and the "
                "complete optical/final-frame equation are not claimed."),
        },
        "summary": {
            "models": len(MODELS),
            "materials": len(material_rows),
            "selected_variation": 146,
            "mapped_texture_roles": len(ROLE_MAPPINGS),
            "eye_option_graph_edges": len(eye_edges),
            "undecoded_authored_textures": 0,
        },
        "selected_options": {
            "EnableNormalMap": "True",
            "EnableParallaxMap": "False",
            "EnableHighlight": "True",
            "EyelidType": "None",
            "NumMaterialLayer": "5",
        },
        "texture_mappings": [
            {"role": role, "sampler": sampler, "stage": "fragment"}
            for role, sampler in ROLE_MAPPINGS.items()
        ],
        "runtime_bridge": {
            "base_and_layer_color": "ordered layer-mask bake",
            "normal": "authored normal map and source scale",
            "highlight": "authored highlight mask and layer-5 emission controls",
            "expression_motion": (
                "source skeletal and UV animation remains authoritative; no "
                "manual Inspector eye-state override is required"),
        },
        "remaining_static_gaps": [
            "anonymous scene-light, projected-shadow, and environment resources",
            "complete source eye BRDF and final framebuffer transfer",
        ],
        "source_sha256": {
            "fragment_program": sha256(program_path),
            "selected_program_abi": sha256(abi_path),
            "option_graph": sha256(graph_path),
            "game_loader": sha256(loader_path),
        },
        "materials": material_rows,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report["summary"], indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
