"""Export project-owned Blender meshes as a Phlosion environment patch."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import sys
from typing import Any

import bpy
from mathutils import Vector


PATCH_COLLECTION = "PAC_EDIT_PATCH"
LAYOUT_COLLECTION = "PAC_LAYOUT_DELTA"
PATCH_KIND = "phlosion_environment_patch"
PATCH_SCHEMA = 1


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args(sys.argv[sys.argv.index("--") + 1 :])


def linear_channel_to_srgb(value: float) -> float:
    value = max(0.0, value)
    if value <= 0.0031308:
        return value * 12.92
    return 1.055 * math.pow(value, 1.0 / 2.4) - 0.055


def blender_to_source_position(value: Vector) -> list[float]:
    return [value.x * 100.0, value.z * 100.0, -value.y * 100.0]


def blender_to_source_direction(value: Vector) -> list[float]:
    converted = Vector((value.x, value.z, -value.y))
    if converted.length_squared <= 1.0e-12:
        raise RuntimeError("Patch contains a zero-length direction")
    converted.normalize()
    return [converted.x, converted.y, converted.z]


def layer_value(mesh: bpy.types.Mesh, name: str, loop_index: int, vertex_index: int) -> Any:
    attribute = mesh.attributes.get(name)
    if attribute is None:
        raise RuntimeError(f"Patch mesh is missing required attribute {name}")
    index = loop_index if attribute.domain == "CORNER" else vertex_index
    datum = attribute.data[index]
    for member in ("vector", "color", "value"):
        if hasattr(datum, member):
            return getattr(datum, member)
    raise RuntimeError(f"Patch attribute {name} has an unsupported data type")


def color_value(mesh: bpy.types.Mesh, name: str, loop_index: int, vertex_index: int) -> list[float]:
    attribute = mesh.color_attributes.get(name)
    if attribute is None:
        raise RuntimeError(f"Patch mesh is missing required color attribute {name}")
    index = loop_index if attribute.domain == "CORNER" else vertex_index
    return [float(component) for component in attribute.data[index].color]


def uv_value(mesh: bpy.types.Mesh, name: str, loop_index: int) -> list[float]:
    layer = mesh.uv_layers.get(name)
    if layer is None:
        raise RuntimeError(f"Patch mesh is missing required UV layer {name}")
    return [float(component) for component in layer.data[loop_index].uv]


def material_index(mesh: bpy.types.Mesh, polygon: bpy.types.MeshPolygon) -> int:
    attribute = mesh.attributes.get("LGPE_MATERIAL_INDEX")
    if attribute is not None and attribute.domain == "FACE":
        return int(attribute.data[polygon.index].value)
    if polygon.material_index >= len(mesh.materials):
        raise RuntimeError("Patch polygon references a missing material slot")
    material = mesh.materials[polygon.material_index]
    if material is None or "lgpe_material_index" not in material:
        raise RuntimeError(
            "Patch material slot does not retain lgpe_material_index provenance"
        )
    return int(material["lgpe_material_index"])


def export_object(obj: bpy.types.Object, depsgraph: bpy.types.Depsgraph) -> dict[str, Any]:
    patch_id = str(obj.get("phlosion_patch_id", "")).strip()
    if not patch_id:
        raise RuntimeError(
            f"Patch object {obj.name!r} is missing a stable phlosion_patch_id"
        )
    evaluated = obj.evaluated_get(depsgraph)
    mesh = evaluated.to_mesh(preserve_all_data_layers=True, depsgraph=depsgraph)
    try:
        mesh.calc_loop_triangles()
        if not mesh.loop_triangles:
            raise RuntimeError(f"Patch object {obj.name!r} has no triangles")
        if mesh.uv_layers.get("LGPE_PreviewUV0") is None:
            raise RuntimeError(
                f"Patch object {obj.name!r} has no LGPE_PreviewUV0 tangent basis"
            )
        mesh.calc_tangents(uvmap="LGPE_PreviewUV0")
        world = evaluated.matrix_world
        normal_matrix = world.to_3x3().inverted_safe().transposed()
        direction_matrix = world.to_3x3()
        groups: dict[int, list[int]] = {}
        vertices: list[dict[str, Any]] = []

        for triangle in mesh.loop_triangles:
            polygon = mesh.polygons[triangle.polygon_index]
            group = groups.setdefault(material_index(mesh, polygon), [])
            triangle_indices: list[int] = []
            for loop_index in triangle.loops:
                loop = mesh.loops[loop_index]
                vertex_index = loop.vertex_index
                vertex = mesh.vertices[vertex_index]
                position = world @ vertex.co
                normal = normal_matrix @ loop.normal
                tangent = direction_matrix @ loop.tangent
                bitangent = direction_matrix @ loop.bitangent

                raw_uv0 = uv_value(mesh, "LGPE_UV0", loop_index)
                preview_uv0 = uv_value(mesh, "LGPE_PreviewUV0", loop_index)
                preview_uv1 = uv_value(mesh, "LGPE_PreviewUV1", loop_index)
                raw_uv3 = uv_value(mesh, "LGPE_UV3", loop_index)
                preview_color = color_value(
                    mesh, "LGPE_VertexColor", loop_index, vertex_index
                )
                colors = [[
                    linear_channel_to_srgb(preview_color[channel])
                    if channel < 3 else preview_color[channel]
                    for channel in range(4)
                ]]
                colors.extend(
                    color_value(mesh, f"LGPE_COLOR{index}", loop_index, vertex_index)
                    for index in range(1, 4)
                )
                source_vertex_index = int(
                    layer_value(
                        mesh,
                        "LGPE_SOURCE_VERTEX_INDEX",
                        loop_index,
                        vertex_index,
                    )
                )
                vertices.append({
                    "position": blender_to_source_position(position),
                    "normal": blender_to_source_direction(normal),
                    "tangent": blender_to_source_direction(tangent)
                    + [float(loop.bitangent_sign)],
                    "bitangent": blender_to_source_direction(bitangent)
                    + [1.0],
                    "texcoords": [
                        raw_uv0,
                        [preview_uv0[0], 1.0 - preview_uv0[1]],
                        [preview_uv1[0], 1.0 - preview_uv1[1]],
                        raw_uv3,
                    ],
                    "colors": colors,
                    "normal_w": float(
                        layer_value(mesh, "LGPE_NORMAL_W", loop_index, vertex_index)
                    ),
                    "joints": [
                        int(layer_value(mesh, f"LGPE_JOINT_{index}", loop_index, vertex_index))
                        for index in range(4)
                    ],
                    "weights": [
                        float(layer_value(mesh, f"LGPE_WEIGHT_{index}", loop_index, vertex_index))
                        for index in range(4)
                    ],
                    "source_vertex_index": source_vertex_index,
                })
                triangle_indices.append(len(vertices) - 1)
            group.extend(triangle_indices)

        return {
            "id": patch_id,
            "display_name": str(obj.get("phlosion_display_name", obj.name)),
            "vertices": vertices,
            "material_groups": [
                {"material_index": index, "indices": indices}
                for index, indices in sorted(groups.items())
            ],
            "provenance": {
                "blender_object": obj.name,
                "source_mesh_index": int(obj.get("lgpe_source_mesh_index", -1)),
            },
        }
    finally:
        evaluated.to_mesh_clear()


def export_patch(output: Path) -> dict[str, int]:
    scene = bpy.context.scene
    if int(scene.get("phlosion_bridge_schema", 0)) != 1:
        raise RuntimeError("The open Blender file is not a Phlosion bridge scene")
    patch = bpy.data.collections.get(PATCH_COLLECTION)
    layout = bpy.data.collections.get(LAYOUT_COLLECTION)
    if patch is None or layout is None:
        raise RuntimeError("The Phlosion patch collections are missing")
    if list(layout.all_objects):
        raise RuntimeError(
            "Layout-delta export is not part of the mesh-patch contract yet; "
            "keep source suppressions in the Phlosion authored scene"
        )

    patch_objects = sorted(
        (obj for obj in patch.all_objects if obj.type == "MESH"),
        key=lambda obj: obj.name,
    )
    depsgraph = bpy.context.evaluated_depsgraph_get()
    meshes = [export_object(obj, depsgraph) for obj in patch_objects]
    document = {
        "schema_version": PATCH_SCHEMA,
        "kind": PATCH_KIND,
        "source": {
            "profile_id": str(scene["lgpe_profile_id"]),
            "model_sha256": str(scene["lgpe_source_model_sha256"]),
            "geometry_sha256": str(scene["lgpe_geometry_sha256"]),
            "coordinate_system": str(scene["lgpe_source_coordinate_system"]),
        },
        "meshes": meshes,
        "validation": {
            "empty_delta": not meshes,
            "source_geometry_exported": False,
            "preview_materials_exported": False,
            "triangle_only_evaluated_capture": True,
            "canonical_material_references": True,
        },
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    vertex_count = sum(len(mesh["vertices"]) for mesh in meshes)
    result = {
        "meshes": len(meshes),
        "vertices": vertex_count,
        "triangles": vertex_count // 3,
    }
    print(
        "PHLOSION_BLENDER_PATCH_PASS "
        + " ".join(f"{key}={value}" for key, value in result.items())
    )
    return result


def main() -> None:
    args = arguments()
    export_patch(args.output)


if __name__ == "__main__":
    main()
