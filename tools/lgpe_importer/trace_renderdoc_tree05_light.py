"""Trace Route 1 FieldTreeShader05 material constants in RenderDoc.

Run this script with qrenderdoc's ``--python`` option.  The three canonical
Route 1 FieldTreeShader05 polygon groups contain 17,556, 40,896, and 17,556
indices respectively.  Those source counts let the script identify the guest
draws without fitting their appearance, then preserve every bound pixel
constant block for inspection.

The script intentionally records raw bytes as well as float32 values.  Shader
constant-buffer names are not retained by the emulator's generated Vulkan
programs, so the raw upload is the authoritative evidence used to correlate
the decoded ``cbuf_5._m0[1].xyz`` lightColor access.
"""

import hashlib
import json
import os
import struct
import traceback

import renderdoc as rd


capture_path = os.environ["ROUTE1_RDOC_CAPTURE"]
output_path = os.environ["ROUTE1_RDOC_TREE05_LIGHT_TRACE"]
disassembly_path = os.environ.get("ROUTE1_RDOC_TREE05_DISASSEMBLY", "")
target_pixel_shader = int(
    os.environ.get("ROUTE1_RDOC_TREE05_PIXEL_SHADER", "0")
)
target_index_counts = {
    int(value)
    for value in os.environ.get(
        "ROUTE1_RDOC_TREE05_INDEX_COUNTS", "17556,40896"
    ).split(",")
    if value.strip()
}
maximum_block_bytes = int(
    os.environ.get("ROUTE1_RDOC_TREE05_MAX_BLOCK_BYTES", "4096")
)


def resource_id(value):
    try:
        return int(value)
    except Exception:
        return str(value)


def descriptor(value):
    return getattr(value, "descriptor", value)


def descriptor_resource(value):
    item = descriptor(value)
    resource = getattr(item, "resource", None)
    if resource is None:
        resource = getattr(item, "resourceId", rd.ResourceId.Null())
    return resource


def descriptor_integer(value, attribute, default=0):
    return int(getattr(descriptor(value), attribute, default))


def walk(actions):
    for action in actions:
        yield action
        if action.children:
            yield from walk(action.children)


