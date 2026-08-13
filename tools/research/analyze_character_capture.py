"""Export draw-level evidence from a protected source character capture.

Run this inside RenderDoc's bundled Python environment. The wrapper or caller
must set CHARACTER_CAPTURE_RDC, CHARACTER_CAPTURE_REPORT, and
CHARACTER_CAPTURE_STATE_ID. CHARACTER_CAPTURE_EVENT_IDS is optional; when it
is empty every draw is inventoried, and when supplied the listed events also
receive detailed pipeline/binding evidence and shader disassembly.
"""

import hashlib
import json
import os
import re
import traceback

import renderdoc as rd


capture_path = os.environ["CHARACTER_CAPTURE_RDC"]
report_path = os.environ["CHARACTER_CAPTURE_REPORT"]
state_id = os.environ["CHARACTER_CAPTURE_STATE_ID"]
event_ids = {
    int(value)
    for value in os.environ.get("CHARACTER_CAPTURE_EVENT_IDS", "").split(",")
    if value.strip()
}
output_directory = os.path.dirname(os.path.abspath(report_path))
shader_directory = os.path.join(output_directory, "shaders")
os.makedirs(shader_directory, exist_ok=True)


def resource_id(value):
    try:
        return int(value)
    except Exception:
        return str(value)


def safe_name(value):
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", value)


def walk(actions):
    for action in actions:
        yield action
        if action.children:
            yield from walk(action.children)


def hash_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as source:
        while True:
            block = source.read(8 * 1024 * 1024)
            if not block:
                break
            digest.update(block)
    return digest.hexdigest()


def result_payload(result):
    ok_method = getattr(result, "OK", None)
    return {
        "text": str(result),
        "ok": bool(ok_method()) if callable(ok_method) else False,
        "code": str(getattr(result, "code", "")),
        "message": str(getattr(result, "message", "")),
    }


def shader_evidence(controller, pipeline, stage, event_id, errors):
    shader_id = pipeline.GetShader(stage)
    if int(shader_id) == 0:
        return None
    reflection = pipeline.GetShaderReflection(stage)
    if reflection is None:
        return {
            "stage": str(stage),
            "resource_id": resource_id(shader_id),
            "reflection": None,
        }
    try:
        pipeline_id = pipeline.GetGraphicsPipelineObject()
        disassembly = controller.DisassembleShader(pipeline_id, reflection, "")
        stage_name = str(stage).replace("ShaderStage.", "")
        filename = "event_{0}_{1}.spvasm".format(
            event_id, safe_name(stage_name)
        )
        output_path = os.path.join(shader_directory, filename)
        with open(output_path, "w", encoding="utf-8") as output:
            output.write(disassembly)
        return {
            "stage": stage_name,
            "resource_id": resource_id(shader_id),
            "file": os.path.relpath(output_path, output_directory).replace("\\", "/"),
            "sha256": hashlib.sha256(disassembly.encode("utf-8")).hexdigest(),
            "characters": len(disassembly),
            "input_count": len(reflection.inputSignature),
            "output_count": len(reflection.outputSignature),
            "constant_block_count": len(reflection.constantBlocks),
            "sampler_count": len(reflection.samplers),
            "readonly_resource_count": len(reflection.readOnlyResources),
            "readwrite_resource_count": len(reflection.readWriteResources),
        }
    except Exception:
        errors.append(
            {
                "event_id": event_id,
                "phase": "shader_disassembly",
                "stage": str(stage),
                "error": traceback.format_exc(),
            }
        )
        return {
            "stage": str(stage),
            "resource_id": resource_id(shader_id),
            "error": "shader_disassembly_failed",
        }


def constant_blocks(pipeline, stage, errors, event_id):
    rows = []
    for index, block in enumerate(pipeline.GetConstantBlocks(stage)):
        row = {
            "stage": str(stage),
            "index": index,
            "name": getattr(block, "name", ""),
            "byte_size": getattr(block, "byteSize", None),
            "bind": str(getattr(block, "bindPoint", "")),
        }
        try:
            bound = pipeline.GetConstantBlock(stage, index, 0)
            row.update(
                {
                    "resource_id": resource_id(bound.resourceId),
                    "byte_offset": bound.byteOffset,
                    "byte_size_bound": bound.byteSize,
                }
            )
        except Exception:
            row["binding_error"] = traceback.format_exc()
            errors.append(
                {
                    "event_id": event_id,
                    "phase": "constant_block_binding",
                    "stage": str(stage),
                    "index": index,
                    "error": row["binding_error"],
                }
            )
        rows.append(row)
    return rows


