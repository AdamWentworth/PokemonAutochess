"""A saved tile blueprint drives newly built terrain, independent of LGPE meshes."""
import json
import math
import bpy
from mathutils import Vector
import arena_terrain as terrain

GUIDE = 'Tile blueprint'
SURFACES = ('lawn', 'dirt', 'dark_lawn')
RAMPS = ('flat', 'north', 'east', 'south', 'west',
         'northeast_foot', 'northeast_crest', 'southeast_foot', 'southeast_crest',
         'southwest_foot', 'southwest_crest', 'northwest_foot', 'northwest_crest')


from arena_coordinates import FIELDS, blender_height as tile_height, surface_polygons


def read_cells():
    mesh = bpy.data.objects[GUIDE].data
    return [{key: mesh.attributes['PAC_'+key].data[face.index].value for key in FIELDS}
            for face in mesh.polygons]


def create_guide(bridge, cells):
    old = bpy.data.objects.get(GUIDE)
    if old: bpy.data.objects.remove(old, do_unlink=True)
    vertices, faces = [], []
    for cell in cells:
        x, y = cell['x'], -cell['z']-1
        faces.append(tuple(range(len(vertices), len(vertices)+4)))
        vertices.extend((px, py, tile_height(cell, px, py)+.035)
                        for px, py in ((x,y),(x+1,y),(x+1,y+1),(x,y+1)))
    mesh = bpy.data.meshes.new(GUIDE)
    mesh.from_pydata(vertices, [], faces)
    for key in FIELDS:
        bridge.attribute(mesh, 'PAC_'+key, 'INT', [c[key] for c in cells], 'FACE')
    obj = bpy.data.objects.new(GUIDE, mesh)
    bridge.collection('PAC_GUIDES').objects.link(obj)
    obj.display_type = 'WIRE'
    obj.show_in_front = True
    obj.hide_render = True
    obj.hide_set(True)
    obj['description'] = 'One face per 1 m tile; edit selected cells using the Autochess panel.'
    return obj


