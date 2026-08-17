#!/usr/bin/env python3
"""Promote emulator-free Z-A scene/color boundary evidence.

This analyzer deliberately separates equations recoverable from compiled
programs from values owned by the missing source runtime. It records hashes,
operation identities, and cross-family coverage without promoting proprietary
shader text from the private study directory.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import sys
from typing import Any

import analyze_za_ik_character_dataflow as flow


SCHEMA = "pokemon-autochess-za-scene-color-boundary-evidence-v3"
SOURCE_PROFILE = "pokemon-legends-za-v2.0.0"
SELECTED_MANIFEST_SCHEMA = "pokemon-autochess-private-za-selected-programs-v1"
OPTION_DATAFLOW_SCHEMA = "pokemon-autochess-za-kanto-option-dataflow-evidence-v1"
INVENTORY_SCHEMA = "pokemon-autochess-za-kanto-shader-inventory-v1"


def read_json(path: pathlib.Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(value, dict):
        raise ValueError(f"Expected a JSON object: {path}")
    return value


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require_fragments(source: str, fragments: list[str], label: str) -> None:
    missing = [fragment for fragment in fragments if fragment not in source]
    if missing:
        raise ValueError(f"{label} operation signature changed: {missing}")


def final_scene_fade(source: str, label: str) -> dict[str, Any]:
    report = flow.final_scene_fade_boundary(source, label)
    return {
        "operation": report["operation"],
        "proof": report["proof"],
        "boundary": report["boundary"],
    }


def camera_relative_vector(source: str, label: str) -> dict[str, Any]:
    pattern = re.compile(
        r"temp_(\d+) = 0\.0 - temp_(\d+);\s*"
        r"temp_(\d+) = temp_\1 \+ fp_c5\.data\[19\]\.([xyz]);")
    channels = {channel for _, _, _, channel in pattern.findall(source)}
    if channels != {"x", "y", "z"}:
        raise ValueError(f"{label} camera-relative vector changed: {channels}")
    require_fragments(
        source,
        ["fp_c5.data[19].x", "fp_c5.data[19].y", "fp_c5.data[19].z",
         "inversesqrt"],
        label,
    )
    return {
        "field": "fp_c5[19].xyz",
        "operation": (
            "subtract interpolated world-position xyz, normalize the result, "
            "and consume it in view/reflection lighting paths"),
        "proof": "cross_program_compiled_operation_identity",
        "semantic_classification": "camera_world_position",
        "classification_strength": (
            "strong structural inference; stripped reflection omits the "
            "source field name"),
    }


def source_without_comments(source: str) -> str:
    return "\n".join(
        line for line in source.splitlines()
        if not line.lstrip().startswith("//"))


def only_expression(
        graph: dict[str, Any], temporary: str, label: str) -> str:
    rows = graph["definitions"].get(temporary, [])
    if len(rows) != 1:
        raise ValueError(
            f"{label} expected one definition for {temporary}: {len(rows)}")
    return str(rows[0]["expression"])


def temporary_product(expression: str) -> tuple[str, str] | None:
    match = re.fullmatch(r"(temp_\d+) \* (temp_\d+)", expression)
    return match.groups() if match else None


def max_abs_leaves(
        graph: dict[str, Any], temporary: str, label: str) -> list[str]:
    expression = only_expression(graph, temporary, label)
    maximum = re.fullmatch(r"max\((temp_\d+), (temp_\d+)\)", expression)
    if maximum:
        return (
            max_abs_leaves(graph, maximum.group(1), label) +
            max_abs_leaves(graph, maximum.group(2), label))
    absolute = re.fullmatch(r"abs\((temp_\d+)\)", expression)
    if absolute:
        return [absolute.group(1)]
    raise ValueError(
        f"{label} max-component normalization changed at {temporary}: "
        f"{expression}")


def max_abs_normalized_vector(
        graph: dict[str, Any], coordinates: tuple[str, str, str],
        label: str, flip_z: bool = True) -> tuple[list[str], list[str]]:
    products = [
        temporary_product(only_expression(graph, coordinate, label))
        for coordinate in coordinates]
    if any(product is None for product in products):
        raise ValueError(f"{label} cube-coordinate products changed")
    resolved = [product for product in products if product is not None]
    common = set(resolved[0]) & set(resolved[1])
    inverse_candidates = [
        temporary for temporary in common
        if re.fullmatch(
            r"1\.0 / temp_\d+",
            only_expression(graph, temporary, label))]
    if len(inverse_candidates) != 1:
        raise ValueError(f"{label} max-component reciprocal changed")
    inverse = inverse_candidates[0]
    normals = [
        next(value for value in product if value != inverse)
        for product in resolved[:2]]
    scale_temporaries = [inverse]
    if flip_z:
        negative_inverse_candidates = [
            value for value in resolved[2]
            if only_expression(graph, value, label) == f"0.0 - {inverse}"]
        if len(negative_inverse_candidates) != 1:
            raise ValueError(f"{label} cube-coordinate Z sign changed")
        negative_inverse = negative_inverse_candidates[0]
        normals.append(next(
            value for value in resolved[2] if value != negative_inverse))
        scale_temporaries.append(negative_inverse)
    else:
        if inverse not in resolved[2]:
            raise ValueError(f"{label} unexpectedly flips a cube coordinate")
        normals.append(next(value for value in resolved[2] if value != inverse))
    maximum = re.fullmatch(
        r"1\.0 / (temp_\d+)",
        only_expression(graph, inverse, label))
    if maximum is None:
        raise ValueError(f"{label} max-component reciprocal changed")
    leaves = max_abs_leaves(graph, maximum.group(1), label)
    if sorted(leaves) != sorted(normals):
        raise ValueError(
            f"{label} cube normalization no longer covers xyz: "
            f"{leaves}/{normals}")
    return normals, scale_temporaries


def diffuse_irradiance_contract(source: str, label: str) -> dict[str, Any]:
    """Prove the shared scene diffuse-cube coordinate convention."""
    compact = source_without_comments(source)
    graph = flow.build_graph(compact)
    pattern = re.compile(
        r"temp_\d+ = textureLod\(fp_t_tcb_34, "
        r"vec3\((temp_\d+), (temp_\d+), (temp_\d+)\), 0\.0\)\.xyz;")
    matches = list(pattern.finditer(compact))
    if len(matches) != 1:
        raise ValueError(f"{label} diffuse-irradiance sample changed")
    coordinates = matches[0].groups()
    normals, _ = max_abs_normalized_vector(
        graph, coordinates, f"{label} diffuse irradiance")
    closure = flow.backward_closure(graph, set(normals))
    samplers, _ = flow.references_in_closure(graph, closure)
    return {
        "resource": "fp_t_tcb_34",
        "semantic_classification": "diffuse_irradiance_cube",
        "direction": "max_abs_normalize(vec3(n.x, n.y, -n.z))",
        "lod": 0,
        "normal_source": (
            "material_mapped_shading_normal"
            if "fp_t_tcb_C" in samplers else
            "resolved_shading_normal"),
        "equivalent_cubemap_direction": "vec3(n.x, n.y, -n.z)",
        "proof": "cross_family_compiled_operation_identity",
        "note": (
            "Positive max-component scaling is homogeneous for cube lookup; "
            "the source-significant operation is the Z flip."),
    }


def dominant_direct_light_contract(source: str, label: str) -> dict[str, Any]:
    """Classify the shared fp_c4[0] direction from output-reachable use."""
    compact = source_without_comments(source)
    graph = flow.build_graph(compact)
    output_roots = set().union(*graph["outputs"].values())
    output_closure = flow.backward_closure(graph, output_roots)
    counts: dict[str, int] = {}
    for channel in "xyz":
        temporaries = re.findall(
            rf"(temp_\d+) = 0\.0 - fp_c4\.data\[0\]\.{channel};",
            compact)
        if not temporaries or any(
                temporary not in output_closure for temporary in temporaries):
            raise ValueError(
                f"{label} dominant-light {channel} use changed")
        counts[channel] = len(temporaries)
    return {
        "field": "fp_c4[0].xyz",
        "semantic_classification": "dominant_directional_light_vector",
        "fragment_to_light_direction": "-fp_c4[0].xyz",
        "output_reachable_negated_uses": counts,
        "proof": "cross_family_compiled_output_reachability",
        "classification_strength": (
            "strong structural inference corroborated by seven Z-A forward "
            "programs; stripped reflection omits the source field name"),
    }


def indexed_c4_loads(
        graph: dict[str, Any], label: str) -> dict[str, list[str]]:
    offsets = {"16": [], "24": [], "0x1B0": [], "0x1B8": []}
    for temporary, rows in graph["definitions"].items():
        if not any(re.fullmatch(
                r"fp_c4\.data\[int\(temp_\d+\)\]\[temp_\d+\]",
                str(row["expression"])) for row in rows):
            continue
        closure = flow.backward_closure(graph, {temporary})
        expressions = "\n".join(
            str(row["expression"])
            for dependency in closure
            for row in graph["definitions"].get(dependency, []))
        if ("fp_c7.data[39].x" not in expressions or
                not re.search(r"int\(temp_\d+\) << 4", expressions)):
            raise ValueError(f"{label} fp_c4 light selector changed")
        matched_offsets = [
            offset for offset in offsets
            if f"+ {offset}" in expressions]
        if len(matched_offsets) != 1:
            raise ValueError(
                f"{label} fp_c4 indexed offset changed for {temporary}: "
                f"{matched_offsets}")
        offsets[matched_offsets[0]].append(temporary)
    return offsets


def dot_terms(
        graph: dict[str, Any], temporary: str,
        label: str) -> list[tuple[str, str]]:
    expression = only_expression(graph, temporary, label)
    fused = re.fullmatch(
        r"fma\((temp_\d+), (temp_\d+), (temp_\d+)\)", expression)
    if fused:
        return [(fused.group(1), fused.group(2))] + dot_terms(
            graph, fused.group(3), label)
    product = temporary_product(expression)
    if product:
        return [product]
    raise ValueError(f"{label} reflection dot product changed: {expression}")


def ik_local_reflection_direction_contract(
        source: str, label: str) -> dict[str, Any]:
    """Prove IkCharacter's exact material-local reflection vector."""
    compact = source_without_comments(source)
    graph = flow.build_graph(compact)
    pattern = re.compile(
        r"temp_\d+ = textureLod\(fp_t_tcb_1C, "
        r"vec3\((temp_\d+), (temp_\d+), (temp_\d+)\), "
        r"fp_c7\.data\[101\]\.w\)\.xyz;")
    matches = list(pattern.finditer(compact))
    if len(matches) != 1:
        raise ValueError(f"{label} local-reflection sample changed")
    coordinates = matches[0].groups()
    reflected, _ = max_abs_normalized_vector(
        graph, coordinates, f"{label} local reflection", flip_z=False)
    view: list[str] = []
    normal: list[str] = []
    negative_dots: list[str] = []
    for component in reflected:
        reflection = re.fullmatch(
            r"fma\((temp_\d+), -2\.0, (temp_\d+)\)",
            only_expression(graph, component, label))
        if reflection is None:
            raise ValueError(f"{label} reflection algebra changed")
        normal_dot, negative_view = reflection.groups()
        negative_view_match = re.fullmatch(
            r"0\.0 - (temp_\d+)",
            only_expression(graph, negative_view, label))
        product = temporary_product(
            only_expression(graph, normal_dot, label))
        if negative_view_match is None or product is None:
            raise ValueError(f"{label} reflection input algebra changed")
        view.append(negative_view_match.group(1))
        normal.append(product[0])
        negative_dots.append(product[1])
    if len(set(negative_dots)) != 1:
        raise ValueError(f"{label} reflection dot-product sharing changed")
    terms = dot_terms(graph, negative_dots[0], label)
    resolved_terms: set[tuple[str, str]] = set()
    for view_term, negative_normal in terms:
        match = re.fullmatch(
            r"0\.0 - (temp_\d+)",
            only_expression(graph, negative_normal, label))
        if match is None:
            raise ValueError(f"{label} reflection dot sign changed")
        resolved_terms.add((view_term, match.group(1)))
    if resolved_terms != set(zip(view, normal)):
        raise ValueError(
            f"{label} reflection dot components changed: {resolved_terms}")
    view_closure = flow.backward_closure(graph, set(view))
    _, view_buffers = flow.references_in_closure(graph, view_closure)
    if not {"fp_c5[19].x", "fp_c5[19].y", "fp_c5[19].z"}.issubset(
            set(view_buffers)):
        raise ValueError(f"{label} reflection camera vector changed")
    return {
        "resource": "fp_t_tcb_1C",
        "direction": "max_abs_normalize(reflect(-view, mapped_normal))",
        "equivalent_cubemap_direction": "reflect(-view, mapped_normal)",
        "lod": "ReflectionsBlur / fp_c7[101].w",
        "camera_field": "fp_c5[19].xyz",
        "coordinate_sign_flip": False,
        "proof": "compiled_symbolic_operation_identity",
        "note": (
            "Positive max-component scaling is homogeneous for cube lookup; "
            "no anonymous scene constant participates in this vector."),
    }


