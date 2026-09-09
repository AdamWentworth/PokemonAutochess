"""Edit and export saved tile arenas owned by Pokemon Autochess.

Historical composition recipes live outside the daily authoring workflow.
Existing pilot scene properties and IDs remain readable for saved .blend files.
"""
from __future__ import annotations
import argparse
import importlib.util
import json
import math
from pathlib import Path
import sys
import bpy

ROOT = Path(__file__).resolve().parent
if str(ROOT) not in sys.path: sys.path.insert(0, str(ROOT))
from arena_recipe import DEFAULT_RECIPE, load_recipe, export_name
from arena_coordinates import source_translation

def module(name, file):
    spec = importlib.util.spec_from_file_location(name, ROOT / file)
    result = importlib.util.module_from_spec(spec)
    sys.modules[name] = result
    spec.loader.exec_module(result)
    return result


def collection(name):
    result = bpy.data.collections.get(name)
    if result is None:
        result = bpy.data.collections.new(name)
        bpy.context.scene.collection.children.link(result)
    return result


def attribute(mesh, name, kind, values, domain='POINT'):
    a = mesh.attributes.new(name=name, type=kind, domain=domain)
    key = 'color' if kind == 'FLOAT_COLOR' else 'value'
    a.data.foreach_set(key, values)
    return a
def compact_patch(path):
    """Share identical complete vertices; UV, normal and material seams survive."""
    document=json.loads(path.read_text())
    def key(value):
        if isinstance(value,dict): return tuple((k,key(v)) for k,v in sorted(value.items()))
        if isinstance(value,list): return tuple(key(v) for v in value)
        return value
    for mesh in document['meshes']:
        vertices,lookup,remap=[],{},[]
        for vertex in mesh['vertices']:
            signature=key(vertex)
            if signature not in lookup:
                lookup[signature]=len(vertices)
                vertices.append(vertex)
            remap.append(lookup[signature])
        mesh['vertices']=vertices
        for group in mesh['material_groups']:
            group['indices']=[remap[i] for i in group['indices']]
    path.write_text(json.dumps(document,separators=(',',':'))+'\n')
    return sum(len(m['vertices']) for m in document['meshes'])


