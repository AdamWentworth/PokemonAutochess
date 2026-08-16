#!/usr/bin/env python3
"""Audit retained Kanto Z-A IkCharacter evidence and runtime boundaries."""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import pathlib
from typing import Any


SCHEMA = "pokemon-autochess-za-ik-character-static-material-evidence-v1"
SOURCE_PROFILE = "pokemon-legends-za-v2.0.0"
PACKED_PROBE_FORMAT = (
    "phlosion-za-local-reflection-rgba16f-cube-mips-packed-v1")
EXPECTED_VARIATIONS = {514: 140, 594: 2, 682: 32, 1214: 48}
EXPECTED_ROLE_COUNTS = {
    "BaseColorMap": 222,
    "NormalMap": 222,
    "OcclusionMap": 222,
    "SpecularMaskMap": 222,
    "ShadowingColorMap": 222,
    "ShadowingColorMaskMap": 222,
    "RimLightMaskMap": 222,
    "LayerMaskMap": 222,
    "LocalReflectionMap": 218,
    "ParallaxMap": 80,
    "HighlightMaskMap": 80,
    "EyelidShadowMaskMap": 48,
    "DisplacementMap": 2,
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
}


def read_json(path: pathlib.Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def selected_za_stems(catalog: dict[str, Any]) -> list[str]:
    rows = [
        row for row in catalog.get("native_import_sets", [])
        if row.get("recipe") == "tools/assets/gamefreak_pokemon_imports_za.json"
    ]
    if len(rows) != 1 or rows[0].get("selection") != "include_stems":
        raise ValueError("Canonical Z-A catalog selection changed")
    stems = [str(value) for value in rows[0].get("stems", [])]
    if len(stems) != 52 or len(stems) != len(set(stems)):
        raise ValueError("Canonical Z-A stem set changed")
    return stems


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
            "kNativeIkCharacterMaterialMode",
            "LocalReflectionMap",
            "kNativeRimCompositeScale",
            "nativeIkCharacterSurfaceProfile"):
        if token not in loader:
            raise ValueError(f"IkCharacter loader contract lost token: {token}")
    for name in ("opengl", "d3d12", "vulkan"):
        source = files[name].read_text(encoding="utf-8-sig")
        for token in (
                "sampleZaLocalReflectionProbe",
                "evaluateNativeIkCharacter" if name == "vulkan"
                else "applyNativeIkCharacter",
                "reflectionBlur",
                "surfaceProfile"):
            if token not in source:
                raise ValueError(
                    f"{name} IkCharacter contract lost token: {token}")
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
    stems = selected_za_stems(catalog)
    role_counts: collections.Counter[str] = collections.Counter()
    role_undecoded: collections.Counter[str] = collections.Counter()
    material_class_counts: collections.Counter[str] = collections.Counter()
    parameter_presence: collections.Counter[str] = collections.Counter()
    material_count = 0
    model_count = 0
    unique_probe_sources: set[str] = set()
    unique_probe_payloads: set[str] = set()
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

    if material_count != 222 or model_count != 52:
        raise ValueError("Retained IkCharacter material/model census changed")
    if dict(role_counts) != EXPECTED_ROLE_COUNTS:
        raise ValueError(
            f"IkCharacter texture-role census changed: {dict(role_counts)}")
    if role_undecoded:
        raise ValueError(
            f"IkCharacter authored textures remain undecoded: {role_undecoded}")
    if material_class_counts != {
            "core_body": 140, "displacement": 2, "eye_options": 80}:
        raise ValueError(
            f"IkCharacter material classes changed: {material_class_counts}")

    abi_path = game_root / "docs" / "kanto" / "evidence" / (
        "za_kanto_selected_program_abi.json")
    option_graph_path = game_root / "docs" / "kanto" / "evidence" / (
        "za_kanto_option_graph.json")
    eye_coverage_path = game_root / "docs" / "kanto" / "evidence" / (
        "za_ik_eye_runtime_coverage.json")
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
            summary.get("differential_count") != 144):
        raise ValueError("Z-A exact option graph is no longer complete")
    eye_coverage = read_json(eye_coverage_path)
    if (eye_coverage.get("schema") !=
            "pokemon-autochess-za-ik-eye-runtime-coverage-v1" or
            eye_coverage.get("summary", {}).get("selected_eye_materials") != 80):
        raise ValueError("Z-A IkCharacter eye runtime coverage changed")
    runtime_sources = source_contract(game_root, engine_root)

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
                "transport are proven. Phlosion's complete IkCharacter BRDF, "
                "live IkCharacter eye parallax/eyelid-shadow composite, "
                "rim/fibre scales, anonymous scene resources, and final "
                "source framebuffer remain reconstructed or unknown."),
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
            "complete_option_graph_edges": 144,
            "ikcharacter_eye_materials": 80,
            "unconsumed_ikcharacter_eye_texture_bindings": 608,
            "backends_bridged": 3,
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
                "SpecularContrast", "Metallic", "ShadowStrength",
                "HalfLambertBias", "ShadowingGIGain", "RimLightOffset",
                "RimLightContrast", "RimLightIntensity",
                "BackRimLightIntensity", "ReflectionsBlur",
                "DiffusionLevels")
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
                "AO, metallic, specular offset/contrast, shadow color, and rim "
                "masks packed without dropping material boundaries"),
            "local_reflection": (
                "decoded authored BC6H cube with all eight mips and "
                "ReflectionsBlur LOD"),
            "eye_options": (
                "base, normal, ordered layers, and highlight are consumed; "
                "live parallax/refraction, eyelid shadow, local reflection, "
                "authored AO, and colored-shadow inputs remain unbridged"),
            "backends": ["opengl", "d3d12", "vulkan"],
        },
        "remaining_equation_gaps": [
            {
                "id": "complete_ikcharacter_brdf_order",
                "severity": "critical",
                "status": "reconstructed",
                "detail": (
                    "Direct/specular/diffuse/color-process ordering is not yet "
                    "a literal port of the 514/594/682/1214 compiled programs."),
            },
            {
                "id": "ikcharacter_eye_live_composite",
                "severity": "critical",
                "status": "partially_baked",
                "detail": (
                    "All 80 selected IkCharacter eye materials enable source "
                    "parallax and Ng iris refraction; 70 carry nonzero parallax "
                    "height and 48 require an eyelid-shadow map. Phlosion "
                    "currently bakes the authored highlight but does not sample "
                    "the parallax or eyelid-shadow textures at runtime."),
            },
            {
                "id": "fibre_feather_response",
                "severity": "critical",
                "status": "narrow_visual_reconstruction",
                "detail": (
                    "No dedicated authored strand-direction texture exists in "
                    "the retained materials; current fur/feather sheen is "
                    "qualified per atlas rather than source-equation complete."),
            },
            {
                "id": "rim_composite_scale",
                "severity": "high",
                "status": "visual_reconstruction",
                "detail": (
                    "The authored rim masks and intensities are preserved, but "
                    "Phlosion's final composite uses a 0.25 scale chosen for its "
                    "different exposure domain."),
            },
            {
                "id": "anonymous_scene_resources",
                "severity": "high",
                "status": "unavailable_in_retained_archives",
                "detail": (
                    "Diffuse irradiance, shadow arrays, scene lighting, exposure, "
                    "and post-processing values are not named or bound by loose "
                    "model assets."),
            },
        ],
        "source_sha256": {
            "catalog": sha256(catalog_path),
            "selected_program_abi": sha256(abi_path),
            "option_graph": sha256(option_graph_path),
            "ik_eye_runtime_coverage": sha256(eye_coverage_path),
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