def ik_indexed_light_contract(source: str, label: str) -> dict[str, Any]:
    """Resolve the indexed fp_c4 direct and diffuse-environment records."""
    compact = source_without_comments(source)
    graph = flow.build_graph(compact)
    loads = indexed_c4_loads(graph, label)
    if (len(loads["16"]) < 2 or len(loads["24"]) < 1 or
            len(loads["0x1B0"]) != 2 or len(loads["0x1B8"]) != 1):
        raise ValueError(f"{label} indexed fp_c4 record layout changed: {loads}")
    direct_loads = set(loads["16"] + loads["24"])
    environment_loads = set(loads["0x1B0"] + loads["0x1B8"])

    inverse_pi = [
        temporary for temporary, rows in graph["definitions"].items()
        if any(re.fullmatch(
            r"temp_\d+ \* 0\.318309873", str(row["expression"]))
            for row in rows)]
    direct_terms: list[str] = []
    environment_terms: list[str] = []
    environment_channels: set[str] = set()
    metallic_temporaries: set[str] = set()
    irradiance_sample = re.search(
        r"(temp_\d+) = textureLod\(fp_t_tcb_34, .*?\)\.xyz;",
        compact)
    if irradiance_sample is None:
        raise ValueError(f"{label} diffuse irradiance sample changed")
    irradiance_components = {
        channel: temporary
        for channel in "xyz"
        for temporary, rows in graph["definitions"].items()
        if any(str(row["expression"]) ==
               f"{irradiance_sample.group(1)}.{channel}" for row in rows)}
    if set(irradiance_components) != set("xyz"):
        raise ValueError(f"{label} diffuse irradiance channels changed")

    for temporary in inverse_pi:
        closure = flow.backward_closure(graph, {temporary})
        samplers, buffers = flow.references_in_closure(graph, closure)
        direct_dependencies = direct_loads & closure
        environment_dependencies = environment_loads & closure
        if "fp_t_tcb_34" not in samplers:
            if len(direct_dependencies) != 1 or environment_dependencies:
                raise ValueError(f"{label} direct inverse-pi input changed")
            direct_terms.append(temporary)
            continue
        if len(environment_dependencies) != 1 or direct_dependencies:
            raise ValueError(f"{label} environment inverse-pi input changed")
        channel_sets = []
        for field in ("26", "41"):
            channels = {
                match.group(1) for value in buffers
                if (match := re.fullmatch(
                    rf"fp_c4\[{field}\]\.([xyz])", value))}
            channel_sets.append(channels)
        if (len(channel_sets[0]) != 1 or
                channel_sets[0] != channel_sets[1] or
                "fp_c3[28].x" not in buffers):
            raise ValueError(
                f"{label} environment fixed multiplier chain changed")
        channel = next(iter(channel_sets[0]))
        environment_channels.add(channel)
        irradiance = irradiance_components[channel]
        complements: list[str] = []
        for negative, rows in graph["definitions"].items():
            if negative not in closure or not any(
                    str(row["expression"]) == f"0.0 - {irradiance}"
                    for row in rows):
                continue
            for candidate, candidate_rows in graph["definitions"].items():
                if candidate not in closure:
                    continue
                for row in candidate_rows:
                    match = re.fullmatch(
                        rf"fma\((temp_\d+), {negative}, {irradiance}\)",
                        str(row["expression"]))
                    if match:
                        complements.append(match.group(1))
        if len(complements) != 1:
            raise ValueError(
                f"{label} nonmetal irradiance factor changed for {channel}")
        metallic_temporaries.add(complements[0])
        environment_terms.append(temporary)
    if (len(direct_terms) != 3 or len(environment_terms) != 3 or
            environment_channels != set("xyz") or
            len(metallic_temporaries) != 1):
        raise ValueError(
            f"{label} direct/environment inverse-pi split changed")
    metallic_closure = flow.backward_closure(graph, metallic_temporaries)
    metallic_samplers, metallic_buffers = flow.references_in_closure(
        graph, metallic_closure)
    if ("fp_c7[1].w" not in metallic_buffers or
            "fp_t_tcb_16" not in metallic_samplers):
        raise ValueError(f"{label} layer-resolved metallic input changed")

    xy = set(loads["16"])
    z = set(loads["24"])
    gate = None
    for sum_temporary, rows in graph["definitions"].items():
        for row in rows:
            add = re.fullmatch(
                r"(temp_\d+) \+ (temp_\d+)", str(row["expression"]))
            if add is None or set(add.groups()) != xy:
                continue
            for negative_z, negative_rows in graph["definitions"].items():
                if not any(re.fullmatch(
                        r"0\.0 - (temp_\d+)", str(item["expression"])) and
                        re.fullmatch(
                            r"0\.0 - (temp_\d+)",
                            str(item["expression"])).group(1) in z
                        for item in negative_rows):
                    continue
                comparison = re.search(
                    rf"(temp_\d+) = {sum_temporary} > {negative_z};",
                    compact)
                if comparison:
                    gate = comparison.group(1)
                    break
            if gate:
                break
        if gate:
            break
    if gate is None:
        raise ValueError(f"{label} direct-light presence gate changed")
    fallback_values = re.findall(
        rf"if \(!{gate}\)\s*\{{\s*temp_\d+ = ([01]\.0);\s*\}}",
        compact)
    if sorted(fallback_values) != ["0.0", "0.0", "1.0"]:
        raise ValueError(f"{label} direct-light fallback vector changed")
    for channel in "xyz":
        if not re.search(
                rf"if \({gate}\)\s*\{{[^}}]*"
                rf"0\.0 - fp_c4\.data\[0\]\.{channel};[^}}]*\}}",
                compact):
            raise ValueError(f"{label} direct-light direction gate changed")

    return {
        "selector": "max(trunc(fp_c7[39].x), 0)",
        "record_stride_bytes": 16,
        "direct_record": {
            "field": "fp_c4[1 + light_index].rgb",
            "byte_offsets": [16, 20, 24],
            "normalization": "1/pi",
            "equation": "fp_c4[1 + light_index].rgb / pi",
            "presence_gate": "sum(rgb) > 0",
            "absent_direction_fallback": "vec3(0, 0, 1)",
            "present_direction": "-fp_c4[0].xyz",
        },
        "diffuse_environment_record": {
            "field": "fp_c4[27 + light_index].rgb",
            "byte_offsets": [432, 436, 440],
            "fixed_fields": [
                "fp_c4[26].rgb", "fp_c4[41].rgb", "fp_c3[28].x"],
            "equation": (
                "diffuse_irradiance_rgb * (1 - layered_metallic) * "
                "fp_c4[41].rgb * fp_c3[28].x * fp_c4[26].rgb * "
                "fp_c4[27 + light_index].rgb / pi"),
        },
        "inverse_pi_terms": {
            "direct_rgb": len(direct_terms),
            "diffuse_environment_rgb": len(environment_terms),
        },
        "proof": "compiled_address_math_plus_backward_dependency_closure",
    }


