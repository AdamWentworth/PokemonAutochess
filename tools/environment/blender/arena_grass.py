"""Full-size grass clumps inside an independently resized bed footprint."""
import math


ASSETS = ('route1/encounter_grass_01', 'route1/encounter_grass_02')


def layout_centers(centers, scale):
    """Mirror EncounterGrassLayout.h; cover still uses the original rectangles."""
    sx, sz = scale
    if not centers or any(not math.isfinite(s) or s <= 0 for s in scale):
        raise ValueError('Grass beds require finite positive dimensions')
    if abs(sx-1) < 1e-6 and abs(sz-1) < 1e-6:
        return list(centers)
    rects = [( (x-50)*sx, (z-50)*sz, (x+50)*sx, (z+50)*sz) for x,z in centers]
    def axis(index, size):
        if abs(size-1) < 1e-6:
            return sorted({p[index] for p in centers})
        lo = min(r[index] for r in rects)+50
        hi = max(r[index+2] for r in rects)-50
        if hi < lo-1e-4: return []
        count = math.ceil(max(0, hi-lo)/50)
        if count > 4096: raise ValueError('Grass bed exceeds the clump limit')
        return [lo+(hi-lo)*i/max(1,count) for i in range(count+1)]
    xs, zs = axis(0,sx), axis(1,sz)
    if len(xs)*len(zs) > 4096: raise ValueError('Grass bed exceeds the clump limit')
    def fits(x,z):
        lo, hi = x-50, x+50
        splits = sorted({lo,hi} | {v for r in rects for v in (r[0],r[2]) if lo < v < hi})
        for a,b in zip(splits,splits[1:]):
            mid = (a+b)/2
            intervals = sorted((r[1],r[3]) for r in rects if r[0] <= mid <= r[2])
            end = z-50
            for low,high in intervals:
                if low > end+1e-4: break
                end = max(end,high)
            if end < z+50-1e-4: return False
        return True
    result = [(x,z) for x in xs for z in zs if fits(x,z)]
    if not result: raise ValueError('Grass bed is too small for a full-size clump')
    return result


def rebuild_preview(obj, composition):
    """Only opted-in beds use this preview; original saved scenes stay intact."""
    import bpy
    import bmesh
    from mathutils import Matrix
    asset = obj.get('pac_grass_asset_id')
    if asset is None: return
    if asset not in ASSETS: raise ValueError(f'Unknown grass blade asset: {asset}')
    record = next(r for r in composition['encounter_grass']['records']
                  if f"encounter-grass/{r['model']}/record-{r['record_index']}" == obj['phlosion_prototype_id'])
    from arena_map import encounter_grass_centers
    centers = layout_centers(encounter_grass_centers(record['core_cells_source_xz']), (obj.scale.x,obj.scale.y))
    template = next((m for m in bpy.data.meshes if m.get('pac_grass_clump_asset') == asset), None)
    if template is None: raise ValueError(f'Missing saved grass clump template: {asset}')
    bm = bmesh.new()
    for x,z in centers:
        part = template.copy()
        part.transform(Matrix.Diagonal((1/obj.scale.x,1/obj.scale.y,1/obj.scale.z,1)) @
                       Matrix.Translation((x*.01,-z*.01,0)))
        bm.from_mesh(part)
        bpy.data.meshes.remove(part)
    mesh = bpy.data.meshes.new(obj.name+' blades')
    bm.to_mesh(mesh)
    bm.free()
    for material in template.materials: mesh.materials.append(material)
    old = obj.data
    obj.data = mesh
    if old.users == 0: bpy.data.meshes.remove(old)
    obj['pac_grass_clump_count'] = len(centers)
