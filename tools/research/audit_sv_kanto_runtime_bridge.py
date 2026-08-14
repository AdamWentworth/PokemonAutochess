#!/usr/bin/env python3
"""Audit source-proven SV Kanto texture roles against Phlosion transport."""

from __future__ import annotations

import argparse
import collections
import json
import pathlib
from typing import Any

import analyze_sv_kanto_shader_permutations as inventory


SCHEMA = "pokemon-autochess-sv-kanto-runtime-bridge-audit-v1"
SOURCE_PROFILE = "pokemon-scarlet-v3.0.1"
RUNTIME_KEYS = {
    "RoughnessMap": "roughness_texture",
    "MetallicMap": "metallic_texture",
    "NormalMap": "normal_texture",
    "EmissionColorMap": "emissive_texture",
}
SSS_ROLES = {
    "BaseColorMap",
    "NormalMap",
    "RoughnessMap",
    "AOMap",
    "SSSMaskMap",
}


def audit_eye_clear_coat_normal_bridge(
    game_root: pathlib.Path,
    selected_program_abi: dict[str, Any],
    eye_static_evidence: dict[str, Any],
) -> dict[str, Any]:
    evidence_rows = [
        row
        for row in eye_static_evidence.get("compiled_permutation_evidence", [])
        if row.get("family") == "EyeClearCoat"
    ]
    if len(evidence_rows) != 1:
        raise ValueError("Expected one promoted EyeClearCoat static-evidence row")
    mapping = evidence_rows[0].get("mapping", {})
    if mapping.get("NormalMap1") != "fp_t_tcb_1E.xy":
        raise ValueError("Promoted EyeClearCoat NormalMap1 mapping changed")

    eye_programs = [
        row
        for row in selected_program_abi.get("programs", [])
        if row.get("shader_family") == "EyeClearCoat"
    ]
    if len(eye_programs) != 4:
        raise ValueError("Selected EyeClearCoat program set changed")
    for program in eye_programs:
        samplers = {
            str(row.get("name")): int(row.get("static_texture_call_count", 0))
            for row in program.get("fragment", {}).get("samplers", [])
        }
        if samplers.get("fp_t_tcb_1E", 0) <= 0:
            raise ValueError(
                "Selected EyeClearCoat variation does not sample fp_t_tcb_1E: "
                f"{program.get('variation_index')}")

    material_count = 0
    mismatches: list[dict[str, Any]] = []
    for selected in inventory.selected_sv_models(game_root):
        manifest = inventory.read_json(
            game_root / "assets" / "models" / f"{selected['stem']}.phmodel")
        for material in manifest.get("materials", []):
            if material.get("shader_family") != "EyeClearCoat":
                continue
            material_count += 1
            roles = {
                str(texture.get("role"))
                for texture in material.get("textures", [])
            }
            options = material.get("shader_options", {})
            if (
                "NormalMap1" not in roles
                or str(options.get("EnableNormalMap1")) != "True"
            ):
                mismatches.append({
                    "model": selected["stem"],
                    "material": str(material.get("name", "")),
                    "has_normal_map_1": "NormalMap1" in roles,
                    "enable_normal_map_1": options.get("EnableNormalMap1"),
                })
    if mismatches:
        raise ValueError(f"EyeClearCoat NormalMap1 corpus drift: {mismatches[:5]}")
    if material_count != 486:
        raise ValueError(
            f"EyeClearCoat material corpus changed: {material_count}")
    return {
        "shader_family": "EyeClearCoat",
        "texture_role": "NormalMap1",
        "source_sampler": "fp_t_tcb_1E",
        "source_components": "xy",
        "runtime_translation_key": "normal_texture",
        "runtime_bridge": "importer_role_override",
        "evidence": "compiled_use_site_data_flow",
        "selected_program_count": len(eye_programs),
        "material_count": material_count,
        "exact_translation_count": material_count,
    }


