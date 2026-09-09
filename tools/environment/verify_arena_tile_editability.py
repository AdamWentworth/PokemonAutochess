"""Exercise the tile authoring controls on a copy, retaining the saved original."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import bpy


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--bridge-root',type=Path,default=Path(__file__).resolve().parent/'blender')
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args(sys.argv[sys.argv.index('--')+1:])
    sys.path.insert(0,str(args.bridge_root))
    import arena_pilot as arena
    import arena_tiles as tiles
    original=Path(bpy.data.filepath)
    digest=hashlib.sha256(original.read_bytes()).hexdigest()
    arena.register()
    before=tiles.read_cells()
    props={o.name:tuple(o.location) for o in arena.collection('PAC_PREFABS').objects}
    details={o.name:(o.data.as_pointer(),tuple(o.location)) for o in arena.collection('PAC_EDIT_PATCH').objects if o.get('pilot_placement_kind')=='source_detail'}
    guide=bpy.data.objects[tiles.GUIDE]
    bpy.ops.pac.edit_tiles()
    # Exercise the same face-selection and Apply button used in the UI.
    bpy.ops.object.mode_set(mode='OBJECT')
    index=next(i for i,c in enumerate(before) if (c['x'],c['z'])==(21,-6))
    for face in guide.data.polygons: face.select=face.index==index
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.context.scene.pac_tile_height=2
    bpy.context.scene.pac_tile_surface='1'
    bpy.context.scene.pac_tile_ramp='1'
    assert bpy.ops.pac.apply_tiles()=={'FINISHED'}
    bpy.ops.object.mode_set(mode='OBJECT')
    after=tiles.read_cells()
    assert after[index]==dict(before[index],height=2,surface=1,ramp=1)
    assert all(a==b for i,(a,b) in enumerate(zip(before,after)) if i!=index)
    ground=next(o for o in arena.collection('PAC_EDIT_PATCH').objects if o.get('phlosion_patch_id')=='arena-pilot/terrain')
    sample=[v.co.z for v in ground.data.vertices if abs(v.co.x-21.4)<.001 and abs(v.co.y-5.4)<.001]
    assert sample and abs(max(sample)-1.2)<.001
    # Dark lawn is a real surface choice, independent of height or dirt.
    dark_index=next(i for i,c in enumerate(before) if (c['x'],c['z'])==(22,-6))
    for face in guide.data.polygons: face.select=face.index==dark_index
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.context.scene.pac_tile_height=before[dark_index]['height']
    bpy.context.scene.pac_tile_surface='2'
    bpy.context.scene.pac_tile_ramp=str(before[dark_index]['ramp'])
    assert bpy.ops.pac.apply_tiles()=={'FINISHED'}
    bpy.ops.object.mode_set(mode='OBJECT')
    dark_after=tiles.read_cells()
    assert dark_after[dark_index]==dict(before[dark_index],surface=2)
    assert all(a==b for i,(a,b) in enumerate(zip(after,dark_after)) if i!=dark_index)
    assert all(tuple(bpy.data.objects[name].location)==loc for name,loc in props.items())
    assert all((bpy.data.objects[name].data.as_pointer(),tuple(bpy.data.objects[name].location))==state for name,state in details.items())
    brush=next(o for o in arena.collection('PAC_PREFABS').objects if o.get('pilot_playable_brush'))
    brush.location.x-=1
    args.output.mkdir(parents=True,exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(args.output/'Edited_Tile_Test_Copy.blend'))
    arena.export(args.output)
    doc=json.loads((args.output/'route1_pilot.scene.json').read_text())
    node=next(n for n in doc['nodes'] if n['id']==brush['phlosion_node_id'])
    assert abs(node['components']['transform']['translation'][0]-brush.location.x*100)<.001
    assert len(tiles.read_cells())==len(before)
    assert hashlib.sha256(original.read_bytes()).hexdigest()==digest
    report={'passed':True,'original_blend_unchanged':True,'tile_count':len(before),
            'selected_cell_height_surface_and_ramp_changed':True,'other_cells_preserved':True,
            'generated_ramp_height_checked':True,'terrain_edit_preserved_props':True,
            'dark_lawn_surface_edit_checked':True,
            'brush_move_preserved_on_export':True,'source_detail_meshes_preserved':len(details)}
    (args.output/'editability-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print('TILE_EDITABILITY_PASS '+json.dumps(report))


if __name__=='__main__': main()
