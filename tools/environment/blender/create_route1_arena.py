"""Seed a new Route 1 arena from a saved reference library and explicit recipe.

Never use for routine edits: the resulting .blend becomes authoritative. This
command refuses to overwrite either an existing output or its library input.
"""
import argparse
from collections import defaultdict
import hashlib
import json
import math
from pathlib import Path
import sys
import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0,str(Path(__file__).resolve().parent))
import arena_pilot as arena
import arena_tiles as tiles
import arena_grass
from arena_recipe import load_recipe
from create_route1_south_clearing import recover_cells, components, bounds, detail, uses_source_layout


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--recipe',required=True)
    parser.add_argument('--blueprint',required=True)
    parser.add_argument('--source-kit',type=Path,required=True,help='Matching published source-layout-kit.json with recovered tiles')
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args(sys.argv[sys.argv.index('--')+1:])
    original=Path(bpy.data.filepath).resolve()
    output=args.output.resolve()
    if output==original or output.exists(): raise RuntimeError('Choose a new output blend; existing sources are never overwritten')
    original_sha=hashlib.sha256(original.read_bytes()).hexdigest()
    scene=bpy.context.scene
    recipe=load_recipe(ROOT,args.recipe)
    config=json.loads((ROOT/args.blueprint).read_text())
    if config['scene_id']!=recipe['scene_id']: raise ValueError('Blueprint belongs to a different arena')
    prefix=config['scene_id'].rsplit('/',1)[-1].removeprefix('route1-')
    kit=json.loads(args.source_kit.read_text())
    if kit['source']!=json.loads(scene['pilot_source_kit'])['source']:raise ValueError('Source kit and Blender library have different provenance')
    scene['pilot_source_kit']=json.dumps(kit)
    prototypes={p['id']:p for p in kit['objects']}
    templates={prototypes[o['phlosion_prototype_id']]['prefab_asset_id']:o.data
               for o in arena.collection('PAC_PREFABS').objects}
    # Keep the local template library alive while replacing its placements.
    for mesh in templates.values(): mesh.use_fake_user=True
    legacy=json.loads((ROOT/config['reference_scene']).read_text())
    overrides={n['id']:n['components']['transform'] for n in legacy['nodes'] if 'transform' in n['components']}
    def placement_transform(proto):
        x,_,z=proto['transform']['translation']
        return proto['transform'] if uses_source_layout(config,math.floor(x*.01),math.floor(z*.01)) else overrides.get(proto['id'],proto['transform'])
    composition=json.loads((ROOT/recipe['composition_path']).read_text())
    cells,_=recover_cells(kit,legacy,config)
    lookup={(c['x'],c['z']):c for c in cells}
    limits=config['tile_bounds']
    def contains(x,y,margin=0):
        return limits['x_min']+margin<=x<limits['x_max']+1-margin and -limits['z_max']-1+margin<=y<-limits['z_min']+1-margin
    def ground(x,y):
        cell=lookup.get((math.floor(x),math.floor(-y)))
        if cell is None: raise ValueError(f'Prop outside terrain: {x}, {y}')
        return tiles.tile_height(cell,x,y)
    board=json.loads((ROOT/recipe['board_path']).read_text())['board_registration']
    ox,oz=board['terrain_grid_origin']; cols,rows=board['board_cells']
    # Both reserve rows are part of the area that must remain free of solids.
    def intrudes(obj):
        points=[obj.matrix_world @ v.co for v in obj.data.vertices]
        return min(v.x for v in points)<ox+cols and max(v.x for v in points)>ox and min(v.y for v in points)<-oz+1 and max(v.y for v in points)>-oz-rows-1
    def overlaps_reserve(lo,hi):
        # Leave room for animated foliage bounds beyond the static preview.
        margin=.10
        return lo.x<ox+cols+margin and hi.x>ox-margin and any(lo.y<-z+margin and hi.y>-z-1-margin for z in (oz-1,oz+rows))
    for group in ('PAC_EDIT_PATCH','PAC_PREFABS','PAC_GUIDES'):
        for obj in list(arena.collection(group).objects): bpy.data.objects.remove(obj,do_unlink=True)
    old_center=json.loads(scene['pilot_blueprint'])['board_center_blender_m']
    scene['pilot_authoring_recipe']=args.recipe
    scene['pilot_game_root']=str(ROOT)
    scene['pilot_reference']=config['name']+'; source tile blueprint with approved overlapping arena terrain'
    scene['phlosion_patch_direction_precision']=64
    scene['phlosion_patch_stable_directions']=True
    config['tile_cells']=cells
    scene['pilot_blueprint']=json.dumps(config)
    tiles.create_guide(arena,cells)
    arena.rebuild_terrain(config)
    omitted=[]
    def place(proto,name=None,position=None,size=1,yaw=0,transform_override=None):
        transform=transform_override or placement_transform(proto)
        x,h,z=transform['translation']; x,y=(x*.01,-z*.01) if position is None else position
        grass=proto['id'].startswith('encounter-grass/')
        asset=proto['prefab_asset_id']
        mesh=bpy.data.meshes.new('Grass preview') if grass else templates[asset]
        obj=bpy.data.objects.new(name or 'Source '+proto['display_name'],mesh)
        arena.collection('PAC_PREFABS').objects.link(obj)
        sx,sy,sz=transform['scale']
        obj.scale=(sx*size,sz*size,sy*size)
        obj.rotation_euler.z=math.radians(transform['rotation_degrees'][1]+yaw)
        obj.location=(x,y,h*.01 if grass else ground(x,y)-min(v.co.z for v in mesh.vertices)*obj.scale.z)
        obj['phlosion_node_id']='authored-prefab/'+prefix+'/'+(proto['id'].replace('/','-') if name is None else name.lower().replace(' ','-'))
        obj['phlosion_prototype_id']=proto['id']
        obj['pilot_placement_kind']='brush_bed' if grass else 'source_plant'
        if grass:
            obj['pac_grass_asset_id']='route1/encounter_grass_01'
            arena_grass.rebuild_preview(obj,composition)
        bpy.context.view_layer.update()
        # small_grass_02 is a tall leafy shrub despite its asset name.
        solid = asset.startswith('route1/tree_') or (not grass and obj.dimensions.z > .65)
        if solid and intrudes(obj):
            omitted.append({'prototype':proto['id'],'reason':'canopy or solid shrub overlaps the board/reserve rows'})
            bpy.data.objects.remove(obj,do_unlink=True)
            return None
        if not grass and config.get('clear_reserve_foliage'):
            points=[obj.matrix_world @ v.co for v in obj.data.vertices]
            lo,hi=(Vector(fn(v[a] for v in points) for a in range(3)) for fn in (min,max))
            if overlaps_reserve(lo,hi):
                omitted.append({'prototype':proto['id'],'reason':'foliage overlaps a dirt reserve row'})
                bpy.data.objects.remove(obj,do_unlink=True)
                return None
        return obj
    for proto in kit['objects']:
        identity=proto['id']
        t=placement_transform(proto); x,_,z=t['translation']
        if not contains(x*.01,-z*.01,1): continue
        if identity.startswith('encounter-grass/'):
            if int(identity.rsplit('-',1)[-1]) in config['encounter_records']:
                custom=[p for p in config.get('grass_bed_placements',[]) if p['prototype_id']==identity]
                if not custom: place(proto)
                for p in custom:
                    place(proto,p['name'],transform_override=dict(t,translation=p['translation_cm'],scale=p['scale']))
        elif identity.startswith(('canonical-tree/','buildmodel-vegetation/')) and proto.get('prefab_asset_id') in templates:
            place(proto)
    # Reinforce the woodland border with the same source species and sizes.
    cx,cy=config['board_center_blender_m']
    woodland=[(13,cy-6,'tree_002'),(12.2,cy-2,'tree_001'),(13,cy+3,'tree_002'),
                                    (12.3,cy+7,'tree_001'),(29.4,cy-5,'tree_002'),(29.2,cy,'tree_001'),
                                    (30,cy+5,'tree_002'),(29.4,cy+10,'tree_001')]
    woodland.extend((p['x'],p['y'],p['family']) for p in config.get('extra_woodland',[]))
    for i,(x,y,family) in enumerate(woodland):
        if not contains(x,y,1):continue
        proto=next(p for p in kit['objects'] if p.get('prefab_asset_id')=='route1/'+family)
        place(proto,f'Woodland {family} {i+1:02}',(x,y),.78+(i%3)*.04,i*37)
    source={o.get('lgpe_source_mesh_index'):o for o in arena.collection('LGPE_SOURCE_LOCKED').all_objects if o.type=='MESH'}
    details=rocks=0
    for index in (2,3,4,9,16,18,20,22,23,24,26):
        mesh=source[index]; groups=defaultdict(list)
        for faces in components(mesh.data):
            lo,hi=bounds(mesh.data,faces); center=(lo+hi)*.5
            if not contains(center.x,center.y,2): continue
            rock=index==26; upright=index in (16,24)
            if index not in (2,3,4,9,26) and lookup[math.floor(center.x),math.floor(-center.y)]['surface']==1:continue
            if not rock and (max(hi.x-lo.x,hi.y-lo.y)>(1.5 if upright else .9) or hi.z-lo.z>(.8 if upright else .12)):continue
            if not all(contains(x,y) for x,y in ((lo.x,lo.y),(hi.x,hi.y))):continue
            if config.get('clear_reserve_foliage') and overlaps_reserve(lo,hi):continue
            if rock and lo.x<ox+cols and hi.x>ox and lo.y<-oz+1 and hi.y>-oz-rows-1:continue
            samples=[ground(x,y) for x,y in ((lo.x,lo.y),(hi.x,hi.y),(lo.x,hi.y),(hi.x,lo.y))]
            if max(samples)-min(samples)>.05:continue
            if rock:
                pivot=Vector((center.x,center.y,lo.z))
                detail(mesh,faces,f'Source rock {rocks:02}',pivot,Vector((center.x,center.y,ground(center.x,center.y)-.012)),prefix)
                rocks+=1;details+=1
            else: groups[math.floor(center.x),math.floor(center.y),round(lo.z,4)].extend(faces)
        for (x,y,base),faces in groups.items():
            pivot=Vector((x+.5,y+.5,base))
            detail(mesh,faces,f'Field detail {index} {x} {y} {details:03}',pivot,Vector((pivot.x,pivot.y,ground(pivot.x,pivot.y)+.006)),prefix)
            details+=1
    for obj in arena.collection('PREVIEW_ONLY').objects:
        obj.location.x+=cx-old_center[0]; obj.location.y+=cy-old_center[1]
    scene.camera.location.z+=2
    arena.register()
    output.parent.mkdir(parents=True,exist_ok=True)
    scene['arena_seed_library_sha256']=original_sha
    scene['arena_seed_blueprint']=args.blueprint
    bpy.ops.wm.save_as_mainfile(filepath=str(output))
    # Export through the normal launcher in a fresh Blender process. Seeding
    # leaves evaluation/tangent caches behind that can differ by one float ULP
    # from a clean load, producing an unnecessary first archive revision.
    if hashlib.sha256(original.read_bytes()).hexdigest()!=original_sha:raise AssertionError('Reference library changed')
    audit={'passed':True,'source_unchanged':True,'source_library':str(original),'source_sha256':original_sha,
           'cells':len(cells),'prefabs':len(arena.collection('PAC_PREFABS').objects),'detail_groups':details,'rocks':rocks,
           'board_origin':[ox,oz],'omitted_solids':omitted}
    (output.parent/'seed-audit.json').write_text(json.dumps(audit,indent=2)+'\n')
    print('ROUTE1_ARENA_CREATED '+json.dumps(audit))


if __name__=='__main__':main()