def require_source_tokens(game_root: pathlib.Path) -> dict[str, list[str]]:
    requirements = {
        "tools/PhlosionNativeModelIr.cpp": [
            "nativeScarletSource",
            "nativeScarletSss",
            'loadTextureByRole(\n                     root,\n                     material,\n                     "SSSMaskMap"',
            "kNativeSssSurfaceFibre",
            "kNativeSssSurfaceDefault",
            '"NormalMap1"',
            '"NormalHeight1"',
            "sourceNormalScale",
        ],
        "src/game/runtime/render_model_cache/RenderModelCache.h": [
            "kNativeSssMaterialMode",
            "kNativeSssSurfaceDefault",
            "kNativeSssSurfaceFibre",
        ],
        "src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshMaterialTemplateCache.cpp": [
            "kNativeSssMaterialMode",
            "emissiveTextureSrgb",
        ],
        "src/game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshPrep.cpp": [
            "kNativeSssMaterialMode",
            "emissiveTextureSrgb",
        ],
    }
    found: dict[str, list[str]] = {}
    for identity, tokens in requirements.items():
        source = (game_root / identity).read_text(encoding="utf-8")
        missing = [token for token in tokens if token not in source]
        if missing:
            raise ValueError(f"Runtime bridge markers missing from {identity}: {missing}")
        found[identity] = tokens
    return found


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--game-root", type=pathlib.Path, required=True)
    parser.add_argument("--differential-evidence", type=pathlib.Path, required=True)
    parser.add_argument("--selected-program-abi", type=pathlib.Path, required=True)
    parser.add_argument("--eye-static-evidence", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()

    game_root = args.game_root.resolve()
    differential = inventory.read_json(args.differential_evidence.resolve())
    selected_program_abi = inventory.read_json(
        args.selected_program_abi.resolve())
    eye_static_evidence = inventory.read_json(
        args.eye_static_evidence.resolve())
    proven = {
        (str(row["shader_family"]), str(row["texture_role"])):
            str(row["sampler_name"])
        for row in differential["proven_texture_mappings"]
    }
    expected = {
        ("SSS", "RoughnessMap"),
        ("Standard", "EmissionColorMap"),
        ("Standard", "MetallicMap"),
        ("Standard", "NormalMap"),
        ("Standard", "RoughnessMap"),
        ("Transparent", "NormalMap"),
    }
    if set(proven) != expected:
        raise ValueError("Promoted differential mapping set changed; review the bridge audit")

    checked = collections.Counter()
    exact = collections.Counter()
    sss_count = 0
    sss_profiles = collections.Counter()
    mismatches: list[dict[str, Any]] = []
    for selected in inventory.selected_sv_models(game_root):
        manifest = inventory.read_json(
            game_root / "assets" / "models" / f"{selected['stem']}.phmodel")
        for material in manifest.get("materials", []):
            family = str(material.get("shader_family", ""))
            roles = {
                str(texture["role"]): texture.get("file")
                for texture in material.get("textures", [])
            }
            translation = material.get("runtime_translation", {})
            for mapped_family, role in sorted(proven):
                if family != mapped_family or role not in roles:
                    continue
                checked[(family, role)] += 1
                runtime_key = RUNTIME_KEYS[role]
                if translation.get(runtime_key) == roles[role]:
                    exact[(family, role)] += 1
                else:
                    mismatches.append({
                        "model": selected["stem"],
                        "material": str(material.get("name", "")),
                        "shader_family": family,
                        "texture_role": role,
                        "source_file": roles[role],
                        "runtime_key": runtime_key,
                        "runtime_file": translation.get(runtime_key),
                    })

            if family == "SSS":
                sss_count += 1
                if not SSS_ROLES.issubset(roles):
                    mismatches.append({
                        "model": selected["stem"],
                        "material": str(material.get("name", "")),
                        "shader_family": family,
                        "missing_sss_roles": sorted(SSS_ROLES.difference(roles)),
                    })
                floats = material.get("float_parameters", {})
                scale = floats.get("SSSMaskScale")
                offset = floats.get("SSSMaskOffset")
                sss_profiles[(str(scale), str(offset))] += 1
                if scale != 1 or offset != 0:
                    mismatches.append({
                        "model": selected["stem"],
                        "material": str(material.get("name", "")),
                        "shader_family": family,
                        "sss_mask_scale": scale,
                        "sss_mask_offset": offset,
                    })

    if mismatches:
        raise ValueError(f"SV Kanto runtime bridge mismatches: {mismatches[:5]}")
    if sss_count != 392 or sum(checked.values()) != 450:
        raise ValueError(
            f"SV runtime bridge corpus changed: SSS={sss_count}, mapped={sum(checked.values())}")

    source_markers = require_source_tokens(game_root)
    mappings = []
    for key in sorted(proven):
        family, role = key
        mappings.append({
            "shader_family": family,
            "texture_role": role,
            "source_sampler": proven[key],
            "runtime_translation_key": RUNTIME_KEYS[role],
            "material_count": checked[key],
            "exact_translation_count": exact[key],
            "evidence": "compiled_single_option_program_differential",
        })
    eye_normal_mapping = audit_eye_clear_coat_normal_bridge(
        game_root,
        selected_program_abi,
        eye_static_evidence,
    )
    mappings.append(eye_normal_mapping)
    mappings.sort(key=lambda row: (row["shader_family"], row["texture_role"]))
    proven_material_bindings = sum(checked.values()) + int(
        eye_normal_mapping["material_count"])
    exact_runtime_translations = sum(exact.values()) + int(
        eye_normal_mapping["exact_translation_count"])

    report = {
        "schema": SCHEMA,
        "source_profile": SOURCE_PROFILE,
        "method": {
            "runtime_execution": False,
            "emulator_used": False,
            "evidence_level": "compiled_differential_plus_manifest_transport_audit",
            "claim_boundary": (
                "Six roles are proven by compiled single-option differentials. "
                "EyeClearCoat NormalMap1 is additionally proven by named material "
                "data flow and a matching sampled symbol in every selected program. "
                "SSS mask/color transport is audited without inferring its full BRDF."
            ),
        },
        "summary": {
            "proven_mapping_count": len(mappings),
            "proven_material_bindings_checked": proven_material_bindings,
            "exact_runtime_translations": exact_runtime_translations,
            "runtime_translation_mismatches": 0,
            "sss_materials_checked": sss_count,
            "sss_complete_texture_stacks": sss_count,
            "sss_neutral_mask_transforms": sss_count,
            "runtime_bridge_files_checked": len(source_markers),
        },
        "proven_runtime_mappings": mappings,
        "eye_clear_coat_normal_transport": {
            "source_role": "NormalMap1",
            "source_sampler": "fp_t_tcb_1E.xy",
            "source_scale_parameter": "NormalHeight1",
            "selected_program_count": eye_normal_mapping[
                "selected_program_count"],
            "material_count": eye_normal_mapping["material_count"],
            "legacy_normal_map_status": (
                "retained in the source manifest but not selected as the live "
                "EyeClearCoat tangent-space normal"
            ),
        },
        "sss_transport": {
            "required_roles": sorted(SSS_ROLES),
            "mask_transport_slot": "emissive_texture",
            "mask_color_space": "linear",
            "subsurface_color_parameter": "SubsurfaceColor",
            "surface_profile_default": "smooth",
            "surface_profile_fibre_qualification": "pm0133_* only",
            "mask_transform_counts": [
                {"scale": key[0], "offset": key[1], "material_count": count}
                for key, count in sorted(sss_profiles.items())
            ],
        },
        "runtime_bridge_source_markers": source_markers,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(
        "[SvKantoRuntimeBridge] "
        f"mappings={len(mappings)} bindings={proven_material_bindings} "
        f"sss={sss_count} -> {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
