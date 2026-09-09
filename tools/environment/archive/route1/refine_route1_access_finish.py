"""Pass 10: paint the traversable route and round its ramp/ledge junctions."""
import argparse
import json
from pathlib import Path
import sys
import bpy


def connected_route(cells, source, settings, height):
    lookup={(c['x'],c['z']):c for c in cells}
    lo,hi=settings['route_corridor_x']
    allowed={key for key in lookup if lo<=key[0]<=hi and not (
        settings['preserve_source_dark_lawn_islands'] and source.get(key,{}).get('surface')=='dark_lawn')}
    seed=tuple(settings['seed_cell'])
    assert seed in allowed
    connected={seed}; pending=[seed]
    while pending:
        x,z=pending.pop(); cell=lookup[(x,z)]; y=-z-1
        for dx,dz,a,b in ((0,1,(x,y),(x+1,y)),(1,0,(x+1,y),(x+1,y+1)),
                          (0,-1,(x+1,y+1),(x,y+1)),(-1,0,(x,y+1),(x,y))):
            key=(x+dx,z+dz)
            if key not in allowed or key in connected: continue
            # An entire shared edge must meet; corner-only contact is not access.
            neighbor=lookup[key]
            if all(abs(height(cell,*p)-height(neighbor,*p))<1e-6 for p in (a,b)):
                connected.add(key); pending.append(key)
    return connected


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--bridge-root',type=Path,required=True)
    p.add_argument('--source-kit',type=Path,required=True)
    p.add_argument('--blueprint',type=Path,required=True)
    args=p.parse_args(sys.argv[sys.argv.index('--')+1:])
    sys.path.insert(0,str(args.bridge_root))
    import arena_pilot as arena
    import arena_tiles as tiles
    before=tiles.read_cells()
    preserved={o.name:(o.data.as_pointer(),tuple(o.matrix_world))
               for collection in ('PAC_PREFABS','PAC_EDIT_PATCH') for o in arena.collection(collection).objects
               if o.get('phlosion_patch_id')!='arena-pilot/terrain'}
    finish=json.loads(args.blueprint.read_text())
    source={(c['x'],c['z']):c for c in json.loads(args.source_kit.read_text())['editor_terrain_tiles']}
    accessible=connected_route(before,source,finish['visual_access_paint'],tiles.tile_height)
    guide=bpy.data.objects[tiles.GUIDE]
    for face,c in zip(guide.data.polygons,before):
        if c['surface']!=1:
            guide.data.attributes['PAC_surface'].data[face.index].value=0 if (c['x'],c['z']) in accessible else 2
    config=json.loads(bpy.context.scene['pilot_blueprint'])
    config.update(finish)
    bpy.context.scene['pilot_blueprint']=json.dumps(config)
    arena.rebuild_terrain(config)
    after=tiles.read_cells()
    assert all(all(a[k]==b[k] for k in ('x','z','height','ramp')) for a,b in zip(before,after))
    assert all(b['surface']==1 for a,b in zip(before,after) if a['surface']==1)
    assert all(c['surface']!=2 for c in after if c['ramp'])
    for name,state in preserved.items():
        o=bpy.data.objects[name]
        assert (o.data.as_pointer(),tuple(o.matrix_world))==state
    ground=next(o for o in arena.collection('PAC_EDIT_PATCH').objects if o.get('phlosion_patch_id')=='arena-pilot/terrain')
    corners=json.loads(ground['rounded_ledge_corners'])
    bpy.context.scene['pilot_composition_pass']=10
    arena.register()
    bpy.ops.wm.save_as_mainfile(filepath=bpy.data.filepath)
    report={'pass':10,'height_and_ramp_cells_preserved':len(after),'connected_route_cells':len(accessible),
            'dark_lawn_cells':sum(c['surface']==2 for c in after),
            'raised_lawn_cells_restored_to_light':sum(a['surface']==2 and b['surface']==0 for a,b in zip(before,after)),
            'rounded_corners':len(corners),'rounded_ramp_junctions':sum(c['adjacent_ramp'] for c in corners),
            'preserved_prefabs':len(arena.collection('PAC_PREFABS').objects),
            'preserved_detail_meshes':len(arena.collection('PAC_EDIT_PATCH').objects)-1,
            'visual_access_paint':finish['visual_access_paint'],'source_layout_kit':str(args.source_kit)}
    (Path(bpy.data.filepath).parent/'export/access-finish-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print('ROUTE1_ACCESS_FINISH '+json.dumps(report))


if __name__=='__main__': main()