def read_binding(controller, binding):
    resource = descriptor_resource(binding)
    byte_offset = descriptor_integer(binding, "byteOffset")
    byte_size = min(
        descriptor_integer(binding, "byteSize"),
        maximum_block_bytes,
    )
    data = bytes(controller.GetBufferData(resource, byte_offset, byte_size))
    float_bytes = len(data) - (len(data) % 4)
    floats = (
        list(struct.unpack("<{0}f".format(float_bytes // 4), data[:float_bytes]))
        if float_bytes
        else []
    )
    return {
        "resource_id": resource_id(resource),
        "byte_offset": byte_offset,
        "bound_byte_size": descriptor_integer(binding, "byteSize"),
        "captured_byte_size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "hex": data.hex(),
        "float32": floats,
    }


report = {
    "capture": os.path.abspath(capture_path),
    "renderdoc_version": rd.GetVersionString(),
    "selection": {
        "pixel_shader": target_pixel_shader,
        "index_counts": sorted(target_index_counts),
        "canonical_polygon_group_sequence": [17556, 40896, 17556],
    },
    "draws": [],
    "correlation": None,
    "shader_disassembly": None,
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

    disassembled_shader = None
    disassembled_shader_id = None

    for action in walk(controller.GetRootActions()):
        if not (action.flags & rd.ActionFlags.Drawcall):
            continue
        if int(action.numIndices) not in target_index_counts:
            continue

        try:
            controller.SetFrameEvent(action.eventId, False)
            pipeline = controller.GetPipelineState()
            pixel_shader = pipeline.GetShader(rd.ShaderStage.Pixel)
            if (
                target_pixel_shader != 0
                and resource_id(pixel_shader) != target_pixel_shader
            ):
                continue

            row = {
                "event_id": int(action.eventId),
                "name": action.GetName(structured),
                "indices": int(action.numIndices),
                "instances": int(action.numInstances),
                "pipeline_id": resource_id(
                    pipeline.GetGraphicsPipelineObject()
                ),
                "vertex_shader_id": resource_id(
                    pipeline.GetShader(rd.ShaderStage.Vertex)
                ),
                "pixel_shader_id": resource_id(pixel_shader),
                "pixel_constant_blocks": [],
                "pixel_read_only_resources": [],
            }

            for index, block in enumerate(
                pipeline.GetConstantBlocks(rd.ShaderStage.Pixel)
            ):
                block_row = {
                    "index": index,
                    "name": getattr(block, "name", ""),
                    "byte_size": int(getattr(block, "byteSize", 0)),
                    "bind_point": str(getattr(block, "bindPoint", "")),
                }
                try:
                    binding = pipeline.GetConstantBlock(
                        rd.ShaderStage.Pixel, index, 0
                    )
                    block_row["upload"] = read_binding(controller, binding)
                except Exception:
                    block_row["error"] = traceback.format_exc()
                row["pixel_constant_blocks"].append(block_row)

            for index, binding in enumerate(
                pipeline.GetReadOnlyResources(rd.ShaderStage.Pixel)
            ):
                item_id = resource_id(descriptor_resource(binding))
                item = resources.get(item_id)
                texture = textures.get(item_id)
                row["pixel_read_only_resources"].append(
                    {
                        "index": index,
                        "resource_id": item_id,
                        "resource_name": item.name if item is not None else "",
                        "width": int(texture.width) if texture is not None else 0,
                        "height": (
                            int(texture.height) if texture is not None else 0
                        ),
                        "mips": int(texture.mips) if texture is not None else 0,
                        "first_mip": descriptor_integer(
                            binding, "firstMip"
                        ),
                        "first_slice": descriptor_integer(
                            binding, "firstSlice"
                        ),
                    }
                )

            report["draws"].append(row)

            if disassembled_shader_id != resource_id(pixel_shader):
                pipeline_id = pipeline.GetGraphicsPipelineObject()
                reflection = pipeline.GetShaderReflection(rd.ShaderStage.Pixel)
                if reflection is not None:
                    disassembled_shader = controller.DisassembleShader(
                        pipeline_id, reflection, ""
                    )
                    disassembled_shader_id = resource_id(pixel_shader)
                    encoded = disassembled_shader.encode("utf-8")
                    report["shader_disassembly"] = {
                        "pixel_shader_id": disassembled_shader_id,
                        "sha256": hashlib.sha256(encoded).hexdigest(),
                        "characters": len(disassembled_shader),
                    }
        except Exception:
            report["errors"].append(
                {
                    "event_id": int(action.eventId),
                    "error": traceback.format_exc(),
                }
            )

    candidates_by_shader = {}
    for draw in report["draws"]:
        candidates_by_shader.setdefault(draw["pixel_shader_id"], []).append(draw)
    candidate_groups = [
        sorted(draws, key=lambda value: value["event_id"])
        for draws in candidates_by_shader.values()
        if sorted(value["indices"] for value in draws)
        == [17556, 17556, 40896]
    ]
    if len(candidate_groups) != 1:
        raise RuntimeError(
            "expected one pixel-shader group with canonical "
            "17,556/40,896/17,556 draws; found {0}".format(
                len(candidate_groups)
            )
        )

    correlated_draws = candidate_groups[0]
    tree002_large = next(
        value for value in correlated_draws if value["indices"] == 40896
    )
    small_draws = [
        value for value in correlated_draws if value["indices"] == 17556
    ]

    def c5_upload(draw):
        blocks = draw["pixel_constant_blocks"]
        if len(blocks) <= 2 or "upload" not in blocks[2]:
            raise RuntimeError(
                "event {0} has no RenderDoc pixel block 2/c5 upload".format(
                    draw["event_id"]
                )
            )
        return blocks[2]["upload"]

    tree002_upload = c5_upload(tree002_large)
    tree002_matches = [
        value
        for value in small_draws
        if c5_upload(value)["sha256"] == tree002_upload["sha256"]
    ]
    if len(tree002_matches) != 1:
        raise RuntimeError(
            "the 40,896-index tree002 draw did not have exactly one "
            "17,556-index draw with the same c5 upload"
        )
    tree002_small = tree002_matches[0]
    tree001 = next(
        value for value in small_draws if value is not tree002_small
    )

    def light_color(draw):
        floats = c5_upload(draw)["float32"]
        if len(floats) < 7:
            raise RuntimeError(
                "event {0} c5 upload does not contain data[4..6]".format(
                    draw["event_id"]
                )
            )
        return floats[4:7]

    report["correlation"] = {
        "pixel_shader_id": tree002_large["pixel_shader_id"],
        "upload_location": {
            "generated_shader_block": "c5",
            "descriptor_set": 0,
            "binding": 3,
            "renderdoc_pixel_constant_block_index": 2,
            "float_range": "data[4..6]",
        },
        "tree002_newsha": {
            "events": [
                tree002_small["event_id"],
                tree002_large["event_id"],
            ],
            "index_counts": [
                tree002_small["indices"],
                tree002_large["indices"],
            ],
            "c5_sha256": tree002_upload["sha256"],
            "light_color_float32": light_color(tree002_large),
        },
        "tree001_newsha1": {
            "events": [tree001["event_id"]],
            "index_counts": [tree001["indices"]],
            "c5_sha256": c5_upload(tree001)["sha256"],
            "light_color_float32": light_color(tree001),
        },
        "proof": (
            "The two tree002 canonical groups are the only 17,556/40,896 "
            "pair sharing one material. Their captured c5 hashes match; "
            "the remaining 17,556 group is tree001."
        ),
    }

    if disassembly_path and disassembled_shader is not None:
        with open(disassembly_path, "w", encoding="utf-8") as output:
            output.write(disassembled_shader)
        report["shader_disassembly"]["path"] = os.path.abspath(
            disassembly_path
        )

except Exception:
    report["errors"].append({"phase": "capture", "error": traceback.format_exc()})

finally:
    if controller is not None:
        controller.Shutdown()
    if capture_file is not None:
        capture_file.Shutdown()
    with open(output_path, "w", encoding="utf-8") as output:
        json.dump(report, output, indent=2)

raise SystemExit(0 if not report["errors"] else 1)
