"""Bootstrap a separate clearing from the qualified entrance's reference library.

Run once with the entrance blend open in background mode and --output pointing
to a NEW file. Subsequent artist edits use arena_pilot.py, never this seed recipe.
The source blend is only read; this script saves exclusively to --output.
"""
import argparse
from collections import defaultdict
import hashlib
import json
import math
from pathlib import Path
import sys

import bpy
import bmesh
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import arena_pilot as arena
import arena_tiles as tiles

RECIPE = 'config/environment/route1_south_clearing.authoring.json'
BLUEPRINT = 'config/environment/route1_south_clearing_blueprint.json'


def recover_cells(kit, legacy, config):
    source = {(c['x'], c['z']): dict(c) for c in kit['source_terrain_tiles']}
    for node in legacy['nodes']:
        t = node['components'].get('terrain_tile')
        if t:
            source[t['grid_x'], t['grid_z']] = dict(x=t['grid_x'], z=t['grid_z'],
                height=t['elevation_level'], surface=t['surface'], shape=t['shape'], occupied=node['enabled'])
    bounds = config['tile_bounds']
    cells = []
    for z in range(bounds['z_min'], bounds['z_max'] + 1):
        for x in range(bounds['x_min'], bounds['x_max'] + 1):
            raw = source.get((x, z))
            if not raw or not raw['occupied']:
                # The source backdrop has gaps beyond its outermost tree banks.
                # Continue the nearest occupied edge beneath the woodland.
                raw = min((c for c in source.values() if c['occupied']),
                          key=lambda c: ((c['x']-x)**2 + (c['z']-z)**2, c['x'], c['z']))
            ramp = int(any(r['z'] == z and r['x_min'] <= x <= r['x_max'] for r in config['ramp_strips']))
            cells.append(dict(x=x, z=z, height=raw['height'], surface=int(raw['surface'] == 'dirt_path'), ramp=ramp))
    lookup = {(c['x'], c['z']): c for c in cells}
    lo, hi = config['visual_access_paint']['route_corridor_x']
    pending = [tuple(config['visual_access_paint']['seed_cell'])]
    accessible = set(pending)
    while pending:
        x, z = pending.pop()
        y = -z-1
        for dx, dz, a, b in ((0, 1, (x,y), (x+1,y)), (1, 0, (x+1,y), (x+1,y+1)),
                              (0, -1, (x,y+1), (x+1,y+1)), (-1, 0, (x,y), (x,y+1))):
            key = (x+dx, z+dz)
            if key in accessible or key not in lookup or not lo <= key[0] <= hi:
                continue
            if all(abs(tiles.tile_height(lookup[(x,z)], *p) - tiles.tile_height(lookup[key], *p)) < 1e-6 for p in (a,b)):
                accessible.add(key)
                pending.append(key)
    for c in cells:
        if c['surface'] != 1:
            c['surface'] = 0 if (c['x'], c['z']) in accessible else 2
    return cells, source