def export(output, recipe_path=None, save_source=False):
    scene=bpy.context.scene
    game_root = ROOT.parents[2]
    recipe_path = recipe_path or scene.get('pilot_authoring_recipe', DEFAULT_RECIPE)
    recipe = load_recipe(game_root, recipe_path)
    if scene.get('pilot_schema')!=1: raise RuntimeError('Not an arena authoring scene')
    config=json.loads(scene['pilot_blueprint'])
    if config.get('terrain_style') != 'tile_blueprint':
        raise RuntimeError('Use the historical authoring bridge to export a freeform pilot')
    kit=json.loads(scene['pilot_source_kit'])
    composition=json.loads((game_root/recipe['composition_path']).read_text())
    output.mkdir(parents=True,exist_ok=True)
    tiles=module('arena_tiles','arena_tiles.py').read_cells()
    (output/'tile-layout.json').write_text(json.dumps({'tile_size_m':1,'elevation_step_m':.5,'cells':tiles},indent=2)+'\n')
    patch_path=output/'terrain.patch.json'
    module('pilot_patch_exporter','export_environment_patch.py').export_patch(patch_path)
    exported_vertices=compact_patch(patch_path)
    base={'kind':'phlosion_authored_scene','schema_version':8,'scene_id':recipe['scene_id'],
          'coordinate_system':kit['source']['coordinate_system'],'base_environment_asset_id':recipe['base_environment_asset_id'],'nodes':[]}
    nodes=base['nodes']
    def folder(id,name,parent=''):
        nodes.append({'id':id,'display_name':name,'parent_id':parent,'sibling_order':len(nodes),'enabled':True,'components':{}})
    folder('folder/environment','Environment')
    folder('folder/environment/source','Hidden source reference','folder/environment')
    folder('folder/environment/terrain','Terrain','folder/environment')
    folder('folder/environment/props','Trees and Props','folder/environment')
    prototypes={o['id']:o for o in kit['objects']}
    for p in kit['objects']:
        if p['imported_source_binding']['target_kind']=='gameplay_board_ground_prototype': continue
        nodes.append({'id':p['id'],'display_name':p['display_name'],'parent_id':'folder/environment/source',
            'sibling_order':len(nodes),'enabled':False,'reason':'Replaced by the independently authored arena',
            'components':{'transform':p['transform'],'imported_source_binding':p['imported_source_binding']}})
    ids=set()
    grass_preview=module('arena_grass', 'arena_grass.py')
    for obj in sorted(collection('PAC_PREFABS').objects,key=lambda o:o.name):
        grass_preview.rebuild_preview(obj, composition)
        id=obj.get('phlosion_node_id')
        if not id: raise RuntimeError(f'Missing prefab ID: {obj.name}')
        if id in ids:
            # Blender duplicates custom properties with Shift-D. Assign the
            # new copy its own durable identity on its first export.
            import uuid
            id='authored-prefab/pilot/'+uuid.uuid4().hex
            obj['phlosion_node_id']=id
        ids.add(id)
        proto=prototypes[obj['phlosion_prototype_id']]
        loc,rot,scale=obj.matrix_world.decompose()
        angles=rot.to_euler('XYZ')
        if abs(angles.x)>1e-5 or abs(angles.y)>1e-5:
            raise RuntimeError(f'Pilot prefab {obj.name}: use upright rotation about Z')
        transform={'translation':source_translation(loc),
            'rotation_degrees':[0,math.degrees(angles.z),0],'scale':[scale.x,scale.z,scale.y]}
        nodes.append({'id':id,'display_name':obj.name,'parent_id':'folder/environment/props','sibling_order':len(nodes),
            'enabled':not obj.hide_render,'components':{'transform':transform,'prefab_instance':{
                'prototype_node_id':proto['id'],'prefab_asset_id':obj.get('pac_grass_asset_id',proto['prefab_asset_id']),
                'creation_transform':proto['transform']}}})
    nodes.append({'id':recipe['terrain_node_id'],'display_name':config.get('name','Garden Clearing')+' Terrain',
        'parent_id':'folder/environment/terrain','sibling_order':0,'enabled':True,
        'components':{'transform':{'translation':[0,0,0],'rotation_degrees':[0,0,0],'scale':[1,1,1]},
            'mesh_patch':{'asset_path':recipe['terrain_path']}}})
    scene_path=output/export_name(recipe, 'scene_path')
    scene_path.write_text(json.dumps(base,indent=2)+'\n')
    arena_map = module('arena_map', 'arena_map.py')
    gameplay = arena_map.build_map(tiles, base,
        json.loads((game_root/recipe['board_path']).read_text()),
        composition)
    (output/'arena-map.json').write_text(json.dumps(gameplay,indent=2)+'\n')
    report={'kind':'arena_pilot_export','scene_id':recipe['scene_id'],'prefab_count':len(ids),
        'source_visible_count':0,'terrain_vertices':sum(len(o.data.vertices) for o in collection('PAC_EDIT_PATCH').objects),
        'exported_vertices':exported_vertices,
        'source_blend':bpy.data.filepath,'scene_output':str(scene_path),'patch_output':str(patch_path)}
    import hashlib
    if save_source:
        # Includes durable IDs assigned to newly duplicated props above.
        bpy.ops.wm.save_as_mainfile(filepath=bpy.data.filepath)
    report['source_blend_sha256'] = hashlib.sha256(Path(bpy.data.filepath).read_bytes()).hexdigest()
    report['authoring_recipe_sha256'] = arena_map.digest(recipe)
    report['export_files_sha256'] = {name: hashlib.sha256((output/name).read_bytes()).hexdigest() for name in
        (scene_path.name, 'arena-map.json', 'terrain.patch.json', 'tile-layout.json')}
    (output/'export-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print('ARENA_PILOT_EXPORT_PASS '+json.dumps(report))


class PAC_OT_ExportArena(bpy.types.Operator):
    bl_idname='pac.export_arena'
    bl_label='Save, Export and Update Game'
    def execute(self,context):
        try:
            context.scene.pac_export_directory=str(Path(bpy.data.filepath).parent/'export')
            export(Path(context.scene.pac_export_directory), save_source=True)
            import subprocess
            game_root=Path(context.scene['pilot_game_root'])
            result=subprocess.run(['powershell.exe','-NoProfile','-File',str(game_root/'tools/environment/export_route1_pilot.ps1'),
                '-BlendFile',bpy.data.filepath,'-Recipe',context.scene.get('pilot_authoring_recipe', DEFAULT_RECIPE),'-UseExistingExport'],capture_output=True,text=True,
                creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            (Path(context.scene.pac_export_directory)/'install.log').write_text(result.stdout+'\n'+result.stderr)
            if result.returncode: raise RuntimeError('Game install failed; inspect export/install.log')
        except Exception as exc:
            self.report({'ERROR'},str(exc))
            return {'CANCELLED'}
        self.report({'INFO'},'Arena installed. Reopen the game preview to load your edits.')
        return {'FINISHED'}


def rebuild_terrain(config=None):
    config = config or json.loads(bpy.context.scene['pilot_blueprint'])
    if config.get('terrain_style') != 'tile_blueprint':
        raise RuntimeError('This authoring tool requires a tile blueprint. Use the archived recipe for older freeform pilots.')
    old = next((o for o in collection('PAC_EDIT_PATCH').objects if o.get('phlosion_patch_id') == 'arena-pilot/terrain'), None)
    if old:
        mesh = old.data
        bpy.data.objects.remove(old, do_unlink=True)
        if mesh.users == 0: bpy.data.meshes.remove(mesh)
    materials = {int(m['lgpe_material_index']): m for m in bpy.data.materials if 'lgpe_material_index' in m}
    module('arena_tiles', 'arena_tiles.py').ground(sys.modules[__name__], config, materials)
    bpy.context.scene['pilot_blueprint'] = json.dumps(config)

class PAC_PT_Arena(bpy.types.Panel):
    bl_label = 'Arena Authoring'
    bl_idname = 'PAC_PT_arena'
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = 'Autochess'
    @classmethod
    def poll(cls, context): return context.scene.get('pilot_schema') == 1
    def draw(self, context):
        ui = self.layout
        ui.label(text='Move props: G | Rotate: R, Z | Copy: Shift D')
        ui.label(text='1 m cells | 0.5 m height steps')
        ui.operator('pac.edit_tiles')
        ui.prop(context.scene, 'pac_tile_height')
        ui.prop(context.scene, 'pac_tile_surface')
        ui.prop(context.scene, 'pac_tile_ramp')
        ui.operator('pac.apply_tiles')
        ui.separator()
        ui.label(text='Applying replaces the generated ground mesh.')
        ui.label(text='Export alone preserves manual mesh edits.')
        ui.operator('pac.export_arena')

def register():
    for name in ['PAC_OT_RebuildTerrain', 'PAC_OT_RebuildLedge', 'PAC_OT_ExportArena', 'PAC_PT_Arena']:
        old = getattr(bpy.types, name, None)
        if old: bpy.utils.unregister_class(old)
    for cls in [PAC_OT_ExportArena, PAC_PT_Arena]: bpy.utils.register_class(cls)
    bpy.types.Scene.pac_export_directory = bpy.props.StringProperty(name='Export folder', subtype='DIR_PATH')
    module('arena_tiles', 'arena_tiles.py').register()
    bpy.context.scene.pac_export_directory = str(Path(bpy.data.filepath).parent / 'export')
    config = json.loads(bpy.context.scene['pilot_blueprint'])
    recipe = load_recipe(ROOT.parents[2], bpy.context.scene.get('pilot_authoring_recipe', DEFAULT_RECIPE))
    board = json.loads((ROOT.parents[2]/recipe['board_path']).read_text())['board_registration']
    ox, oz = board['terrain_grid_origin']
    cols, rows = board['board_cells']
    cx, cy = ox+cols/2, -(oz+rows/2)
    if bpy.context.screen:
        for area in bpy.context.screen.areas:
            if area.type != 'VIEW_3D': continue
            space = area.spaces.active
            space.show_region_ui = True
            space.region_3d.view_location = (cx, cy, 0)
            space.region_3d.view_distance = 27
            space.region_3d.view_rotation = bpy.context.scene.camera.rotation_euler.to_quaternion()
            space.region_3d.view_perspective = 'ORTHO'

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('mode', choices=['export', 'ui'])
    parser.add_argument('--output', type=Path)
    parser.add_argument('--game-root', type=Path)
    parser.add_argument('--recipe', default=DEFAULT_RECIPE)
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
    bpy.context.scene['pilot_authoring_recipe'] = args.recipe
    if args.mode == 'ui':
        if args.game_root: bpy.context.scene['pilot_game_root'] = str(args.game_root)
        register()
        return
    if not args.output: raise RuntimeError('--output required')
    export(args.output, save_source=True)

if __name__ == '__main__': main()
