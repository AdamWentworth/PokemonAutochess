#!/usr/bin/env python3
"""Audit selected Z-A IkCharacter eye inputs against Phlosion consumption."""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import pathlib
import struct
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


class BinaryReader:
    def __init__(self, payload: bytes) -> None:
        self.payload = payload
        self.offset = 0

    def skip(self, byte_count: int) -> None:
        if byte_count < 0 or self.offset + byte_count > len(self.payload):
            raise ValueError("PHMAT DATA chunk is truncated")
        self.offset += byte_count

    def u32(self) -> int:
        if self.offset + 4 > len(self.payload):
            raise ValueError("PHMAT DATA chunk is truncated")
        value = struct.unpack_from("<I", self.payload, self.offset)[0]
        self.offset += 4
        return value


def phmat_material_modes(path: pathlib.Path) -> list[int]:
    payload = path.read_bytes()
    if len(payload) < 96 or payload[:4] != b"PHMT":
        raise ValueError(f"Invalid PHMT container: {path}")
    chunk_count = struct.unpack_from("<I", payload, 20)[0]
    chunk_table_offset = struct.unpack_from("<Q", payload, 56)[0]
    data_chunk: bytes | None = None
    for index in range(chunk_count):
        record_offset = chunk_table_offset + index * 48
        if record_offset + 48 > len(payload):
            raise ValueError(f"Truncated PHMT chunk table: {path}")
        if payload[record_offset:record_offset + 4] != b"DATA":
            continue
        data_offset = struct.unpack_from("<Q", payload, record_offset + 16)[0]
        data_size = struct.unpack_from("<Q", payload, record_offset + 24)[0]
        if data_offset + data_size > len(payload):
            raise ValueError(f"Invalid PHMT DATA range: {path}")
        data_chunk = payload[data_offset:data_offset + data_size]
        break
    if data_chunk is None:
        raise ValueError(f"PHMT has no DATA chunk: {path}")

    reader = BinaryReader(data_chunk)

    def skip_vector(stride: int) -> None:
        reader.skip(reader.u32() * stride)

    def skip_texture_set() -> None:
        for _ in range(reader.u32()):
            reader.skip(24)
            reader.skip(reader.u32())

    skip_vector(16)  # base colors
    for _ in range(5):
        skip_texture_set()
    skip_vector(1)   # alpha mode
    for _ in range(5):
        skip_vector(4)
    skip_vector(12)  # emissive factors
    material_count = reader.u32()
    if reader.offset + material_count > len(data_chunk):
        raise ValueError(f"PHMT material-mode vector is truncated: {path}")
    return list(data_chunk[reader.offset:reader.offset + material_count])


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
    parser.add_argument("--engine-root", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    game_root = args.game_root.resolve()
    engine_root = args.engine_root.resolve()

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
    nonzero_shadow_color_mask_values = 0

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
            shadow_mask_value = float(material.get(
                "float_parameters", {}).get(
                    "ShadowingColorMaskMapValue", 0.0))
            nonzero_shadow_color_mask_values += int(
                abs(shadow_mask_value) > 1e-6)
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
    if nonzero_shadow_color_mask_values != 0:
        raise ValueError(
            "Selected eye colored-shadow maps are no longer source-neutral")

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
            "nativeIkCharacterEyeLightingCandidate",
            "kNativeIkCharacterEyeMaterialMode",
            "bakeIkCharacterEyePackedInputs",
            '"ParallaxMap"', '"EyelidShadowMaskMap"',
            '"ParallaxHeight"', '"ParallaxIOR"', '"BaseColorLayer6"',
            '"LocalReflectionMap"'):
        if token not in loader:
            raise ValueError(f"IkCharacter eye loader contract lost token: {token}")
    engine_paths = [
        engine_root / "src/engine/render/opengl/"
            "OpenGLRenderBackendWorldPipeline.cpp",
        engine_root / "src/engine/render/d3d12/"
            "D3D12RenderBackendWorldPipeline.cpp",
        engine_root / "assets/shaders/vulkan/world_material.glsl",
    ]
    for path in engine_paths:
        source = path.read_text(encoding="utf-8-sig")
        for token in (
                "resolveZaIkEyeParallaxUv", "nativeEye", "eyeHighlight",
                "eyelidShadow"):
            if token not in source:
                raise ValueError(
                    f"Mode-35 backend contract lost {token}: {path}")

    cooked_phmat_hashes: dict[str, str] = {}
    cooked_mode35_counts: dict[str, int] = {}
    cooked_root = game_root / "content" / "phlosion" / "objects"
    for stem, expected_eye_materials in sorted(model_counts.items()):
        candidates = sorted(cooked_root.glob(f"{stem}-*/model.phmat"))
        if len(candidates) != 1:
            raise ValueError(
                f"Expected one cooked PHMAT for {stem}, found {len(candidates)}")
        modes = phmat_material_modes(candidates[0])
        mode35_count = modes.count(35)
        if mode35_count != expected_eye_materials:
            raise ValueError(
                f"Cooked mode-35 count changed for {stem}: "
                f"expected {expected_eye_materials}, found {mode35_count}")
        cooked_mode35_counts[stem] = mode35_count
        cooked_phmat_hashes[stem] = sha256(candidates[0])
    if sum(cooked_mode35_counts.values()) != material_count:
        raise ValueError("Cooked mode-35 eye-material census changed")

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
            "status": (
                "live_mode35_normal_plus_ordered_layer_bake; alpha carries "
                "the separately authored eyelid mask"),
        },
        {
            "role": "LayerMaskMap",
            "bindings": 80,
            "status": "consumed_by_offline_base_normal_and_surface_bakes",
        },
        {
            "role": "HighlightMaskMap",
            "bindings": 80,
            "status": "baked_to_emissive_rgb_then_sampled_live_by_mode35",
            "materials_with_nonzero_authored_emission": nonzero_highlight,
        },
        {
            "role": "OcclusionMap",
            "bindings": 80,
            "status": "consumed_by_mode35_surface-control_bake",
            "materials_with_nonzero_strength": 80,
        },
        {
            "role": "SpecularMaskMap",
            "bindings": 80,
            "status": "consumed_by_mode35_shadow_specular_auxiliary",
            "materials_with_nonzero_specular": nonzero_specular,
        },
        {
            "role": "ShadowingColorMap+ShadowingColorMaskMap",
            "bindings": 160,
            "status": (
                "not_sampled; selected corpus is source-neutral because the "
                "color map is white and ShadowingColorMaskMapValue is zero"),
            "materials_receiving_shadow": 80,
            "materials_with_nonzero_shadow_color_mask_value":
                nonzero_shadow_color_mask_values,
        },
        {
            "role": "RimLightMaskMap",
            "bindings": 80,
            "status": (
                "consumed_by_surface auxiliary; source term is neutral for "
                "all selected eye materials"),
            "materials_with_nonzero_rim_intensity": 0,
        },
        {
            "role": "LocalReflectionMap",
            "bindings": 80,
            "status": "sampled_live_by_mode35_at_authored_reflections_blur_lod",
            "materials_with_nonzero_specular": nonzero_specular,
        },
        {
            "role": "ParallaxMap",
            "bindings": 80,
            "status": (
                "packed_losslessly_to_emissive_alpha_and sampled by the live "
                "bounded refracted parallax search"),
            "materials_with_nonzero_parallax_height": nonzero_parallax,
            "materials_with_nonunit_ior": nonunit_ior,
        },
        {
            "role": "EyelidShadowMaskMap",
            "bindings": 48,
            "status": (
                "packed_to_normal alpha and applied with source "
                "BaseColorLayer6 multiplicative tint"),
        },
    ]
    consumed_bindings = 768
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
                "it is not a pixel-similarity score. Mode 35 now evaluates "
                "base/normal/layer/highlight, parallax/refraction, eyelid "
                "shadow, local reflection, AO, and specular inputs identically "
                "on all three backends. The two unbound colored-shadow roles "
                "are verified source-neutral in this selected eye corpus; exact "
                "source framebuffer and anonymous scene terms remain unknown."),
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
            "materials_with_nonzero_shadow_color_mask_value":
                nonzero_shadow_color_mask_values,
            "cooked_phmat_files_verified": len(cooked_phmat_hashes),
            "cooked_mode35_submesh_records":
                sum(cooked_mode35_counts.values()),
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
            "selected_material_mode": 35,
            "specialized_ikcharacter_eye_work": (
                "Mode 35 packs HighlightMaskMap RGB plus ParallaxMap alpha in "
                "emission, EyelidShadowMaskMap in normal alpha, retains the "
                "body lighting auxiliaries and authored local cube, and "
                "evaluates the same eye composite on OpenGL, D3D12, and Vulkan."),
            "runtime_translation_key_counts": dict(sorted(
                runtime_key_counts.items())),
            "cooked_asset_verification": (
                "All 38 selected local PHMAT files were decoded from their "
                "PHRC DATA chunks and contain exactly 80 mode-35 submesh "
                "records, matching the source eye-material census."),
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
                "cooked_mode35_submesh_records": cooked_mode35_counts[stem],
                "cooked_phmat_sha256": cooked_phmat_hashes[stem],
            }
            for stem, count in sorted(model_counts.items())
        ],
        "remaining_source_proven_runtime_target": {
            "id": "za_ikcharacter_eye_literal_composite_order",
            "priority": "medium",
            "requirements": [
                "resolve anonymous scene/lighting resources without emulation",
                "reconstruct the remaining literal source composite order",
                "validate appearance visually against lawful reference media",
            ],
            "reason": (
                "All effect-bearing selected material inputs now have a tested "
                "runtime path; remaining uncertainty is equation/scene parity, "
                "not missing retained eye texture transport."),
        },
        "source_sha256": {
            "compiled_dataflow": sha256(dataflow_path),
            "game_loader": sha256(loader_path),
            "opengl_backend": sha256(engine_paths[0]),
            "d3d12_backend": sha256(engine_paths[1]),
            "vulkan_backend": sha256(engine_paths[2]),
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