report = {
    "schema": "pokemon-autochess-character-capture-analysis-v1",
    "state_id": state_id,
    "capture_path": os.path.abspath(capture_path),
    "capture_sha256": None,
    "renderdoc_version": rd.GetVersionString(),
    "requested_event_ids": sorted(event_ids),
    "open_file": None,
    "open_capture": None,
    "frame": None,
    "draw_inventory": [],
    "selected_draws": [],
    "textures": [],
    "resources": [],
    "errors": [],
}

capture_file = None
controller = None

try:
    report["capture_sha256"] = hash_file(capture_path)
    capture_file = rd.OpenCaptureFile()
    open_status = capture_file.OpenFile(capture_path, "rdc", None)
    report["open_file"] = result_payload(open_status)
    if not report["open_file"]["ok"]:
        raise RuntimeError("RenderDoc could not open the capture")

    replay_status, controller = capture_file.OpenCapture(rd.ReplayOptions(), None)
    report["open_capture"] = result_payload(replay_status)
    if not report["open_capture"]["ok"] or controller is None:
        raise RuntimeError("RenderDoc could not create a replay controller")

    frame = controller.GetFrameInfo()
    report["frame"] = {
        "number": frame.frameNumber,
        "capture_time": frame.captureTime,
        "compressed_bytes": frame.compressedFileSize,
        "uncompressed_bytes": frame.uncompressedFileSize,
        "api": str(controller.GetAPIProperties().pipelineType),
    }
    structured = controller.GetStructuredFile()

    actions_by_id = {}
    for action in walk(controller.GetRootActions()):
        if not (action.flags & rd.ActionFlags.Drawcall):
            continue
        actions_by_id[action.eventId] = action
        report["draw_inventory"].append(
            {
                "event_id": action.eventId,
                "name": action.GetName(structured),
                "indices": action.numIndices,
                "instances": action.numInstances,
                "base_vertex": action.baseVertex,
                "vertex_offset": action.vertexOffset,
                "index_offset": action.indexOffset,
                "outputs": [resource_id(value) for value in action.outputs],
                "depth_output": resource_id(action.depthOut),
            }
        )

    for event_id in sorted(event_ids):
        action = actions_by_id.get(event_id)
        if action is None:
            report["errors"].append(
                {
                    "event_id": event_id,
                    "phase": "event_selection",
                    "error": "requested event is not a draw call",
                }
            )
            continue
        controller.SetFrameEvent(event_id, False)
        pipeline = controller.GetPipelineState()
        draw = {
            "event_id": event_id,
            "name": action.GetName(structured),
            "indices": action.numIndices,
            "instances": action.numInstances,
            "pipeline_id": resource_id(pipeline.GetGraphicsPipelineObject()),
            "output_targets": [
                resource_id(target.resourceId)
                for target in pipeline.GetOutputTargets()
            ],
            "depth_target": resource_id(pipeline.GetDepthTarget().resourceId),
            "shaders": [],
            "constant_blocks": [],
            "vertex_buffers": [],
        }
        for stage in (rd.ShaderStage.Vertex, rd.ShaderStage.Fragment):
            shader = shader_evidence(
                controller, pipeline, stage, event_id, report["errors"]
            )
            if shader is not None:
                draw["shaders"].append(shader)
            draw["constant_blocks"].extend(
                constant_blocks(pipeline, stage, report["errors"], event_id)
            )
        for index, bound in enumerate(pipeline.GetVBuffers()):
            draw["vertex_buffers"].append(
                {
                    "index": index,
                    "resource_id": resource_id(bound.resourceId),
                    "byte_offset": bound.byteOffset,
                    "byte_size": bound.byteSize,
                    "byte_stride": bound.byteStride,
                }
            )
        report["selected_draws"].append(draw)

    for texture in controller.GetTextures():
        report["textures"].append(
            {
                "resource_id": resource_id(texture.resourceId),
                "width": texture.width,
                "height": texture.height,
                "depth": texture.depth,
                "array_size": texture.arraysize,
                "mips": texture.mips,
                "samples": texture.msSamp,
                "byte_size": texture.byteSize,
                "type": str(texture.type),
                "format": str(texture.format),
                "creation_flags": str(texture.creationFlags),
            }
        )
    for resource in controller.GetResources():
        report["resources"].append(
            {
                "resource_id": resource_id(resource.resourceId),
                "name": resource.name,
                "type": str(resource.type),
                "autogenerated_name": bool(resource.autogeneratedName),
                "derived_resources": [
                    resource_id(value) for value in resource.derivedResources
                ],
                "parent_resources": [
                    resource_id(value) for value in resource.parentResources
                ],
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
    os.makedirs(output_directory, exist_ok=True)
    with open(report_path, "w", encoding="utf-8") as output:
        json.dump(report, output, indent=2)

raise SystemExit(0 if not report["errors"] else 1)
