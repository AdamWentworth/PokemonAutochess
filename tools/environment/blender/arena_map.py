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
from arena_coordinates import FIELDS, height_cm


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def encounter_grass_centers(core_cells):
    """Mirror game/arena/EncounterGrassFootprint.h in prefab-local centimetres."""
    core = {tuple(cell) for cell in core_cells}
    if not core: raise ValueError('Encounter-grass footprint is empty')
    expanded = {(x+dx, z+dz) for x, z in core for dx in (-1, 0, 1) for dz in (-1, 0, 1)}
    centers = []
    for x, z in sorted(expanded):
        if (x, z) not in core:
            nx, nz = min(core, key=lambda p: ((p[0]-x)**2+(p[1]-z)**2, p[0], p[1]))
            x, z = (x+nx)*.5, (z+nz)*.5
        centers.append(((x+.5)*100, (z+.5)*100))
    return centers


def normalized_cells(rows):
    if not isinstance(rows, list) or not 0 < len(rows) <= 65536:
        raise ValueError('The tile blueprint must contain 1 to 65536 cells')
    cells = []
    occupied = set()
    for row in rows:
        if any(type(row.get(key)) is not int for key in FIELDS):
            raise ValueError('Tile coordinates, height, surface and ramp must be integers')
        cell = {key: row[key] for key in FIELDS}
        if not (0 <= cell['height'] <= 8 and 0 <= cell['surface'] <= 2 and 0 <= cell['ramp'] <= 4):
            raise ValueError('Tile attributes are outside the authoring range')
        key = cell['x'], cell['z']
        if any(abs(value) > 100000 for value in key): raise ValueError('Tile coordinates exceed the supported range')
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
    slots, gap = registration['bench_slots'], registration['bench_gap_cells']
    limits = ((ox,-100000,100000),(oz,-100000,100000),(cols,1,256),(rows,1,256),(slots,1,256),(gap,0,64))
    if any(type(v) is not int or not low <= v <= high for v,low,high in limits):
        raise ValueError('Board registration exceeds the supported range')
    if (cols+slots) % 2: raise ValueError('The reserve row must align to whole terrain cells')
    playable = [(x, z) for z in range(oz, oz+rows) for x in range(ox, ox+cols)]
    reserve = []
    bx = ox + (cols-slots)//2
    sides = registration['bench_sides']
    if not sides or len(sides) != len(set(sides)): raise ValueError('Reserve sides must be present and unique')
    for side in registration['bench_sides']:
        if side not in ('north', 'south'): raise ValueError(f'Unknown bench side {side}')
        # Matches northBenchTerrainGridOrigin/southBenchTerrainGridOrigin.
        z = oz+rows+gap if side == 'north' else oz-1-gap
        reserve.extend((x, z) for x in range(bx, bx+slots))
    reserve.sort(key=lambda p: (p[1], p[0]))
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
        if not node['enabled'] or not prefab or not prefab['prototype_node_id'].startswith('encounter-grass/'):
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
        polygons = [[point(cx+dx, cz+dz) for dx, dz in ((-50,-50),(50,-50),(50,50),(-50,50))]
                    for cx, cz in encounter_grass_centers(record['core_cells_source_xz'])]
        cover.append({'id': node['id'], 'node_id': node['id'], 'footprint_policy': 'rendered_clump_footprints',
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
