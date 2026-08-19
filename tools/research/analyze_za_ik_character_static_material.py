#!/usr/bin/env python3
"""Audit retained Kanto Z-A IkCharacter evidence and runtime boundaries."""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import pathlib
from typing import Any

from za_corpus import selected_za_stems


SCHEMA = "pokemon-autochess-za-ik-character-static-material-evidence-v2"
SOURCE_PROFILE = "pokemon-legends-za-v2.0.0"
PACKED_PROBE_FORMAT = (
    "phlosion-za-local-reflection-rgba16f-cube-mips-packed-v1")
EXPECTED_VARIATIONS = {514: 600, 594: 4, 682: 240, 1214: 188, 1650: 4}
EXPECTED_ROLE_COUNTS = {
    "BaseColorMap": 1036,
    "NormalMap": 1036,
    "OcclusionMap": 1036,
    "SpecularMaskMap": 1036,
    "ShadowingColorMap": 1036,
    "ShadowingColorMaskMap": 1036,
    "RimLightMaskMap": 1036,
    "LayerMaskMap": 1036,
    "LocalReflectionMap": 1032,
    "ParallaxMap": 428,
    "HighlightMaskMap": 428,
    "EyelidShadowMaskMap": 188,
    "DisplacementMap": 4,
    "NoiseSourceMap": 4,
}
MACHOP_STEMS = ("0066_Machop_ZA", "0066_Machop_ZA_Shiny")
MACHOP_BODY_TEXTURE_HASHES = {
    "BaseColorMap": "d256d413b0a4f3ba918d650df4bc03411ded642364ae1194790000ac18bcaeff",
    "NormalMap": "d8e4aeaf0c8f16e86b7d6a3e9e3c01766514b9cd7783ba87203ef135ae471f6f",
    "OcclusionMap": "41f9c54db46a1f1017ea10bbdc0e628cb1fde169063975ec13780c2d5edc5135",
    "SpecularMaskMap": "010736c43489585bec016dc46d4ad93c489ce2498f5359daed8c0d808fa23ff1",
    "ShadowingColorMap": "8cec5dbd9d6eebf1df8ad11544a8857084251be2b4ad319fb0f3bae164898e42",
    "ShadowingColorMaskMap": "01b36c092af354fac89bf04fbc0d9e6661c805ab5c1c8fa8a46c1f7b63a9b500",
    "RimLightMaskMap": "d022d2332fb6312d859fbd527f75f00e2c8bdca922cee81bedf298d0e0fc24b2",
    "LocalReflectionMap": "dab01d593c6cc43a23d207986ed3a13348b2026fe97ab219678714a781d3f080",
    "LayerMaskMap": "5d8fd1e0be4071317c1f57195f224f501c6d0d699c8b1306ac58cee46b66f484",
}
MACHOP_EYE_TEXTURE_HASHES = {
    "BaseColorMap": "0b891ac9c4272dc9110df35521c8a3df20a2cc56d7122d81e7d4ed632122efbb",
    "NormalMap": "d552683619a00b7428f48bed058bf52513bf6f13b919aed0edabc68fa7afba6e",
    "OcclusionMap": "8cec5dbd9d6eebf1df8ad11544a8857084251be2b4ad319fb0f3bae164898e42",
    "SpecularMaskMap": "01b36c092af354fac89bf04fbc0d9e6661c805ab5c1c8fa8a46c1f7b63a9b500",
    "ShadowingColorMap": "8cec5dbd9d6eebf1df8ad11544a8857084251be2b4ad319fb0f3bae164898e42",
    "ShadowingColorMaskMap": "01b36c092af354fac89bf04fbc0d9e6661c805ab5c1c8fa8a46c1f7b63a9b500",
    "RimLightMaskMap": "01b36c092af354fac89bf04fbc0d9e6661c805ab5c1c8fa8a46c1f7b63a9b500",
    "LocalReflectionMap": "dab01d593c6cc43a23d207986ed3a13348b2026fe97ab219678714a781d3f080",
    "LayerMaskMap": "6c5905474194009fb3000e8639931122fdfdaa010444a230e3ca98d40f7973a2",
    "ParallaxMap": "47a12c5e4cbca7d829252aff7daf45221e7692d6f389e9ee46f42580b3f980a7",
    "HighlightMaskMap": "01b36c092af354fac89bf04fbc0d9e6661c805ab5c1c8fa8a46c1f7b63a9b500",
}
TEXTURE_MAPPINGS = {
    "BaseColorMap": ("fragment", "fp_t_tcb_8", "sampler2D"),
    "NormalMap": ("fragment", "fp_t_tcb_C", "sampler2D"),
    "OcclusionMap": ("fragment", "fp_t_tcb_14", "sampler2D"),
    "SpecularMaskMap": ("fragment", "fp_t_tcb_A", "sampler2D"),
    "ShadowingColorMap": ("fragment", "fp_t_tcb_12", "sampler2D"),
    "ShadowingColorMaskMap": ("fragment", "fp_t_tcb_10", "sampler2D"),
    "LayerMaskMap": ("fragment", "fp_t_tcb_16", "sampler2D"),
    "RimLightMaskMap": ("fragment", "fp_t_tcb_18", "sampler2D"),
    "LocalReflectionMap": ("fragment", "fp_t_tcb_1C", "samplerCube"),
    "ParallaxMap": ("fragment", "fp_t_tcb_E", "sampler2D"),
    "HighlightMaskMap": ("fragment", "fp_t_tcb_1E", "sampler2D"),
    "EyelidShadowMaskMap": ("fragment", "fp_t_tcb_20", "sampler2D"),
    "DisplacementMap": ("vertex", "vp_t_tcb_24", "sampler2D"),
    "NoiseSourceMap": ("fragment", "fp_t_tcb_46", "sampler2D"),
}


