#!/usr/bin/env python3
"""Audit selected Z-A IkCharacter eye inputs against Phlosion consumption."""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import pathlib
import sys
from typing import Any


SCHEMA = "pokemon-autochess-za-ik-eye-runtime-coverage-v1"
SOURCE_PROFILE = "pokemon-legends-za-v2.0.0"
EXPECTED_ROLE_COUNTS = {
    "BaseColorMap": 80,
    "NormalMap": 80,
    "OcclusionMap": 80,
    "SpecularMaskMap": 80,
    "ShadowingColorMap": 80,
    "ShadowingColorMaskMap": 80,
    "RimLightMaskMap": 80,
    "LocalReflectionMap": 80,
    "LayerMaskMap": 80,
    "ParallaxMap": 80,
    "HighlightMaskMap": 80,
    "EyelidShadowMaskMap": 48,
}


def read_json(path: pathlib.Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(value, dict):
        raise ValueError(f"Expected a JSON object: {path}")
    return value


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def selected_za_stems(game_root: pathlib.Path) -> list[str]:
    catalog = read_json(game_root / "config" / "assets" / "asset_catalog.json")
    rows = [
        row for row in catalog.get("native_import_sets", [])
        if row.get("recipe") == "tools/assets/gamefreak_pokemon_imports_za.json"
    ]
    if len(rows) != 1 or rows[0].get("selection") != "include_stems":
        raise ValueError("Canonical Z-A selection changed")
    stems = [str(value) for value in rows[0].get("stems", [])]
    if len(stems) != 52 or len(stems) != len(set(stems)):
        raise ValueError("Canonical Z-A stem census changed")
    return stems


def normalized_value(value: Any) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-root", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    game_root = args.game_root.resolve()

    role_counts: collections.Counter[str] = collections.Counter()
    option_counts: dict[str, collections.Counter[str]] = collections.defaultdict(
        collections.Counter)
    parameter_counts: dict[str, collections.Counter[str]] = collections.defaultdict(
        collections.Counter)
    runtime_key_counts: collections.Counter[str] = collections.Counter()
    variation_counts: collections.Counter[int] = collections.Counter()
    model_counts: collections.Counter[str] = collections.Counter()
    source_hashes: dict[str, set[str]] = collections.defaultdict(set)
    manifest_hashes: dict[str, str] = {}
    material_count = 0

    for stem in selected_za_stems(game_root):
        path = game_root / "assets" / "models" / f"{stem}.phmodel"
        manifest = read_json(path)
        if manifest.get("source", {}).get("profile") != SOURCE_PROFILE:
            raise ValueError(f"{stem} source profile changed")
        manifest_hashes[stem] = sha256(path)
        for material in manifest.get("materials", []):
            options = material.get("shader_options", {})
            if (material.get("shader_family") != "IkCharacter" or
                    options.get("EnableEyeOptions") != "True"):
                continue
            material_count += 1
            model_counts[stem] += 1
            variation_counts[
                1214 if options.get("RequireEyelidShadowMap") == "True"
                else 682] += 1
            for key in (
                    "EnableEyeOptions", "EnableHighlight", "EnableParallaxMap",
                    "EnableIrisRefraction", "RequireEyelidShadowMap",
                    "EyelidType"):
                option_counts[key][str(options.get(key, "<missing>"))] += 1
            for group in ("float_parameters", "vec4_parameters"):
                for key, value in material.get(group, {}).items():
                    parameter_counts[key][normalized_value(value)] += 1
            runtime_key_counts.update(material.get("runtime_translation", {}).keys())
            for texture in material.get("textures", []):
                role = str(texture.get("role"))
                role_counts[role] += 1
                if texture.get("decoded") is not True:
                    raise ValueError(f"{stem}/{material.get('name')}/{role} undecoded")
                source_hashes[role].add(str(texture.get("source_sha256")))

    if material_count != 80 or dict(role_counts) != EXPECTED_ROLE_COUNTS:
        raise ValueError(
            "Selected IkCharacter eye corpus changed: "
            f"materials={material_count}, roles={dict(role_counts)}")
    expected_options = {
        "EnableEyeOptions": {"True": 80},
        "EnableHighlight": {"True": 80},
        "EnableParallaxMap": {"True": 80},
        "EnableIrisRefraction": {"Ng": 80},
        "RequireEyelidShadowMap": {"<missing>": 32, "True": 48},
        "EyelidType": {"None": 80},
    }
    if {key: dict(value) for key, value in option_counts.items()} != (
            expected_options):
        raise ValueError("Selected IkCharacter eye option census changed")
    if dict(variation_counts) != {682: 32, 1214: 48}:
        raise ValueError("Selected IkCharacter eye variation census changed")

    dataflow_path = game_root / "docs" / "kanto" / "evidence" / (
        "za_ik_character_dataflow_report.json")
    dataflow = read_json(dataflow_path)
    expected_mappings = {
        "ParallaxHeight": "fp_c7[5].y",
        "ParallaxIOR": "fp_c7[5].z",
        "UVScaleOffset1": "fp_c8[2].xyzw",
        "UVScaleOffset2": "fp_c8[3].xyzw",
        "UVRotation2": "fp_c7[21].y",
        "UVCenter0": "fp_c8[139].xy",
        "UVCenter1": "fp_c8[140].xy",
    }
    if dataflow.get("eye_material_buffer_mappings") != expected_mappings:
        raise ValueError("Promoted IkCharacter eye buffer mappings changed")

    loader_path = game_root / "tools" / "PhlosionNativeModelIr.cpp"
    loader = loader_path.read_text(encoding="utf-8-sig")
    for token in (
            "bool nativeIkCharacterEye", "bakeEyeHighlightEmission",
            "!shaderOptionEnabled(material, \"EnableEyeOptions\")",
            "nativeIkCharacterEyeMaterial &&"):
        if token not in loader:
            raise ValueError(f"IkCharacter eye loader contract lost token: {token}")
    for unconsumed in (
            "ParallaxMap", "EyelidShadowMaskMap", "ParallaxHeight",
            "ParallaxIOR", "UVRotation2"):
        if f'"{unconsumed}"' in loader:
            raise ValueError(
                f"Runtime now references {unconsumed}; update this coverage audit")

    nonzero_parallax = material_count - parameter_counts["ParallaxHeight"].get(
        "0", 0)
    nonunit_ior = material_count - parameter_counts["ParallaxIOR"].get("1", 0)
    nonzero_highlight = material_count - parameter_counts[
        "EmissionIntensityLayer5"].get("0", 0)
    nonzero_specular = 0
    for stem in selected_za_stems(game_root):
        manifest = read_json(
            game_root / "assets" / "models" / f"{stem}.phmodel")
        for material in manifest.get("materials", []):
            options = material.get("shader_options", {})
            if (material.get("shader_family") != "IkCharacter" or
                    options.get("EnableEyeOptions") != "True"):
                continue
            values = material.get("float_parameters", {})
            intensities = [float(values.get("SpecularIntensity", 0.0))]
            intensities.extend(float(values.get(
                f"SpecularLayer{index}Intensity", 0.0)) for index in range(1, 5))
            nonzero_specular += int(max(intensities) > 0.0)

    coverage = [
        {
            "role": "BaseColorMap",
            "bindings": 80,
            "status": "live_generic_texture_plus_ordered_layer_bake",
        },
        {
            "role": "NormalMap",
            "bindings": 80,
            "status": "live_generic_texture_plus_ordered_layer_bake",
        },
        {
            "role": "LayerMaskMap",
            "bindings": 80,
            "status": "consumed_by_offline_base_normal_and_surface_bakes",
        },
        {
            "role": "HighlightMaskMap",
            "bindings": 80,
            "status": "consumed_by_offline_emission_bake",
            "materials_with_nonzero_authored_emission": nonzero_highlight,
        },
        {
            "role": "OcclusionMap",
            "bindings": 80,
            "status": "retained_but_authored_texture_not_bound",
            "materials_with_nonzero_strength": 80,
        },
        {
            "role": "SpecularMaskMap",
            "bindings": 80,
            "status": "retained_but_excluded_from_ikcharacter_eye_path",
            "materials_with_nonzero_specular": nonzero_specular,
        },
        {
            "role": "ShadowingColorMap+ShadowingColorMaskMap",
            "bindings": 160,
            "status": "retained_but_source_colored_shadow_equation_not_evaluated",
            "materials_receiving_shadow": 80,
        },
        {
            "role": "RimLightMaskMap",
            "bindings": 80,
            "status": "retained_and_source_term_neutral_for_selected_eye_materials",
            "materials_with_nonzero_rim_intensity": 0,
        },
        {
            "role": "LocalReflectionMap",
            "bindings": 80,
            "status": "retained_but_excluded_from_ikcharacter_eye_path",
            "materials_with_nonzero_specular": nonzero_specular,
        },
        {
            "role": "ParallaxMap",
            "bindings": 80,
            "status": "retained_but_not_sampled_by_runtime",
            "materials_with_nonzero_parallax_height": nonzero_parallax,
            "materials_with_nonunit_ior": nonunit_ior,
        },
        {
            "role": "EyelidShadowMaskMap",
            "bindings": 48,
            "status": "retained_but_not_sampled_by_runtime",
        },
    ]
    consumed_bindings = 320
    authored_bindings = sum(role_counts.values())
    report = {
        "schema": SCHEMA,
        "source_profile": SOURCE_PROFILE,
        "method": {
            "runtime_execution": False,
            "emulator_used": False,
            "evidence_level": (
                "exact_selected_material_census_plus_compiled_resource_"
                "subgraphs_plus_runtime_source_contract"),
            "claim_boundary": (
                "This measures whether Phlosion consumes each authored input; "
                "it is not a pixel-similarity score. Base/normal/layer/highlight "
                "transport is live, while source parallax/refraction, eyelid "
                "shadow, local reflection, authored AO, and colored-shadow "
                "terms are not yet evaluated for IkCharacter eyes."),
        },
        "summary": {
            "selected_models_with_ikcharacter_eyes": len(model_counts),
            "selected_eye_materials": material_count,
            "selected_eye_variations": dict(sorted(variation_counts.items())),
            "authored_texture_bindings": authored_bindings,
            "consumed_texture_bindings": consumed_bindings,
            "unconsumed_texture_bindings": authored_bindings - consumed_bindings,
            "materials_with_nonzero_parallax_height": nonzero_parallax,
            "materials_with_nonunit_parallax_ior": nonunit_ior,
            "materials_with_eyelid_shadow_map": 48,
            "materials_with_nonzero_highlight_emission": nonzero_highlight,
            "materials_with_nonzero_specular": nonzero_specular,
        },
        "selected_option_census": {
            key: dict(sorted(value.items()))
            for key, value in sorted(option_counts.items())
        },
        "selected_parameter_census": {
            key: dict(sorted(parameter_counts[key].items()))
            for key in (
                "ParallaxHeight", "ParallaxIOR", "EmissionIntensityLayer5",
                "NormalHeight", "RimLightIntensity", "BackRimLightIntensity",
                "ReflectionsBlur")
        },
        "compiled_eye_material_buffer_mappings": expected_mappings,
        "runtime_bridge": {
            "selected_material_mode": 2,
            "specialized_ikcharacter_eye_work": (
                "HighlightMaskMap is baked into emission; IkCharacter mode 32 "
                "explicitly excludes EnableEyeOptions materials."),
            "runtime_translation_key_counts": dict(sorted(
                runtime_key_counts.items())),
        },
        "texture_role_coverage": coverage,
        "unique_source_payloads_by_role": {
            role: len(hashes) for role, hashes in sorted(source_hashes.items())
        },
        "models": [
            {
                "stem": stem,
                "eye_materials": count,
                "manifest_sha256": manifest_hashes[stem],
            }
            for stem, count in sorted(model_counts.items())
        ],
        "next_source_proven_runtime_target": {
            "id": "za_ikcharacter_eye_live_path",
            "priority": "critical",
            "requirements": [
                "preserve ParallaxMap and EyelidShadowMaskMap as live inputs",
                "evaluate ParallaxHeight/ParallaxIOR and mapped eye UV transforms",
                "reuse the authored local-reflection/AO/specular/shadow stack",
                "retain the current source highlight mask without double lighting",
                "implement identically in OpenGL, D3D12, and Vulkan",
            ],
            "headless_change_authorized": False,
            "reason": (
                "The exact inputs and buffer fields are now mapped, but packing "
                "two additional live masks and matching the final eye composite "
                "needs a separately testable cross-backend material contract."),
        },
        "source_sha256": {
            "compiled_dataflow": sha256(dataflow_path),
            "game_loader": sha256(loader_path),
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
    except (FileNotFoundError, KeyError, TypeError, ValueError) as error:
        print(f"[ZaIkEyeRuntimeCoverage] ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
