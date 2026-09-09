"""Pass 9: soften terrain corners and apply an editable dark upper lawn."""
import argparse
import json
from pathlib import Path
import sys
import bpy


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--bridge-root',type=Path,required=True)
    args=p.parse_args(sys.argv[sys.argv.index('--')+1:])
    sys.path.insert(0,str(args.bridge_root))
    import arena_pilot as arena
    import arena_tiles as tiles
    before=tiles.read_cells()
    props={o.name:(o.data.as_pointer(),tuple(o.matrix_world)) for o in arena.collection('PAC_PREFABS').objects}
    details={o.name:(o.data.as_pointer(),tuple(o.matrix_world)) for o in arena.collection('PAC_EDIT_PATCH').objects if o.get('phlosion_patch_id')!='arena-pilot/terrain'}
    guide=bpy.data.objects[tiles.GUIDE]
    for face,c in zip(guide.data.polygons,before):
        if c['surface']==0 and (c['height']>0 or c['ramp']):
            guide.data.attributes['PAC_surface'].data[face.index].value=2
    config=json.loads(bpy.context.scene['pilot_blueprint'])
    config['composition_pass']=9
    config['ledge_corner_radius_m']=.18
    bpy.context.scene['pilot_blueprint']=json.dumps(config)
    arena.rebuild_terrain(config)
    after=tiles.read_cells()
    assert all(all(a[k]==b[k] for k in ('x','z','height','ramp')) for a,b in zip(before,after))
    assert all(b['surface']==1 for a,b in zip(before,after) if a['surface']==1)
    for name,state in {**props,**details}.items():
        o=bpy.data.objects[name]
        assert (o.data.as_pointer(),tuple(o.matrix_world))==state
    ground=next(o for o in arena.collection('PAC_EDIT_PATCH').objects if o.get('phlosion_patch_id')=='arena-pilot/terrain')
    bpy.context.scene['pilot_composition_pass']=9
    arena.register()
    bpy.ops.wm.save_as_mainfile(filepath=bpy.data.filepath)
    report={'pass':9,'height_and_ramp_cells_preserved':len(after),'dark_lawn_cells':sum(c['surface']==2 for c in after),
            'rounded_corners':len(json.loads(ground['rounded_ledge_corners'])),'corner_radius_m':.18,
            'preserved_prefabs':len(props),'preserved_detail_meshes':len(details),'overlapping_rim_removed':True}
    (Path(bpy.data.filepath).parent/'export/ledge-finish-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print('ROUTE1_LEDGE_FINISH '+json.dumps(report))


if __name__=='__main__': main()
