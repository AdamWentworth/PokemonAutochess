#!/usr/bin/env python3
"""Promote emulator-free Z-A FresnelEffect material evidence."""

from __future__ import annotations

import argparse
import json
import pathlib
from typing import Any

import analyze_sv_fresnel_effect_static_material as common


SCHEMA = "pokemon-autochess-za-fresnel-effect-static-material-evidence-v1"
SOURCE_PROFILE = "pokemon-legends-za-v2.0.0"
MODE = 34
MODELS = {
    "0120_Staryu_ZA": "body_01",
    "0120_Staryu_ZA_Shiny": "body_01",
    "0121_Starmie_ZA": "body_b",
    "0121_Starmie_ZA_Shiny": "body_b",
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
    "EmissionIntensity",
    "EmissionIntensityLayer1",
    "LayerMaskScale1",
    "LocalSpecularProbeIntensity",
    "FresnelAlphaMin",
    "FresnelAlphaMax",
    "FresnelAngleBias",
]


def find_program(shader_study: pathlib.Path) -> pathlib.Path:
    path = (
        shader_study
        / "selected-programs"
        / "fresnel_effect"
        / "v0000"
        / "v0000.fsh.maxwell.glsl"
    )
    if not path.is_file():
        raise ValueError("Retained Z-A FresnelEffect variation 0 is missing")
    return path


def require_shader_data_flow(source: str) -> None:
    markers = [
        "texture(fp_t_tcb_1C",
        "texture(fp_t_tcb_C",
        "texture(fp_t_tcb_14",
        "texture(fp_t_tcb_8",
        "texture(fp_t_tcb_1A",
        "textureLod(fp_t_tcb_18",
        "textureLod(fp_t_tcb_34",
        "temp_260 = 0.0 - fp_c7.data[56].z",
        "temp_265 = 0.0 - fp_c7.data[30].x",
        "temp_266 = fp_c7.data[30].y + temp_265",
        "temp_267 = temp_264 * temp_264",
        "temp_268 = temp_266 * temp_267",
        "temp_269 = temp_267 * temp_268",
        "temp_275 = fma(temp_264, temp_269, fp_c7.data[30].x)",
        "temp_991 = temp_990 * fp_c7.data[5].w",
        "temp_1124 = temp_1118 * fp_c7.data[70].x",
        "temp_1125 = temp_1119 * fp_c7.data[70].x",
        "temp_1126 = temp_1120 * fp_c7.data[70].x",
    ]
    missing = [marker for marker in markers if marker not in source]
    if missing:
        raise ValueError(f"Z-A FresnelEffect data-flow markers changed: {missing}")