def components(mesh):
    """Connected pieces, welding UV seam positions for component discovery only."""
    parent = list(range(len(mesh.vertices)))
    positions = {}
    def root(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    def join(a, b):
        parent[root(b)] = root(a)
    for v in mesh.vertices:
        key = tuple(round(q, 4) for q in v.co)
        if key in positions:
            join(v.index, positions[key])
        positions[key] = v.index
    for f in mesh.polygons:
        for v in f.vertices[1:]:
            join(f.vertices[0], v)
    result = defaultdict(list)
    for f in mesh.polygons:
        result[root(f.vertices[0])].append(f.index)
    return list(result.values())


def bounds(mesh, faces):
    indices = {v for i in faces for v in mesh.polygons[i].vertices}
    return [Vector([fn(mesh.vertices[v].co[a] for v in indices) for a in range(3)]) for fn in (min, max)]


def detail(source, faces, name, pivot, location):
    mesh = source.data.copy()
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bm.faces.ensure_lookup_table()
    keep = set(faces)
    bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.index not in keep], context='FACES')
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context='VERTS')
    bm.to_mesh(mesh)
    bm.free()
    for v in mesh.vertices:
        v.co -= pivot
    obj = bpy.data.objects.new(name, mesh)
    arena.collection('PAC_EDIT_PATCH').objects.link(obj)
    obj.location = location
    obj['phlosion_patch_id'] = 'south-clearing/detail/' + name.lower().replace(' ', '-')
    obj['phlosion_display_name'] = name
    obj['lgpe_source_mesh_index'] = -1
    obj['source_reference_mesh'] = source['lgpe_source_mesh_index']
    obj['pilot_placement_kind'] = 'source_detail'
    return obj


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--source-kit', type=Path, required=True, help='Published Route 1 source-layout-kit.json')
    parser.add_argument('--replace', action='store_true', help='Replace a previously generated clearing seed only')
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:])
    original = Path(bpy.data.filepath).resolve()
    output = args.output.resolve()
    if output == original or (output.exists() and not args.replace):
        raise RuntimeError('Output must be a separate new blend; use --replace only to rebuild a clearing seed.')
    scene = bpy.context.scene
    kit = json.loads(args.source_kit.read_text())
    if kit['source'] != json.loads(scene['pilot_source_kit'])['source']:
        raise RuntimeError('Layout kit and blend reference library must describe the same published source.')
    scene['pilot_source_kit'] = json.dumps(kit)
    config = json.loads((ROOT / BLUEPRINT).read_text())
    legacy_path = ROOT / config['reference_scene']
    legacy = json.loads(legacy_path.read_text())
    overrides = {n['id']: n for n in legacy['nodes'] if 'transform' in n['components']}
    prototypes = {p['id']: p for p in kit['objects']}
    templates = {prototypes[o['phlosion_prototype_id']]['prefab_asset_id']: o.data
                 for o in arena.collection('PAC_PREFABS').objects}
    # Grass 01 records 3 and 4 have identical cores and use the same local mesh.
    # Grass 02 hooked beds are retained individually because their pivots differ.
    grass_templates = {o['phlosion_prototype_id']: o.data for o in arena.collection('PAC_PREFABS').objects
                       if o['phlosion_prototype_id'].startswith('encounter-grass/')}
    cells, recovered = recover_cells(kit, legacy, config)
    lookup = {(c['x'], c['z']): c for c in cells}
    def ground(x, y):
        cell = lookup.get((math.floor(x), math.floor(-y)))
        if not cell:
            raise RuntimeError(f'Prop outside authored ground: {x}, {y}')
        return tiles.tile_height(cell, x, y)
    for group in ('PAC_EDIT_PATCH', 'PAC_PREFABS', 'PAC_GUIDES'):
        for obj in list(arena.collection(group).objects):
            bpy.data.objects.remove(obj, do_unlink=True)
    config['tile_cells'] = cells
    scene['pilot_blueprint'] = json.dumps(config)
    scene['pilot_authoring_recipe'] = RECIPE
    scene['pilot_game_root'] = str(ROOT)
    scene['pilot_reference'] = 'South Clearing source blueprint plus saved Phlosion edits; independent Blender terrain'
    scene['pilot_composition_pass'] = 1
    scene['phlosion_patch_direction_precision'] = 64
    tiles.create_guide(arena, cells)
    arena.rebuild_terrain(config)
    def place(proto, name=None, position=None, scale=1.0, yaw=0.0):
        transform = overrides.get(proto['id'], {}).get('components', {}).get('transform', proto['transform'])
        x, h, z = transform['translation']
        x, y = (x*.01, -z*.01) if position is None else position
        mesh = grass_templates.get(proto['id']) if proto['id'].startswith('encounter-grass/') else None
        mesh = mesh or templates[proto['prefab_asset_id']]
        obj = bpy.data.objects.new(name or ('Source ' + proto['display_name']), mesh)
        arena.collection('PAC_PREFABS').objects.link(obj)
        sx, sy, sz = transform['scale']
        obj.scale = (sx*scale, sz*scale, sy*scale)
        obj.rotation_euler.z = math.radians(transform['rotation_degrees'][1] + yaw)
        # Runtime trees share the family prototype pivot; ground their preview
        # mesh's root consistently even when selecting another source instance.
        obj.location = (x, y, ground(x, y) - min(v.co.z for v in mesh.vertices)*obj.scale.z)
        if proto['id'].startswith('encounter-grass/'):
            obj.location.z = h*.01
        obj['phlosion_node_id'] = 'authored-prefab/south-clearing/' + proto['id'].replace('/', '-') if name is None else 'authored-prefab/south-clearing/' + name.lower().replace(' ', '-')
        obj['phlosion_prototype_id'] = proto['id']
        obj['pilot_placement_kind'] = 'brush_bed' if proto['id'].startswith('encounter-grass/') else 'source_plant'
        return obj
    for proto in kit['objects']:
        identity = proto['id']
        transform = overrides.get(identity, {}).get('components', {}).get('transform', proto['transform'])
        x, _, z = transform['translation']
        x, y = x*.01, -z*.01
        if not (6 <= x < 36 and 3 <= y < 29):
            continue
        if identity.startswith('encounter-grass/'):
            if int(identity.rsplit('-', 1)[-1]) in config['encounter_records']:
                place(proto)
        elif identity.startswith(('canonical-tree/', 'buildmodel-vegetation/')):
            if proto.get('prefab_asset_id') in templates:
                place(proto)
        elif identity == 'canonical-mesh/mesh-37':
            place(proto)
    # Fill gaps between the source perimeter trunks, keeping canopy bounds out
    # of the board and reserve rows. These additions use the same species mix.
    for i, (family, x, y, size) in enumerate([
        ('tree_002', 13.0, 18.5, .78), ('tree_001', 11.8, 14.5, .82),
        ('tree_002', 13.0, 10.1, .82), ('tree_001', 12.3, 22.6, .86),
        ('tree_002', 30.3, 21.0, .82), ('tree_001', 29.3, 12.5, .86),
        ('tree_002', 29.2, 25.0, .84), ('tree_001', 9.0, 25.8, 1.0),
        ('tree_006', 13.9, 16.6, .76), ('tree_006', 28.6, 15.0, .76),
        ('tree_006', 14.5, 20.7, .8), ('tree_006', 28.5, 21.5, .82)]):
        proto = next(p for p in kit['objects'] if p.get('prefab_asset_id') == 'route1/' + family)
        obj = place(proto, f'Woodland {family} {i+1:02}', (x,y), size, i*37 % 360)
        obj['pilot_placement_kind'] = 'edge_woodland'
    source = {o.get('lgpe_source_mesh_index'): o for o in arena.collection('LGPE_SOURCE_LOCKED').all_objects if o.type == 'MESH'}
    details = 0
    for index in (2, 3, 4, 9, 16, 18, 20, 22, 23, 24, 26):
        original_mesh = source[index]
        groups = defaultdict(list)
        for faces in components(original_mesh.data):
            lo, hi = bounds(original_mesh.data, faces)
            center = (lo+hi)*.5
            if not (9 <= center.x <= 32 and 5 <= center.y <= 28):
                continue
            rock = index == 26
            upright = index in (16, 24)
            if index not in (2, 3, 4, 9, 26) and lookup[math.floor(center.x),math.floor(-center.y)]['surface'] == 1:
                continue
            if not rock and (max(hi.x-lo.x, hi.y-lo.y) > (1.5 if upright else .9) or hi.z-lo.z > (.8 if upright else .12)):
                continue
            samples = [ground(x,y) for x,y in ((lo.x,lo.y),(hi.x,hi.y),(lo.x,hi.y),(hi.x,lo.y))]
            if rock and 28 < center.x < 30 and 17 < center.y < 18:
                # The source boulder overhangs the upper bank by 16 cm. Move it
                # 18 cm south so its base clears the rebuilt vertical ledge.
                pivot = Vector((center.x, center.y, lo.z))
                location = Vector((center.x, center.y-.18, ground(center.x,center.y-.18)-.012))
                detail(original_mesh, faces, 'East bank landmark boulder', pivot, location)
                details += 1
                continue
            if max(samples)-min(samples) > .05:
                continue
            if rock:
                pivot = Vector((center.x, center.y, lo.z))
                detail(original_mesh, faces, f'Source rock {details:03}', pivot, Vector((center.x, center.y, ground(center.x,center.y)-.012)))
                details += 1
            else:
                groups[math.floor(center.x), math.floor(center.y), round(lo.z,4)].extend(faces)
        for (x,y,base), faces in groups.items():
            pivot = Vector((x+.5, y+.5, base))
            detail(original_mesh, faces, f'Field detail {index} {x} {y} {details:03}', pivot,
                   Vector((pivot.x,pivot.y,ground(pivot.x,pivot.y)+.006)))
            details += 1
    scene.camera.location.y += 9
    scene.camera.location.z += 1
    for obj in arena.collection('PREVIEW_ONLY').objects:
        if obj != scene.camera:
            obj.location.y += 9
    arena.register()
    output.parent.mkdir(parents=True, exist_ok=True)
    scene['clearing_source_scene_sha256'] = hashlib.sha256(legacy_path.read_bytes()).hexdigest()
    scene['clearing_library_blend_sha256'] = hashlib.sha256(Path(bpy.data.filepath).read_bytes()).hexdigest()
    bpy.ops.wm.save_as_mainfile(filepath=str(output))
    # Publish from the saved source, following the same path as daily exports.
    bpy.ops.wm.open_mainfile(filepath=str(output))
    scene = bpy.context.scene
    export_dir = output.parent / 'export'
    arena.export(export_dir, save_source=True)
    audit = {'source_scene': config['reference_scene'], 'source_scene_sha256': scene['clearing_source_scene_sha256'],
             'library_blend': str(original), 'library_blend_sha256': scene['clearing_library_blend_sha256'],
             'cells': len(cells), 'prefabs': len(arena.collection('PAC_PREFABS').objects), 'detail_groups': details,
             'height_differences': [[c['x'],c['z']] for c in cells if recovered.get((c['x'],c['z']),{}).get('occupied') and c['height'] != recovered[c['x'],c['z']]['height']],
             'board_origin': [17,-19], 'encounter_records': config['encounter_records']}
    (export_dir / 'clearing-reference-audit.json').write_text(json.dumps(audit, indent=2)+'\n')
    print('SOUTH_CLEARING_CREATED ' + json.dumps(audit))


if __name__ == '__main__':
    main()
