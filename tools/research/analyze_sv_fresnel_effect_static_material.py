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
import struct
import zlib
from typing import Any


SCHEMA = "pokemon-autochess-sv-fresnel-effect-static-material-evidence-v2"
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
PACKED_PROBE_FORMAT = (
    "phlosion-sv-local-specular-probe-rgba16f-cube-packed-v1")
PACKED_PROBE_FACE_ORDER = ["+X", "-X", "+Y", "-Y", "+Z", "-Z"]


def read_json(path: pathlib.Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_png_rgba(path: pathlib.Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"Packed probe is not PNG: {path}")
    offset = 8
    width = height = 0
    compressed = bytearray()
    while offset + 12 <= len(data):
        length = struct.unpack_from(">I", data, offset)[0]
        kind = data[offset + 4:offset + 8]
        payload = data[offset + 8:offset + 8 + length]
        offset += 12 + length
        if kind == b"IHDR":
            width, height, depth, color, compression, filtering, interlace = (
                struct.unpack(">IIBBBBB", payload))
            if (depth, color, compression, filtering, interlace) != (
                    8, 6, 0, 0, 0):
                raise ValueError(f"Packed probe PNG encoding changed: {path}")
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind == b"IEND":
            break
    stride = width * 4
    filtered = zlib.decompress(compressed)
    if len(filtered) != (stride + 1) * height:
        raise ValueError(f"Packed probe PNG byte count changed: {path}")
    output = bytearray(stride * height)
    source_offset = 0
    for y in range(height):
        filter_type = filtered[source_offset]
        source_offset += 1
        row = filtered[source_offset:source_offset + stride]
        source_offset += stride
        previous = output[(y - 1) * stride:y * stride] if y else bytes(stride)
        for x, raw in enumerate(row):
            left = output[y * stride + x - 4] if x >= 4 else 0
            above = previous[x]
            upper_left = previous[x - 4] if x >= 4 else 0
            if filter_type == 0:
                value = raw
            elif filter_type == 1:
                value = raw + left
            elif filter_type == 2:
                value = raw + above
            elif filter_type == 3:
                value = raw + ((left + above) >> 1)
            elif filter_type == 4:
                estimate = left + above - upper_left
                distances = (
                    abs(estimate - left),
                    abs(estimate - above),
                    abs(estimate - upper_left))
                predictor = (left, above, upper_left)[distances.index(min(distances))]
                value = raw + predictor
            else:
                raise ValueError(f"Unsupported packed probe PNG filter: {filter_type}")
            output[y * stride + x] = value & 0xFF
    return width, height, bytes(output)


def validate_packed_probe(
        manifest_path: pathlib.Path,
        texture: dict[str, Any]) -> dict[str, Any]:
    if texture.get("decoded") is not True:
        raise ValueError("LocalSpecularProbe is no longer decoded")
    if texture.get("decoded_format") != PACKED_PROBE_FORMAT:
        raise ValueError("LocalSpecularProbe packed format changed")
    if texture.get("cube_face_order") != PACKED_PROBE_FACE_ORDER:
        raise ValueError("LocalSpecularProbe face order changed")
    face_size = int(texture.get("cube_face_size", 0))
    width = int(texture.get("decoded_width", 0))
    height = int(texture.get("decoded_height", 0))
    if face_size <= 0 or width != face_size * 6 or height != face_size * 2:
        raise ValueError("LocalSpecularProbe atlas dimensions changed")
    if (texture.get("source_array_count"), texture.get("source_mip_count")) != (6, 1):
        raise ValueError("LocalSpecularProbe source topology changed")
    relative = pathlib.PurePosixPath(str(texture.get("file", "")))
    if relative.is_absolute() or ".." in relative.parts:
        raise ValueError("LocalSpecularProbe resource path is unsafe")
    path = manifest_path.parent.joinpath(*relative.parts)
    png_width, png_height, rgba = read_png_rgba(path)
    if (png_width, png_height) != (width, height):
        raise ValueError("LocalSpecularProbe PNG dimensions differ from metadata")
    linear = bytearray()
    for face in range(6):
        origin_x = (face % 3) * face_size * 2
        origin_y = (face // 3) * face_size
        for y in range(face_size):
            for x in range(face_size):
                first = ((origin_y + y) * width + origin_x + x * 2) * 4
                linear.extend(rgba[first:first + 8])
    payload_sha = hashlib.sha256(linear).hexdigest()
    if payload_sha != texture.get("source_payload_sha256"):
        raise ValueError("Packed probe PNG does not losslessly round-trip source payload")
    return {
        "decoded_format": PACKED_PROBE_FORMAT,
        "atlas_width": width,
        "atlas_height": height,
        "cube_face_size": face_size,
        "cube_face_order": PACKED_PROBE_FACE_ORDER,
        "source_format": texture.get("source_format"),
        "source_array_count": 6,
        "source_mip_count": 1,
        "source_payload_sha256": payload_sha,
        "packed_png_sha256": sha256(path),
    }


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
    probe = validate_packed_probe(
        manifest_path, roles["LocalSpecularProbe"])
    return {
        "stem": stem,
        "material": name,
        "manifest_sha256": sha256(manifest_path),
        "base_color": vec4["BaseColor"],
        "base_color_layer_1": vec4["BaseColorLayer1"],
        "float_parameters": {key: floats[key] for key in FLOAT_PARAMETERS},
        "local_specular_probe": probe,
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
                "is block-linear deswizzled and losslessly transported as its "
                "demonstrated RGBA16F runtime alias. Final source framebuffer "
                "parity is still not claimed without anonymous scene inputs."
            ),
        },
        "summary": {
            "materials_checked": len(materials),
            "exact_programs": 1,
            "mapped_material_textures": 6,
            "mapped_material_constants": 12,
            "runtime_material_mode": MODE,
            "remaining_undecoded_authored_resources": 0,
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
            {"role": "LocalSpecularProbe", "sampler": "fp_t_tcb_18", "components": "xyz", "color_space": "linear_hdr_rgba16f"},
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
            "local_probe_status": "lossless RGBA16F six-face cube decode; authored base mip sampled directly",
        },
        "runtime_bridge": {
            "mode": MODE,
            "runtime_constant": "kNativeFresnelEffectMaterialMode",
            "primary_map_slot": "base_color_texture_srgb",
            "secondary_map_slot": "emissive_texture_linear",
            "normal_map_slot": "normal_texture_linear",
            "ao_map_slot": "occlusion_texture_linear",
            "quality_policy": "retain foundational maps; vary explicit texture-detail LOD bias",
            "local_probe_slot": "environment_texture_linear_packed_rgba16f_cube",
            "local_probe_fallback": "shared neutral environment only when authored packed cube is absent",
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