def shadow_sampling_contract(source: str, label: str) -> dict[str, Any]:
    """Prove the shared projected/cascaded scene-shadow output slice."""
    require_fragments(source, [
        "uniform sampler2D fp_t_tcb_3E;",
        "uniform sampler2DArray fp_t_tcb_28;",
        "uniform sampler2DArray fp_t_tcb_38;",
        "textureSize(fp_t_tcb_28, 0)",
        "textureSize(fp_t_tcb_38, 0)",
        "fp_c6.data[int(",
        "floatBitsToInt(",
    ], label)
    depth_samples = len(re.findall(
        r"texture\(fp_t_tcb_28\b", source))
    tag_fetches = len(re.findall(
        r"texelFetch\(fp_t_tcb_38\b", source))
    projected_mask_samples = len(re.findall(
        r"texture\(fp_t_tcb_3E\b", source))
    tap_weights = len(re.findall(r"\b0\.0625\b", source))
    if (depth_samples, tag_fetches, projected_mask_samples, tap_weights) != (
            16, 16, 1, 16):
        raise ValueError(
            f"{label} scene-shadow sampling changed: "
            f"{depth_samples}/{tag_fetches}/{projected_mask_samples}/"
            f"{tap_weights}")

    compact = source_without_comments(source)
    merge_pattern = re.compile(
        r"(?P<negative>temp_\d+) = 0\.0 - (?P<weight>temp_\d+);\s*"
        r"(?P<blend>temp_\d+) = fma\((?P=weight), "
        r"(?P<mask>temp_\d+), (?P=negative)\);\s*"
        r"(?P<output>temp_\d+) = fma\((?P=blend), "
        r"(?P<cascade>temp_\d+), (?P=cascade)\);")
    matches = list(merge_pattern.finditer(compact))
    if len(matches) != 1:
        raise ValueError(f"{label} scene-shadow merge changed: {len(matches)}")
    match = matches[0]
    graph = flow.build_graph(compact)
    mask_rows = graph["definitions"].get(match.group("mask"), [])
    if not any("fp_t_tcb_3E" in row["expression"] for row in mask_rows):
        raise ValueError(f"{label} projected shadow-mask source changed")
    cascade_closure = flow.backward_closure(
        graph, {match.group("cascade")})
    cascade_samplers, _ = flow.references_in_closure(
        graph, cascade_closure)
    # The decoder reuses the tag-fetch temporaries across control-flow joins
    # in variation 1214, so its conservative SSA closure can lose the tag
    # alias. The exact 16 direct texelFetch calls and their bitwise comparisons
    # above remain the authoritative tag-array proof.
    if "fp_t_tcb_28" not in cascade_samplers:
        raise ValueError(f"{label} cascaded shadow-array source changed")
    return {
        "resources": {
            "fp_t_tcb_3E": {
                "type": "sampler2D",
                "structural_role": "projected_scene_shadow_mask",
                "samples": projected_mask_samples,
            },
            "fp_t_tcb_28": {
                "type": "sampler2DArray",
                "structural_role": "cascaded_filtered_shadow_depth",
                "samples": depth_samples,
            },
            "fp_t_tcb_38": {
                "type": "sampler2DArray",
                "structural_role": "cascaded_shadow_texel_tag",
                "integer_texel_fetches": tag_fetches,
            },
        },
        "filter": {
            "cascade_taps": 16,
            "tap_weight": "1/16",
            "tag_validation": (
                "companion texel tags are compared bitwise before depth "
                "comparisons contribute to the filter"),
            "cascade_transform_buffer": "fp_c6 dynamic rows",
        },
        "combine": (
            "cascade_visibility * "
            "(1 + projection_weight * (projected_mask - 1))"),
        "proof": "cross_family_compiled_operation_identity",
        "classification_strength": (
            "strong structural classification; stripped reflection omits "
            "the source resource names"),
    }


