"""Shared logical tiles and Blender/source coordinate conventions."""
import math
import struct
FIELDS = ('x', 'z', 'height', 'surface', 'ramp')
TILE_SIZE_CM = 100
ELEVATION_STEP_CM = 50


def height_cm(cell, x, z):
    u = max(0, min(1, x-cell['x']))
    v = max(0, min(1, z-cell['z']))
    return ELEVATION_STEP_CM * (cell['height'] + (0, 1-v, u, v, 1-u)[cell['ramp']])


def blender_height(cell, x, y):
    return height_cm(cell, x, -y) / TILE_SIZE_CM


def source_translation(location):
    return [location[0]*100, location[2]*100, -location[1]*100]


def source_direction_float64(value):
    """Normalize with double intermediates, then serialize runtime float32.

    Blender's float32 SIMD normalization can differ by one ULP for equivalent
    tangent magnitudes. This avoids spurious archive revisions across exports.
    """
    direction = (float(value[0]), float(value[2]), -float(value[1]))
    length = math.hypot(*direction)
    if not math.isfinite(length) or length <= 1e-6:
        raise ValueError('Patch contains a non-finite or zero-length direction')
    return [struct.unpack('<f', struct.pack('<f', component/length))[0] for component in direction]