def material_row(
        game_root: pathlib.Path,
        stem: str,
        material_name: str) -> dict[str, Any]:
    manifest_path = game_root / "assets" / "models" / f"{stem}.phmodel"
    manifest = common.read_json(manifest_path)
    if manifest.get("source", {}).get("profile") != SOURCE_PROFILE:
        raise ValueError(f"{stem} no longer uses {SOURCE_PROFILE}")
    matches = [
        material
        for material in manifest.get("materials", [])
        if material.get("name") == material_name
        and material.get("shader_family") == "FresnelEffect"
    ]
    if len(matches) != 1:
        raise ValueError(
            f"Expected one FresnelEffect material {stem}/{material_name}")
    material = matches[0]
    roles = {
        str(row.get("role")): row
        for row in material.get("textures", [])
    }
    if set(roles) != set(REQUIRED_ROLES):
        raise ValueError(
            f"{stem}/{material_name} texture-role set changed: {sorted(roles)}")
    floats = material.get("float_parameters", {})
    vec4 = material.get("vec4_parameters", {})
    missing = [name for name in FLOAT_PARAMETERS if name not in floats]
    if missing or "BaseColor" not in vec4 or "BaseColorLayer1" not in vec4:
        raise ValueError(
            f"{stem}/{material_name} parameter schema changed: {missing}")
    probe = common.validate_packed_probe(
        manifest_path, roles["LocalSpecularProbe"])
    return {
        "stem": stem,
        "material": material_name,
        "manifest_sha256": common.sha256(manifest_path),
        "base_color": vec4["BaseColor"],
        "base_color_layer_1": vec4["BaseColorLayer1"],
        "float_parameters": {
            name: floats[name] for name in FLOAT_PARAMETERS
        },
        "local_specular_probe": probe,
        "textures": [
            {
                "role": role,
                "slot": roles[role].get("slot"),
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
        "za_kanto_selected_program_abi.json")
    inventory_path = game_root / "docs" / "kanto" / "evidence" / (
        "za_kanto_shader_inventory.json")
    abi = common.read_json(abi_path)
    selected = [
        row for row in abi.get("programs", [])
        if row.get("shader_family") == "FresnelEffect"
        and row.get("variation_index") == 0
    ]
    if len(selected) != 1:
        raise ValueError("Promoted Z-A ABI lost FresnelEffect variation 0")
    program_sha = common.sha256(program)
    if selected[0].get("fragment", {}).get("sha256") != program_sha:
        raise ValueError("Retained Z-A FresnelEffect program hash changed")

    inventory = common.read_json(inventory_path)
    family_rows = [
        row for row in inventory.get("families", [])
        if row.get("shader_family") == "FresnelEffect"
    ]
    if len(family_rows) != 1:
        raise ValueError("Promoted Z-A inventory lost FresnelEffect")
    family = family_rows[0]
    materials = [
        material_row(game_root, stem, name)
        for stem, name in MODELS.items()
    ]
    loader_path = game_root / "tools" / "PhlosionNativeModelIr.cpp"
    loader_source = loader_path.read_text(encoding="utf-8-sig")
    for marker in (
            "(nativeScarletSource || nativeZaSource)",
            "nativeFresnelEffect",
            "kNativeFresnelEffectMaterialMode",
            '"NormalMap1"',
            '"LocalSpecularProbe"'):
        if marker not in loader_source:
            raise ValueError(
                f"Z-A FresnelEffect runtime bridge lost marker: {marker}")

    report = {
        "schema": SCHEMA,
        "source_profile": SOURCE_PROFILE,
        "method": {
            "runtime_execution": False,
            "emulator_used": False,
            "evidence_level": (
                "compiled_program_data_flow_plus_lossless_probe_transport"),
            "claim_boundary": (
                "The selected program, six authored texture roles, fifth-power "
                "Fresnel alpha, roughness-driven local-probe LOD, probe "
                "intensity, and lossless RGBA16F cube transport are proven. "
                "Anonymous source scene buffers and final framebuffer parity "
                "remain outside this static claim."),
        },
        "summary": {
            "materials_checked": len(materials),
            "exact_programs": 1,
            "mapped_material_textures": 6,
            "mapped_material_constants": 10,
            "runtime_material_mode": MODE,
            "remaining_undecoded_authored_resources": 0,
        },
        "program": {
            "variation_index": 0,
            "shader_key_hex": "0x159",
            "global_key_hex": "0x0",
            "fragment_identity": program.name,
            "fragment_sha256": program_sha,
            "archive_sha256": family["archive"]["sha256"],
            "metadata_sha256": family["metadata"]["sha256"],
            "abi_report_sha256": common.sha256(abi_path),
            "inventory_report_sha256": common.sha256(inventory_path),
            "runtime_loader_sha256": common.sha256(loader_path),
        },
        "texture_mappings": [
            {"role": "BaseColorMap", "sampler": "fp_t_tcb_8", "components": "xyz", "color_space": "srgb"},
            {"role": "NormalMap", "sampler": "fp_t_tcb_C", "components": "xy", "color_space": "linear"},
            {"role": "AOMap", "sampler": "fp_t_tcb_14", "components": "x", "color_space": "linear"},
            {"role": "BaseColorMap1", "sampler": "fp_t_tcb_1A", "components": "xyz", "color_space": "linear"},
            {"role": "NormalMap1", "sampler": "fp_t_tcb_1C", "components": "xy", "color_space": "linear"},
            {"role": "LocalSpecularProbe", "sampler": "fp_t_tcb_18", "components": "xyz", "color_space": "linear_hdr_rgba16f"},
        ],
        "constant_mappings": {
            "UVScaleOffset": "fp_c8.data[1].xyzw",
            "UVScaleOffset1": "fp_c8.data[2].xyzw",
            "BaseColor": "fp_c8.data[9].xyzw",
            "BaseColorLayer1": "fp_c8.data[10].xyzw",
            "NormalHeight": "fp_c7.data[4].z",
            "NormalHeight1": "fp_c7.data[4].w",
            "Roughness": "fp_c7.data[5].w",
            "FresnelAlphaMinMax": "fp_c7.data[30].xy",
            "FresnelAngleBias": "fp_c7.data[56].z",
            "LocalSpecularProbeIntensity": "fp_c7.data[70].x",
        },
        "equations": {
            "angle_term": "1 - max(NdotV - FresnelAngleBias, 0)",
            "fresnel_alpha": (
                "FresnelAlphaMin + (FresnelAlphaMax - FresnelAlphaMin) * "
                "pow(angle_term, 5)"),
            "local_probe_lod": "log2(cube_face_size) * Roughness",
            "proven_local_probe_factor": (
                "the fp_t_tcb_18 RGB contribution is multiplied componentwise "
                "by LocalSpecularProbeIntensity"),
        },
        "system_resources": {
            "local_specular_probe": "fp_t_tcb_18",
            "normal_direction_diffuse_irradiance": "fp_t_tcb_34",
            "local_probe_status": (
                "lossless RGBA16F six-face cube decode; source roughness "
                "selects its LOD (the retained probes contain one authored mip)"),
        },
        "runtime_bridge": {
            "mode": MODE,
            "runtime_constant": "kNativeFresnelEffectMaterialMode",
            "primary_map_slot": "base_color_texture_srgb",
            "secondary_map_slot": "emissive_texture_linear",
            "primary_normal_map_slot": "normal_texture_linear",
            "secondary_normal_map_slot": (
                "metallic_roughness_texture_linear_repurposed_in_mode_34"),
            "ao_map_slot": "occlusion_texture_linear",
            "local_probe_slot": (
                "environment_texture_linear_packed_rgba16f_cube"),
            "backends": ["opengl", "d3d12", "vulkan"],
        },
        "remaining_static_gaps": [
            "anonymous scene-buffer values and projected shadow resources",
            "source exposure, tone mapping, and final framebuffer transfer",
            "complete BRDF and refraction equation order outside the proven subgraphs",
        ],
        "materials": materials,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(
        "[ZaFresnelEffectStaticMaterial] "
        f"materials={len(materials)} program={program_sha[:12]} -> {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