def ik_scene_light_contract(source: str, label: str) -> dict[str, Any]:
    """Prove how IkCharacter consumes the shared scene-shadow result."""
    compact = source_without_comments(source)
    graph = flow.build_graph(compact)
    shift_pattern = re.compile(
        r"(?P<negative>temp_\d+) = 0\.0 - "
        r"fp_c7\.data\[104\]\.x;\s*"
        r"(?P<shifted>temp_\d+) = (?P<shadowed>temp_\d+) \+ "
        r"(?P=negative);\s*"
        r"(?P<clamped>temp_\d+) = clamp\((?P=shifted), 0\.0, 1\.0\);")
    shift_matches = list(shift_pattern.finditer(compact))
    if len(shift_matches) != 1:
        raise ValueError(f"{label} ShadowingShift input changed")
    shadowed = shift_matches[0].group("shadowed")
    multiply_rows = [
        row for row in graph["definitions"].get(shadowed, [])
        if re.fullmatch(r"temp_\d+ \* temp_\d+", row["expression"])]
    if len(multiply_rows) != 1:
        raise ValueError(f"{label} shadowed half-Lambert multiply changed")
    operands = re.findall(r"temp_\d+", multiply_rows[0]["expression"])
    half_lambert_operands = [
        operand for operand in operands
        if any(re.fullmatch(
            r"fma\(temp_\d+, 0\.5, 0\.5\)", row["expression"])
            for row in graph["definitions"].get(operand, []))]
    if len(half_lambert_operands) != 1:
        raise ValueError(f"{label} wrapped NdotL operand changed")
    visibility_operand = next(
        operand for operand in operands if operand != half_lambert_operands[0])
    visibility_closure = flow.backward_closure(graph, {visibility_operand})
    visibility_samplers, _ = flow.references_in_closure(
        graph, visibility_closure)
    if not {"fp_t_tcb_28", "fp_t_tcb_3E"}.issubset(
            visibility_samplers):
        raise ValueError(f"{label} scene visibility operand changed")

    bypass_pattern = re.compile(
        r"(?P<sum>temp_\d+) = fma\(fp_c7\.data\[97\]\.w, "
        r"fp_c7\.data\[97\]\.w, (?P<scene>temp_\d+)\);\s*"
        r"(?P<clamped>temp_\d+) = clamp\((?P=sum), 0\.0, 1\.0\);")
    bypass_matches = list(bypass_pattern.finditer(compact))
    if len(bypass_matches) != 1:
        raise ValueError(f"{label} direct-light visibility bypass changed")
    bypass_match = bypass_matches[0]
    bypass_scene = bypass_match.group("scene")
    bypass_clamped = bypass_match.group("clamped")
    bypass_scene_closure = flow.backward_closure(graph, {bypass_scene})
    bypass_scene_samplers, _ = flow.references_in_closure(
        graph, bypass_scene_closure)
    if not {"fp_t_tcb_28", "fp_t_tcb_3E"}.issubset(
            bypass_scene_samplers):
        raise ValueError(f"{label} visibility-bypass scene source changed")

    scalar_pattern = re.compile(
        r"temp_\d+ = 0\.0 - fp_c7\.data\[102\]\.y;\s*"
        r"temp_\d+ = (?P<scalar>temp_\d+) \+ temp_\d+;")
    scalar_matches = list(scalar_pattern.finditer(compact))
    if len(scalar_matches) != 1:
        raise ValueError(f"{label} middle-area scalar input changed")
    scalar = scalar_matches[0].group("scalar")
    scalar_rows = graph["definitions"].get(scalar, [])
    if len(scalar_rows) != 1 or not re.fullmatch(
            r"max\(temp_\d+, temp_\d+\)", scalar_rows[0]["expression"]):
        raise ValueError(f"{label} direct-light RGB maximum changed")
    scalar_operands = re.findall(r"temp_\d+", scalar_rows[0]["expression"])
    nested_maximums = sum(
        any(re.fullmatch(
            r"max\(temp_\d+, temp_\d+\)", row["expression"])
            for row in graph["definitions"].get(operand, []))
        for operand in scalar_operands)
    if nested_maximums != 1:
        raise ValueError(f"{label} three-channel maximum shape changed")
    scalar_closure = flow.backward_closure(graph, {scalar})
    if bypass_clamped not in scalar_closure:
        raise ValueError(f"{label} direct-light visibility bypass disconnected")
    scalar_expressions = "\n".join(
        row["expression"]
        for temporary in scalar_closure
        for row in graph["definitions"].get(temporary, []))
    inverse_pi_terms = scalar_expressions.count("0.318309873")
    _, scalar_buffers = flow.references_in_closure(graph, scalar_closure)
    if inverse_pi_terms != 3 or "fp_c4[dynamic]" not in scalar_buffers:
        raise ValueError(f"{label} direct-diffuse scene input changed")
    return {
        "shadow_process_input": (
            "clamp(wrapped_NdotL * combined_scene_shadow_visibility - "
            "ShadowingShift, 0, 1)"),
        "direct_light_visibility": (
            "clamp(combined_scene_shadow_visibility + "
            "fp_c7[97].w^2, 0, 1)"),
        "direct_light_visibility_control": {
            "field": "fp_c7[97].w",
            "semantic_classification": "anonymous_shadow_bypass_scalar",
            "classification_strength": (
                "exact operation and use sites; stripped reflection omits "
                "the source field name"),
        },
        "middle_dark_input": "max(direct_diffuse_rgb)",
        "direct_diffuse_evidence": {
            "rgb_channels": 3,
            "inverse_pi_terms": inverse_pi_terms,
            "scene_light_buffer": "fp_c4 dynamic fields",
            "depends_on_combined_scene_shadow": True,
        },
        "normalized_phlosion_boundary": (
            "biasedLambert * clamp(neutral_scene_shadow_visibility + "
            "neutral_shadow_bypass^2, 0, 1)"),
        "proof": "compiled_backward_dependency_plus_operation_identity",
        "unavailable_runtime_values": [
            "bound projected and cascaded shadow textures",
            "fp_c4 scene-light RGB and intensity",
            "fp_c6 cascade transforms",
            "scene shadow selection, fade, and bias constants",
            "fp_c7[97].w shadow-bypass value",
        ],
    }