def ground(bridge, config, materials):
    cells = read_cells()
    lookup = {(c['x'],c['z']):c for c in cells}
    def edges(cell):
        x,y=cell['x'],-cell['z']-1
        return ((0,1,(x,y),(x+1,y)),(1,0,(x+1,y),(x+1,y+1)),
                (0,-1,(x+1,y+1),(x,y+1)),(-1,0,(x,y+1),(x,y)))
    dirt_edges=[]
    for c in cells:
        if c['surface']!=1: continue
        for dx,dz,a,b in edges(c):
            if lookup.get((c['x']+dx,c['z']+dz),{}).get('surface')!=1:
                dirt_edges.append((a,b))
    lawn,dirt=config['material_roles']['ground_uv2_lawn'],config['material_roles']['ground_uv2_dirt']
    vertices,faces,uvs,colors,mats=[],[],[],[],[]
    def vertex(px,py,z,uv2,color,uv1=None):
        vertices.append((px,py,z))
        uvs.append(((px/3,-py/3),uv1 or (px/2,-py/2),uv2,(0,0)))
        colors.append(color)
        return len(vertices)-1
    def paint(cell,px,py):
        distance=min((terrain.nearest_path(px,py,[a,b])[0] for a,b in dirt_edges),default=100)
        soil=terrain.clamp(.5+(distance if cell['surface']==1 else -distance)/.20)
        dark=1 if cell['surface']==2 else 0
        light=(.74,.84,.77)
        raised=(.180392161,.482352942,.431372553)
        grass=tuple(a+(b-a)*dark for a,b in zip(light,raised))
        return ((lawn[0],lawn[1]+soil*(dirt[1]-lawn[1])),
                tuple(a+(b-a)*soil for a,b in zip(grass,(.93,.90,.81)))+(1,))
    def cap(cell,points,height=None):
        if len(points)<3: return
        for polygon in surface_polygons(cell,points) if height is None else [points]:
            face=[]
            for px,py in polygon:
                uv,color=paint(cell,px,py)
                z=tile_height(cell,px,py) if height is None else (height(px,py) if callable(height) else height)
                face.append(vertex(px,py,z,uv,color))
            faces.append(tuple(face)); mats.append(19)

    # Clip both flat and ramp-adjacent corners. The removed pocket extends the
    # lower floor as a plane matching each incident edge, including its slope.
    radius=max(0,min(.24,config.get('ledge_corner_radius_m',.18)))
    ramp_radius=max(0,min(.30,config.get('ledge_ramp_corner_radius_m',radius)))
    rounded={}
    for cell in cells:
        if not radius: continue
        x,y=cell['x'],-cell['z']-1
        for dx,sy,angle in ((-1,-1,180),(1,-1,270),(1,1,0),(-1,1,90)):
            dz=-sy
            ns=[lookup.get((x+a,cell['z']+b)) for a,b in ((dx,0),(0,dz),(dx,dz))]
            if not all(ns): continue
            corner=Vector((x+(dx>0),y+(sy>0)))
            heights=[tile_height(n,*corner) for n in ns]
            if max(heights)-min(heights)>1e-6 or tile_height(cell,*corner)-heights[0]<.01: continue
            adjacent_ramp=bool(cell['ramp'] or any(n['ramp'] for n in ns))
            r=ramp_radius if adjacent_ramp else radius
            if not r: continue
            # Along X use the north/south neighbour; along Y use east/west.
            # Sample within their edge intervals to respect clamped ramp ends.
            gx=(tile_height(ns[1],corner.x-dx*r,corner.y)-heights[0])/(-dx*r)
            gy=(tile_height(ns[0],corner.x,corner.y-sy*r)-heights[0])/(-sy*r)
            plane=(heights[0]-gx*corner.x-gy*corner.y,gx,gy)
            center=corner-Vector((dx,sy))*r
            arc=[center+Vector((math.cos(math.radians(angle+i*15)),math.sin(math.radians(angle+i*15))))*r for i in range(7)]
            if any(tile_height(cell,*p)-(plane[0]+gx*p.x+gy*p.y)<.01 for p in arc): continue
            rounded[(x,cell['z'],dx,sy)]={'corner':corner,'center':center,'arc':arc,'lower':ns[0],
                'floor_plane':plane,'radius':r,'adjacent_ramp':adjacent_ramp}

    def wall(cell,p,q,bottoms,along=None):
        tops=[tile_height(cell,*r) for r in (p,q)]
        bottoms=[min(t,b) for t,b in zip(tops,bottoms)]
        if max(t-b for t,b in zip(tops,bottoms))<.01: return
        fringe=[min(.10,(t-b)*.5) for t,b in zip(tops,bottoms)]
        # One cross-section: earth ends where the grass fringe starts. No
        # overlapping shells or outward offset to make a self-shadowing seam.
        for rim in (False,True):
            i=len(vertices)
            for upper in (False,True):
                for n,(r,top,bottom,depth) in enumerate(zip((p,q),tops,bottoms,fringe)):
                    u=along[n] if along else (r.x if abs(q.x-p.x)>abs(q.y-p.y) else r.y)
                    if rim:
                        z=top if upper else top-depth
                        # The small-leaf strip has a neutral RGB background.
                        # The old blue-inked strip created a dark outline even
                        # after the geometric overlap was removed.
                        uv2=(u/1.6,1-90/1024 if upper else 1-128/1024)
                        color=paint(cell,*r)[1] if upper else (1,1,1,1)
                        v1=1 if upper else .57
                    else:
                        z=top-depth if upper else bottom
                        uv2=(-.05,.987889409)
                        color=(1,1,1,1) if upper else (.72,.73,.65,1)
                        v1=.57 if upper else .04
                    vertex(*r,z,uv2,color,(u/1.7,v1))
            for tri in ((i,i+1,i+3),(i,i+3,i+2)):
                v0,v1,v2=[Vector(vertices[t]) for t in tri]
                if (v1-v0).cross(v2-v0).length>2e-8:
                    faces.append(tri); mats.append(18)

    subdivisions=5
    for cell in cells:
        x,y=cell['x'],-cell['z']-1
        outline=[]
        corners={}
        for dx,sy in ((-1,-1),(1,-1),(1,1),(-1,1)):
            corner=rounded.get((x,cell['z'],dx,sy))
            p=Vector((x+(dx>0),y+(sy>0)))
            if corner:
                outline.extend(corner['arc']); corners[tuple(p)]=corner
            else: outline.append(p)
        for j in range(subdivisions):
            for i in range(subdivisions):
                polygon=terrain.clip_cell(outline,x+i/subdivisions,y+j/subdivisions,x+(i+1)/subdivisions,y+(j+1)/subdivisions)
                cap(cell,polygon)
        for corner in corners.values():
            arc=corner['arc']; lower=corner['lower']; intercept,gx,gy=corner['floor_plane']
            def floor(px,py): return intercept+gx*px+gy*py
            for index,(a,b) in enumerate(zip(arc,arc[1:])):
                cap(lower,[corner['corner'],b,a],floor)
                # A continuous arc-length UV avoids texture axis changes at 45°.
                along=[corner['radius']*math.pi/12*k for k in (index,index+1)]
                wall(cell,a,b,[floor(*a),floor(*b)],along)
        for dx,dz,a,b in edges(cell):
            neighbor=lookup.get((x+dx,cell['z']+dz))
            if neighbor is None: continue
            start=corners[a]['radius'] if a in corners else 0
            end=1-corners[b]['radius'] if b in corners else 1
            steps=sorted({start,end,*[k/subdivisions for k in range(1,subdivisions) if start<k/subdivisions<end]})
            for s,t in zip(steps,steps[1:]):
                p,q=Vector(a).lerp(Vector(b),s),Vector(a).lerp(Vector(b),t)
                wall(cell,p,q,[tile_height(neighbor,*r) for r in (p,q)])
    obj=terrain.create_mesh(bridge,'Tile-built Route 1 terrain','arena-pilot/terrain',vertices,faces,mats,uvs,colors,materials)
    obj['tile_blueprint_object']=GUIDE
    obj['ledge_corner_radius_m']=radius
    obj['ledge_ramp_corner_radius_m']=ramp_radius
    obj['rounded_ledge_corners']=json.dumps([{'cell':[x,z],'outward':[dx,sy],'center':list(c['center']),
        'radius':c['radius'],'top':tile_height(lookup[(x,z)],*c['corner']),
        'bottom':tile_height(c['lower'],*c['corner']),'floor_plane':c['floor_plane'],
        'adjacent_ramp':c['adjacent_ramp']} for (x,z,dx,sy),c in rounded.items()])
    obj['ledge_surface']='Continuous wall and grass fringe; shared top colour and coordinates'
    return obj

