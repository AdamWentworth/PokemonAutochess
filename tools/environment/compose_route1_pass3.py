"""One-time south-entrance composition; normal export retains all saved edits."""
import argparse
import json
from pathlib import Path
import sys
import bpy
import bmesh
from mathutils import Vector


def encounter_template(arena, kit, library_root, bridge_root, record_index=0):
    """Preview the same expanded grass record used by the runtime prefab."""
    importer=arena.module('pilot_canonical_importer','import_canonical_environment.py')
    reader=arena.module('pilot_canonical_reader','canonical_scene.py')
    composition=json.loads((bridge_root.parent/'lgpe_importer/route1.composition.json').read_text())
    record=composition['encounter_grass']['records'][record_index]
    canonical=reader.CanonicalScene(library_root/('route1_'+record['model']))
    materials=importer.canonical_materials(canonical,None)
    for material in materials:
        # These are standalone prefab preview materials, not terrain slots.
        material.pop('lgpe_material_index',None)
        material.diffuse_color=(.045,.22,.065,1)
    source=importer.build_mesh_object(next(iter(canonical.meshes())),materials,arena.collection('PAC_PREFABS'))
    core={tuple(c) for c in record['core_cells_source_xz']}
    expanded={(x+dx,z+dz) for x,z in core for dx in (-1,0,1) for dz in (-1,0,1)}
    bpy.ops.object.select_all(action='DESELECT')
    copies=[]
    for x,z in sorted(expanded):
        if (x,z) not in core:
            near=min(core,key=lambda p:((p[0]-x)**2+(p[1]-z)**2,p[0],p[1]))
            x,z=(x+near[0])*.5,(z+near[1])*.5
        obj=source.copy()
        arena.collection('PAC_PREFABS').objects.link(obj)
        obj.location=(x+.5,-z-.5,0)
        obj.hide_select=False
        obj.select_set(True)
        copies.append(obj)
    bpy.context.view_layer.objects.active=copies[0]
    bpy.ops.object.join()
    combined=copies[0]
    # Baking the active copy's residual origin keeps the prototype pivot exact.
    matrix=combined.matrix_world.copy()
    for vertex in combined.data.vertices: vertex.co=matrix @ vertex.co
    mesh=combined.data
    bpy.data.objects.remove(combined,do_unlink=True)
    bpy.data.objects.remove(source,do_unlink=True)
    proto=next(p for p in kit['objects'] if p['id']==f"encounter-grass/{record['model']}/record-{record_index}")
    return mesh,proto