def receive_shadow_corpus(study_root: pathlib.Path) -> dict[str, Any]:
    path = study_root / "za_kanto_shader_inventory.json"
    report = read_json(path)
    if (report.get("schema") != INVENTORY_SCHEMA or
            report.get("scope", {}).get("source_profile") != SOURCE_PROFILE):
        raise ValueError("Unsupported private Z-A shader inventory")
    declared = [
        row for row in report.get("material_permutations", [])
        if "ReceiveShadow" in row.get("shader_options", {})]
    enabled = [
        row for row in declared
        if row.get("shader_options", {}).get("ReceiveShadow") == "1"]
    declared_materials = sum(int(row["material_count"]) for row in declared)
    enabled_materials = sum(int(row["material_count"]) for row in enabled)
    if (len(declared), len(enabled), declared_materials,
            enabled_materials) != (10, 10, 226, 226):
        raise ValueError("Selected Z-A ReceiveShadow corpus changed")
    return {
        "declaring_permutations": len(declared),
        "enabled_permutations": len(enabled),
        "declaring_materials": declared_materials,
        "enabled_materials": enabled_materials,
        "non_declaring_materials": (
            int(report["summary"]["selected_materials"]) -
            declared_materials),
        "conclusion": (
            "Every selected forward material that declares ReceiveShadow "
            "requests it enabled; the remaining eight standalone Eye-family "
            "materials do not declare the option."),
        "proof": "complete_selected_material_census",
        "inventory_sha256": sha256(path),
    }


