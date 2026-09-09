"""Ray-check actual ledge meshes, including joins near every tile corner."""
import argparse
import json
import math
from pathlib import Path
import sys
import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--bridge-root',type=Path,default=Path(__file__).resolve().parent/'blender')
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args(sys.argv[sys.argv.index('--')+1:])
    sys.path.insert(0,str(args.bridge_root))
    import arena_tiles as tiles
    cells=tiles.read_cells()
    lookup={(c['x'],c['z']):c for c in cells}
    obj=next(o for o in bpy.data.collections['PAC_EDIT_PATCH'].objects if o.get('phlosion_patch_id')=='arena-pilot/terrain')
    mesh=obj.data
    material=mesh.attributes['LGPE_MATERIAL_INDEX']
    tree=BVHTree.FromPolygons([v.co for v in mesh.vertices],
                             [list(f.vertices) for f in mesh.polygons if material.data[f.index].value==18])
    ground_tree=BVHTree.FromPolygons([v.co for v in mesh.vertices],
                             [list(f.vertices) for f in mesh.polygons if material.data[f.index].value==19])
    corners=json.loads(obj.get('rounded_ledge_corners','[]'))
    checked=0
    errors=[]
    def lower_floor(corner,point):
        plane=corner.get('floor_plane',(corner['bottom'],0,0))
        return plane[0]+plane[1]*point.x+plane[2]*point.y
    for cell in cells:
        x,y=cell['x'],-cell['z']-1
        for dx,dz,a,b in ((0,1,(x,y),(x+1,y)),(1,0,(x+1,y),(x+1,y+1)),
                          (0,-1,(x+1,y+1),(x,y+1)),(-1,0,(x,y+1),(x,y))):
            neighbor=lookup.get((x+dx,cell['z']+dz))
            if not neighbor: continue
            outward=Vector((dx,-dz,0))
            for t in (.01,.25,.5,.75,.99):
                p=Vector(a).lerp(Vector(b),t)
                top=tiles.tile_height(cell,*p)
                bottom=tiles.tile_height(neighbor,*p)
                if top-bottom<.06: continue
                for height_fraction in (.05,.5,.95):
                    target=Vector((p.x,p.y,bottom+(top-bottom)*height_fraction))
                    expected=target.copy()
                    expected_normal=outward.copy()
                    rounded_corner=None
                    for corner in corners:
                        if corner['cell']!=[x,cell['z']]: continue
                        center=Vector(corner['center']); radius=corner['radius']
                        axis=1 if dx else 0
                        tangent=p[axis]-center[axis]
                        radial=Vector(corner['outward'])
                        if radial[axis]*tangent<=0 or abs(tangent)>=radius: continue
                        if (dx and radial.x!=dx) or (dz and radial.y!=-dz): continue
                        normal_axis=1-axis
                        expected[normal_axis]=center[normal_axis]+radial[normal_axis]*math.sqrt(radius*radius-tangent*tangent)
                        expected_normal=Vector((expected.x-center.x,expected.y-center.y,0)).normalized()
                        rounded_corner=corner
                    if rounded_corner:
                        bottom=lower_floor(rounded_corner,expected)
                        top=tiles.tile_height(cell,expected.x,expected.y)
                        expected.z=target.z=bottom+(top-bottom)*height_fraction
                    location,normal,face,distance=tree.ray_cast(target+outward*.12,-outward,.45)
                    checked+=1
                    if location is None or (location-expected).length>.006 or normal.dot(expected_normal)<.98:
                        errors.append({'cell':[x,cell['z']],'edge':[dx,dz],'along':t,'height':height_fraction,
                                       'hit':list(location) if location else None})
    corner_samples=0
    corner_wall_samples=0
    for corner in corners:
        center=Vector(corner['center']); direction=Vector(corner['outward']).normalized()
        cell=lookup[tuple(corner['cell'])]
        # The clipped pocket has a lower floor; the inside of the arc retains
        # its upper cap. Both must remain closed after rounding.
        for radius,upper in ((corner['radius']*.85,True),(corner['radius']*1.32,False)):
            point=center+direction*radius
            height=tiles.tile_height(cell,*point) if upper else lower_floor(corner,point)
            hit,_,_,_=ground_tree.ray_cast(Vector((*point,corner['top']+1)),Vector((0,0,-1)),5)
            corner_samples+=1
            assert hit is not None and abs(hit.z-height)<.001, (corner,point,hit,height)
        angle=math.atan2(direction.y,direction.x)-math.pi/4
        # Probe the centre of every curved wall segment through all its levels,
        # including the varying lower floor where a ramp meets the ledge.
        for segment in range(6):
            theta=angle+(segment+.5)*math.pi/12
            radial=Vector((math.cos(theta),math.sin(theta),0))
            point=Vector((*center,0))+radial*corner['radius']*math.cos(math.pi/24)
            bottom=lower_floor(corner,point); top=tiles.tile_height(cell,point.x,point.y)
            for fraction in (.05,.5,.95):
                target=Vector((point.x,point.y,bottom+(top-bottom)*fraction))
                hit,normal,_,_=tree.ray_cast(target+radial*.1,-radial,.2)
                corner_wall_samples+=1
                if hit is None or (hit-target).length>.002 or normal.dot(radial)<.995:
                    errors.append({'corner':corner['cell'],'segment':segment,'height':fraction,'hit':list(hit) if hit else None})
    rocks=0
    for o in bpy.data.collections['PAC_EDIT_PATCH'].objects:
        if o.get('source_reference_mesh')!=26: continue
        m=o.data
        preview=m.color_attributes['LGPE_VertexColor']
        assert preview.domain=='POINT'
        for raw,paint in zip(m.color_attributes['LGPE_COLOR0'].data,preview.data):
            for a,b in zip(raw.color[:3],paint.color[:3]):
                expected=a/12.92 if a<=.04045 else ((a+.055)/1.055)**2.4
                assert abs(expected-b)<1e-6
        rocks+=1
    report={'passed':not errors,'wall_ray_samples':checked+corner_wall_samples,'straight_edge_ray_samples':checked,
            'curved_wall_ray_samples':corner_wall_samples,'missing_or_offset_wall_samples':len(errors),
            'rounded_corners':len(corners),'rounded_cap_and_floor_samples':corner_samples,
            'rounded_ramp_junctions':sum(c.get('adjacent_ramp',False) for c in corners),
            'source_coloured_rocks':rocks,'first_errors':errors[:12]}
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print('LEDGE_GEOMETRY '+json.dumps(report))
    assert checked>100 and not errors and rocks==6


if __name__=='__main__': main()