def entrance_railing(arena,terrain,materials,name,cx,cy,length):
    """An independently editable rail using the source sign's pale frame finish."""
    bm=bmesh.new()
    def box(center,scale):
        verts=bmesh.ops.create_cube(bm,size=1)['verts']
        bmesh.ops.scale(bm,vec=Vector(scale),verts=verts)
        bmesh.ops.translate(bm,vec=Vector(center),verts=verts)
    box((0,0,.58),(.10,length,.12))
    for y in (-length*.5,0,length*.5):
        verts=bmesh.ops.create_cone(bm,cap_ends=True,segments=8,radius1=.065,radius2=.065,depth=.70)['verts']
        bmesh.ops.translate(bm,vec=Vector((0,y,.35)),verts=verts)
        if y:
            verts=bmesh.ops.create_uvsphere(bm,u_segments=10,v_segments=6,radius=.10)['verts']
            bmesh.ops.translate(bm,vec=Vector((0,y,.76)),verts=verts)
    bm.normal_update()
    vertices,faces,uvs,colors=[],[],[],[]
    for f in bm.faces:
        face=[]
        axis=max(range(3),key=lambda i:abs(f.normal[i]))
        uv_axes=[i for i in range(3) if i!=axis]
        for v in f.verts:
            face.append(len(vertices)); vertices.append(tuple(v.co))
            uvs.append(((.215,.355),(v.co[uv_axes[0]],v.co[uv_axes[1]]),(0,0),(0,0)))
            colors.append((.94,.97,1,1))
        faces.append(tuple(face))
    bm.free()
    obj=terrain.create_mesh(arena,name,'arena-pilot/railing/'+name.lower().replace(' ','-'),vertices,faces,[20]*len(faces),uvs,colors,materials)
    obj.location=(cx,cy,0)
    obj['pilot_placement_kind']='entrance_railing'
    return obj


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--bridge-root',type=Path,required=True)
    parser.add_argument('--blueprint',type=Path,required=True)
    parser.add_argument('--library-root',type=Path,required=True)
    args=parser.parse_args(sys.argv[sys.argv.index('--')+1:])
    sys.path.insert(0,str(args.bridge_root))
    import arena_pilot as arena
    import arena_terrain as terrain
    config=json.loads(args.blueprint.read_text())
    scene=bpy.context.scene
    kit=json.loads(scene['pilot_source_kit'])
    cx,cy=config['board_center_blender_m']
    prototypes={p['id']:p for p in kit['objects']}
    templates={}
    for obj in arena.collection('PAC_PREFABS').objects:
        proto=prototypes[obj['phlosion_prototype_id']]
        templates[proto['prefab_asset_id']]=(obj.data,proto)
    for family,value in arena.tree_meshes(kit,arena.collection('LGPE_SOURCE_LOCKED')).items():
        templates['route1/'+family]=value
    templates['route1/encounter_grass_02']=encounter_template(arena,kit,args.library_root,args.bridge_root)
    if config.get('entrance_layout_version',3)>=4:
        templates['route1/encounter_grass_02_hook']=encounter_template(arena,kit,args.library_root,args.bridge_root,1)
    for group in ('PAC_PREFABS','PAC_EDIT_PATCH'):
        for obj in list(arena.collection(group).objects): bpy.data.objects.remove(obj,do_unlink=True)
    guides=arena.collection('PAC_GUIDES')
    for obj in list(guides.objects):
        if obj.name in ('Path centerline','Upper path centerline') or 'terrace_patch_id' in obj: bpy.data.objects.remove(obj,do_unlink=True)
    scene['pilot_blueprint']=json.dumps(config)
    arena.curve('Path centerline',[(cx+x,cy+y,.035) for x,y in config['path_points_m']],guides)
    if config.get('upper_path_points_m'):
        upper=arena.curve('Upper path centerline',[(cx+x,cy+y,.535) for x,y in config['upper_path_points_m']],guides)
        upper['terrace_guide_name']='Outline - Rear landing'
    materials={int(m['lgpe_material_index']):m for m in bpy.data.materials if 'lgpe_material_index' in m}
    terrain.base_ground(arena,config,materials)
    for row in config['terraces']:
        guide=arena.curve('Outline - '+row['name'],[(cx+x,cy+y,row['base']+row['height']+.035) for x,y in row['points']],guides)
        guide.data.splines[0].use_cyclic_u=True
        guide['terrace_patch_id']='arena-pilot/terrace/'+row['id']
        guide['height_m']=row['height']
        guide['base_m']=row['base']
        guide['corner_radius_m']=.25
        if row.get('paint_upper_path'): guide['paint_upper_path']=True
        guide.id_properties_ui('height_m').update(min=.25,max=2)
        guide.id_properties_ui('base_m').update(min=0,max=4)
        terrain.terrace(arena,guide,config,materials)
    for row in config['trees']+config['brush']+config['small_plants']:
        name,asset,x,y,z,scale,rotation=row
        mesh,proto=templates['route1/'+asset]
        obj=arena.place(name,mesh,proto,cx+x,cy+y,z,rotation,scale)
        if asset.startswith('encounter_grass_'):
            obj['pilot_placement_kind']='brush_bed'
            if name in config.get('playable_brush',[]):
                obj['pilot_playable_brush']=True
    mesh,proto=templates['route1/source_mesh_037']
    sx,sy,sz,scale=config.get('sign_placement',[1.5,-6.0,0,.76])
    arena.place('South entrance route sign',mesh,proto,cx+sx,cy+sy,sz,0,scale)
    for name,x,y,length in config.get('railings',[]):
        entrance_railing(arena,terrain,materials,name,cx+x,cy+y,length)
    arena.clear_board_canopies()
    scene.camera.location=(cx,cy-19,20)
    scene.camera.rotation_euler=(Vector((cx,cy+.5,0))-scene.camera.location).to_track_quat('-Z','Y').to_euler()
    scene.camera.data.ortho_scale=23
    scene['pilot_composition_pass']=config.get('composition_pass',3)
    scene['pilot_reference']='LGPE Route 1 southern entrance: brush beds, short stepped ledges, entrance opening'
    arena.register()
    bpy.ops.wm.save_as_mainfile(filepath=bpy.data.filepath)
    print(f"ARENA_COMPOSITION_PASS_{scene['pilot_composition_pass']}_READY")


if __name__=='__main__': main()
