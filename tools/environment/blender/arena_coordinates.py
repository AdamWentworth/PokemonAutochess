"""Shared logical tiles and Blender/source coordinate conventions."""
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
