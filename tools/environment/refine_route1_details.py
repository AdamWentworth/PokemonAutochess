"""Restore source dressing and grass proportions without replacing tile authoring."""
import argparse
from collections import defaultdict
import json
import math
from pathlib import Path
import sys
import bpy
import bmesh
from mathutils import Vector


def components(mesh):
    """Join by position as well as index so UV seams do not split a rock."""
    parent=list(range(len(mesh.vertices)))
    positions={}
    def root(i):
        while parent[i]!=i:
            parent[i]=parent[parent[i]]; i=parent[i]
        return i
    def join(a,b): parent[root(b)]=root(a)
    for v in mesh.vertices:
        key=tuple(round(q,4) for q in v.co)
        if key in positions: join(v.index,positions[key])
        else: positions[key]=v.index
    for f in mesh.polygons:
        for v in f.vertices[1:]: join(f.vertices[0],v)
    result=defaultdict(list)
    for f in mesh.polygons: result[root(f.vertices[0])].append(f.index)
    return list(result.values())


def bounds(mesh,faces):
    vertices={v for i in faces for v in mesh.polygons[i].vertices}
    lo=Vector(tuple(min(mesh.vertices[v].co[a] for v in vertices) for a in range(3)))
    hi=Vector(tuple(max(mesh.vertices[v].co[a] for v in vertices) for a in range(3)))
    return lo,hi


def extract_patch(arena,source,faces,name,identity,pivot,location=None):
    mesh=source.data.copy()
    bm=bmesh.new(); bm.from_mesh(mesh)
    bm.faces.ensure_lookup_table()
    keep=set(faces)
    bmesh.ops.delete(bm,geom=[f for f in bm.faces if f.index not in keep],context='FACES')
    bmesh.ops.delete(bm,geom=[v for v in bm.verts if not v.link_faces],context='VERTS')
    bm.to_mesh(mesh); bm.free()
    for v in mesh.vertices: v.co-=pivot
    obj=bpy.data.objects.new(name,mesh)
    arena.collection('PAC_EDIT_PATCH').objects.link(obj)
    obj.location=location if location is not None else pivot
    obj['phlosion_patch_id']=identity
    obj['phlosion_display_name']=name
    obj['lgpe_source_mesh_index']=-1
    obj['pilot_placement_kind']='source_detail'
    obj['source_reference_mesh']=source.get('lgpe_source_mesh_index')
    return obj


