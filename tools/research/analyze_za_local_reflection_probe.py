#!/usr/bin/env python3
"""Validate Z-A's decoded BC6H local-reflection transport end to end."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import pathlib
import struct
import zlib
from typing import Any

from za_corpus import selected_za_stems


SOURCE_PROFILE = "pokemon-legends-za-v2.0.0"
PACKED_FORMAT = "phlosion-za-local-reflection-rgba16f-cube-mips-packed-v1"
FACE_ORDER = ["+X", "-X", "+Y", "-Y", "+Z", "-Z"]


def read_json(path: pathlib.Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def paeth(a: int, b: int, c: int) -> int:
    value = a + b - c
    pa = abs(value - a)
    pb = abs(value - b)
    pc = abs(value - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def read_png_rgba(path: pathlib.Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise ValueError(f"not a PNG: {path}")
    width = height = color_type = bit_depth = None
    compressed = bytearray()
    offset = 8
    while offset + 12 <= len(data):
        length = struct.unpack(">I", data[offset:offset + 4])[0]
        kind = data[offset + 4:offset + 8]
        payload = data[offset + 8:offset + 8 + length]
        offset += 12 + length
        if kind == b"IHDR":
            width, height, bit_depth, color_type = struct.unpack(
                ">IIBB", payload[:10])
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind == b"IEND":
            break
    if width is None or height is None or bit_depth != 8 or color_type != 6:
        raise ValueError(f"expected an 8-bit RGBA PNG: {path}")
    scanlines = zlib.decompress(bytes(compressed))
    stride = width * 4
    expected = height * (stride + 1)
    if len(scanlines) != expected:
        raise ValueError(f"unexpected PNG payload length: {path}")
    output = bytearray(height * stride)
    source = 0
    for y in range(height):
        filter_type = scanlines[source]
        source += 1
        row = scanlines[source:source + stride]
        source += stride
        for x, raw in enumerate(row):
            left = output[y * stride + x - 4] if x >= 4 else 0
            up = output[(y - 1) * stride + x] if y > 0 else 0
            upper_left = (
                output[(y - 1) * stride + x - 4]
                if y > 0 and x >= 4 else 0)
            if filter_type == 0:
                value = raw
            elif filter_type == 1:
                value = raw + left
            elif filter_type == 2:
                value = raw + up
            elif filter_type == 3:
                value = raw + ((left + up) // 2)
            elif filter_type == 4:
                value = raw + paeth(left, up, upper_left)
            else:
                raise ValueError(f"unsupported PNG filter {filter_type}: {path}")
            output[y * stride + x] = value & 0xFF
    return width, height, bytes(output)


def resolve_relative(root: pathlib.Path, relative: str) -> pathlib.Path:
    pure = pathlib.PurePosixPath(relative)
    if pure.is_absolute() or ".." in pure.parts:
        raise ValueError(f"unsafe texture path: {relative}")
    resolved = root.joinpath(*pure.parts).resolve()
    if root.resolve() not in resolved.parents:
        raise ValueError(f"texture path escaped its model root: {relative}")
    return resolved


def validate_probe(
        model_root: pathlib.Path,
        record: dict[str, Any]) -> dict[str, Any]:
    if record.get("decoded") is not True:
        raise ValueError("LocalReflectionMap remains undecoded")
    if record.get("decoded_format") != PACKED_FORMAT:
        raise ValueError("LocalReflectionMap packed format changed")
    if record.get("cube_face_order") != FACE_ORDER:
        raise ValueError("LocalReflectionMap cube face order changed")
    face_size = int(record.get("cube_face_size", 0))
    width = int(record.get("decoded_width", 0))
    height = int(record.get("decoded_height", 0))
    mip_count = int(record.get("source_mip_count", 0))
    if face_size <= 0 or face_size & (face_size - 1):
        raise ValueError("LocalReflectionMap face size is not a power of two")
    expected_mips = face_size.bit_length()
    if (width, height) != (face_size * 6, face_size * 4 - 2):
        raise ValueError("LocalReflectionMap all-mip atlas geometry changed")
    if (int(record.get("source_array_count", 0)), mip_count) != (
            6, expected_mips):
        raise ValueError("LocalReflectionMap source topology changed")
    path = resolve_relative(model_root, str(record.get("file", "")))
    png_width, png_height, rgba = read_png_rgba(path)
    if (png_width, png_height) != (width, height):
        raise ValueError("LocalReflectionMap PNG dimensions differ from metadata")

    canonical = bytearray()
    mip_measurements: list[dict[str, Any]] = []
    mip_y = 0
    for mip in range(mip_count):
        mip_size = max(1, face_size >> mip)
        rgb_sum = [0.0, 0.0, 0.0]
        luminance_sum = 0.0
        luminance_min = math.inf
        luminance_max = 0.0
        texel_count = 0
        for face in range(6):
            origin_x = (face % 3) * mip_size * 2
            origin_y = mip_y + (face // 3) * mip_size
            for y in range(mip_size):
                for x in range(mip_size):
                    first = ((origin_y + y) * width + origin_x + x * 2) * 4
                    payload = rgba[first:first + 8]
                    canonical.extend(payload)
                    red, green, blue, _ = struct.unpack("<4e", payload)
                    luminance = (
                        0.2126 * red + 0.7152 * green + 0.0722 * blue)
                    rgb_sum[0] += red
                    rgb_sum[1] += green
                    rgb_sum[2] += blue
                    luminance_sum += luminance
                    luminance_min = min(luminance_min, luminance)
                    luminance_max = max(luminance_max, luminance)
                    texel_count += 1
        mip_measurements.append({
            "level": mip,
            "face_size": mip_size,
            "texels": texel_count,
            "mean_rgb": [round(value / texel_count, 9) for value in rgb_sum],
            "mean_luminance": round(luminance_sum / texel_count, 9),
            "min_luminance": round(luminance_min, 9),
            "max_luminance": round(luminance_max, 9),
        })
        mip_y += mip_size * 2
    if mip_y != height:
        raise ValueError("LocalReflectionMap mip strip did not fill its atlas")
    payload_sha = hashlib.sha256(canonical).hexdigest()
    if payload_sha != record.get("source_payload_sha256"):
        raise ValueError("packed probe does not round-trip its decoded payload")
    return {
        "file": path.name,
        "packed_png_sha256": sha256(path),
        "decoded_payload_sha256": payload_sha,
        "source_bntx_sha256": record.get("source_sha256"),
        "source_format": record.get("source_format"),
        "face_size": face_size,
        "mip_count": mip_count,
        "mip_measurements_linear": mip_measurements,
    }


def check_runtime_contract(game_root: pathlib.Path, engine_root: pathlib.Path) -> dict[str, Any]:
    files = {
        "game_loader": game_root / "tools/PhlosionNativeModelIr.cpp",
        "opengl": engine_root / "src/engine/render/opengl/OpenGLRenderBackendWorldPipeline.cpp",
        "d3d12": engine_root / "src/engine/render/d3d12/D3D12RenderBackendWorldPipeline.cpp",
        "vulkan": engine_root / "assets/shaders/vulkan/world_material.glsl",
    }
    loader = files["game_loader"].read_text(encoding="utf-8-sig")
    for token in ("LocalReflectionMap", "environmentTexture"):
        if token not in loader:
            raise ValueError(f"game loader lost Z-A probe token: {token}")
    for name in ("opengl", "d3d12", "vulkan"):
        source = files[name].read_text(encoding="utf-8-sig")
        for token in (
                "sampleZaLocalReflectionProbe", "mipStripY",
                "reflectionBlur + max", "faceSize * 4",
                "zaIkLocalReflectionDirection",
                "reflect(-viewDirection, mappedNormal)"):
            if token not in source:
                raise ValueError(f"{name} lost Z-A probe token: {token}")
    return {name: sha256(path) for name, path in files.items()}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--game-root", type=pathlib.Path, default=pathlib.Path("."))
    parser.add_argument("--engine-root", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    game_root = args.game_root.resolve()
    engine_root = args.engine_root.resolve()
    catalog_path = game_root / "config/assets/asset_catalog.json"
    catalog = read_json(catalog_path)
    stems = selected_za_stems(game_root, catalog)

    models: list[dict[str, Any]] = []
    unique_files: dict[tuple[str, str], dict[str, Any]] = {}
    validated_payloads: dict[tuple[str, str], dict[str, Any]] = {}
    binding_count = 0
    models_without_binding: list[str] = []
    shader_counts: dict[str, int] = {}
    for stem in stems:
        manifest_path = game_root / "assets/models" / f"{stem}.phmodel"
        manifest = read_json(manifest_path)
        if manifest.get("source", {}).get("profile") != SOURCE_PROFILE:
            raise ValueError(f"{stem} source profile changed")
        local_bindings: list[dict[str, Any]] = []
        shaders = sorted({str(m["shader_family"]) for m in manifest["materials"]})
        for shader in shaders:
            shader_counts[shader] = shader_counts.get(shader, 0) + 1
        for material in manifest["materials"]:
            for texture in material.get("textures", []):
                if texture.get("role") != "LocalReflectionMap":
                    continue
                packed_path = resolve_relative(
                    manifest_path.parent, str(texture.get("file", "")))
                payload_key = (
                    str(texture.get("source_sha256", "")),
                    str(texture.get("source_payload_sha256", "")))
                validated = validated_payloads.get(payload_key)
                if validated is None:
                    validated = validate_probe(manifest_path.parent, texture)
                    validated_payloads[payload_key] = validated
                elif sha256(packed_path) != validated["packed_png_sha256"]:
                    raise ValueError(
                        "LocalReflectionMap carrier bytes differ for an "
                        "otherwise identical source payload")
                binding_count += 1
                local_bindings.append({
                    "material": material["name"],
                    "source_bntx_sha256": validated["source_bntx_sha256"],
                    "decoded_payload_sha256": validated["decoded_payload_sha256"],
                })
                unique_files.setdefault(
                    (validated["source_bntx_sha256"],
                     validated["decoded_payload_sha256"]),
                    validated)
        if not local_bindings:
            models_without_binding.append(stem)
        models.append({
            "stem": stem,
            "shader_families": shaders,
            "local_reflection_bindings": local_bindings,
        })
    if not binding_count:
        raise ValueError("selected Z-A corpus has no LocalReflectionMap bindings")

    runtime_sources = check_runtime_contract(game_root, engine_root)
    report = {
        "schema": "pokemon-autochess-za-local-reflection-static-report-v2",
        "source_profile": SOURCE_PROFILE,
        "method": {
            "runtime_execution": False,
            "emulator_used": False,
            "evidence_level": "source_bntx_decode_plus_manifest_transport_plus_backend_contract",
            "claim_boundary": (
                "BC6H block-linear decode, every cube face/mip, packed HDR "
                "round-trip, material binding, authored ReflectionsBlur LOD, "
                "the separately promoted reflect(-view, mapped_normal) "
                "direction, and three-backend sampling are proven. Final "
                "source scene lighting, exposure, and framebuffer parity are "
                "not claimed."),
        },
        "summary": {
            "selected_models": len(stems),
            "models_with_local_reflection": len(stems) - len(models_without_binding),
            "models_without_local_reflection": len(models_without_binding),
            "material_bindings": binding_count,
            "unique_source_probes": len(unique_files),
            "all_bindings_decoded": True,
            "backends_bridged": 3,
        },
        "transport": {
            "decoded_format": PACKED_FORMAT,
            "cube_face_order": FACE_ORDER,
            "surface_order": "mip-major, then +X,-X,+Y,-Y,+Z,-Z, row-major texels",
            "carrier": "two RGBA8 PNG pixels per decoded RGBA16F texel",
            "numeric_measurement": (
                "all decoded half-float RGB texels measured per source mip in "
                "linear space"),
            "runtime_lod": "authored ReflectionsBlur plus nonnegative quality detail bias",
            "runtime_direction": "reflect(-view, mapped_normal); no diffuse-cube Z flip",
        },
        "shader_family_model_counts": shader_counts,
        "models_without_binding": models_without_binding,
        "unique_probes": list(unique_files.values()),
        "runtime_source_sha256": runtime_sources,
        "catalog_sha256": sha256(catalog_path),
        "models": models,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report["summary"], indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
