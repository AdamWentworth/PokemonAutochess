"""Rebuild the entrance from its editor tile plan using independent new geometry."""
import argparse
import json
from pathlib import Path
import sys
import bpy


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--bridge-root',type=Path,required=True)
    p.add_argument('--blueprint',type=Path,required=True)
    p.add_argument('--source-kit',type=Path,help='Optional: reseed cells from the recovered editor layout instead of the saved tile map')
    args=p.parse_args(sys.argv[sys.argv.index('--')+1:])
    sys.path.insert(0,str(args.bridge_root))
    import arena_pilot as arena
    import arena_tiles as tiles
    config=json.loads(args.blueprint.read_text())
    kit=json.loads(args.source_kit.read_text()) if args.source_kit else None
    source={(c['x'],c['z']):c for c in kit['editor_terrain_tiles']} if kit else {}
    bounds=config['tile_bounds']
    cells=[]
    for z in range(bounds['z_min'],bounds['z_max']+1):
        for x in range(bounds['x_min'],bounds['x_max']+1):
            # Continue the entry's border to the camera edge. Inside the route,
            # the editor's logical cells remain the source of the blueprint.
            original=source.get((x,min(-1,z)))
            if not original or not original['occupied']:
                original={'height':1,'surface':'light_lawn','shape':'flat'}
            shape=original['shape']
            # Rebuild coherent ramp strips; isolated slope classifications in
            # cliff/fringe cells become flat steps at their recovered level.
            ramp=int((z==-9 and 16<=x<=19) or (z==-13 and 22<=x<=25) or (z==-19 and 16<=x<=20))
            surface=int(original['surface']=='dirt_path') if z<0 else 0
            cells.append({'x':x,'z':z,'height':original['height'],'surface':surface,'ramp':ramp})
    if not args.source_kit:
        saved=json.loads((args.blueprint.parent/config['tile_layout_file']).read_text())
        cells=[dict(zip(saved['columns'],row)) for row in saved['cells']]
    config['tile_cells']=cells
    config['reference_scene']='scenes/route1.scene.json'
    bpy.context.scene['pilot_blueprint']=json.dumps(config)
    for obj in list(arena.collection('PAC_EDIT_PATCH').objects): bpy.data.objects.remove(obj,do_unlink=True)
    for obj in list(arena.collection('PAC_GUIDES').objects):
        if obj.name in ('Path centerline','Upper path centerline') or 'terrace_patch_id' in obj:
            bpy.data.objects.remove(obj,do_unlink=True)
    tiles.create_guide(arena,cells)
    arena.rebuild_terrain(config)
    props=arena.collection('PAC_PREFABS')
    hooked=next(o for o in props.objects if o.name=='West hooked brush')
    regular=next(o for o in props.objects if o.name=='Southern grass threshold')
    hook_mesh,regular_mesh=hooked.data,regular.data
    prototypes={p['id']:p for p in json.loads(bpy.context.scene['pilot_source_kit'])['objects']}
    hook_proto=prototypes[hooked['phlosion_prototype_id']]
    regular_proto=prototypes[regular['phlosion_prototype_id']]
    for obj in list(props.objects):
        if obj.get('pilot_placement_kind')=='brush_bed': bpy.data.objects.remove(obj,do_unlink=True)
    for name,kind,x,y,z,scale in config['tile_brush']:
        mesh,proto=(hook_mesh,hook_proto) if kind=='hook' else (regular_mesh,regular_proto)
        obj=arena.place(name,mesh,proto,x,y,z,0,scale)
        obj['pilot_placement_kind']='brush_bed'
        obj['pilot_playable_brush']=kind=='hook'
    lookup={(c['x'],c['z']):c for c in cells}
    for obj in props.objects:
        if obj.get('pilot_placement_kind')=='brush_bed': continue
        if obj.name=='South entrance route sign': obj.location.x,obj.location.y=20.1,7.8
        cell=lookup.get((int(obj.location.x//1),int((-obj.location.y)//1)))
        if cell:
            proto=prototypes[obj['phlosion_prototype_id']]
            floor_offset=(proto['transform']['translation'][1]-proto['bounds_minimum_cm'][1])*.01*obj.scale.z
            obj.location.z=tiles.tile_height(cell,obj.location.x,obj.location.y)+floor_offset
    bpy.context.scene['pilot_composition_pass']=6
    bpy.context.scene['pilot_reference']='Phlosion south entrance tile blueprint, rebuilt as independent Blender terrain'
    arena.register()
    bpy.ops.wm.save_as_mainfile(filepath=bpy.data.filepath)
    print(f'ARENA_TILE_BLUEPRINT_READY cells={len(cells)}')


if __name__=='__main__': main()
