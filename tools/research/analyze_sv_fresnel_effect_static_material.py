#!/usr/bin/env python3
"""Promote emulator-free SV FresnelEffect data-flow evidence.

The report contains hashes, semantic mappings, equations, and runtime claim
boundaries only. It never copies the retained proprietary shader text.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
from typing import Any


SCHEMA = "pokemon-autochess-sv-fresnel-effect-static-material-evidence-v1"
SOURCE_PROFILE = "pokemon-scarlet-v3.0.1"
MODE = 34
MODELS = {
    "0072_Tentacool_SV": "body_02",
    "0072_Tentacool_SV_Shiny": "body_02",
    "0073_Tentacruel_SV": "body_a_02",
    "0073_Tentacruel_SV_Shiny": "body_a_02",
}
REQUIRED_ROLES = [
    "BaseColorMap",
    "NormalMap",
    "BaseColorMap1",
    "NormalMap1",
    "AOMap",
    "LocalSpecularProbe",
]
FLOAT_PARAMETERS = [
    "BaseColorMapSaturation",
    "NormalHeight",
    "NormalHeight1",
    "Metallic",
    "Roughness",
    "LayerMaskScale1",
    "LocalSpecularProbeIntensity",
    "FresnelAlphaMin",
    "FresnelAlphaMax",
    "FresnelAngleBias",
]


def read_json(path: pathlib.Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def find_program(shader_study: pathlib.Path) -> pathlib.Path:
    candidates = [
        shader_study
        / "selected-programs"
        / "fresnel_effect"
        / "v0000"
        / "v0000.fsh.maxwell.glsl",
        shader_study / "v0000.fsh.maxwell.glsl",
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise ValueError(
        "The retained FresnelEffect variation-0 fragment program is missing")


def require_shader_data_flow(source: str) -> None:
    markers = [
        "texture(fp_t_tcb_1E",
        "texture(fp_t_tcb_C",
        "texture(fp_t_tcb_14",
        "texture(fp_t_tcb_8",
        "texture(fp_t_tcb_1A",
        "textureLod(fp_t_tcb_18",
        "textureLod(fp_t_tcb_34",
        "fp_c8.data[1].x",
        "fp_c8.data[2].x",
        "fp_c8.data[9].x",
        "fp_c8.data[10].x",
        "fp_c7.data[4].z",
        "fp_c7.data[4].w",
        "fp_c7.data[8].y",
        "fp_c7.data[25].x",
        "fp_c7.data[25].y",
        "fp_c7.data[50].z",
        "fp_c7.data[63].w",
        "fp_c7.data[64].x",
        "temp_262 = temp_259 * temp_259",
        "temp_267 = temp_262 * temp_265",
        "temp_270 = fma(temp_259, temp_267, fp_c7.data[25].x)",
    ]
    missing = [marker for marker in markers if marker not in source]
    if missing:
        raise ValueError(f"FresnelEffect data-flow markers changed: {missing}")


def material_row(game_root: pathlib.Path, stem: str, name: str) -> dict[str, Any]:
    manifest_path = game_root / "assets" / "models" / f"{stem}.phmodel"
    manifest = read_json(manifest_path)
    if manifest.get("source", {}).get("profile") != SOURCE_PROFILE:
        raise ValueError(f"{stem} no longer uses {SOURCE_PROFILE}")
    matches = [
        material
        for material in manifest.get("materials", [])
        if material.get("name") == name
        and material.get("shader_family") == "FresnelEffect"
    ]
    if len(matches) != 1:
        raise ValueError(f"Expected one FresnelEffect material {stem}/{name}")
    material = matches[0]
    roles = {str(row.get("role")): row for row in material.get("textures", [])}
    if set(roles) != set(REQUIRED_ROLES):
        raise ValueError(f"{stem}/{name} texture-role set changed: {sorted(roles)}")
    floats = material.get("float_parameters", {})
    vec4 = material.get("vec4_parameters", {})
    missing = [key for key in FLOAT_PARAMETERS if key not in floats]
    if missing or "BaseColor" not in vec4 or "BaseColorLayer1" not in vec4:
        raise ValueError(f"{stem}/{name} parameter schema changed: {missing}")
    if roles["BaseColorMap"].get("srgb") is not True:
        raise ValueError(f"{stem}/{name} primary color map lost sRGB sampling")
    if roles["BaseColorMap1"].get("srgb") is not False:
        raise ValueError(f"{stem}/{name} secondary color map lost linear sampling")
    if roles["LocalSpecularProbe"].get("decoded") is not False:
        raise ValueError(
            f"{stem}/{name} local probe is now decoded; revise the runtime boundary")
    return {
        "stem": stem,
        "material": name,
        "manifest_sha256": sha256(manifest_path),
        "base_color": vec4["BaseColor"],
        "base_color_layer_1": vec4["BaseColorLayer1"],
        "float_parameters": {key: floats[key] for key in FLOAT_PARAMETERS},
        "textures": [
            {
                "role": role,
                "source_sha256": roles[role].get("source_sha256"),
                "decoded": bool(roles[role].get("decoded")),
                "srgb": bool(roles[role].get("srgb")),
            }
            for role in REQUIRED_ROLES
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--game-root", type=pathlib.Path, required=True)
    parser.add_argument("--shader-study", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()

    game_root = args.game_root.resolve()
    program = find_program(args.shader_study.resolve())
    program_source = program.read_text(encoding="utf-8")
    require_shader_data_flow(program_source)

    abi_path = game_root / "docs" / "kanto" / "evidence" / (
        "sv_kanto_selected_program_abi.json")
    shader_inventory_path = game_root / "docs" / "kanto" / "evidence" / (
        "sv_kanto_shader_inventory.json")
    abi = read_json(abi_path)
    programs = [
        row
        for row in abi.get("programs", [])
        if row.get("shader_family") == "FresnelEffect"
        and row.get("variation_index") == 0
    ]
    if len(programs) != 1:
        raise ValueError("Promoted ABI lost FresnelEffect variation 0")
    program_sha = sha256(program)
    if programs[0].get("fragment", {}).get("sha256") != program_sha:
        raise ValueError("Retained program hash differs from the promoted ABI")

    inventory = read_json(shader_inventory_path)
    families = [
        row
        for row in inventory.get("families", [])
        if row.get("shader_family") == "FresnelEffect"
    ]
    if len(families) != 1:
        raise ValueError("Promoted inventory lost FresnelEffect")
    family = families[0]
    materials = [
        material_row(game_root, stem, material)
        for stem, material in MODELS.items()
    ]

    report = {
        "schema": SCHEMA,
        "source_profile": SOURCE_PROFILE,
        "method": {
            "runtime_execution": False,
            "emulator_used": False,
            "evidence_level": "compiled_program_data_flow_plus_manifest_transport",
            "claim_boundary": (
                "The selected program, semantic material-buffer layout, sampler "
                "use sites, fifth-power Fresnel equation, and authored controls "
                "are static evidence. The retained LocalSpecularProbe BNTX cube "
                "is not decoded, so Phlosion uses its shared neutral environment "
                "as a bounded substitute and does not claim source framebuffer parity."
            ),
        },
        "summary": {
            "materials_checked": len(materials),
            "exact_programs": 1,
            "mapped_material_textures": 5,
            "mapped_material_constants": 12,
            "runtime_material_mode": MODE,
            "remaining_undecoded_authored_resources": 1,
        },
        "program": {
            "variation_index": 0,
            "shader_key_hex": "0x59",
            "global_key_hex": "0x0",
            "fragment_identity": program.name,
            "fragment_sha256": program_sha,
            "archive_sha256": family["archive"]["sha256"],
            "metadata_sha256": family["metadata"]["sha256"],
            "abi_report_sha256": sha256(abi_path),
            "inventory_report_sha256": sha256(shader_inventory_path),
        },
        "texture_mappings": [
            {"role": "BaseColorMap", "sampler": "fp_t_tcb_8", "components": "xyzw", "color_space": "srgb"},
            {"role": "NormalMap", "sampler": "fp_t_tcb_C", "components": "xy", "color_space": "linear"},
            {"role": "AOMap", "sampler": "fp_t_tcb_14", "components": "x", "color_space": "linear"},
            {"role": "BaseColorMap1", "sampler": "fp_t_tcb_1A", "components": "xyz", "color_space": "linear"},
            {"role": "NormalMap1", "sampler": "fp_t_tcb_1E", "components": "xy", "color_space": "linear"},
        ],
        "constant_mappings": {
            "UVScaleOffset": "fp_c8.data[1].xyzw",
            "UVScaleOffset1": "fp_c8.data[2].xyzw",
            "BaseColor": "fp_c8.data[9].xyzw",
            "BaseColorLayer1": "fp_c8.data[10].xyzw",
            "NormalHeight": "fp_c7.data[4].z",
            "NormalHeight1": "fp_c7.data[4].w",
            "LayerMaskScale1": "fp_c7.data[8].y",
            "FresnelAlphaMin": "fp_c7.data[25].x",
            "FresnelAlphaMax": "fp_c7.data[25].y",
            "FresnelAngleBias": "fp_c7.data[50].z",
            "BaseColorMapSaturation": "fp_c7.data[63].w",
            "LocalSpecularProbeIntensity": "fp_c7.data[64].x",
        },
        "equations": {
            "primary_color": "saturation_mix(BaseColorMap_srgb * BaseColor, BaseColorMapSaturation)",
            "angle_term": "1 - max(NdotV - FresnelAngleBias, 0)",
            "fresnel_alpha": "mix(FresnelAlphaMin, FresnelAlphaMax, pow(angle_term, 5))",
            "secondary_layer": "BaseColorMap1_linear * BaseColorLayer1 * AOMap * LayerMaskScale1 * (1 - fresnel_alpha)",
            "proven_local_probe_factor": "the fp_t_tcb_18 contribution is multiplied by LocalSpecularProbeIntensity; anonymous source scene/BRDF factors are not claimed",
        },
        "system_resources": {
            "local_specular_probe": "fp_t_tcb_18",
            "normal_direction_diffuse_irradiance": "fp_t_tcb_34",
            "local_probe_status": "retained source identity; cube payload not decoded",
        },
        "runtime_bridge": {
            "mode": MODE,
            "runtime_constant": "kNativeFresnelEffectMaterialMode",
            "primary_map_slot": "base_color_texture_srgb",
            "secondary_map_slot": "emissive_texture_linear",
            "normal_map_slot": "normal_texture_linear",
            "ao_map_slot": "occlusion_texture_linear",
            "quality_policy": "retain foundational maps; vary explicit texture-detail LOD bias",
            "local_probe_substitute": "shared neutral environment",
            "backends": ["opengl", "d3d12", "vulkan"],
        },
        "materials": materials,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(
        "[SvFresnelEffectStaticMaterial] "
        f"materials={len(materials)} program={program_sha[:12]} -> {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
