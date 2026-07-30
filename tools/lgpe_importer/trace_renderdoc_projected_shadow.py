"""Trace LGPE's projected-shadow inputs in a RenderDoc capture.

Run this script with qrenderdoc's ``--python`` option.  It finds draws using
the requested pixel shader, records the bound depth texture, and decodes the
16-float light projection stored at c3.data[80..95].  This is intentionally a
capture-evidence tool: it does not fit or infer a projection matrix.
"""

import hashlib
import json
import os
import struct
import traceback

import renderdoc as rd


capture_path = os.environ["ROUTE1_RDOC_CAPTURE"]
output_path = os.environ["ROUTE1_RDOC_PROJECTED_SHADOW_TRACE"]
target_pixel_shader = int(
    os.environ.get("ROUTE1_RDOC_PROJECTED_SHADOW_PIXEL_SHADER", "2431")
)
target_depth_texture = int(
    os.environ.get("ROUTE1_RDOC_PROJECTED_SHADOW_DEPTH_TEXTURE", "68498")
)
matrix_first_float = int(
    os.environ.get("ROUTE1_RDOC_PROJECTED_SHADOW_MATRIX_FIRST_FLOAT", "80")
)
matrix_float_count = int(
    os.environ.get("ROUTE1_RDOC_PROJECTED_SHADOW_MATRIX_FLOAT_COUNT", "16")
)
require_depth_texture = (
    os.environ.get(
        "ROUTE1_RDOC_PROJECTED_SHADOW_REQUIRE_DEPTH_TEXTURE", "1"
    )
    != "0"
)


def resource_id(value):
    try:
        return int(value)
    except Exception:
        return str(value)


def descriptor_resource_id(value):
    descriptor = getattr(value, "descriptor", value)
    for attribute in ("resource", "resourceId"):
        resource = getattr(descriptor, attribute, None)
        if resource is not None:
            return resource_id(resource)
    return resource_id(rd.ResourceId.Null())


def descriptor_attribute(value, attribute, default=0):
    descriptor = getattr(value, "descriptor", value)
    return getattr(descriptor, attribute, default)


def walk(actions):
    for action in actions:
        yield action
        if action.children:
            yield from walk(action.children)


def resource_binding(binding):
    return {
        "resource_id": descriptor_resource_id(binding),
        "byte_offset": int(
            descriptor_attribute(binding, "byteOffset", 0)
        ),
        "byte_size": int(
            descriptor_attribute(binding, "byteSize", 0)
        ),
    }


def read_bound_floats(controller, binding, first_float, float_count):
    descriptor = getattr(binding, "descriptor", binding)
    resource = getattr(descriptor, "resource", None)
    if resource is None:
        resource = getattr(descriptor, "resourceId")
    byte_offset = int(getattr(descriptor, "byteOffset", 0)) + first_float * 4
    byte_count = float_count * 4
    data = bytes(
        controller.GetBufferData(
            resource,
            byte_offset,
            byte_count,
        )
    )
    if len(data) != byte_count:
        raise RuntimeError(
            "requested {0} bytes at {1}, received {2}".format(
                byte_count, byte_offset, len(data)
            )
        )
    return {
        "absolute_byte_offset": byte_offset,
        "byte_count": byte_count,
        "sha256": hashlib.sha256(data).hexdigest(),
        "float32": list(struct.unpack("<{0}f".format(float_count), data)),
        "hex": data.hex(),
    }


report = {
    "capture": os.path.abspath(capture_path),
    "renderdoc_version": rd.GetVersionString(),
    "target_pixel_shader": target_pixel_shader,
    "target_depth_texture": target_depth_texture,
    "matrix_range": {
        "first_float": matrix_first_float,
        "float_count": matrix_float_count,
    },
    "require_depth_texture": require_depth_texture,
    "target_resource": None,
    "target_usages": [],
    "draws": [],
    "errors": [],
}

capture_file = None
controller = None

