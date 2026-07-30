"""Recover orthographic projection matrices from captured Vulkan buffers.

LGPE's projected-shadow fragment programs multiply world position by a
column-major matrix.  For a directional-light orthographic projection its
fourth row is [0, 0, 0, 1].  This scanner searches captured constant-capable
buffers for that exact structural contract and reports the source bytes.

Run with qrenderdoc's ``--python`` option.
"""

import hashlib
import json
import math
import os
import re
import struct
import traceback

import renderdoc as rd


capture_path = os.environ["ROUTE1_RDOC_CAPTURE"]
output_path = os.environ["ROUTE1_RDOC_ORTHOGRAPHIC_MATRIX_REPORT"]
maximum_buffer_bytes = int(
    os.environ.get("ROUTE1_RDOC_MAX_BUFFER_BYTES", str(128 * 1024 * 1024))
)
maximum_axis_scale = float(
    os.environ.get("ROUTE1_RDOC_ORTHO_MAX_AXIS_SCALE", "0.02")
)
minimum_axis_scale = float(
    os.environ.get("ROUTE1_RDOC_ORTHO_MIN_AXIS_SCALE", "0.000001")
)
maximum_normalized_dot = float(
    os.environ.get("ROUTE1_RDOC_ORTHO_MAX_NORMALIZED_DOT", "0.02")
)
maximum_xy_scale_ratio = float(
    os.environ.get("ROUTE1_RDOC_ORTHO_MAX_XY_SCALE_RATIO", "4.0")
)


def length3(row):
    return math.sqrt(sum(value * value for value in row))


def normalized_dot(a, b, length_a, length_b):
    return sum(x * y for x, y in zip(a, b)) / (length_a * length_b)


def matrix_candidate(values):
    if not all(math.isfinite(value) for value in values):
        return None
    if abs(values[15]) < 1.0e-12:
        return None
    normalized = tuple(value / values[15] for value in values)
    if (
        abs(normalized[3]) > 1.0e-7
        or abs(normalized[7]) > 1.0e-7
        or abs(normalized[11]) > 1.0e-7
        or abs(normalized[15] - 1.0) > 1.0e-6
    ):
        return None

    rows = [
        (normalized[0], normalized[4], normalized[8]),
        (normalized[1], normalized[5], normalized[9]),
        (normalized[2], normalized[6], normalized[10]),
    ]
    lengths = [length3(row) for row in rows]
    if any(length < minimum_axis_scale for length in lengths):
        return None
    if any(length > maximum_axis_scale for length in lengths):
        return None

    dots = [
        normalized_dot(rows[0], rows[1], lengths[0], lengths[1]),
        normalized_dot(rows[0], rows[2], lengths[0], lengths[2]),
        normalized_dot(rows[1], rows[2], lengths[1], lengths[2]),
    ]
    if any(abs(value) > maximum_normalized_dot for value in dots):
        return None

    xy_scale_ratio = max(lengths[0], lengths[1]) / min(
        lengths[0], lengths[1]
    )
    if xy_scale_ratio > maximum_xy_scale_ratio:
        return None

    return {
        "row_axis_lengths": lengths,
        "normalized_row_dots": dots,
        "xy_scale_ratio": xy_scale_ratio,
        "homogeneous_scale": values[15],
        "normalized_values": list(normalized),
        "translation": [
            normalized[12],
            normalized[13],
            normalized[14],
        ],
    }


report = {
    "capture": os.path.abspath(capture_path),
    "renderdoc_version": rd.GetVersionString(),
    "contract": {
        "layout": "column_major",
        "fourth_row": [0.0, 0.0, 0.0, 1.0],
        "minimum_axis_scale": minimum_axis_scale,
        "maximum_axis_scale": maximum_axis_scale,
        "maximum_normalized_dot": maximum_normalized_dot,
        "maximum_xy_scale_ratio": maximum_xy_scale_ratio,
    },
    "buffers_scanned": [],
    "candidates": [],
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

    resources = {
        int(resource.resourceId): resource
        for resource in controller.GetResources()
    }
    zero = b"\x00\x00\x00\x00"
    homogeneous_row_pattern = re.compile(
        b"(?=(" + zero + b".{12}" + zero + b".{12}" + zero + b"))",
        re.DOTALL,
    )

    for buffer in controller.GetBuffers():
        flags = str(buffer.creationFlags)
        if "Constants" not in flags:
            continue
        if buffer.length < 64 or buffer.length > maximum_buffer_bytes:
            continue

        resource_id = int(buffer.resourceId)
        raw = bytes(
            controller.GetBufferData(
                buffer.resourceId,
                0,
                buffer.length,
            )
        )
        buffer_row = {
            "resource_id": resource_id,
            "name": getattr(resources.get(resource_id), "name", ""),
            "length": int(buffer.length),
            "candidate_count": 0,
        }
        report["buffers_scanned"].append(buffer_row)

        for match in homogeneous_row_pattern.finditer(raw):
            zero_offset = match.start()
            matrix_offset = zero_offset - 12
            if matrix_offset < 0 or matrix_offset % 16 != 0:
                continue
            if matrix_offset + 64 > len(raw):
                continue
            if (
                raw[matrix_offset + 12 : matrix_offset + 16]
                != zero
                or raw[matrix_offset + 28 : matrix_offset + 32]
                != zero
                or raw[matrix_offset + 44 : matrix_offset + 48]
                != zero
            ):
                continue

            values = struct.unpack_from("<16f", raw, matrix_offset)
            candidate = matrix_candidate(values)
            if candidate is None:
                continue

            matrix_bytes = raw[matrix_offset : matrix_offset + 64]
            report["candidates"].append(
                {
                    "resource_id": resource_id,
                    "resource_name": buffer_row["name"],
                    "buffer_length": int(buffer.length),
                    "byte_offset": matrix_offset,
                    "float_offset": matrix_offset // 4,
                    "sha256": hashlib.sha256(matrix_bytes).hexdigest(),
                    "values": list(values),
                    **candidate,
                }
            )
            buffer_row["candidate_count"] += 1

    report["candidates"].sort(
        key=lambda row: (
            abs(row["xy_scale_ratio"] - 1.0),
            max(abs(value) for value in row["normalized_row_dots"]),
            row["resource_id"],
            row["byte_offset"],
        )
    )

except Exception:
    report["errors"].append({"error": traceback.format_exc()})

finally:
    if controller is not None:
        controller.Shutdown()
    if capture_file is not None:
        capture_file.Shutdown()
    with open(output_path, "w", encoding="utf-8") as output:
        json.dump(report, output, indent=2)

raise SystemExit(0 if not report["errors"] else 1)