def material_program_rows(study_root: pathlib.Path) -> list[dict[str, Any]]:
    selected_root = study_root / "selected-programs"
    manifest_path = selected_root / "selected_programs_manifest.json"
    manifest = read_json(manifest_path)
    if (manifest.get("schema") != SELECTED_MANIFEST_SCHEMA or
            manifest.get("source_profile") != SOURCE_PROFILE):
        raise ValueError("Unsupported private Z-A selected-program manifest")

    rows: list[dict[str, Any]] = []
    selected = [
        row for row in manifest.get("programs", [])
        if (row.get("shader_family") == "IkCharacter" or
            row.get("shader_family") == "FresnelEffect")
    ]
    for record in selected:
        relative = pathlib.PurePosixPath(str(record["directory"]))
        variation = int(record["variation_index"])
        path = selected_root.joinpath(*relative.parts) / (
            f"v{variation:04d}.fsh.maxwell.glsl")
        expected = str(record["fragment_glsl_sha256"])
        if sha256(path) != expected:
            raise ValueError(f"Selected program hash changed: {path}")
        source = path.read_text(encoding="utf-8-sig")
        rows.append({
            "shader_family": str(record["shader_family"]),
            "variation_index": variation,
            "role": "selected_kanto_material_program",
            "fragment_sha256": expected,
            "final_scene_fade": final_scene_fade(
                source, f"{record['shader_family']} variation {variation}"),
            "camera_relative_vector": camera_relative_vector(
                source, f"{record['shader_family']} variation {variation}"),
            "dominant_direct_light": dominant_direct_light_contract(
                source, f"{record['shader_family']} variation {variation}"),
            "diffuse_irradiance": diffuse_irradiance_contract(
                source, f"{record['shader_family']} variation {variation}"),
            "scene_shadow": shadow_sampling_contract(
                source, f"{record['shader_family']} variation {variation}"),
        })
        if record.get("shader_family") == "IkCharacter":
            rows[-1]["ik_scene_light"] = ik_scene_light_contract(
                source, f"IkCharacter variation {variation}")
            rows[-1]["ik_indexed_light"] = ik_indexed_light_contract(
                source, f"IkCharacter variation {variation}")
            rows[-1]["ik_local_reflection_direction"] = (
                ik_local_reflection_direction_contract(
                    source, f"IkCharacter variation {variation}"))

    adjacent = [
        ("Hair", "hair"),
        ("IkStandard", "ik_standard"),
    ]
    for family, directory in adjacent:
        path = (study_root / "adjacent-programs" / directory / "v0000" /
                "v0000.fsh.maxwell.glsl")
        source = path.read_text(encoding="utf-8-sig")
        rows.append({
            "shader_family": family,
            "variation_index": 0,
            "role": "adjacent_cross_family_control",
            "fragment_sha256": sha256(path),
            "final_scene_fade": final_scene_fade(source, family),
            "camera_relative_vector": camera_relative_vector(source, family),
            "dominant_direct_light": dominant_direct_light_contract(
                source, family),
            "diffuse_irradiance": diffuse_irradiance_contract(source, family),
            "scene_shadow": shadow_sampling_contract(source, family),
        })
    if len(rows) != 7:
        raise ValueError(f"Z-A material scene-boundary coverage changed: {len(rows)}")
    return rows