try:
    capture_file = rd.OpenCaptureFile()
    status = capture_file.OpenFile(capture_path, "rdc", None)
    if not status.OK():
        raise RuntimeError(str(status))

    status, controller = capture_file.OpenCapture(rd.ReplayOptions(), None)
    if not status.OK():
        raise RuntimeError(str(status))

    structured = controller.GetStructuredFile()
    resources = {
        resource_id(resource.resourceId): resource
        for resource in controller.GetResources()
    }
    textures = {
        resource_id(texture.resourceId): texture
        for texture in controller.GetTextures()
    }

    target_resource = resources.get(target_depth_texture)
    target_texture = textures.get(target_depth_texture)
    if target_resource is not None or target_texture is not None:
        report["target_resource"] = {
            "resource_id": target_depth_texture,
            "name": (
                target_resource.name if target_resource is not None else ""
            ),
            "type": (
                str(target_resource.type)
                if target_resource is not None
                else ""
            ),
            "parents": (
                [resource_id(value) for value in target_resource.parentResources]
                if target_resource is not None
                else []
            ),
            "derived": (
                [
                    resource_id(value)
                    for value in target_resource.derivedResources
                ]
                if target_resource is not None
                else []
            ),
            "width": (
                int(target_texture.width) if target_texture is not None else 0
            ),
            "height": (
                int(target_texture.height) if target_texture is not None else 0
            ),
            "format": (
                str(target_texture.format)
                if target_texture is not None
                else ""
            ),
            "creation_flags": (
                str(target_texture.creationFlags)
                if target_texture is not None
                else ""
            ),
        }

    if target_resource is not None:
        for usage in controller.GetUsage(target_resource.resourceId):
            report["target_usages"].append(
                {
                    "event_id": int(usage.eventId),
                    "usage": str(usage.usage),
                }
            )

    for action in walk(controller.GetRootActions()):
        if not (action.flags & rd.ActionFlags.Drawcall):
            continue

        try:
            controller.SetFrameEvent(action.eventId, False)
            pipeline = controller.GetPipelineState()
            pixel_shader = pipeline.GetShader(rd.ShaderStage.Pixel)
            read_only = list(
                pipeline.GetReadOnlyResources(rd.ShaderStage.Pixel)
            )
            reads_target_depth = any(
                descriptor_resource_id(bound) == target_depth_texture
                for bound in read_only
            )
            if (
                target_pixel_shader != 0
                and resource_id(pixel_shader) != target_pixel_shader
            ):
                continue
            if require_depth_texture and not reads_target_depth:
                continue

            row = {
                "event_id": int(action.eventId),
                "name": action.GetName(structured),
                "indices": int(action.numIndices),
                "instances": int(action.numInstances),
                "pipeline_id": resource_id(
                    pipeline.GetGraphicsPipelineObject()
                ),
                "pixel_shader_id": resource_id(pixel_shader),
                "reads_target_depth": reads_target_depth,
                "constant_blocks": [],
                "read_only_resources": [],
            }

            for index, block in enumerate(
                pipeline.GetConstantBlocks(rd.ShaderStage.Pixel)
            ):
                block_row = {
                    "index": index,
                    "name": getattr(block, "name", ""),
                    "bind_point": str(getattr(block, "bindPoint", "")),
                    "byte_size": int(getattr(block, "byteSize", 0)),
                }
                try:
                    binding = pipeline.GetConstantBlock(
                        rd.ShaderStage.Pixel, index, 0
                    )
                    block_row["binding"] = resource_binding(binding)
                    if index == 0:
                        block_row["matrix"] = read_bound_floats(
                            controller,
                            binding,
                            matrix_first_float,
                            matrix_float_count,
                        )
                except Exception:
                    block_row["error"] = traceback.format_exc()
                row["constant_blocks"].append(block_row)

            for index, bound in enumerate(read_only):
                row["read_only_resources"].append(
                    {
                        "index": index,
                        "resource_id": descriptor_resource_id(bound),
                        "first_mip": int(
                            descriptor_attribute(bound, "firstMip", 0)
                        ),
                        "first_slice": int(
                            descriptor_attribute(bound, "firstSlice", 0)
                        ),
                        "type": str(
                            descriptor_attribute(bound, "type", "")
                        ),
                    }
                )

            report["draws"].append(row)
        except Exception:
            report["errors"].append(
                {
                    "event_id": int(action.eventId),
                    "error": traceback.format_exc(),
                }
            )

except Exception:
    report["errors"].append(
        {"phase": "capture", "error": traceback.format_exc()}
    )

finally:
    if controller is not None:
        controller.Shutdown()
    if capture_file is not None:
        capture_file.Shutdown()
    with open(output_path, "w", encoding="utf-8") as output:
        json.dump(report, output, indent=2)

raise SystemExit(0 if not report["errors"] else 1)
