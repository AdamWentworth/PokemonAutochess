"""Geometry helpers used by the maintained tile terrain generator."""
import math
import bpy
from mathutils import Vector
from mathutils.geometry import tessellate_polygon

def nearest_path(x,y,points):
    distance,along,travel=float('inf'),0,0
    for (ax,ay),(bx,by) in zip(points,points[1:]):
        dx,dy=bx-ax,by-ay
        length=math.hypot(dx,dy)
        if length<1e-6: continue
        t=max(0,min(1,((x-ax)*dx+(y-ay)*dy)/(length*length)))
        candidate=math.hypot(x-ax-t*dx,y-ay-t*dy)
        if candidate<distance: distance,along=candidate,travel+t*length
        travel+=length
    return distance,along


def clamp(v): return max(0,min(1,v))


def clip_cell(outline,x0,y0,x1,y1):
    result=[(p.x,p.y) for p in outline]
    for axis,bound,sign in ((0,x0,1),(0,x1,-1),(1,y0,1),(1,y1,-1)):
        previous=result
        result=[]
        if not previous: break
        for a,b in zip(previous,previous[1:]+previous[:1]):
            a_inside=(a[axis]-bound)*sign>=0
            b_inside=(b[axis]-bound)*sign>=0
            if a_inside: result.append(a)
            if a_inside!=b_inside:
                t=(bound-a[axis])/(b[axis]-a[axis])
                result.append((a[0]+(b[0]-a[0])*t,a[1]+(b[1]-a[1])*t))
    # Clipping on an existing corner can repeat that vertex.
    clean=[]
    for p in result:
        if not clean or math.dist(p,clean[-1])>1e-6: clean.append(p)
    if len(clean)>1 and math.dist(clean[0],clean[-1])<1e-6: clean.pop()
    return clean


def create_mesh(bridge,name,identity,vertices,faces,material_ids,uvs,colors,materials):
    # The runtime tangent exporter accepts triangles and quads. Cell clipping
    # and capped posts can produce larger polygons; triangulate those here.
    prepared_faces,prepared_materials=[],[]
    for face,material in zip(faces,material_ids):
        if len(face)<=4:
            prepared_faces.append(face); prepared_materials.append(material)
            continue
        for tri in tessellate_polygon([[Vector(vertices[i]) for i in face]]):
            indices=tuple(face[p] if isinstance(p,int) else min(face,key=lambda i:(Vector(vertices[i])-p).length_squared) for p in tri)
            a,b,c=[Vector(vertices[i]) for i in indices]
            if (b-a).cross(c-a).length<=2e-8: continue
            prepared_faces.append(indices); prepared_materials.append(material)
    faces,material_ids=prepared_faces,prepared_materials
    old=next((o for o in bridge.collection('PAC_EDIT_PATCH').objects if o.get('phlosion_patch_id')==identity),None)
    if old:
        previous=old.data
        bpy.data.objects.remove(old,do_unlink=True)
        if not previous.users: bpy.data.meshes.remove(previous)
    mesh=bpy.data.meshes.new(name)
    mesh.from_pydata(vertices,[],faces)
    mesh.update()
    ids=sorted(set(material_ids))
    for index in ids: mesh.materials.append(materials[index])
    for p,index in zip(mesh.polygons,material_ids): p.material_index=ids.index(index)
    bridge.attribute(mesh,'LGPE_MATERIAL_INDEX','INT',material_ids,'FACE')
    bridge.attribute(mesh,'LGPE_SOURCE_VERTEX_INDEX','INT',[-1]*len(vertices))
    bridge.attribute(mesh,'LGPE_NORMAL_W','FLOAT',[1]*len(vertices))
    for index in range(4):
        bridge.attribute(mesh,f'LGPE_JOINT_{index}','INT',[0]*len(vertices))
        bridge.attribute(mesh,f'LGPE_WEIGHT_{index}','FLOAT',[0]*len(vertices))
        layer=mesh.uv_layers.new(name=f'LGPE_UV{index}')
        layer.data.foreach_set('uv',[v for loop in mesh.loops for v in uvs[loop.vertex_index][index]])
        values=colors if index==0 else [(1,1,1,1)]*len(vertices)
        bridge.attribute(mesh,f'LGPE_COLOR{index}','FLOAT_COLOR',[v for c in values for v in c])
    for preview_name,index in [('LGPE_PreviewUV0',1),('LGPE_PreviewUV1',2)]:
        layer=mesh.uv_layers.new(name=preview_name)
        layer.data.foreach_set('uv',[v for loop in mesh.loops for v in (uvs[loop.vertex_index][index][0],1-uvs[loop.vertex_index][index][1])])
    bridge.attribute(mesh,'LGPE_VertexColor','FLOAT_COLOR',[
        (v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4) if k<3 else v
        for c in colors for k,v in enumerate(c)])
    obj=bpy.data.objects.new(name,mesh)
    bridge.collection('PAC_EDIT_PATCH').objects.link(obj)
    obj['phlosion_patch_id']=identity
    obj['phlosion_display_name']=name
    obj['lgpe_source_mesh_index']=-1
    return obj
