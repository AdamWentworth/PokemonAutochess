"""Run inside Blender on the pilot .blend; writes only a separate test copy."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import bpy


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--bridge', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
    original = Path(bpy.data.filepath)
    digest = hashlib.sha256(original.read_bytes()).hexdigest()
    spec = importlib.util.spec_from_file_location('arena', args.bridge)
    arena = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = arena
    spec.loader.exec_module(arena)
    arena.register()
    props = bpy.data.collections['PAC_PREFABS']
    before = {o['phlosion_node_id']: tuple(o.location) for o in props.objects}
    tree = next(o for o in props.objects if o['phlosion_prototype_id'].startswith('canonical-tree/'))
    moved_id = tree['phlosion_node_id']
    tree.location.x -= 1.25
    brush = next((o for o in props.objects if o.get('pilot_playable_brush')),
                 next((o for o in props.objects if o.get('pilot_placement_kind')=='brush_bed'),None))
    if brush:
        cx=json.loads(bpy.context.scene['pilot_blueprint'])['board_center_blender_m'][0]
        brush_delta=-.45 if brush.location.x<cx else .45
        brush.location.x+=brush_delta
    flower = next(o for o in props.objects if 'vegetation/flowers02/' in o['phlosion_prototype_id'])
    duplicate = flower.copy()
    props.objects.link(duplicate)
    duplicate.location.y += 2.0
    guide = bpy.data.objects['Path centerline']
    guide.data.splines[0].points[2].co.x -= .45
    bpy.context.scene.pac_path_width += .5
    ledges = [o for o in bpy.data.collections['PAC_GUIDES'].objects if 'terrace_patch_id' in o]
    original_ledges = {o.name: o.data.as_pointer() for o in bpy.data.collections['PAC_EDIT_PATCH'].objects if 'terrace_guide_name' in o}
    rails=[o for o in bpy.data.collections['PAC_EDIT_PATCH'].objects if o.get('pilot_placement_kind')=='entrance_railing']
    original_rails={o.name:o.data.as_pointer() for o in rails}
    bpy.ops.pac.rebuild_arena_terrain()
    for name,pointer in original_ledges.items(): assert bpy.data.objects[name].data.as_pointer()==pointer
    upper=bpy.data.objects.get('Upper path centerline')
    if upper:
        upper_guide=bpy.data.objects[upper['terrace_guide_name']]
        identity=upper_guide['terrace_patch_id']
        upper_mesh=next(o for o in bpy.data.collections['PAC_EDIT_PATCH'].objects if o['phlosion_patch_id']==identity)
        before_paint=[tuple(v.uv) for v in upper_mesh.data.uv_layers['LGPE_PreviewUV1'].data]
        upper.data.splines[0].points[2].co.y+=.4
        bpy.context.view_layer.objects.active=upper
        bpy.ops.pac.rebuild_arena_ledge()
        after_mesh=next(o for o in bpy.data.collections['PAC_EDIT_PATCH'].objects if o['phlosion_patch_id']==identity)
        assert before_paint!=[tuple(v.uv) for v in after_mesh.data.uv_layers['LGPE_PreviewUV1'].data]
    if ledges:
        selected=ledges[-1]
        identity=selected['terrace_patch_id']
        old_mesh=next(o for o in bpy.data.collections['PAC_EDIT_PATCH'].objects if o['phlosion_patch_id']==identity)
        previous_height=max(v.co.z for v in old_mesh.data.vertices)
        selected['height_m']+=.2
        bpy.context.view_layer.objects.active=selected
        bpy.ops.pac.rebuild_arena_ledge()
        new_mesh=next(o for o in bpy.data.collections['PAC_EDIT_PATCH'].objects if o['phlosion_patch_id']==identity)
        assert abs(max(v.co.z for v in new_mesh.data.vertices)-previous_height-.2)<.0001
    # Rebuilding terrain must never reset a hand-placed tree.
    for name,pointer in original_rails.items(): assert bpy.data.objects[name].data.as_pointer()==pointer
    assert abs(tree.location.x - before[moved_id][0] + 1.25) < .0001
    for obj in props.objects:
        if obj in (tree, duplicate, brush): continue
        assert tuple(obj.location) == before[obj['phlosion_node_id']]
    args.output.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(args.output/'Edited_Test_Copy.blend'))
    arena.export(args.output)
    bpy.ops.wm.save_as_mainfile(filepath=bpy.data.filepath)
    document = json.loads((args.output/'route1_pilot.scene.json').read_text())
    nodes = [n for n in document['nodes'] if 'prefab_instance' in n['components']]
    assert len(nodes) == len(before) + 1
    assert len({n['id'] for n in nodes}) == len(nodes)
    moved = next(n for n in nodes if n['id'] == moved_id)
    assert abs(moved['components']['transform']['translation'][0] - (before[moved_id][0]-1.25)*100) < .001
    if brush:
        brush_node=next(n for n in nodes if n['id']==brush['phlosion_node_id'])
        assert abs(brush_node['components']['transform']['translation'][0]-(before[brush['phlosion_node_id']][0]+brush_delta)*100)<.001
        assert brush_node['components']['prefab_instance']['prefab_asset_id']=='route1/encounter_grass_02'
    assert hashlib.sha256(original.read_bytes()).hexdigest() == digest
    report = {'passed': True, 'original_blend_unchanged': True,
              'tree_move_metres': 1.25, 'path_widened_metres': .5,
              'path_control_point_moved_metres': .45,
              'duplicate_received_unique_id': True,
              'unrelated_placements_preserved': True,
              'path_edit_preserved_ledge_meshes': True,
              'independent_ledge_height_edit': bool(ledges),
              'brush_bed_edit_preserved': bool(brush),
              'independent_upper_path_edit': bool(upper),
              'railings_preserved_by_terrain_edits': bool(rails),
              'playable_brush_edit_preserved': bool(brush and brush.get('pilot_playable_brush')),
              'prefab_count': len(nodes)}
    (args.output/'editability-report.json').write_text(json.dumps(report, indent=2)+'\n')
    print('ARENA_EDITABILITY_PASS ' + json.dumps(report))


if __name__ == '__main__': main()