def receive_shadow_boundary(game_root: pathlib.Path) -> dict[str, Any]:
    path = game_root / "docs" / "kanto" / "evidence" / (
        "za_kanto_option_dataflow.json")
    report = read_json(path)
    if report.get("schema") != OPTION_DATAFLOW_SCHEMA:
        raise ValueError("Unsupported promoted Z-A option-dataflow report")
    rows = [
        row for row in report.get("differentials", [])
        if row.get("shader_family") == "IkCharacter" and
        row.get("changed_option") == "ReceiveShadow"
    ]
    if len(rows) != 3 or any(
            row.get("fragment", {}).get("classification") !=
            "identical_compiled_output_slice" for row in rows):
        raise ValueError("ReceiveShadow no longer has three identical fragments")
    return {
        "one_option_edges": len(rows),
        "selected_variations": sorted(
            int(row["selected_variation"]) for row in rows),
        "comparison_variations": sorted(
            int(row["comparison_variation"]) for row in rows),
        "fragment_classification": "identical_compiled_output_slice",
        "conclusion": (
            "ReceiveShadow is not a material-program equation switch in these "
            "selected permutations; its effect is supplied by shared scene "
            "state or draw setup."),
        "proof": "complete_exact_one_option_graph",
    }


def tonemap_boundary(study_root: pathlib.Path) -> dict[str, Any]:
    directory = study_root / "adjacent-programs" / "post_effects_tonemap" / "v0000"
    path = directory / "v0000.fsh.maxwell.glsl"
    descriptor_path = directory / "v0000.json"
    descriptor = read_json(descriptor_path)
    if (int(descriptor.get("variation_index", -1)) != 0 or
            int(descriptor.get("fragment_bytes", -1)) != 1792):
        raise ValueError("Z-A tone-map program descriptor changed")
    source = path.read_text(encoding="utf-8-sig")
    require_fragments(source, [
        "uniform sampler2D fp_t_tcb_8;",
        "uniform sampler3D fp_t_tcb_A;",
        "temp_31 = fp_c3.data[114].w * 1.44269502;",
        "temp_51 = exp2(temp_31);",
        "temp_66 = fma(temp_63, fp_c1.data[0].z, 0.0479959995);",
        "temp_72 = fma(temp_68, fp_c1.data[0].w, 0.386036009);",
        "temp_82 = texture(fp_t_tcb_A, vec3(temp_79, temp_80, temp_81),",
        "temp_101 = temp_83 * 12.9200001;",
        "temp_111 = fma(temp_109, fp_c1.data[1].y, -0.0549999997);",
        "temp_134 = fma(temp_127, fp_c3.data[25].x, temp_131);",
        "temp_137 = fma(temp_130, fp_c3.data[26].x, temp_134);",
        "temp_140 = temp_137 + fp_c3.data[27].x;",
    ], "PostEffectsToneMap variation 0")
    return {
        "shader_family": "PostEffectsToneMap",
        "variation_index": 0,
        "fragment_sha256": sha256(path),
        "descriptor_sha256": sha256(descriptor_path),
        "input_roles": {
            "fp_t_tcb_8": "rendered_scene_color",
            "fp_t_tcb_A": "three_dimensional_color_lut",
        },
        "ordered_operations": [
            "sample rendered scene color",
            "apply screen-position-dependent source color blend",
            "apply exponential exposure from fp_c3[114].w",
            "encode and inset logarithmic three-dimensional LUT coordinates",
            "sample the three-dimensional color LUT",
            "apply the piecewise sRGB output transfer",
            "apply a final three-by-three color transform and RGB offset",
        ],
        "exact_equations": {
            "exposure_multiplier": "exp(fp_c3[114].w)",
            "output_transfer": "piecewise_srgb_oetf",
            "final_transform": "mat3(fp_c3[24..26].rgb) * rgb + fp_c3[27].rgb",
        },
        "proof": "compiled_operation_identity",
        "unavailable_runtime_values": [
            "fp_c3[114].w exposure",
            "screen-position blend parameters fp_c3[50..52]",
            "the bound three-dimensional color LUT payload",
            "final color-transform rows fp_c3[24..27]",
            "render-target format and presentation state",
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-root", type=pathlib.Path, required=True)
    parser.add_argument("--shader-study", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    game_root = args.game_root.resolve()
    study_root = args.shader_study.resolve()
    program_rows = material_program_rows(study_root)
    receive_shadow = receive_shadow_boundary(game_root)
    receive_shadow_inventory = receive_shadow_corpus(study_root)
    tonemap = tonemap_boundary(study_root)
    report = {
        "schema": SCHEMA,
        "source_profile": SOURCE_PROFILE,
        "method": {
            "runtime_execution": False,
            "emulator_used": False,
            "evidence_level": (
                "cross_family_compiled_operation_identity_plus_symbolic_"
                "environment_vector_proof_plus_ik_backward_dependency_"
                "closure_plus_complete_option_and_material_graph"),
            "claim_boundary": (
                "This proves the projected/cascaded scene-shadow sampling "
                "shape, the direct/diffuse environment field layout, exact "
                "cube-coordinate conventions, IkCharacter light-scalar "
                "insertion points, and final color operation order. It does "
                "not recover values or textures supplied by the source "
                "runtime, its bound LUT, or final presentation state."),
        },
        "summary": {
            "material_fragment_programs": len(program_rows),
            "material_shader_families": len({
                row["shader_family"] for row in program_rows}),
            "programs_with_exact_final_scene_fade": sum(
                "final_scene_fade" in row for row in program_rows),
            "programs_with_camera_position_classification": sum(
                "camera_relative_vector" in row for row in program_rows),
            "programs_with_dominant_light_direction": sum(
                "dominant_direct_light" in row for row in program_rows),
            "programs_with_diffuse_irradiance_direction": sum(
                "diffuse_irradiance" in row for row in program_rows),
            "receive_shadow_identical_fragment_edges": receive_shadow[
                "one_option_edges"],
            "receive_shadow_enabled_materials": receive_shadow_inventory[
                "enabled_materials"],
            "programs_with_exact_scene_shadow_sampling": sum(
                "scene_shadow" in row for row in program_rows),
            "ik_programs_with_exact_scene_light_inputs": sum(
                "ik_scene_light" in row for row in program_rows),
            "ik_programs_with_indexed_light_records": sum(
                "ik_indexed_light" in row for row in program_rows),
            "ik_programs_with_exact_local_reflection_direction": sum(
                "ik_local_reflection_direction" in row
                for row in program_rows),
            "tonemap_programs": 1,
            "runtime_changes_authorized_by_this_report": 3,
        },
        "shared_scene_fields": {
            "camera_world_position": "fp_c5[19].xyz",
            "dominant_directional_light_vector": "fp_c4[0].xyz",
            "final_scene_color": "fp_c10[12].rgb",
            "final_scene_fade": "fp_c10[12].w",
        },
        "material_programs": program_rows,
        "receive_shadow": receive_shadow,
        "receive_shadow_inventory": receive_shadow_inventory,
        "scene_light_composition": {
            "shadow_sampling": (
                "one projected 2D mask plus a 16-tap cascaded depth array "
                "with a companion texel-tag array"),
            "shadow_combine": (
                "cascade_visibility * "
                "(1 + projection_weight * (projected_mask - 1))"),
            "ik_shadow_process_input": (
                "wrapped_NdotL * combined_scene_shadow_visibility"),
            "ik_direct_light_visibility": (
                "clamp(combined_scene_shadow_visibility + "
                "fp_c7[97].w^2, 0, 1)"),
            "ik_middle_dark_input": "max(direct_diffuse_rgb)",
            "direct_diffuse_normalization": "three inverse-pi channel terms",
            "indexed_direct_radiance": (
                "fp_c4[1 + light_index].rgb / pi"),
            "diffuse_irradiance_direction": (
                "max_abs_normalize(vec3(n.x, n.y, -n.z)) at LOD 0"),
            "ik_diffuse_environment": (
                "diffuse_irradiance_rgb * (1 - layered_metallic) * "
                "fp_c4[41].rgb * fp_c3[28].x * fp_c4[26].rgb * "
                "fp_c4[27 + light_index].rgb / pi"),
            "ik_local_reflection_direction": (
                "max_abs_normalize(reflect(-view, mapped_normal))"),
            "cube_coordinate_boundary": (
                "the scene diffuse cube flips shading-normal Z; the material "
                "local-reflection cube does not"),
            "proof": (
                "seven_program_cross_family_operation_identity_plus_four_"
                "program_symbolic_vector_and_backward_dependency_proof"),
        },
        "post_effect_tonemap": tonemap,
        "implementation_decision": {
            "authorized": [
                (
                    "replace Phlosion's power-law Z-A rim domain with the "
                    "exact compiled smoothstep plus symmetric contrast remap"),
                (
                    "stage the normalized Z-A direct-light scalar and "
                    "ShadowingShift input behind one explicit neutral scene-"
                    "shadow visibility boundary on all rendering APIs"),
                (
                    "pin IkCharacter local-probe lookup to the proven "
                    "reflect(-view, mapped_normal) direction without applying "
                    "the separate diffuse-cube Z flip"),
            ],
            "kept_neutral": [
                "bound projected and cascaded scene-shadow textures",
                "fp_c7[97].w anonymous shadow-bypass scalar",
                "source scene-light RGB and intensity values",
                "bound diffuse-irradiance cube payload",
                "fp_c10[12] final scene fade",
                "source exposure",
                "source three-dimensional color LUT",
                "source final color matrix and framebuffer state",
            ],
        },
        "source_sha256": {
            "selected_program_manifest": sha256(
                study_root / "selected-programs" /
                "selected_programs_manifest.json"),
            "promoted_option_dataflow": sha256(
                game_root / "docs" / "kanto" / "evidence" /
                "za_kanto_option_dataflow.json"),
            "private_shader_inventory": sha256(
                study_root / "za_kanto_shader_inventory.json"),
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
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
