"""Exercise the model plugin's CPU-only pose query against a private cooked model."""

import argparse
import ctypes as c
import json
import math
import os
from pathlib import Path


class Request(c.Structure):
    _fields_ = [(name, c.c_char_p) for name in ("assetId", "phloPath", "animation", "nodeName")] + [
        ("animationTime", c.c_float), ("position", c.c_float * 3),
        ("yawRadians", c.c_float), ("scale", c.c_float)]


class Result(c.Structure):
    _fields_ = [("worldPosition", c.c_float * 3)]


class Api(c.Structure):
    _fields_ = [("abi", c.c_uint32), ("size", c.c_uint32)] + [
        (name, c.c_void_p) for name in ("context", "render", "lastError", "destroy", "sampleNodePose")]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plugin", type=Path, required=True)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--clip", required=True)
    parser.add_argument("--node", required=True)
    parser.add_argument("--dll-directory", type=Path, action="append", default=[])
    args = parser.parse_args()
    directories = [os.add_dll_directory(str(p.resolve()))
                   for p in [args.plugin.parent, *args.dll_directory]]
    plugin = c.CDLL(str(args.plugin.resolve()))
    create = plugin.phlosionModelRenderPluginCreate
    create.argtypes, create.restype = [c.c_uint32, c.POINTER(Api)], c.c_bool
    api = Api()
    assert not create(1, c.byref(api)), "Old ABI must be rejected"
    assert create(2, c.byref(api)), "Current ABI must load"
    assert api.abi == 2 and api.size == c.sizeof(Api) and api.sampleNodePose
    query = c.CFUNCTYPE(c.c_bool, c.c_void_p, c.POINTER(Request), c.POINTER(Result))(api.sampleNodePose)
    last_error = c.CFUNCTYPE(c.c_char_p, c.c_void_p)(api.lastError)
    destroy = c.CFUNCTYPE(None, c.c_void_p)(api.destroy)
    request = Request(b"pose-contract", os.fsencode(args.model.resolve()), args.clip.encode(),
                      args.node.encode(), 0., (c.c_float * 3)(0., 0., 0.), 0., 1.)

    def sample():
        result = Result()
        assert query(api.context, c.byref(request), c.byref(result)), last_error(api.context)
        values = list(result.worldPosition)
        assert all(math.isfinite(v) for v in values)
        return values

    def fails():
        result = Result((c.c_float * 3)(17., 23., 42.))
        assert not query(api.context, c.byref(request), c.byref(result))
        assert list(result.worldPosition) == [17., 23., 42.], "Failure changed result"
        assert last_error(api.context)

    try:
        first = sample()  # No renderer or render call has occurred.
        request.animationTime = .4
        animated = sample()
        request.animationTime = 0.
        assert sample() == first, "An arbitrary-time query changed subsequent samples"
        assert max(abs(a - b) for a, b in zip(first, animated)) > 1e-5, "Selected node did not animate"
        request.position[:] = [3., 0., -2.]
        translated = sample()
        assert max(abs(v - expected) for v, expected in zip(
            translated, [first[0] + 3, first[1], first[2] - 2])) < 2e-5
        request.position[:] = [0., 0., 0.]
        request.yawRadians = math.pi / 2
        rotated = sample()
        assert max(abs(v - expected) for v, expected in zip(
            rotated, [first[2], first[1], -first[0]])) < 2e-5
        request.yawRadians = 0.
        request.scale = 1.4
        scaled = sample()
        assert abs(scaled[0] - 1.4 * first[0]) < 2e-5
        assert abs(scaled[2] - 1.4 * first[2]) < 2e-5
        request.nodeName = b"__missing_node__"
        fails()
        request.nodeName = args.node.encode()
        request.animation = b"__missing_clip__"
        fails()
        request.animation = args.clip.encode()
        for value in (-1., math.nan, math.inf):
            request.animationTime = value
            fails()
        request.animationTime = 0.
        request.scale = math.nan
        fails()
        request.scale = 1.
        request.position[1] = math.inf
        fails()
        result = Result()
        assert not query(api.context, None, c.byref(result))
        assert not query(api.context, c.byref(request), None)
        print(json.dumps({"passed": True, "node": args.node, "initial": first,
                          "animated": animated, "scale_1_4": scaled}, indent=2))
    finally:
        destroy(api.context)
        for directory in directories:
            directory.close()


if __name__ == "__main__":
    main()
