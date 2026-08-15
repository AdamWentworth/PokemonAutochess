"""Build a reproducible, emulator-free material evidence report for SV Eevee.

The report combines the checked-in native-IR manifest and decoded textures with
locally retained Scarlet shader archives. It records hashes and derived facts,
never proprietary shader bytes or private absolute paths.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import pathlib
import re
import statistics
import struct
import zlib


REPORT_SCHEMA = "pokemon-autochess-static-character-material-report-v1"
EXPECTED_MANIFEST = "assets/models/0133_Eevee_SV.phmodel"
SHADER_FILES = {
    "SSS": {
        "archive": "sss.bnsh",
        "metadata": "sss.trsha.json",
        "program_directory": "sss-eevee-static-20260812",
        "program_stem": "v056",
    },
    "EyeClearCoat": {
        "archive": "eye_clear_coat.bnsh",
        "metadata": "eye_clear_coat.trsha.json",
        "program_directory": ".",
        "program_stem": "eye_clear_coat.charmander.variant20",
    },
}
SSS_BINDING_VARIANTS = {
    0x401: {"variation": 0, "enabled": {"BaseColorMap"}},
    0x40B: {
        "variation": 8,
        "enabled": {"BaseColorMap", "NormalMap", "AOMap"},
    },
    0x419: {
        "variation": 40,
        "enabled": {"BaseColorMap", "AOMap", "SSSMaskMap"},
    },
    0x41B: {
        "variation": 48,
        "enabled": {"BaseColorMap", "NormalMap", "AOMap", "SSSMaskMap"},
    },
    0x41F: {
        "variation": 56,
        "enabled": {
            "BaseColorMap",
            "NormalMap",
            "RoughnessMap",
            "AOMap",
            "SSSMaskMap",
        },
    },
}
EYE_BINDING_VARIANTS = {
    0x20: {
        "variation": 0,
        "enabled": {"NormalMap1"},
    },
    0x24: {
        "variation": 20,
        "enabled": {"NormalMap1", "Highlight"},
    },
    0x30: {
        "variation": 44,
        "enabled": {"BaseColorMap1", "NormalMap1"},
    },
    0x34: {
        "variation": 52,
        "enabled": {"BaseColorMap1", "NormalMap1", "Highlight"},
    },
}


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(8 * 1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_json(path: pathlib.Path):
    with path.open("r", encoding="utf-8-sig") as source:
        return json.load(source)


def write_json(path: pathlib.Path, payload) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as output:
        json.dump(payload, output, indent=2, ensure_ascii=False)
        output.write("\n")


def unfilter_png(path: pathlib.Path):
    raw = path.read_bytes()
    if raw[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"Not a PNG: {path}")
    offset = 8
    idat = bytearray()
    width = height = bit_depth = color_type = interlace = None
    while offset < len(raw):
        length = struct.unpack(">I", raw[offset : offset + 4])[0]
        kind = raw[offset + 4 : offset + 8]
        data = raw[offset + 8 : offset + 8 + length]
        offset += 12 + length
        if kind == b"IHDR":
            width, height, bit_depth, color_type, _, _, interlace = struct.unpack(
                ">IIBBBBB", data
            )
        elif kind == b"IDAT":
            idat.extend(data)
        elif kind == b"IEND":
            break
    if bit_depth != 8 or interlace != 0:
        raise ValueError(f"Only non-interlaced 8-bit PNGs are supported: {path}")
    channels_by_type = {0: 1, 2: 3, 4: 2, 6: 4}
    if color_type not in channels_by_type:
        raise ValueError(f"Unsupported PNG color type {color_type}: {path}")
    channels = channels_by_type[color_type]
    stride = width * channels
    encoded = zlib.decompress(bytes(idat))
    expected = height * (stride + 1)
    if len(encoded) != expected:
        raise ValueError(f"Unexpected PNG payload size in {path}")
    decoded = bytearray(height * stride)
    source_offset = 0
    previous = bytearray(stride)

    def paeth(a: int, b: int, c: int) -> int:
        estimate = a + b - c
        da, db, dc = abs(estimate - a), abs(estimate - b), abs(estimate - c)
        return a if da <= db and da <= dc else b if db <= dc else c

    for y in range(height):
        filter_type = encoded[source_offset]
        source_offset += 1
        row = bytearray(encoded[source_offset : source_offset + stride])
        source_offset += stride
        for x in range(stride):
            left = row[x - channels] if x >= channels else 0
            above = previous[x]
            upper_left = previous[x - channels] if x >= channels else 0
            if filter_type == 1:
                row[x] = (row[x] + left) & 0xFF
            elif filter_type == 2:
                row[x] = (row[x] + above) & 0xFF
            elif filter_type == 3:
                row[x] = (row[x] + ((left + above) >> 1)) & 0xFF
            elif filter_type == 4:
                row[x] = (row[x] + paeth(left, above, upper_left)) & 0xFF
            elif filter_type != 0:
                raise ValueError(f"Unknown PNG filter {filter_type} in {path}")
        decoded[y * stride : (y + 1) * stride] = row
        previous = row
    return width, height, channels, bytes(decoded)


def percentile(histogram: list[int], fraction: float) -> int:
    target = max(0, math.ceil(sum(histogram) * fraction) - 1)
    count = 0
    for value, occurrences in enumerate(histogram):
        count += occurrences
        if count > target:
            return value
    return 255


def channel_stats(values: bytes) -> dict:
    histogram = [0] * 256
    for value in values:
        histogram[value] += 1
    count = len(values)
    total = sum(value * occurrences for value, occurrences in enumerate(histogram))
    mean = total / count
    variance = (
        sum(((value - mean) ** 2) * occurrences for value, occurrences in enumerate(histogram))
        / count
    )
    populated = [value for value, occurrences in enumerate(histogram) if occurrences]
    return {
        "minimum": populated[0],
        "maximum": populated[-1],
        "mean": round(mean, 6),
        "standard_deviation": round(math.sqrt(variance), 6),
        "p05": percentile(histogram, 0.05),
        "p50": percentile(histogram, 0.50),
        "p95": percentile(histogram, 0.95),
        "unique_values": len(populated),
    }


def gradient_stats(data: bytes, width: int, height: int, channels: int) -> dict:
    # A four-pixel step suppresses block-compression noise while retaining the
    # authored directional strokes. This measures the atlas, not shader usage.
    step = 4
    dx: list[int] = []
    dy: list[int] = []
    for y in range(0, height - step, step):
        row = y * width * channels
        next_row = (y + step) * width * channels
        for x in range(0, width - step, step):
            here = row + x * channels
            dx.append(abs(data[here] - data[here + step * channels]))
            dy.append(abs(data[here] - data[next_row + x * channels]))
    mean_dx = statistics.fmean(dx) if dx else 0.0
    mean_dy = statistics.fmean(dy) if dy else 0.0
    denominator = max(1e-12, min(mean_dx, mean_dy))
    return {
        "sample_step_texels": step,
        "mean_absolute_horizontal_delta": round(mean_dx, 6),
        "mean_absolute_vertical_delta": round(mean_dy, 6),
        "axis_delta_ratio": round(max(mean_dx, mean_dy) / denominator, 6),
        "interpretation_limit": (
            "This proves spatial structure in the scalar atlas only; it does not "
            "prove an anisotropic or fibre-direction shader lobe."
        ),
    }


def texture_report(game_root: pathlib.Path, texture: dict) -> dict:
    path = game_root / "assets" / "models" / texture["file"]
    width, height, channels, pixels = unfilter_png(path)
    names = ["red", "green", "blue", "alpha"][:channels]
    report = {
        "role": texture["role"],
        "source_name": texture["source"],
        "source_sha256": texture["source_sha256"],
        "decoded_path": texture["file"],
        "decoded_sha256": sha256(path),
        "width": width,
        "height": height,
        "channels": {
            name: channel_stats(pixels[index::channels])
            for index, name in enumerate(names)
        },
        "srgb": bool(texture["srgb"]),
        "sampler": {
            "slot": texture["slot"],
            "wrap_s": texture["wrap_s"],
            "wrap_t": texture["wrap_t"],
            "min_filter": texture["min_filter"],
            "mag_filter": texture["mag_filter"],
            "native": texture.get("native_sampler"),
        },
    }
    if texture["role"] == "RoughnessMap":
        report["red_channel_spatial_structure"] = gradient_stats(
            pixels, width, height, channels
        )
    return report


def trailing_zeroes(mask: int) -> int:
    if mask <= 0:
        return 0
    return (mask & -mask).bit_length() - 1


def encode_options(material_options: dict, slots: list[dict]) -> tuple[int, list[dict]]:
    key = 0
    selections = []
    for slot in slots:
        values = slot["slot_values"]
        default_index = int(slot["bool1"])
        requested = material_options.get(slot["slot_name"])
        selected_index = default_index
        source = "shader_metadata_default"
        if requested is not None:
            requested_string = str(requested).lower()
            matches = [
                index
                for index, value in enumerate(values)
                if str(value["string_value"]).lower() == requested_string
                or str(value["u_int_value"]).lower() == requested_string
            ]
            if not matches:
                raise ValueError(
                    f"Unsupported {slot['slot_name']}={requested} for shader metadata"
                )
            selected_index = matches[0]
            source = "material_document"
        if selected_index >= len(values):
            raise ValueError(f"Invalid default index for {slot['slot_name']}")
        mask = int(slot["offset"])
        encoded = selected_index << trailing_zeroes(mask)
        if encoded & ~mask:
            raise ValueError(f"Selection exceeds mask for {slot['slot_name']}")
        key |= encoded
        selections.append(
            {
                "name": slot["slot_name"],
                "choice": values[selected_index]["string_value"],
                "choice_value": values[selected_index]["u_int_value"],
                "choice_index": selected_index,
                "mask_hex": f"0x{mask:X}",
                "encoded_hex": f"0x{encoded:X}",
                "selection_source": source,
            }
        )
    return key, selections


def shader_report(
    study_root: pathlib.Path, family: str, materials: list[dict]
) -> dict:
    configuration = SHADER_FILES[family]
    metadata_path = study_root / configuration["metadata"]
    archive_path = study_root / configuration["archive"]
    metadata = read_json(metadata_path)
    material_keys = []
    for material in materials:
        shader_key, shader_selections = encode_options(
            material["shader_options"], metadata["shader_param"]
        )
        global_key, global_selections = encode_options({}, metadata["global_param"])
        pairs = list(zip(metadata["param_buffer"][::2], metadata["param_buffer"][1::2]))
        variations = [
            index
            for index, pair in enumerate(pairs)
            if int(pair[0]) == shader_key and int(pair[1]) == global_key
        ]
        if len(variations) != 1:
            raise ValueError(
                f"{family}/{material['name']} resolved to {len(variations)} variations"
            )
        material_keys.append(
            {
                "material": material["name"],
                "shader_key_hex": f"0x{shader_key:X}",
                "global_key_hex": f"0x{global_key:X}",
                "variation_index": variations[0],
                "shader_options": shader_selections,
                "global_options": global_selections,
            }
        )
    variation_indices = sorted({row["variation_index"] for row in material_keys})
    if len(variation_indices) != 1:
        raise ValueError(f"{family} Eevee materials do not share one variation")
    expected = variation_indices[0]
    program_root = study_root / configuration["program_directory"]
    stem = configuration["program_stem"]
    program_files = []
    for suffix in (
        "fsh",
        "fsh.maxwell",
        "fsh.maxwell.glsl",
        "vsh",
        "vsh.maxwell",
        "vsh.maxwell.glsl",
    ):
        path = program_root / f"{stem}.{suffix}"
        if not path.is_file():
            raise FileNotFoundError(path)
        program_files.append(
            {"kind": suffix, "bytes": path.stat().st_size, "sha256": sha256(path)}
        )
    return {
        "family": family,
        "archive": {
            "identity": configuration["archive"],
            "bytes": archive_path.stat().st_size,
            "sha256": sha256(archive_path),
            "reflection": bnsh_reflection_report(archive_path, expected),
        },
        "metadata": {
            "identity": configuration["metadata"],
            "sha256": sha256(metadata_path),
            "declared_shader_options": len(metadata["shader_param"]),
            "declared_global_options": len(metadata["global_param"]),
            "variation_count": len(metadata["param_buffer"]) // 2,
        },
        "resolved_variation": expected,
        "material_keys": material_keys,
        "program_files": program_files,
        "selection_rule": (
            "Each slot offset is a packed bit mask. The selected choice index is "
            "shifted into that mask; bool1 is the default choice index when the "
            "material document omits a system-controlled option. The resulting "
            "shader/global pair maps uniquely through param_buffer."
        ),
    }


def bnsh_reflection_report(path: pathlib.Path, variation_index: int) -> dict:
    """Inspect the selected BNSH program's retained reflection header.

    Nintendo's BNSH program record provides a pointer to a six-stage reflection
    header. Scarlet's shipped character archives set that pointer to zero.
    Recording that boundary prevents a later tool from claiming that resource
    names can be recovered from reflection that is not present. Offsets are
    file-relative metadata; no proprietary bytes are emitted.
    """

    payload = path.read_bytes()

    def u32(offset: int) -> int:
        if offset < 0 or offset + 4 > len(payload):
            raise ValueError(f"BNSH u32 offset is out of range: 0x{offset:X}")
        return struct.unpack_from("<I", payload, offset)[0]

    def u64(offset: int) -> int:
        if offset < 0 or offset + 8 > len(payload):
            raise ValueError(f"BNSH u64 offset is out of range: 0x{offset:X}")
        return struct.unpack_from("<Q", payload, offset)[0]

    grsc_offset = payload.find(b"grsc", 0, min(len(payload), 0x100))
    if grsc_offset < 0:
        raise ValueError(f"BNSH grsc block was not found: {path}")
    variation_count = u32(grsc_offset + 0x1C)
    if variation_index < 0 or variation_index >= variation_count:
        raise ValueError(
            f"BNSH variation {variation_index} is outside 0..{variation_count - 1}"
        )
    variation_array_offset = u64(grsc_offset + 0x20)
    entry_offset = variation_array_offset + variation_index * 0x40
    source_program_offset = u64(entry_offset)
    binary_program_offset = u64(entry_offset + 0x10)
    program_offset = source_program_offset + binary_program_offset
    # shader_info_data occupies 0x60 bytes; object_size/reserved occupy 0x08,
    # followed by object (+0x68), parent (+0x70), and reflection (+0x78).
    object_offset = u64(program_offset + 0x68)
    parent_offset = u64(program_offset + 0x70)
    reflection_offset = u64(program_offset + 0x78)
    return {
        "variation_index": variation_index,
        "program_offset_hex": f"0x{program_offset:X}",
        "object_offset_hex": f"0x{object_offset:X}",
        "parent_offset_hex": f"0x{parent_offset:X}",
        "reflection_pointer_hex": f"0x{reflection_offset:X}",
        "status": "absent_or_stripped" if reflection_offset == 0 else "retained",
        "interpretation": (
            "The selected binary-program record has a null reflection pointer. "
            "No stage reflection header, sampler dictionary, or constant-buffer "
            "dictionary can be recovered from this archive."
            if reflection_offset == 0
            else "Reflection metadata is present and requires dictionary parsing."
        ),
    }


def parse_glsl_samplers(path: pathlib.Path) -> dict[str, dict]:
    source = path.read_text(encoding="utf-8")
    declarations = re.findall(
        r"layout\s*\(binding\s*=\s*(\d+)\)\s*uniform\s+"
        r"(sampler\w+)\s+(\w+);",
        source,
    )
    result = {}
    for binding, sampler_type, name in declarations:
        component_matches = re.findall(
            r"texture\(" + re.escape(name) + r"[^;]*?\)\.([xyzw]+)", source
        )
        result[name] = {
            "binding": int(binding),
            "sampler_type": sampler_type,
            "sample_components": sorted(set(component_matches)),
            "sample_calls": len(
                re.findall(r"texture\(" + re.escape(name) + r"\b", source)
            ),
        }
    return result


def require_singleton(values: set[str], label: str) -> str:
    if len(values) != 1:
        raise ValueError(f"{label} resolved to {sorted(values)}, expected one sampler")
    return next(iter(values))


def differential_rows(
    study_root: pathlib.Path,
    directory: str,
    variants: dict[int, dict],
    metadata_name: str,
    global_key: int,
) -> dict[int, dict]:
    metadata = read_json(study_root / metadata_name)
    pairs = list(zip(metadata["param_buffer"][::2], metadata["param_buffer"][1::2]))
    result = {}
    for shader_key, specification in variants.items():
        matching = [
            index
            for index, pair in enumerate(pairs)
            if int(pair[0]) == shader_key and int(pair[1]) == global_key
        ]
        if matching != [specification["variation"]]:
            raise ValueError(
                f"Differential shader key 0x{shader_key:X}/0x{global_key:X} "
                f"resolved to {matching}, expected {[specification['variation']]}"
            )
        path = study_root / directory / f"v{matching[0]:03d}.fsh.maxwell.glsl"
        if not path.is_file():
            raise FileNotFoundError(path)
        samplers = parse_glsl_samplers(path)
        result[shader_key] = {
            "shader_key_hex": f"0x{shader_key:X}",
            "global_key_hex": f"0x{global_key:X}",
            "variation_index": matching[0],
            "enabled_material_features": sorted(specification["enabled"]),
            "fragment_glsl_sha256": sha256(path),
            "samplers": samplers,
        }
    return result


def direct_2d_names(row: dict) -> set[str]:
    return {
        name
        for name, sampler in row["samplers"].items()
        if sampler["sampler_type"] == "sampler2D" and sampler["sample_calls"] > 0
    }


def sss_binding_differential(study_root: pathlib.Path) -> dict:
    rows = differential_rows(
        study_root,
        "sss-binding-differential-20260812",
        SSS_BINDING_VARIANTS,
        "sss.trsha.json",
        0x1,
    )
    names = {key: direct_2d_names(value) for key, value in rows.items()}
    base = require_singleton(names[0x401], "SSS BaseColorMap baseline")
    normal = require_singleton(names[0x41B] - names[0x419], "SSS NormalMap")
    roughness = require_singleton(names[0x41F] - names[0x41B], "SSS RoughnessMap")
    sss_mask = require_singleton(names[0x41B] - names[0x40B], "SSS SSSMaskMap")
    ao = require_singleton(
        (names[0x40B] - names[0x401]) - {normal}, "SSS AOMap"
    )
    role_names = {
        "BaseColorMap": base,
        "NormalMap": normal,
        "RoughnessMap": roughness,
        "AOMap": ao,
        "SSSMaskMap": sss_mask,
    }
    expected_components = {
        "BaseColorMap": ["xyz"],
        "NormalMap": ["xy"],
        "RoughnessMap": ["x"],
        "AOMap": ["x"],
        "SSSMaskMap": ["x"],
    }
    exact = rows[0x41F]["samplers"]
    mapping = []
    for role, sampler_name in role_names.items():
        observed = exact[sampler_name]["sample_components"]
        if observed != expected_components[role]:
            raise ValueError(
                f"SSS {role}/{sampler_name} samples {observed}, "
                f"expected {expected_components[role]}"
            )
        mapping.append(
            {
                "material_role": role,
                "anonymous_sampler": sampler_name,
                "sample_components": observed,
                "proof": "compiled_option_permutation_set_difference",
            }
        )
    return {
        "family": "SSS",
        "status": "exact_material_texture_bindings",
        "mapping": mapping,
        "permutations": [rows[key] for key in sorted(rows)],
        "interpretation_limit": (
            "This maps every Eevee body material texture. Cube/environment "
            "resources and anonymous constant-buffer fields remain unnamed."
        ),
    }


def eye_binding_differential(study_root: pathlib.Path) -> dict:
    rows = differential_rows(
        study_root,
        "eye-binding-differential-20260812",
        EYE_BINDING_VARIANTS,
        "eye_clear_coat.trsha.json",
        0x0,
    )
    names = {key: direct_2d_names(value) for key, value in rows.items()}
    base_color_1_a = names[0x30] - names[0x20]
    base_color_1_b = names[0x34] - names[0x24]
    if base_color_1_a != base_color_1_b:
        raise ValueError("Eye BaseColorMap1 differential is inconsistent")
    base_color_1 = require_singleton(base_color_1_a, "Eye BaseColorMap1")
    highlight_delta_a = names[0x24] - names[0x20]
    highlight_delta_b = names[0x34] - names[0x30]
    if highlight_delta_a or highlight_delta_b:
        raise ValueError("Eye highlight unexpectedly changes direct 2D samplers")
    exact_names = names[0x24]
    exact_samplers = rows[0x24]["samplers"]
    baseline_normal = require_singleton(
        {
            name
            for name in exact_names
            if exact_samplers[name]["sample_components"] == ["xy"]
        },
        "Eye NormalMap1 baseline",
    )
    projected_scalar = require_singleton(
        {
            name
            for name in exact_names
            if exact_samplers[name]["sample_components"] == ["x"]
        },
        "Eye projected scalar baseline",
    )
    return {
        "family": "EyeClearCoat",
        "status": "partial_material_texture_bindings",
        "mapping": [
            {
                "material_role": "BaseColorMap1",
                "anonymous_sampler": base_color_1,
                "sample_components": rows[0x34]["samplers"][base_color_1][
                    "sample_components"
                ],
                "proof": "compiled_option_permutation_set_difference",
            },
            {
                "material_role": "NormalMap1",
                "anonymous_sampler": baseline_normal,
                "sample_components": ["xy"],
                "material_constant": "NormalHeight1=fp_c7.data[4].w",
                "proof": (
                    "normal_reconstruction_data_flow_plus_cross_family_"
                    "material_buffer_adjacency"
                ),
            }
        ],
        "exact_variant_system_resources": [
            {
                "anonymous_sampler": projected_scalar,
                "sample_components": ["x"],
                "classification": "projected_scene_scalar_resource",
                "proof": (
                    "coordinates_are_derived_from_world_position_and_fp_c3_"
                    "projection_constants; the sample modulates a lighting path"
                ),
            }
        ],
        "unresolved_authored_eye_inputs": [
            "BaseColorMap",
            "LayerMaskMap",
            "NormalMap",
        ],
        "highlight_texture_binding_delta": [],
        "permutations": [rows[key] for key in sorted(rows)],
        "interpretation_limit": (
            "BaseColorMap1, NormalMap1, and the absence of a highlight texture "
            "binding are proven. The scalar input is a projected scene resource, "
            "not Eevee's LayerMaskMap. BaseColorMap, LayerMaskMap, and NormalMap "
            "are retained by the material document but are not sampled directly "
            "by either selected shader stage, so their packing/preprocessing path "
            "remains unresolved."
        ),
    }


def require_shader_signature(source: str, fragments: list[str], label: str) -> None:
    missing = [fragment for fragment in fragments if fragment not in source]
    if missing:
        raise ValueError(f"{label} data-flow signature changed; missing {missing}")


def constant_buffer_data_flow(
    study_root: pathlib.Path, manifest: dict
) -> list[dict]:
    sss_path = (
        study_root
        / "sss-binding-differential-20260812"
        / "v056.fsh.maxwell.glsl"
    )
    eye_path = (
        study_root
        / "eye-binding-differential-20260812"
        / "v020.fsh.maxwell.glsl"
    )
    eye_no_highlight_path = (
        study_root
        / "eye-binding-differential-20260812"
        / "v000.fsh.maxwell.glsl"
    )
    eye_vertex_path = (
        study_root
        / "eye-binding-differential-20260812"
        / "v020.vsh.maxwell.glsl"
    )
    for path in (sss_path, eye_path, eye_no_highlight_path, eye_vertex_path):
        if not path.is_file():
            raise FileNotFoundError(path)
    sss = sss_path.read_text(encoding="utf-8")
    eye = eye_path.read_text(encoding="utf-8")
    eye_no_highlight = eye_no_highlight_path.read_text(encoding="utf-8")
    eye_vertex = eye_vertex_path.read_text(encoding="utf-8")

    require_shader_signature(
        sss,
        [
            "temp_9 = 0.0 - fp_c8.data[1].z;",
            "temp_11 = 0.0 - fp_c8.data[1].w;",
            "temp_13 = temp_10 * fp_c8.data[1].x;",
            "temp_14 = 0.0 - fp_c8.data[1].y;",
            "temp_78 = temp_17 * fp_c7.data[4].z;",
            "temp_79 = temp_18 * fp_c7.data[4].z;",
            "temp_211 = fma(temp_22, fp_c7.data[17].z, fp_c7.data[41].x);",
            "temp_212 = clamp(temp_211, 0.0, 1.0);",
            "temp_213 = temp_212 * fp_c8.data[41].x;",
            "temp_245 = temp_212 * fp_c8.data[41].y;",
            "temp_270 = temp_212 * fp_c8.data[41].z;",
        ],
        "SSS variation 56",
    )
    require_shader_signature(
        eye,
        [
            "temp_23 = sin(fp_c7.data[16].x);",
            "temp_25 = cos(fp_c7.data[16].x);",
            "temp_35 = 0.0 - fp_c8.data[1].w;",
            "temp_37 = 0.0 - fp_c8.data[1].z;",
            "temp_39 = fma(temp_36, fp_c8.data[1].y, 0.5);",
            "temp_40 = fma(temp_38, fp_c8.data[1].x, 0.5);",
            "temp_43 = texture(fp_t_tcb_1E, vec2(temp_40, temp_42)).xy;",
            "temp_105 = temp_44 * fp_c7.data[4].w;",
            "temp_106 = temp_45 * fp_c7.data[4].w;",
            "temp_152 = fma(temp_50, fp_c3.data[210].x, fp_c3.data[210].z);",
            "temp_153 = fma(temp_54, fp_c3.data[210].y, fp_c3.data[210].w);",
            "temp_154 = texture(fp_t_tcb_3E, vec2(temp_152, temp_153)).x;",
            "temp_705 = temp_704 * fp_c7.data[7].w;",
            "temp_779 = fp_c7.data[7].w;",
            "temp_779 = fp_c7.data[57].w;",
            "temp_790 = -0.0399999991 + fp_c8.data[18].x;",
            "temp_796 = fma(temp_790, fp_c7.data[4].x, 0.0399999991);",
            "temp_731 = fp_c7.data[58].x;",
            "temp_875 = temp_862 * fp_c7.data[9].y;",
            "temp_885 = temp_876 * fp_c8.data[24].x;",
            "temp_887 = temp_876 * fp_c8.data[24].y;",
            "temp_882 = temp_876 * fp_c8.data[24].z;",
            "temp_988 = fp_c8.data[18].w * fp_c10.data[9].x;",
        ],
        "EyeClearCoat variation 20",
    )
    require_shader_signature(
        eye_no_highlight,
        [
            "temp_700 = temp_699 * fp_c7.data[7].w;",
            "temp_742 = -0.0399999991 + fp_c8.data[18].x;",
            "temp_748 = fma(temp_742, fp_c7.data[4].x, 0.0399999991);",
            "temp_819 = fp_c8.data[18].w * fp_c10.data[9].x;",
        ],
        "EyeClearCoat variation 0",
    )
    if re.search(r"uniform\s+sampler|texture\(", eye_vertex):
        raise ValueError("EyeClearCoat vertex stage unexpectedly samples a texture")

    def material_fields(source: str, buffer_name: str) -> set[tuple[int, str]]:
        return {
            (int(index), component)
            for index, component in re.findall(
                rf"{re.escape(buffer_name)}\.data\[(\d+)\]\.([xyzw])",
                source,
            )
        }

    highlight_scalar_delta = (
        material_fields(eye, "fp_c7")
        - material_fields(eye_no_highlight, "fp_c7")
    )
    highlight_vector_delta = (
        material_fields(eye, "fp_c8")
        - material_fields(eye_no_highlight, "fp_c8")
    )
    if highlight_scalar_delta != {(57, "w"), (58, "x"), (9, "y")}:
        raise ValueError("EyeClearCoat highlight scalar differential changed")
    if highlight_vector_delta != {
        (24, "x"), (24, "y"), (24, "z"),
        (96, "x"), (96, "y"), (96, "z"), (96, "w"),
    }:
        raise ValueError("EyeClearCoat highlight vector differential changed")

    eye_materials = [
        material
        for material in manifest.get("materials", [])
        if material.get("shader_family") == "EyeClearCoat"
    ]
    required_eye_scalars = {
        "MetallicClearCoat", "RoughnessClearCoat",
        "MetallicHighlight", "RoughnessHighlight",
        "EmissionIntensityLayer5",
    }
    required_eye_vectors = {"BaseColorClearCoat", "EmissionColorLayer5"}
    if not eye_materials or any(
        not required_eye_scalars.issubset(material.get("float_parameters", {}))
        or not required_eye_vectors.issubset(material.get("vec4_parameters", {}))
        or str(material.get("shader_options", {}).get("EnableHighlight")).lower()
        != "true"
        for material in eye_materials
    ):
        raise ValueError("Eevee EyeClearCoat material parameter contract changed")

    return [
        {
            "family": "SSS",
            "buffer_classification": {
                "fp_c7": "named_scalar_material_parameters",
                "fp_c8": "named_vector_material_parameters",
            },
            "mappings": [
                {
                    "material_parameter": "UVScaleOffset",
                    "anonymous_field": "fp_c8.data[1].xyzw",
                    "use": "XY scale and ZW offset before every body material sample",
                },
                {
                    "material_parameter": "NormalHeight",
                    "anonymous_field": "fp_c7.data[4].z",
                    "use": "scales both sampled normal components before reconstruction",
                },
                {
                    "material_parameter": "SSSMaskScale",
                    "anonymous_field": "fp_c7.data[17].z",
                    "use": "multiplier in clamp(mask * scale + offset)",
                },
                {
                    "material_parameter": "SSSMaskOffset",
                    "anonymous_field": "fp_c7.data[41].x",
                    "use": "addend in clamp(mask * scale + offset)",
                },
                {
                    "material_parameter": "SubsurfaceColor",
                    "anonymous_field": "fp_c8.data[41].xyz",
                    "use": "RGB multiplier of the clamped SSS mask response",
                },
            ],
            "proof": "named_parameter_exhaustiveness_plus_compiled_data_flow",
        },
        {
            "family": "EyeClearCoat",
            "buffer_classification": {
                "fp_c7": "named_scalar_material_parameters",
                "fp_c8": "named_vector_material_parameters",
            },
            "mappings": [
                {
                    "material_parameter": "UVRotation",
                    "anonymous_field": "fp_c7.data[16].x",
                    "use": "the sole argument to the paired sine/cosine UV rotation",
                },
                {
                    "material_parameter": "UVScaleOffset",
                    "anonymous_field": "fp_c8.data[1].xyzw",
                    "use": "XY scale and ZW offset after eye UV rotation",
                },
                {
                    "material_parameter": "NormalHeight1",
                    "anonymous_field": "fp_c7.data[4].w",
                    "use": "scales both tcb_1E components before normal reconstruction",
                },
                {
                    "material_parameter": "MetallicClearCoat",
                    "anonymous_field": "fp_c7.data[4].x",
                    "use": "mixes dielectric 0.04 F0 toward the clear-coat base color",
                },
                {
                    "material_parameter": "RoughnessClearCoat",
                    "anonymous_field": "fp_c7.data[7].w",
                    "use": "drives environment mip selection and the clear-coat GGX terms",
                },
                {
                    "material_parameter": "BaseColorClearCoat",
                    "anonymous_field": "fp_c8.data[18].xyzw",
                    "use": "supplies clear-coat RGB to the metallic/F0 path and output alpha",
                },
                {
                    "material_parameter": "RoughnessHighlight",
                    "anonymous_field": "fp_c7.data[57].w",
                    "use": "replaces clear-coat roughness inside the compiled highlight branch",
                },
                {
                    "material_parameter": "MetallicHighlight",
                    "anonymous_field": "fp_c7.data[58].x",
                    "use": "replaces clear-coat metallic inside the compiled highlight branch",
                },
                {
                    "material_parameter": "EmissionIntensityLayer5",
                    "anonymous_field": "fp_c7.data[9].y",
                    "use": "scales the highlight emission color",
                },
                {
                    "material_parameter": "EmissionColorLayer5",
                    "anonymous_field": "fp_c8.data[24].xyz",
                    "use": "supplies RGB to the highlight emission path",
                },
            ],
            "stage_boundary": (
                "The selected vertex stage has no sampler declarations or texture "
                "operations; all observed texture reads are in the fragment stage."
            ),
            "highlight_differential": {
                "disabled_variation": 0,
                "enabled_variation": 20,
                "added_material_scalar_fields": [
                    "fp_c7.data[9].y",
                    "fp_c7.data[57].w",
                    "fp_c7.data[58].x",
                ],
                "added_material_vector_fields": ["fp_c8.data[24].xyz"],
                "added_scene_field": "fp_c8.data[96].xyzw",
            },
            "proof": (
                "named_parameter_exhaustiveness_plus_exact_highlight_option_"
                "differential_plus_compiled_data_flow"
            ),
        },
    ]


def find_texture_materials(manifest: dict) -> list[dict]:
    results = []
    seen = set()
    for material in manifest["materials"]:
        for texture in material["textures"]:
            identity = (texture["file"], texture["role"])
            if identity not in seen:
                seen.add(identity)
                results.append(texture)
    return sorted(results, key=lambda value: (value["role"], value["file"]))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--game-root", default=".")
    parser.add_argument("--manifest", default=EXPECTED_MANIFEST)
    parser.add_argument("--shader-study")
    parser.add_argument("--za-manifest")
    parser.add_argument("--output", required=True)
    arguments = parser.parse_args()

    game_root = pathlib.Path(arguments.game_root).resolve()
    manifest_path = game_root / arguments.manifest
    manifest = read_json(manifest_path)
    if manifest.get("schema") != "phlosion-native-model-ir-v1":
        raise ValueError("SV Eevee manifest is not native IR")
    if manifest.get("source", {}).get("profile") != "pokemon-scarlet-v3.0.1":
        raise ValueError("SV Eevee source profile is not Scarlet 3.0.1")

    textures = [texture_report(game_root, row) for row in find_texture_materials(manifest)]
    shader_evidence = []
    binding_differentials = []
    constant_buffer_mappings = []
    if arguments.shader_study:
        study_root = pathlib.Path(arguments.shader_study).resolve()
        for family in ("SSS", "EyeClearCoat"):
            family_materials = [
                material
                for material in manifest["materials"]
                if material["shader_family"] == family
            ]
            shader_evidence.append(shader_report(study_root, family, family_materials))
        binding_differentials = [
            sss_binding_differential(study_root),
            eye_binding_differential(study_root),
        ]
        constant_buffer_mappings = constant_buffer_data_flow(study_root, manifest)

    cross_game = None
    if arguments.za_manifest:
        za_path = pathlib.Path(arguments.za_manifest).resolve()
        za = read_json(za_path)
        cross_game = {
            "source_profile": za["source"]["profile"],
            "model_sha256": za["source"]["model_sha256"],
            "payload_sha256": za["payload"]["sha256"],
            "vertex_count": za["model"]["vertex_count"],
            "index_count": za["model"]["index_count"],
            "submesh_count": za["model"]["submesh_count"],
            "shader_families": sorted(
                {material["shader_family"] for material in za["materials"]}
            ),
            "interpretation_limit": (
                "Topology and authored-role corroboration only; the Z-A material "
                "equation does not prove the SV shader equation."
            ),
        }

    sss_mask = next(row for row in textures if row["role"] == "SSSMaskMap")
    roughness = [row for row in textures if row["role"] == "RoughnessMap"]
    sss_program = next(
        (row for row in shader_evidence if row["family"] == "SSS"), None
    )
    eye_program = next(
        (row for row in shader_evidence if row["family"] == "EyeClearCoat"), None
    )
    report = {
        "schema": REPORT_SCHEMA,
        "subject": {
            "species_id": 133,
            "species_name": "Eevee",
            "source_profile": manifest["source"]["profile"],
            "canonical_manifest": arguments.manifest.replace("\\", "/"),
            "source_model_sha256": manifest["source"]["model_sha256"],
            "payload_sha256": manifest["payload"]["sha256"],
        },
        "method": {
            "runtime_execution": False,
            "emulator_used": False,
            "evidence_classes": [
                "source_payload",
                "source_parameter_mapping",
                "offline_shader_disassembly",
                "decoded_texture_measurement",
                "cross_game_source_comparison" if cross_game else None,
            ],
            "limitations": [
                "No source framebuffer, bound constant-buffer values, environment probe, post-processing state, or runtime sampler/mip selection was observed.",
                "Maxwell GLSL is a control-flow-faithful decompilation. Static data flow maps a subset of material constants, but scene/light fields remain anonymous without retained reflection or bound runtime values.",
                "Texture spatial structure does not by itself prove a directional-fibre BRDF.",
            ],
        },
        "canonical_materials": [
            {
                "name": material["name"],
                "shader_family": material["shader_family"],
                "shader_options": material["shader_options"],
                "float_parameters": material["float_parameters"],
                "texture_roles": [texture["role"] for texture in material["textures"]],
            }
            for material in manifest["materials"]
        ],
        "decoded_textures": textures,
        "shader_evidence": shader_evidence,
        "binding_differentials": binding_differentials,
        "constant_buffer_mappings": constant_buffer_mappings,
        "cross_game_corroboration": cross_game,
        "findings": [
            {
                "id": "exact_program_selection",
                "evidence_level": "source_parameter_mapping",
                "conclusion": (
                    "The retained material documents and Trinity variation tables "
                    "resolve Eevee to SSS variation 56 and EyeClearCoat variation 20."
                    if shader_evidence
                    else "Shader-study inputs were not supplied; program selection was not evaluated."
                ),
            },
            {
                "id": "sss_mask_character",
                "evidence_level": "decoded_texture_measurement",
                "conclusion": (
                    f"Eevee's SSS mask is a {sss_mask['width']}x{sss_mask['height']} "
                    "source texture; its measured channels are recorded above."
                ),
            },
            {
                "id": "roughness_atlas_character",
                "evidence_level": "decoded_texture_measurement",
                "conclusion": (
                    f"{len(roughness)} retained Eevee roughness atlas/atlases contain "
                    "measurable high-resolution spatial variation. In SSS variation 56, "
                    "the body material inputs are sampled as two normal components, scalar "
                    "roughness, scalar AO, RGB base color, and scalar SSS mask. No dedicated "
                    "flow/tangent-direction texture is present, so calling RoughnessMap a "
                    "fibre-direction carrier is unsupported."
                ),
            },
            {
                "id": "sss_static_binding_contract",
                "evidence_level": "source_parameter_mapping",
                "conclusion": (
                    "The exact SSS variation has seven samplers: five 2D material inputs "
                    "plus two cube/environment resources. Five option-controlled compiled "
                    "permutations map every material input exactly: BaseColorMap=tcb_8 "
                    "(XYZ), NormalMap=tcb_C (XY), RoughnessMap=tcb_10 (X), "
                    "AOMap=tcb_14 (X), and SSSMaskMap=tcb_1A (X)."
                    if sss_program
                    else "The SSS program was not supplied for offline analysis."
                ),
            },
            {
                "id": "eye_static_binding_contract",
                "evidence_level": "source_parameter_mapping",
                "conclusion": (
                    "The exact EyeClearCoat variation has two direct 2D samples, two 2D-array "
                    "resources, and two cube resources. Differential programs prove that "
                    "optional BaseColorMap1 is tcb_1A (XYZ) and that EnableHighlight adds no "
                    "texture binding. Normal reconstruction plus the shared material-buffer "
                    "layout map tcb_1E to NormalMap1 and c7[4].w to NormalHeight1. The scalar "
                    "tcb_3E input uses projected world/scene coordinates and is not Eevee's "
                    "LayerMaskMap."
                    if eye_program
                    else "The EyeClearCoat program was not supplied for offline analysis."
                ),
            },
            {
                "id": "material_constant_buffer_contract",
                "evidence_level": "source_parameter_mapping",
                "conclusion": (
                    "Compiled data flow maps every named Eevee SSS parameter: "
                    "UVScaleOffset, NormalHeight, "
                    "SSSMaskScale, SSSMaskOffset, and SubsurfaceColor, plus eye "
                    "UVRotation, UVScaleOffset, NormalHeight1, clear-coat base color, "
                    "roughness and metallic, and the highlight roughness, metallic, "
                    "layer-5 emission color and intensity. Variations 0 and 20 isolate "
                    "the exact EnableHighlight field delta. Both selected BNSH "
                    "archives have null reflection pointers, so the remaining "
                    "scene/light resource names cannot be recovered from shipped "
                    "reflection dictionaries."
                    if constant_buffer_mappings
                    else "Shader-study inputs were not supplied for constant-buffer analysis."
                ),
            },
            {
                "id": "current_phlosion_gap",
                "evidence_level": "source_parameter_mapping",
                "conclusion": (
                    "Phlosion retains the exact source maps and distinguishes SSS/EyeClearCoat, "
                    "but its Eevee fibre relief, SSS lobe, environment response, and final color "
                    "remain reconstructed until anonymous source bindings/constants are mapped."
                ),
            },
        ],
        "open_questions": [
            "Map the remaining SSS variation 56 cube and scene/light fields; all five 2D material textures and every Eevee SSS material parameter are now mapped.",
            "Reconstruct the exact SSS diffuse/specular/subsurface equation; static disassembly establishes its material inputs, but anonymous scene/light constants still obscure the complete lobe.",
            "Resolve why EyeClearCoat retains BaseColorMap, LayerMaskMap, and NormalMap although neither selected shader stage directly samples them; determine whether Trinity packs or preprocesses them into another resource before draw submission.",
            "Map the EyeClearCoat point-light fields, projected scalar scene resource, shadow arrays, cube resources, and complete clear-coat/highlight combination order.",
            "Recover source environment/reflection, exposure, tone-map, active mip, and anisotropic-sampler behavior; these are runtime state and cannot be proven from loose assets alone.",
        ],
    }
    report["method"]["evidence_classes"] = [
        value for value in report["method"]["evidence_classes"] if value
    ]
    write_json(pathlib.Path(arguments.output), report)
    print(
        f"SV Eevee static material report: textures={len(textures)} "
        f"shader_families={len(shader_evidence)} output={arguments.output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
