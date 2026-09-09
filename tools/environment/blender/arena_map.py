"""Export explicit arena data without interpreting rendered meshes or colours.

This module has no Blender dependency. Connections describe elevation changes;
the simulation will decide traversal and visibility rules in a later change.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path

KIND = 'pokemon_autochess_arena_map'
FIELDS = ('x', 'z', 'height', 'surface', 'ramp')


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def height_cm(cell, x, z):
    u, v = x - cell['x'], z - cell['z']
    return 50 * (cell['height'] + (0, 1-v, u, v, 1-u)[cell['ramp']])


def normalized_cells(rows):
    cells = []
    occupied = set()
    for row in rows:
        if any(type(row.get(key)) is not int for key in FIELDS):
            raise ValueError('Tile coordinates, height, surface and ramp must be integers')
        cell = {key: row[key] for key in FIELDS}
        if not (0 <= cell['height'] <= 8 and 0 <= cell['surface'] <= 2 and 0 <= cell['ramp'] <= 4):
            raise ValueError('Tile attributes are outside the authoring range')
        key = cell['x'], cell['z']
        if key in occupied: raise ValueError(f'Duplicate tile {key}')
        occupied.add(key)
        cells.append(cell)
    if not cells: raise ValueError('The tile blueprint is empty')
    return sorted(cells, key=lambda c: (c['z'], c['x']))


def build_map(rows, scene, board, composition):
    cells = normalized_cells(rows)
    lookup = {(c['x'], c['z']): c for c in cells}
    registration = board['board_registration']
    if registration['terrain_tile_size_cm'] != 100:
        raise ValueError('The arena authoring contract uses 100 cm tiles')
    ox, oz = registration['terrain_grid_origin']
    cols, rows = registration['board_cells']
    playable = [(x, z) for z in range(oz, oz+rows) for x in range(ox, ox+cols)]
    reserve = []
    slots, gap = registration['bench_slots'], registration['bench_gap_cells']
    bx = ox + (cols-slots)//2
    for side in registration['bench_sides']:
        if side not in ('north', 'south'): raise ValueError(f'Unknown bench side {side}')
        z = oz-1-gap if side == 'north' else oz+rows+gap
        reserve.extend((x, z) for x in range(bx, bx+slots))
    if any(cell not in lookup for cell in playable+reserve):
        raise ValueError('The tile blueprint does not cover every board and reserve cell')

    connections = []
    playable_set = set(playable)
    for x, z in playable:
        for dx, dz in ((0, -1), (1, 0), (0, 1), (-1, 0)):
            destination = x+dx, z+dz
            if destination not in playable_set: continue
            if dx:
                edge = [(x+(dx > 0), z), (x+(dx > 0), z+1)]
            else:
                edge = [(x, z+(dz > 0)), (x+1, z+(dz > 0))]
            delta = [height_cm(lookup[destination], *p) - height_cm(lookup[(x, z)], *p) for p in edge]
            connections.append({'from': [x, z], 'to': list(destination), 'edge_height_delta_cm': delta})

    records = {f"encounter-grass/{r['model']}/record-{r['record_index']}": r
               for r in composition['encounter_grass']['records']}
    cover = []
    ids = set()
    for node in scene['nodes']:
        if node['id'] in ids: raise ValueError('Scene node IDs must be unique')
        ids.add(node['id'])
        prefab = node.get('components', {}).get('prefab_instance')
        if not node['enabled'] or not prefab or not prefab['prefab_asset_id'].startswith('route1/encounter_grass_'):
            continue
        record = records.get(prefab['prototype_node_id'])
        if not record: raise ValueError(f"Missing encounter footprint for {node['id']}")
        transform = node['components']['transform']
        pitch, yaw, roll = transform['rotation_degrees']
        if abs(pitch) > .001 or abs(roll) > .001: raise ValueError('Cover regions require upright props')
        tx, _, tz = transform['translation']
        sx, _, sz = transform['scale']
        angle = math.radians(yaw)
        def point(x, z):
            x, z = x*sx, z*sz
            return [round(tx+math.cos(angle)*x+math.sin(angle)*z, 6),
                    round(tz-math.sin(angle)*x+math.cos(angle)*z, 6)]
        polygons = [[point(cx*100+dx, cz*100+dz) for dx, dz in ((-50,-50),(50,-50),(50,50),(-50,50))]
                    for cx, cz in record['core_cells_source_xz']]
        cover.append({'id': node['id'], 'node_id': node['id'], 'footprint_policy': 'published_core_cells',
                      'polygons_source_xz_cm': polygons})
    result = {'kind': KIND, 'schema_version': 1, 'scene_id': scene['scene_id'],
              'coordinate_system': 'source_centimetres_xyz_y_up',
              'scene_content_sha256': digest(scene), 'board_content_sha256': digest(board),
              'tile_size_cm': 100, 'elevation_step_cm': 50,
              'cells': cells, 'playable_cells': [list(p) for p in playable],
              'reserve_cells': [list(p) for p in reserve], 'connections': connections,
              'cover_regions': sorted(cover, key=lambda r: r['id'])}
    # Reject non-finite transforms instead of serializing non-standard JSON.
    json.dumps(result, allow_nan=False)
    return result


def validate_map(document, scene, board, composition):
    if document.get('kind') != KIND or document.get('schema_version') != 1:
        raise ValueError('Unsupported authored arena map')
    if document != build_map(document['cells'], scene, board, composition):
        raise ValueError('Arena map differs from its scene, board, or derived cell connections')
    return {'passed': True, 'cells': len(document['cells']),
            'playable_cells': len(document['playable_cells']), 'reserve_cells': len(document['reserve_cells']),
            'cover_regions': len(document['cover_regions']), 'directed_connections': len(document['connections'])}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--map', type=Path, required=True)
    parser.add_argument('--scene', type=Path, required=True)
    parser.add_argument('--board', type=Path, required=True)
    parser.add_argument('--composition', type=Path, required=True)
    parser.add_argument('--tiles', type=Path, help='Export a new map from a saved tile-layout JSON')
    args = parser.parse_args()
    read = lambda p: json.loads(p.read_text(encoding='utf-8-sig'))
    scene, board, composition = read(args.scene), read(args.board), read(args.composition)
    if args.tiles:
        document = build_map(read(args.tiles)['cells'], scene, board, composition)
        args.map.write_text(json.dumps(document, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(validate_map(read(args.map), scene, board, composition)))


if __name__ == '__main__': main()
