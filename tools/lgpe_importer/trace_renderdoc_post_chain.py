"""Inventory fullscreen draws in an LGPE RenderDoc capture.

Run this with qrenderdoc's ``--python`` option.  The script deliberately
records bindings and shader disassembly instead of guessing which draw is
fog, gamma correction, or another post stage.
"""

import hashlib
import json
import os
import traceback

import renderdoc as rd


capture_path = os.environ["LGPE_RDOC_CAPTURE"]
output_path = os.environ["LGPE_RDOC_POST_CHAIN"]
maximum_indices = int(os.environ.get("LGPE_RDOC_POST_MAX_INDICES", "6"))

report = {
    "capture": os.path.abspath(capture_path),
    "renderdoc_version": rd.GetVersionString(),
    "maximum_indices": maximum_indices,
    "draws": [],
    "errors": [],
}


def walk(actions):
    for action in actions:
        yield action
        if action.children:
            yield from walk(action.children)


def resource_row(resource):
    return {
        "resource_id": int(getattr(resource, "resourceId", rd.ResourceId.Null())),
        "first_mip": int(getattr(resource, "firstMip", 0)),
        "first_slice": int(getattr(resource, "firstSlice", 0)),
        "type": str(getattr(resource, "type", "")),
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
    texture_dimensions = {
        int(texture.resourceId): {
            "width": int(texture.width),
            "height": int(texture.height),
            "depth": int(texture.depth),
            "arraysize": int(texture.arraysize),
            "mips": int(texture.mips),
            "format": str(texture.format),
        }
        for texture in controller.GetTextures()
    }

    for action in walk(controller.GetRootActions()):
        if not (action.flags & rd.ActionFlags.Drawcall):
            continue
        if action.numIndices > maximum_indices:
            continue

        try:
            controller.SetFrameEvent(action.eventId, False)
            pipe = controller.GetPipelineState()
            pixel_shader = pipe.GetShader(rd.ShaderStage.Pixel)
            reflection = pipe.GetShaderReflection(rd.ShaderStage.Pixel)
            if reflection is None or pixel_shader == rd.ResourceId.Null():
                continue

            entry = pipe.GetShaderEntryPoint(rd.ShaderStage.Pixel)
            disassembly = controller.DisassembleShader(
                pipe.GetGraphicsPipelineObject(),
                reflection,
                "",
            )
            outputs = [resource_row(value) for value in pipe.GetOutputTargets()]
            depth = resource_row(pipe.GetDepthTarget())

            for output in outputs:
                output.update(
                    texture_dimensions.get(output["resource_id"], {})
                )
            depth.update(texture_dimensions.get(depth["resource_id"], {}))

            constant_blocks = []
            for index, block in enumerate(
                pipe.GetConstantBlocks(rd.ShaderStage.Pixel)
            ):
                row = {
                    "index": index,
                    "name": getattr(block, "name", ""),
                    "byte_size": getattr(block, "byteSize", None),
                    "bind": str(getattr(block, "bindPoint", "")),
                }
                try:
                    bound = pipe.GetConstantBlock(
                        rd.ShaderStage.Pixel,
                        index,
                        0,
                    )
                    row.update(
                        {
                            "resource_id": int(bound.resourceId),
                            "byte_offset": int(bound.byteOffset),
                            "byte_size_bound": int(bound.byteSize),
                        }
                    )
                except Exception:
                    row["binding_error"] = traceback.format_exc()
                constant_blocks.append(row)

            read_only = []
            for resource in pipe.GetReadOnlyResources(rd.ShaderStage.Pixel):
                row = resource_row(resource)
                row.update(texture_dimensions.get(row["resource_id"], {}))
                read_only.append(row)

            report["draws"].append(
                {
                    "event_id": int(action.eventId),
                    "name": action.GetName(structured),
                    "indices": int(action.numIndices),
                    "instances": int(action.numInstances),
                    "pipeline_id": int(pipe.GetGraphicsPipelineObject()),
                    "pixel_shader_id": int(pixel_shader),
                    "pixel_entry": getattr(entry, "name", ""),
                    "shader_sha256": hashlib.sha256(
                        disassembly.encode("utf-8")
                    ).hexdigest(),
                    "shader_disassembly": disassembly,
                    "outputs": outputs,
                    "depth": depth,
                    "constant_blocks": constant_blocks,
                    "read_only_resources": read_only,
                }
            )
        except Exception:
            report["errors"].append(
                {
                    "event_id": int(action.eventId),
                    "error": traceback.format_exc(),
                }
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
