"""Shared logical tiles and Blender/source coordinate conventions."""
import math
import struct
FIELDS = ('x', 'z', 'height', 'surface', 'ramp')
TILE_SIZE_CM = 100
ELEVATION_STEP_CM = 50
MAX_RAMP = 12


def height_cm(cell, x, z):
    u = max(0, min(1, x-cell['x']))
    v = max(0, min(1, z-cell['z']))
    # Corner feet have one high corner; crests have three. Pairing them
    # produces the diagonal slope band used where an LGPE ramp turns a corner.
    rise = (0, 1-v, u, v, 1-u,
            max(0, u-v), min(1, 1+u-v),       # northeast: foot, crest
            max(0, u+v-1), min(1, u+v),       # southeast
            max(0, v-u), min(1, 1+v-u),       # southwest
            max(0, 1-u-v), min(1, 2-u-v))     # northwest
    return ELEVATION_STEP_CM * (cell['height'] + rise[cell['ramp']])


def blender_height(cell, x, y):
    return height_cm(cell, x, -y) / TILE_SIZE_CM


def surface_polygons(cell, points):
    """Split Blender XY caps at a corner ramp's diagonal change of slope.

    Unsplit non-planar quads triangulate differently in Blender and the game,
    leaving the visible floor inconsistent with the logical height sampler.
    Existing flat/cardinal geometry passes through verbatim.
    """
    if cell['ramp'] < 5: return [points]
    x, y = cell['x'], -cell['z']-1
    if cell['ramp'] in (5, 6, 9, 10):
        distance = lambda p: p[0]+p[1]-x-y-1
    else:
        distance = lambda p: p[0]-p[1]-x+y
    if not any(distance(p) > 1e-8 for p in points) or not any(distance(p) < -1e-8 for p in points):
        return [points]
    result = []
    for sign in (-1, 1):
        polygon = []
        for a, b in zip(points, points[1:]+points[:1]):
            da, db = sign*distance(a), sign*distance(b)
            if da >= 0: polygon.append(tuple(a))
            if (da >= 0) != (db >= 0):
                t = da/(da-db)
                polygon.append(tuple(a[i]+t*(b[i]-a[i]) for i in (0, 1)))
        clean = []
        for p in polygon:
            if not clean or math.dist(p, clean[-1]) > 1e-8: clean.append(p)
        if len(clean) > 1 and math.dist(clean[0], clean[-1]) < 1e-8: clean.pop()
        if len(clean) >= 3: result.append(clean)
    return result


def source_translation(location):
    return [location[0]*100, location[2]*100, -location[1]*100]


def source_direction_float64(value, stabilize=False):
    """Normalize with double intermediates, then serialize runtime float32.

    Blender's float32 SIMD normalization can differ by one ULP for equivalent
    tangent magnitudes. This avoids spurious archive revisions across exports.
    """
    direction = (float(value[0]), float(value[2]), -float(value[1]))
    length = math.hypot(*direction)
    if not math.isfinite(length) or length <= 1e-6:
        raise ValueError('Patch contains a non-finite or zero-length direction')
    if stabilize:
        # MikkTSpace can also vary the direction itself by one float32 ULP.
        # Snap unit lighting vectors before renormalizing; positions, UVs and
        # legacy source exports retain their full existing precision.
        direction = tuple(round(component/length, 5) for component in direction)
        length = math.hypot(*direction)
    return [struct.unpack('<f', struct.pack('<f', component/length))[0] for component in direction]
