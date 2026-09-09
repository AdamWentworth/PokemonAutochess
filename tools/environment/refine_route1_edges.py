"""Pass 8: dress the border, restore rock colour, and rebuild closed ledges."""
import argparse
import json
import math
from pathlib import Path
import sys
import bpy
from mathutils import Vector


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--bridge-root',type=Path,required=True)
    p.add_argument('--blueprint',type=Path,required=True)
    args=p.parse_args(sys.argv[sys.argv.index('--')+1:])
    sys.path.insert(0,str(args.bridge_root))
    import arena_pilot as arena
    import arena_tiles as tiles
    scene=bpy.context.scene
    config=json.loads(scene['pilot_blueprint'])
    edges=json.loads(args.blueprint.read_text())
    cells=tiles.read_cells()
    config['composition_pass']=8
    config['edge_dressing']=edges
    scene['pilot_blueprint']=json.dumps(config)
    lookup={(c['x'],c['z']):c for c in cells}
    def ground(x,y):
        cell=lookup.get((math.floor(x),math.floor(-y)))
        if cell is None: raise ValueError(f'Prop outside the tile map: {x}, {y}')
        return tiles.tile_height(cell,x,y)
    props=arena.collection('PAC_PREFABS')
    for obj in list(props.objects):
        if obj.get('pilot_placement_kind','').startswith('edge_'):
            bpy.data.objects.remove(obj,do_unlink=True)
    kit=json.loads(scene['pilot_source_kit'])
    prototypes={p['id']:p for p in kit['objects']}
    plant_templates={prototypes[o['phlosion_prototype_id']]['prefab_asset_id']:(o.data,prototypes[o['phlosion_prototype_id']]) for o in props.objects}
    trees=arena.tree_meshes(kit,arena.collection('LGPE_SOURCE_LOCKED'))
    for name,family,x,y,scale,rotation in edges['trees']:
        mesh,proto=trees[family]
        obj=arena.place(name,mesh,proto,x,y,ground(x,y),rotation,scale)
        obj['pilot_placement_kind']='edge_tree'
    for name,x,y,scale,rotation in edges['shrubs']:
        mesh,proto=trees['tree_006']
        obj=arena.place(name,mesh,proto,x,y,ground(x,y),rotation,scale)
        obj['pilot_placement_kind']='edge_shrub'
    for name,family,x,y,scale,rotation in edges['plants']:
        mesh,proto=plant_templates['route1/'+family]
        obj=arena.place(name,mesh,proto,x,y,ground(x,y),rotation,scale)
        obj['pilot_placement_kind']='edge_plant'
    # Return every rock to its source colour. Do not paint a substitute moss cap.
    rocks=0
    for obj in arena.collection('PAC_EDIT_PATCH').objects:
        if obj.get('source_reference_mesh')!=26: continue
        mesh=obj.data
        mesh.color_attributes.remove(mesh.color_attributes['LGPE_VertexColor'])
        color=mesh.color_attributes.new(name='LGPE_VertexColor',type='FLOAT_COLOR',domain='POINT')
        for src,dst in zip(mesh.color_attributes['LGPE_COLOR0'].data,color.data):
            raw=tuple(src.color)
            dst.color=tuple(v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in raw[:3])+(raw[3],)
        obj['phlosion_display_name']=obj.get('phlosion_display_name',obj.name).replace('mossy rock','field rock')
        obj['rock_colour']='original LGPE vertex colour and textures'
        rocks+=1
    arena.rebuild_terrain(config)
    assert tiles.read_cells()==cells
    bpy.context.view_layer.update()
    # Check the new canopy bounds without moving an artist placement silently.
    for obj in props.objects:
        if not obj.get('pilot_placement_kind','').startswith('edge_'): continue
        corners=[obj.matrix_world @ Vector(p) for p in obj.bound_box]
        lo=[min(p[a] for p in corners) for a in range(3)]
        hi=[max(p[a] for p in corners) for a in range(3)]
        if lo[0]<25 and hi[0]>17 and lo[1]<11 and hi[1]>1:
            raise ValueError(f'New border prop intrudes into board/reserves: {obj.name}, {lo}, {hi}')
    scene['pilot_composition_pass']=8
    scene['pilot_reference']='LGPE edge woodland; closed ledge walls and source rock colour'
    arena.register()
    bpy.ops.wm.save_as_mainfile(filepath=bpy.data.filepath)
    report={'pass':8,'added_trees':len(edges['trees']),'added_shrubs':len(edges['shrubs']),
            'added_plant_clumps':len(edges['plants']),'rocks_source_colour_restored':rocks,
            'tile_cells_unchanged':len(cells),'prefabs':len(props.objects)}
    (Path(bpy.data.filepath).parent/'export/edge-refinement-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print('ROUTE1_EDGES_PASS '+json.dumps(report))


if __name__=='__main__': main()