class PAC_OT_EditTiles(bpy.types.Operator):
    bl_idname = 'pac.edit_tiles'
    bl_label = 'Select Blueprint Tiles'
    def execute(self,context):
        if context.object and context.object.mode!='OBJECT': bpy.ops.object.mode_set(mode='OBJECT')
        bpy.ops.object.select_all(action='DESELECT')
        obj=bpy.data.objects[GUIDE]
        obj.hide_set(False); obj.select_set(True)
        context.view_layer.objects.active=obj
        context.tool_settings.mesh_select_mode=(False,False,True)
        bpy.ops.object.mode_set(mode='EDIT')
        bpy.ops.mesh.select_all(action='DESELECT')
        return {'FINISHED'}


class PAC_OT_ApplyTiles(bpy.types.Operator):
    bl_idname = 'pac.apply_tiles'
    bl_label = 'Apply to Selected Tiles'
    bl_options = {'REGISTER','UNDO'}
    def execute(self,context):
        import arena_pilot as bridge
        obj=context.active_object
        if not obj or obj.name!=GUIDE:
            self.report({'ERROR'},'Select blueprint tile faces first'); return {'CANCELLED'}
        was_edit=obj.mode=='EDIT'
        if was_edit: bpy.ops.object.mode_set(mode='OBJECT')
        selected=[f for f in obj.data.polygons if f.select]
        for face in selected:
            for key,value in (('height',context.scene.pac_tile_height),('surface',int(context.scene.pac_tile_surface)),('ramp',int(context.scene.pac_tile_ramp))):
                obj.data.attributes['PAC_'+key].data[face.index].value=value
        for face,cell in zip(obj.data.polygons,read_cells()):
            for vi in face.vertices:
                p=obj.data.vertices[vi].co
                p.z=tile_height(cell,p.x,p.y)+.035
        bridge.rebuild_terrain()
        context.view_layer.objects.active=obj
        if was_edit: bpy.ops.object.mode_set(mode='EDIT')
        self.report({'INFO'},f'Updated {len(selected)} tiles; prop positions retained')
        return {'FINISHED'}


def register():
    for cls in (PAC_OT_EditTiles,PAC_OT_ApplyTiles):
        old=getattr(bpy.types,cls.__name__,None)
        if old: bpy.utils.unregister_class(old)
        bpy.utils.register_class(cls)
    bpy.types.Scene.pac_tile_height=bpy.props.IntProperty(name='Height level (0.5 m)',default=0,min=0,max=8)
    bpy.types.Scene.pac_tile_surface=bpy.props.EnumProperty(name='Ground',items=[('0','Lawn',''),('1','Dirt',''),('2','Dark lawn','LGPE decorative banks and inaccessible areas')])
    bpy.types.Scene.pac_tile_ramp=bpy.props.EnumProperty(name='Shape',items=[
        ('0','Flat',''),('1','Rises north',''),('2','Rises east',''),('3','Rises south',''),('4','Rises west',''),
        *[(str(5+direction*2+part), f'{name} corner {label}',
           'One high corner; pair with a crest to turn a ramp' if part == 0 else 'Three high corners; joins the upper terrace')
          for direction,name in enumerate(('NE','SE','SW','NW')) for part,label in enumerate(('foot','crest'))]])
