"""One-time composition pass. Daily exports continue reading the saved .blend."""
import argparse
import json
from pathlib import Path
import sys
import bpy
from mathutils import Vector


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--bridge-root',type=Path,required=True)
    parser.add_argument('--blueprint',type=Path,required=True)
    args=parser.parse_args(sys.argv[sys.argv.index('--')+1:])
    sys.path.insert(0,str(args.bridge_root))
    import arena_pilot as arena
    import arena_terrain as terrain
    config=json.loads(args.blueprint.read_text())
    scene=bpy.context.scene
    kit=json.loads(scene['pilot_source_kit'])
    scene['pilot_blueprint']=json.dumps(config)
    cx,cy=config['board_center_blender_m']
    # Retain the existing reusable meshes; replace only this pilot's placement layout.
    templates={}
    prototypes={o['id']:o for o in kit['objects']}
    for obj in arena.collection('PAC_PREFABS').objects:
        proto=prototypes[obj['phlosion_prototype_id']]
        templates[proto['prefab_asset_id']]=(obj.data,proto)
    tree_library=arena.tree_meshes(kit,arena.collection('LGPE_SOURCE_LOCKED'))
    for family,value in tree_library.items(): templates['route1/'+family]=value
    for obj in list(arena.collection('PAC_PREFABS').objects): bpy.data.objects.remove(obj,do_unlink=True)
    for obj in list(arena.collection('PAC_EDIT_PATCH').objects): bpy.data.objects.remove(obj,do_unlink=True)
    guides=arena.collection('PAC_GUIDES')
    for obj in list(guides.objects):
        if obj.name=='Path centerline' or 'terrace_patch_id' in obj: bpy.data.objects.remove(obj,do_unlink=True)
    arena.curve('Path centerline',[(cx+x,cy+y,.035) for x,y in config['path_points_m']],guides)
    materials={int(m['lgpe_material_index']):m for m in bpy.data.materials if 'lgpe_material_index' in m}
    terrain.base_ground(arena,config,materials)
    for row in config['terraces']:
        guide=arena.curve('Outline - '+row['name'],[(cx+x,cy+y,row['base']+row['height']+.035) for x,y in row['points']],guides)
        guide.data.splines[0].use_cyclic_u=True
        guide['terrace_patch_id']='arena-pilot/terrace/'+row['id']
        guide['height_m']=row['height']; guide['base_m']=row['base']
        guide.id_properties_ui('height_m').update(min=.25,max=2)
        guide.id_properties_ui('base_m').update(min=0,max=4)
        terrain.terrace(arena,guide,config,materials)
    def place(name,asset,x,y,z,scale=1,rotation=0):
        mesh,proto=templates['route1/'+asset]
        return arena.place(name,mesh,proto,cx+x,cy+y,z,rotation,scale)
    trees=[
        ('West oak','tree_003',-9.1,-4.5,.65,.68,-12),
        ('West young tree','tree_001',-7.7,-1.7,.65,.82,14),
        ('West background tree','tree_002',-10.1,1.4,.65,.85,-18),
        ('West foreground tree','tree_001',-10.0,-8.1,0,.76,30),
        ('North pair left','tree_002',-6.2,8.1,.85,.87,-10),
        ('North pair right','tree_001',-3.5,8.9,.85,.80,17),
        ('North skyline','tree_003',-10.5,11.0,1.55,.72,20),
        ('East meadow tree','tree_001',9.2,3.4,.45,.86,-24),
        ('East meadow companion','tree_002',11.7,5.0,.45,.78,13),
        ('Path exit tree','tree_001',5.8,9.3,0,.78,25),
        ('Southeast tree','tree_002',8.7,-7.4,0,.79,14),
        ('Southeast sapling','tree_001',10.4,-9.0,0,.65,-21),
    ]
    for name,asset,x,y,z,s,r in trees: place(name,asset,x,y,z,s,r)
    place('Route sign','source_mesh_037',-4.65,-6.3,0,.95,0)
    flowers=[
        (-6.7,-4.0,.65),(-7.1,-4.6,.65),(-6.65,-3.1,.65),
        (-2.5,6.65,.85),(-3.4,6.7,.85),(-1.8,7.1,.85),
        (7.7,1.2,.45),(8.3,1.4,.45),(7.65,2.0,.45),(8.6,2.3,.45),
        (6.6,-5.7,0),(7.1,-6.1,0),(-4.8,-7.0,0)
    ]
    for i,(x,y,z) in enumerate(flowers):
        place(f'Wildflowers {i+1:02}','flowers_02' if i%3 else 'flowers_04',x,y,z,.68+(i%3)*.09,i*43)
    for i,(x,y,z) in enumerate([(-6.5,.3,.65),(-7.1,1.2,.65),(-8,-6.1,.65),(-5.1,6.8,.85),(-.9,8.2,.85),(7.8,5.3,.45),(8.4,.3,.45),(7.2,-7.0,0),(-6.0,-8.0,0)]):
        place(f'Meadow grass {i+1:02}','small_grass_02',x,y,z,.44+(i%2)*.08,i*27)
    arena.clear_board_canopies()
    # Material preview and authoring camera frame the same important footprint.
    scene.camera.location=(cx,cy-19,20)
    scene.camera.rotation_euler=(Vector((cx,cy+.5,0))-scene.camera.location).to_track_quat('-Z','Y').to_euler()
    scene.camera.data.ortho_scale=24
    scene['pilot_composition_pass']=2
    bpy.ops.wm.save_as_mainfile(filepath=bpy.data.filepath)
    print('ARENA_COMPOSITION_PASS_2_READY')


if __name__=='__main__': main()