def tint_rock_cap(mesh):
    """Paint a moss cap in the editable colour layer, retaining the rock atlas."""
    prior=[tuple(v.color) for v in mesh.color_attributes['LGPE_COLOR0'].data]
    mesh.color_attributes.remove(mesh.color_attributes['LGPE_VertexColor'])
    color=mesh.color_attributes.new(name='LGPE_VertexColor',type='FLOAT_COLOR',domain='CORNER')
    height=max(v.co.z for v in mesh.vertices)
    for face in mesh.polygons:
        upper=face.normal.z>.4 and face.center.z>height*.45
        tint=(.62,.96,.38) if upper else (.70,.75,.73)
        for loop_index in face.loop_indices:
            raw=prior[mesh.loops[loop_index].vertex_index]
            rgb=[raw[q]*tint[q] for q in range(3)]
            color.data[loop_index].color=tuple(v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in rgb)+(raw[3],)


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--bridge-root',type=Path,required=True)
    p.add_argument('--library-root',type=Path,required=True)
    p.add_argument('--blueprint',type=Path,required=True)
    args=p.parse_args(sys.argv[sys.argv.index('--')+1:])
    sys.path.insert(0,str(args.bridge_root)); sys.path.insert(0,str(Path(__file__).parent))
    import arena_pilot as arena
    import arena_tiles as tiles
    from compose_route1_pass3 import encounter_template
    scene=bpy.context.scene
    config=json.loads(args.blueprint.read_text())
    kit=json.loads(scene['pilot_source_kit'])
    prototypes={p['id']:p for p in kit['objects']}
    saved=json.loads((args.blueprint.parent/config['tile_layout_file']).read_text())
    cells=[dict(zip(saved['columns'],row)) for row in saved['cells']]
    config['tile_cells']=cells
    scene['pilot_blueprint']=json.dumps(config)
    tiles.create_guide(arena,cells)
    lookup={(c['x'],c['z']):c for c in cells}
    def ground(x,y):
        cell=lookup.get((math.floor(x),math.floor(-y)))
        return tiles.tile_height(cell,x,y) if cell else 0
    source={o.get('lgpe_source_mesh_index'):o for o in arena.collection('LGPE_SOURCE_LOCKED').all_objects if o.type=='MESH'}
    # Preserve independent artist trees; replace the earlier approximate grass
    # and small planting scheme with named source records.
    props=arena.collection('PAC_PREFABS')
    templates={prototypes[o['phlosion_prototype_id']]['prefab_asset_id']:(o.data,prototypes[o['phlosion_prototype_id']]) for o in props.objects}
    for obj in list(props.objects):
        if obj.get('pilot_placement_kind')=='brush_bed' or obj['phlosion_prototype_id'].startswith('buildmodel-vegetation/') or obj.get('pilot_placement_kind')=='shrub':
            bpy.data.objects.remove(obj,do_unlink=True)
    for obj in list(arena.collection('PAC_EDIT_PATCH').objects):
        if obj.get('pilot_placement_kind')=='source_detail': bpy.data.objects.remove(obj,do_unlink=True)
    arena.rebuild_terrain(config)
    for record_index,name in ((0,'South entrance encounter grass'),(1,'West hooked encounter grass'),(2,'East hooked encounter grass'),(3,'Northern square encounter grass')):
        mesh,proto=encounter_template(arena,kit,args.library_root,args.bridge_root,record_index)
        x,h,z=proto['transform']['translation']
        obj=arena.place(name,mesh,proto,x*.01,-z*.01,h*.01)
        if record_index==0:
            # The current Phlosion entrance clears the two southern grass
            # rows. Match that shorter threshold with an editable bed transform.
            # Keep blade height and the full east/west footprint unchanged.
            obj.location.y=config.get('southern_grass_pivot_y_m',3.275)
            obj.scale.y=config.get('southern_grass_depth_scale',.5)
        obj['pilot_placement_kind']='brush_bed'
        obj['pilot_playable_brush']=record_index<3
        obj['reference_record']=record_index
    tree_templates=arena.tree_meshes(kit,arena.collection('LGPE_SOURCE_LOCKED'))
    shrub_mesh,shrub_proto=tree_templates['tree_006']
    for name,x,y,scale in config['shrubs']:
        obj=arena.place(name,shrub_mesh,shrub_proto,x,y,ground(x,y),0,scale)
        obj['pilot_placement_kind']='shrub'
    restored_plants=0
    for proto in kit['objects']:
        if not proto['id'].startswith('buildmodel-vegetation/'): continue
        x,h,z=proto['transform']['translation']; x,y=x*.01,-z*.01
        if not (12<=x<=30 and 1<=y<=17): continue
        if proto['prefab_asset_id'] not in templates: continue
        if proto['id']=='buildmodel-vegetation/flowers02/record-6': x=16.4
        mesh,_=templates[proto['prefab_asset_id']]
        obj=arena.place('Source '+proto['display_name'],mesh,proto,x,y,ground(x,y),0,1)
        obj['pilot_placement_kind']='source_plant'
        restored_plants+=1
    # Rocks retain their original geometry, atlas coordinates and vertex colour.
    rock=source[26]; rock_components=components(rock.data)
    for name,component_index,x,y,scale in config['rocks']:
        faces=rock_components[component_index]
        lo,hi=bounds(rock.data,faces)
        pivot=Vector(((lo.x+hi.x)*.5,(lo.y+hi.y)*.5,lo.z))
        obj=extract_patch(arena,rock,faces,name,'arena-pilot/detail/'+name.lower().replace(' ','-'),pivot,Vector((x,y,ground(x,y)-.012)))
        obj.scale=(scale,)*3
        tint_rock_cap(obj.data)
    # Keep upright tufts and small, nearly flat ground patches. The other field
    # components include broad baked leaf silhouettes and terrain cleanup cards;
    # replanting those by their bounds produces floating sheets.
    detail_groups=0
    for index in (16,18,20,22,23,24):
        original=source[index]
        grouped=defaultdict(list)
        for faces in components(original.data):
            lo,hi=bounds(original.data,faces); center=(lo+hi)*.5
            if not (12<=center.x<=30 and .5<=center.y<=18): continue
            upright=index in (16,24)
            if max(hi.x-lo.x,hi.y-lo.y)>(1.5 if upright else .9): continue
            if hi.z-lo.z>(.8 if upright else .12): continue
            # Do not let a source tuft bridge a new ledge or float over a ramp.
            samples=[ground(x,y) for x,y in ((lo.x,lo.y),(hi.x,hi.y),(lo.x,hi.y),(hi.x,lo.y))]
            if max(samples)-min(samples)>.05: continue
            grouped[(math.floor(center.x),math.floor(center.y),round(lo.z,4))].extend(faces)
        for (x,y,base),faces in grouped.items():
            pivot=Vector((x+.5,y+.5,base))
            location=Vector((pivot.x,pivot.y,ground(pivot.x,pivot.y)+.006))
            extract_patch(arena,original,faces,f'Field detail {index} - {x} {y}',f'arena-pilot/detail/field-{index}-{x}-{y}-{round(base*10000)}',pivot,location)
            detail_groups+=1
    for index in (2,9):
        original=source[index]; faces=list(range(len(original.data.polygons)))
        lo,hi=bounds(original.data,faces)
        pivot=(lo+hi)*.5
        extract_patch(arena,original,faces,f'Clearing pebbles {index}',f'arena-pilot/detail/pebbles-{index}',pivot,Vector((pivot.x,pivot.y,ground(pivot.x,pivot.y)+.006)))
    scene['pilot_composition_pass']=7
    scene['pilot_reference']='South entrance source dressing and encounter records; separated half-metre rear ledges'
    arena.register()
    bpy.ops.wm.save_as_mainfile(filepath=bpy.data.filepath)
    report={'pass':7,'grass_records':[0,1,2,3],'grass_scale':1,'south_grass_depth_scale':.5,
            'south_grass_bounds_y_m':[2,4.05],'rock_moss_tint':'authored corner colour','rock_pieces':len(config['rocks']),
            'shrubs':len(config['shrubs']),'source_plants':restored_plants,'field_detail_groups':detail_groups}
    (Path(bpy.data.filepath).parent/'export/detail-refinement-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print('ROUTE1_DETAIL_PASS '+json.dumps(report))


if __name__=='__main__': main()