def read_json(path: pathlib.Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def machop_source_canary(game_root: pathlib.Path) -> list[dict[str, Any]]:
    rows = []
    for stem in MACHOP_STEMS:
        path = game_root / "assets" / "models" / f"{stem}.phmodel"
        manifest = read_json(path)
        materials = {
            str(material.get("name")): material
            for material in manifest.get("materials", [])
        }
        if set(materials) != {"l_eye", "r_eye", "body"}:
            raise ValueError(
                f"Z-A Machop material partition changed: {stem}: "
                f"{sorted(materials)}")
        body = materials["body"]
        eyes = [materials["l_eye"], materials["r_eye"]]
        body_hashes = {
            str(texture.get("role")): str(texture.get("source_sha256"))
            for texture in body.get("textures", [])
        }
        if body_hashes != MACHOP_BODY_TEXTURE_HASHES:
            raise ValueError(f"Z-A Machop body source textures changed: {stem}")
        for eye in eyes:
            eye_hashes = {
                str(texture.get("role")): str(texture.get("source_sha256"))
                for texture in eye.get("textures", [])
            }
            if eye_hashes != MACHOP_EYE_TEXTURE_HASHES:
                raise ValueError(
                    f"Z-A Machop eye source textures changed: "
                    f"{stem}/{eye.get('name')}")
        options = body.get("shader_options", {})
        expected_options = {
            "BaseColorMultiply": "True",
            "EnableHairSpecular": "False",
            "CategoryLabel": "6",
            "EnableAdditionalLight": "1",
        }
        if any(options.get(key) != value
               for key, value in expected_options.items()):
            raise ValueError(f"Z-A Machop body options changed: {stem}")
        floats = body.get("float_parameters", {})
        expected_floats = {
            "NormalHeight": 1.0,
            "Metallic": 0.0,
            "OcclusionStrength": 1.7,
            "SpecularIntensity": 0.04,
            "ShadowStrength": 0.7,
            "ShadingBias": 1.0,
            "ShadowingBias": 1.0,
            "HalfLambertBias": 0.4,
            "ShadowingGIGain": 0.5,
            "RimLightOffset": 0.3,
            "RimLightContrast": 3.0,
            "RimLightIntensity": 0.8,
            "BackRimLightIntensity": 0.01,
            "ReflectionsBlur": 0.0,
        }
        if any(abs(float(floats.get(key, float("nan"))) - value) > 1e-6
               for key, value in expected_floats.items()):
            raise ValueError(f"Z-A Machop body controls changed: {stem}")
        for eye in eyes:
            eye_options = eye.get("shader_options", {})
            eye_floats = eye.get("float_parameters", {})
            if (eye_options.get("EnableEyeOptions") != "True" or
                    eye_options.get("EnableParallaxMap") != "True" or
                    eye_options.get("EnableIrisRefraction") != "Ng" or
                    abs(float(eye_floats.get("ParallaxHeight", 0.0)) -
                        0.03) > 1e-6 or
                    abs(float(eye_floats.get("ParallaxIOR", 0.0)) -
                        1.0) > 1e-6):
                raise ValueError(
                    f"Z-A Machop eye optics changed: "
                    f"{stem}/{eye.get('name')}")
        if any("RoughnessMap" in hashes for hashes in (
                body_hashes, MACHOP_EYE_TEXTURE_HASHES)):
            raise ValueError("Z-A Machop unexpectedly gained a roughness atlas")
        rows.append({
            "stem": stem,
            "manifest_sha256": sha256(path),
            "material_partition": ["l_eye", "r_eye", "body"],
            "body_texture_roles": sorted(body_hashes),
            "eye_texture_roles": sorted(MACHOP_EYE_TEXTURE_HASHES),
            "body_source_controls": expected_floats,
            "eye_parallax_height": 0.03,
            "eye_parallax_ior": 1.0,
            "source_surface_classification": (
                "smooth_matte_ikcharacter_without_roughness_or_hair_lobe"),
            "source_specular_boundary": (
                "authored black ShadowingColorMaskMap suppresses the direct "
                "specular lane despite retaining patterned SpecularMaskMap"),
        })
    return rows


def source_contract(
        game_root: pathlib.Path,
        engine_root: pathlib.Path) -> dict[str, Any]:
    files = {
        "game_loader": game_root / "tools" / "PhlosionNativeModelIr.cpp",
        "opengl": engine_root / "src" / "engine" / "render" / "opengl" /
            "OpenGLRenderBackendWorldPipeline.cpp",
        "d3d12": engine_root / "src" / "engine" / "render" / "d3d12" /
            "D3D12RenderBackendWorldPipeline.cpp",
        "vulkan": engine_root / "assets" / "shaders" / "vulkan" /
            "world_material.glsl",
    }
    loader = files["game_loader"].read_text(encoding="utf-8-sig")
    for token in (
            "bakeIkCharacterLightingAuxiliary",
            "CachedTextureRgba occlusionMap",
            "CachedTextureRgba shadowingColorMap",
            "CachedTextureRgba shadowingColorMask",
            "layerWeightSum",
            "1.0f - baseEmissionIntensity",
            "1.0f - layerEmissionIntensities[layer]",
            "SpecularMaskMapValue",
            "BaseColorDarkness",
            "sourceOcclusion * std::max(occlusionStrength, 0.0f)",
            "kNativeIkCharacterMaterialMode",
            "kNativeIkCharacterEyeMaterialMode",
            "bakeIkCharacterEyeColorComposite",
            "bakeIkCharacterEyePackedInputs",
            "EyelidShadowMaskMap",
            "ParallaxIOR",
            "LocalReflectionMap",
            "pre-composite rim scalars",
            "emissionLuminance",
            "linearToSrgb(emissionLuminance)",
            "packIkCharacterEmissionColor",
            "nativeLightingCategory / 16.0f",
            "resolvedMaterialFlags >= 0.5f"):
        if token not in loader:
            raise ValueError(f"IkCharacter loader contract lost token: {token}")
    for forbidden in (
            "kNativeRimCompositeScale",
            "nativeIkCharacterSurfaceProfile",
            "loadSupplementalScarletSurfaceDetail"):
        if forbidden in loader:
            raise ValueError(
                f"IkCharacter loader restored a provisional asset bake: {forbidden}")
    for name in ("opengl", "d3d12", "vulkan"):
        source = files[name].read_text(encoding="utf-8-sig")
        for token in (
                "sampleZaLocalReflectionProbe",
                "evaluateNativeIkCharacter" if name == "vulkan"
                else "applyNativeIkCharacter",
                "reflectionBlur",
                "zaIkRimPresentationScale",
                "rimShape",
                "resolveZaIkEyeParallaxUv",
                "halfLambertBiasSquared",
                "shadowProcessArea",
                "bodyEmission",
                "zaIkEmissionColor",
                "zaUiLightingCategory",
                "zaUiDirectIntensity",
                "zaUiGiIntensity",
                "normalDotHalf - specularOffset",
                "shadowingGiGain",
                "shadowAmount * shadowingGiGain",
                "source-disabled"):
            if token not in source:
                raise ValueError(
                f"{name} IkCharacter contract lost token: {token}")
        for forbidden in (
                "eyeShadowDomain", "eyeHighlight", "eyelidShadow",
                "normalDetailDelta",
                "!nativeEye && hasAuthoredColorProcess",
                "surfaceSpecular = nativeEye"):
            if forbidden in source:
                raise ValueError(
                    f"{name} restored unsupported IkCharacter heuristic: "
                    f"{forbidden}")
        exact_rim_formula = (
            "rimSmooth * (1.0f + 2.0f * rimContrast) - rimContrast"
            if name == "d3d12" else
            "rimSmooth * (1.0 + 2.0 * rimContrast) - rimContrast")
        legacy_contrast = (
            "max(uEmissiveFactor.g, 1.0)" if name == "opengl" else
            "max(rimParameters.g, 1.0f)" if name == "d3d12" else
            "max(rimParameters.g, 1.0)")
        if exact_rim_formula not in source or legacy_contrast in source:
            raise ValueError(
                f"{name} IkCharacter lost the exact Z-A rim contrast response")
        function_start = source.find(
            "evaluateNativeIkCharacter" if name == "vulkan"
            else "applyNativeIkCharacter")
        function_end = source.find(
            "evaluateNativeSssSurface" if name == "vulkan"
            else "applyNativeSssSurface",
            function_start)
        if function_start < 0 or function_end <= function_start:
            raise ValueError(
                f"{name} IkCharacter runtime function boundary changed")
        body_source = source[function_start:function_end]
        for token in (
                "biasedLambert",
                "halfLambertBiasSquared",
                "shadowBandLow",
                "shadowProcessArea",
                "baseToMidHue",
                "darkToBaseMid",
                "baseMidToDark",
                "hueAreaScale",
                "colorProcessLight",
                "DiffusionLevels scales" if name == "vulkan"
                else "diffusionLevels",
                "normalDotLightSigned",
                "0.4",
                "2.5",
                "environmentRadiance * sourceAlbedo * metallic"
                if name == "vulkan"
                else "environmentRadiance * albedo * metallic"):
            if token not in body_source:
                raise ValueError(
                    f"{name} IkCharacter exact body contract lost token: "
                    f"{token}")
        for forbidden in (
                "hueStrength",
                "surfaceSpecular = max",
                "authoredShadowDomain",
                "pow(authoredShadowDomain",
                "mix(shaded, midHueColor, midArea *",
                "lerp(shaded, midHueColor, midArea *"):
            if forbidden in body_source:
                raise ValueError(
                    f"{name} IkCharacter restored a disproven body heuristic: "
                    f"{forbidden}")
    return {name: sha256(path) for name, path in files.items()}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--game-root", type=pathlib.Path, required=True)
    parser.add_argument("--engine-root", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    game_root = args.game_root.resolve()
    engine_root = args.engine_root.resolve()

    catalog_path = game_root / "config" / "assets" / "asset_catalog.json"
    catalog = read_json(catalog_path)
    stems = selected_za_stems(game_root, catalog)
    role_counts: collections.Counter[str] = collections.Counter()
    role_undecoded: collections.Counter[str] = collections.Counter()
    material_class_counts: collections.Counter[str] = collections.Counter()
    parameter_presence: collections.Counter[str] = collections.Counter()
    material_count = 0
    model_count = 0
    unique_probe_sources: set[str] = set()
    unique_probe_payloads: set[str] = set()
    hair_specular_enabled = 0
    model_rows: list[dict[str, Any]] = []

    for stem in stems:
        path = game_root / "assets" / "models" / f"{stem}.phmodel"
        manifest = read_json(path)
        if manifest.get("source", {}).get("profile") != SOURCE_PROFILE:
            raise ValueError(f"{stem} source profile changed")
        ik_materials = [
            row for row in manifest.get("materials", [])
            if row.get("shader_family") == "IkCharacter"
        ]
        if not ik_materials:
            continue
        model_count += 1
        classes: collections.Counter[str] = collections.Counter()
        for material in ik_materials:
            material_count += 1
            options = material.get("shader_options", {})
            if options.get("EnableHairSpecular") == "True":
                hair_specular_enabled += 1
            if options.get("EnableDisplacementMap") == "True":
                material_class = "displacement"
            elif options.get("EnableEyeOptions") == "True":
                material_class = "eye_options"
            else:
                material_class = "core_body"
            material_class_counts[material_class] += 1
            classes[material_class] += 1
            for group in (
                    "float_parameters", "vec2_parameters",
                    "vec3_parameters", "vec4_parameters"):
                parameter_presence.update(material.get(group, {}).keys())
            for texture in material.get("textures", []):
                role = str(texture.get("role"))
                role_counts[role] += 1
                if texture.get("decoded") is not True:
                    role_undecoded[role] += 1
                if role == "LocalReflectionMap":
                    if texture.get("decoded_format") != PACKED_PROBE_FORMAT:
                        raise ValueError(
                            f"{stem}/{material.get('name')} probe format changed")
                    if (texture.get("source_array_count"),
                            texture.get("source_mip_count")) != (6, 8):
                        raise ValueError(
                            f"{stem}/{material.get('name')} probe topology changed")
                    unique_probe_sources.add(str(texture.get("source_sha256")))
                    unique_probe_payloads.add(
                        str(texture.get("source_payload_sha256")))
        model_rows.append({
            "stem": stem,
            "manifest_sha256": sha256(path),
            "ik_character_materials": len(ik_materials),
            "material_classes": dict(sorted(classes.items())),
        })

    if material_count != 1036 or model_count != 212:
        raise ValueError("Retained IkCharacter material/model census changed")
    if dict(role_counts) != EXPECTED_ROLE_COUNTS:
        raise ValueError(
            f"IkCharacter texture-role census changed: {dict(role_counts)}")
    if role_undecoded:
        raise ValueError(
            f"IkCharacter authored textures remain undecoded: {role_undecoded}")
    if material_class_counts != {
            "core_body": 604, "displacement": 4, "eye_options": 428}:
        raise ValueError(
            f"IkCharacter material classes changed: {material_class_counts}")
    if hair_specular_enabled != 0:
        raise ValueError(
            "Selected Kanto Z-A corpus unexpectedly enables HairSpecular")
    machop_canary = machop_source_canary(game_root)

    abi_path = game_root / "docs" / "kanto" / "evidence" / (
        "za_kanto_selected_program_abi.json")
    option_graph_path = game_root / "docs" / "kanto" / "evidence" / (
        "za_kanto_option_graph.json")
    eye_coverage_path = game_root / "docs" / "kanto" / "evidence" / (
        "za_ik_eye_runtime_coverage.json")
    dataflow_path = game_root / "docs" / "kanto" / "evidence" / (
        "za_ik_character_dataflow_report.json")
    abi = read_json(abi_path)
    programs = {
        int(row["variation_index"]): row
        for row in abi.get("programs", [])
        if row.get("shader_family") == "IkCharacter"
    }
    if {
            index: int(row.get("material_count", 0))
            for index, row in programs.items()} != EXPECTED_VARIATIONS:
        raise ValueError("Promoted IkCharacter variation coverage changed")
    sampler_symbols = {
        (stage_name, sampler["name"], sampler["type"])
        for row in programs.values()
        for stage_name, stage in (
            ("fragment", row["fragment"]), ("vertex", row["vertex"]))
        for sampler in stage.get("samplers", [])
    }
    missing_mappings = [
        role for role, mapping in TEXTURE_MAPPINGS.items()
        if mapping not in sampler_symbols
    ]
    if missing_mappings:
        raise ValueError(
            f"Selected IkCharacter programs lost mapped roles: {missing_mappings}")

    option_graph = read_json(option_graph_path)
    summary = option_graph.get("summary", {})
    if (summary.get("unresolved_option_choices") != 0 or
            summary.get("differential_count") != 183):
        raise ValueError("Z-A exact option graph is no longer complete")
    eye_coverage = read_json(eye_coverage_path)
    if (eye_coverage.get("schema") !=
            "pokemon-autochess-za-ik-eye-runtime-coverage-v1" or
            eye_coverage.get("summary", {}).get("selected_eye_materials") != 428):
        raise ValueError("Z-A IkCharacter eye runtime coverage changed")
    dataflow = read_json(dataflow_path)
    dataflow_summary = dataflow.get("summary", {})
    if (dataflow.get("schema") !=
            "pokemon-autochess-za-ik-character-dataflow-evidence-v2" or
            dataflow_summary.get("cooked_phmat_files_verified") != 212 or
            not isinstance(dataflow_summary.get(
                "cooked_mode32_submesh_records_verified"), int) or
            dataflow_summary.get(
                "cooked_mode32_submesh_records_verified") <= 0 or
            dataflow_summary.get(
                "cooked_mode32_native_parameter_records_verified") !=
            dataflow_summary.get(
                "cooked_mode32_submesh_records_verified") or
            dataflow_summary.get(
                "cooked_neutral_hair_auxiliary_records_verified") !=
            dataflow_summary.get(
                "cooked_mode32_submesh_records_verified") or
            dataflow_summary.get(
                "machop_cooked_material_records_verified") != 6 or
            dataflow_summary.get(
                "machop_cooked_zero_specular_records_verified") != 6 or
            dataflow_summary.get(
                "cooked_body_emission_records_verified") != 4):
        raise ValueError("Z-A IkCharacter cooked body coverage changed")
    if (dataflow_summary.get(
            "eye_variations_with_exact_parallax_march") != 2 or
            dataflow.get("eye_parallax_data_flow", {}).get(
                "view_schedule", {}).get("sample_count_range") != [4, 14]):
        raise ValueError("Z-A exact eye parallax evidence changed")
    if dataflow_summary.get("mapped_body_material_fields") != 64:
        raise ValueError("Z-A IkCharacter compiled body mapping coverage changed")
    body_flow = dataflow.get("body_constant_buffer_data_flow", {})
    if (body_flow.get("back_rim_gate", {}).get("proof") !=
            "compiled_operation_identity" or
            body_flow.get("color_process_layout", {}).get("proof") !=
            "compiled_register_group_plus_backward_dependency_closure_"
            "plus_operation_identity" or
            body_flow.get("local_reflection", {}).get("proof") !=
            "compiled_metallic_branch_plus_lod_plus_floor_identity" or
            body_flow.get("direct_specular_boundary", {}).get("proof") !=
            "compiled_operation_and_branch_identity"):
        raise ValueError("Z-A exact body operation evidence changed")
    runtime_sources = source_contract(game_root, engine_root)
    ui_light_path = game_root / "docs" / "kanto" / "evidence" / \
        "za_ui_offscreen_light.json"
    ui_diffuse_path = game_root / "docs" / "kanto" / "evidence" / \
        "za_ui_offscreen_diffuse_probe.json"
    ui_specular_path = game_root / "docs" / "kanto" / "evidence" / \
        "za_ui_offscreen_specular_probe.json"
    ui_light = read_json(ui_light_path)
    if (ui_light.get("component_count") != 24 or
            not any(component.get("name") == "DirectionalMain"
                    for component in ui_light.get("components", []))):
        raise ValueError("Z-A off-screen source-light evidence changed")

    report = {
        "schema": SCHEMA,
        "source_profile": SOURCE_PROFILE,
        "method": {
            "runtime_execution": False,
            "emulator_used": False,
            "evidence_level": (
                "exact_material_census_plus_compiled_program_abi_plus_"
                "complete_single_option_graph_plus_runtime_source_contract"),
            "claim_boundary": (
                "Texture transport, selected variation identity, mapped sampler "
                "roles, semantic material controls, and authored local-probe "
                "transport are proven. Dedicated mode 35 now consumes the "
                "selected eye parallax/refraction, eyelid, highlight, AO, "
                "specular, and reflection inputs on all three backends. Its "
                "refraction basis, derivative footprint, view fade, 4-to-14 "
                "sample height march, hit test, and refinement now match the "
                "compiled variations 682/1214. The "
                "ordinary-body AO/shadow-color blend and layered "
                "metallic/specular offset, intensity, contrast, smoothstep, "
                "and contrast-remap order are compiled-program proven. The "
                "ordinary-body ShadowingBias polynomial, half-Lambert shadow "
                "band, authored ShadowingGIGain scaling of the RGB shadow "
                "difference, front/back rim gates, middle/dark hue targets and "
                "ordered cross-blend, local diffusion scale, direct-specular "
                "material path, and metallic local-reflection gate are now "
                "operation-proven and execute on all three backends. "
                "selected corpus' achromatic and chromatic body emission is "
                "preserved through the ordered layer bake and final combine; "
                "per-pixel luminance is paired with an exact packed material "
                "color and the legacy sRGB upload is compensated. "
                "All selected materials disable the optional HairSpecular "
                "branch, so the runtime no longer invents a species-based "
                "fur/feather lobe. Authored rim values remain raw in the asset. "
                "Machop regular/shiny are pinned as source and cooked "
                "canaries: Z-A supplies full-resolution body normal/AO/rim/"
                "layer maps and live eye parallax, but no roughness atlas or "
                "hair lobe; its black shadowing masks deliberately suppress "
                "the packed direct-specular lane. This is a smooth matte "
                "source material, not missing PLA-style gloss. "
                "The recovered off-screen source-stage profile now supplies "
                "the retained directional transform, selectable direct/GI/rim "
                "records, and exact diffuse probe without changing the neutral "
                "review profiles. Projected/cascaded shadow payloads, the final "
                "LUT, output-transform values, and literal source-frame exposure "
                "remain reconstructed or unknown."),
        },
        "summary": {
            "selected_models": model_count,
            "materials": material_count,
            "selected_programs": len(programs),
            "selected_variations": EXPECTED_VARIATIONS,
            "material_classes": dict(sorted(material_class_counts.items())),
            "texture_roles": len(role_counts),
            "texture_bindings": sum(role_counts.values()),
            "undecoded_authored_textures": sum(role_undecoded.values()),
            "unique_local_reflection_sources": len(unique_probe_sources),
            "unique_local_reflection_payloads": len(unique_probe_payloads),
            "complete_option_graph_edges": 183,
            "ikcharacter_eye_materials": 428,
            "consumed_ikcharacter_eye_texture_bindings":
                eye_coverage["summary"]["consumed_texture_bindings"],
            "unconsumed_ikcharacter_eye_texture_bindings":
                eye_coverage["summary"]["unconsumed_texture_bindings"],
            "cooked_phmat_files_verified":
                dataflow_summary["cooked_phmat_files_verified"],
            "cooked_mode32_submesh_records_verified":
                dataflow_summary["cooked_mode32_submesh_records_verified"],
            "cooked_mode32_native_parameter_records_verified":
                dataflow_summary[
                    "cooked_mode32_native_parameter_records_verified"],
            "cooked_body_emission_records_verified":
                dataflow_summary["cooked_body_emission_records_verified"],
            "cooked_neutral_hair_auxiliary_records_verified":
                dataflow_summary[
                    "cooked_neutral_hair_auxiliary_records_verified"],
            "machop_source_material_records_verified": 6,
            "machop_cooked_material_records_verified": dataflow_summary[
                "machop_cooked_material_records_verified"],
            "machop_cooked_zero_specular_records_verified": dataflow_summary[
                "machop_cooked_zero_specular_records_verified"],
            "hair_specular_enabled_materials": hair_specular_enabled,
            "mapped_body_material_fields": dataflow_summary[
                "mapped_body_material_fields"],
            "exact_final_scene_fade_programs": dataflow_summary[
                "selected_programs_with_exact_final_scene_fade"],
            "backends_bridged": 3,
            "shadowing_gi_gain_runtime_backends": 3,
            "source_scene_components_verified": 24,
            "source_global_probe_payloads_verified": 2,
        },
        "texture_role_counts": dict(sorted(role_counts.items())),
        "texture_mappings": [
            {
                "role": role,
                "stage": mapping[0],
                "sampler": mapping[1],
                "sampler_type": mapping[2],
                "status": "compiled_selected_program_mapping",
            }
            for role, mapping in TEXTURE_MAPPINGS.items()
        ],
        "semantic_controls_retained": [
            name for name in (
                "BaseColor", "BaseColorLayer1", "BaseColorLayer2",
                "BaseColorLayer3", "BaseColorLayer4", "ShadowingColor",
                "ShadowingColorLayer1", "ShadowingColorLayer2",
                "ShadowingColorLayer3", "ShadowingColorLayer4",
                "OcclusionStrength", "SpecularIntensity", "SpecularOffset",
                "SpecularContrast", "Metallic", "MetallicLayer1",
                "MetallicLayer2", "MetallicLayer3", "MetallicLayer4",
                "ShadowStrength",
                "HalfLambertBias", "ShadowingGIGain", "RimLightOffset",
                "RimLightContrast", "RimLightIntensity",
                "BackRimLightIntensity", "ReflectionsBlur",
                "DiffusionLevels", "ShadowingBias", "ShadowingShift",
                "ShadowingContrast", "HueShiftBias", "MidAreaShift",
                "MidAreaContrast", "MidAreaHueOffset", "DarkAreaShift",
                "DarkAreaContrast", "DarkAreaHueOffset",
                "HueShiftAreaValue", "EmissionIntensity",
                "EmissionIntensityLayer1", "EmissionIntensityLayer2",
                "EmissionIntensityLayer3", "EmissionIntensityLayer4")
            if parameter_presence[name] > 0
        ],
        "programs": [
            {
                "variation_index": index,
                "material_count": row["material_count"],
                "fragment_sha256": row["fragment"]["sha256"],
                "vertex_sha256": row["vertex"]["sha256"],
            }
            for index, row in sorted(programs.items())
        ],
        "runtime_bridge": {
            "core_body_mode": 32,
            "base_layer_color": "ordered source layer-mask bake",
            "normal": "authored tangent-space normal map and scale",
            "auxiliary_controls": (
                "the exact AO-weighted source shadow color, metallic, and "
                "specular intensity/offset/contrast are packed without "
                "dropping material boundaries; source offset subtraction, "
                "smoothstep, contrast remap, ShadowingBias, half-Lambert band, "
                "ShadowingGIGain-scaled RGB shadow difference, and ordered "
                "middle/dark color process execute on all "
                "backends; front/back rim retain raw authored values with "
                "sRGB compensation and use the exact local source gates, while "
                "blue carries ordered-layer body-emission luminance while "
                "params0.z carries its exact 24-bit material color"),
            "local_reflection": (
                "decoded authored BC6H cube with all eight mips and "
                "ReflectionsBlur LOD; the body path samples it only for "
                "layer-resolved metallic regions"),
            "eye_options": (
                "the cooker applies the compiled eyelid-then-highlight order "
                "to both base and shadow color; mode 35 consumes those colors "
                "with source-proven live parallax/refraction, local reflection, "
                "authored AO, and specular inputs; the remaining colored-shadow "
                "bindings are source-neutral in the selected corpus"),
            "hair_specular": (
                "source-proven disabled for all selected Kanto materials; no "
                "fabricated fibre/feather lobe executes in mode 32"),
            "rim_presentation": (
                "raw pre-composite source values in assets; Source Stage "
                "applies the retained category rim color while generic review "
                "profiles keep their explicit 0.25 calibration"),
            "source_scene": (
                "the Inspector-only source-stage profile consumes the exact "
                "retained UI diffuse cube and source-derived directional, "
                "direct-intensity, GI-intensity, and rim-category values on "
                "all three backends; carrier exposure and transform-to-buffer "
                "convention remain documented calibration boundaries"),
            "backends": ["opengl", "d3d12", "vulkan"],
        },
        "machop_source_canary": machop_canary,
        "remaining_equation_gaps": [
            {
                "id": "complete_ikcharacter_brdf_order",
                "severity": "high",
                "status": "partially_source_exact",
                "detail": (
                    "The local AO/shadow, layered metallic/specular, half-"
                    "Lambert, ShadowingGIGain, back-rim, and color-process "
                    "orders are literal "
                    "for 514/594. The retained off-screen stage closes the "
                    "directional transform, selectable direct/GI/rim values, "
                    "and diffuse-probe payload. ReceiveShadow textures, the "
                    "transform-to-buffer convention, calibrated exposure, and "
                    "final scene-level order remain reconstructed."),
            },
            {
                "id": "ikcharacter_eye_scene_boundary",
                "severity": "medium",
                "status": "material_local_eye_math_source_exact",
                "detail": (
                    "All 428 selected IkCharacter eye materials enable source "
                    "parallax and Ng iris refraction; 366 carry nonzero parallax "
                    "height, six carry non-unit IOR, and 188 require an eyelid "
                    "shadow map. The compiled static eyelid and highlight order "
                    "is exact for both base and shadow color, and the complete "
                    "view-dependent parallax march is literal on all three "
                    "backends. The recovered source stage supplies its global "
                    "diffuse/light records; shadow terms and final framebuffer "
                    "order remain reconstructed or unknown."),
            },
            {
                "id": "mega_gengar_upward_noise",
                "severity": "medium",
                "status": "source_resource_retained_runtime_gap",
                "detail": (
                    "Four Mega Gengar body materials select variation 1650 "
                    "and retain NoiseSourceMap plus the authored upward-noise "
                    "parameters. Phlosion does not yet execute that animated "
                    "object-space emission branch."),
            },
            {
                "id": "rim_composite_scale",
                "severity": "medium",
                "status": "source_stage_closed_generic_review_calibrated",
                "detail": (
                    "Raw authored rim masks and intensities are preserved in "
                    "the asset. Source Stage applies the retained category rim "
                    "color (black for the selected category-6 corpus); generic "
                    "review profiles retain an explicit 0.25 scale."),
            },
            {
                "id": "anonymous_scene_resources",
                "severity": "high",
                "status": "major_offscreen_stage_recovered_remaining_gaps_named",
                "detail": (
                    "The retained off-screen package provides the scene-light "
                    "record plus diffuse/specular global probes. Projected and "
                    "cascaded shadow payloads/transforms, the exact shader-buffer "
                    "binding convention, final 3D LUT, and final color-matrix "
                    "values remain unavailable."),
            },
        ],
        "source_sha256": {
            "catalog": sha256(catalog_path),
            "selected_program_abi": sha256(abi_path),
            "option_graph": sha256(option_graph_path),
            "ik_eye_runtime_coverage": sha256(eye_coverage_path),
            "ik_character_dataflow": sha256(dataflow_path),
            "ui_offscreen_light": sha256(ui_light_path),
            "ui_offscreen_diffuse_probe": sha256(ui_diffuse_path),
            "ui_offscreen_specular_probe": sha256(ui_specular_path),
            **runtime_sources,
        },
        "models": model_rows,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report["summary"], indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
