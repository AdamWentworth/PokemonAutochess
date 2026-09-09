"""Asset-independent checks for authored map data and scene drift detection."""
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent/'blender'))
from arena_map import build_map, validate_map, encounter_grass_centers
from arena_coordinates import source_direction_float64, height_cm, blender_height, surface_polygons
from arena_grass import layout_centers


class ArenaMapTests(unittest.TestCase):
    def test_grass_style_preserves_cover_and_resized_clumps_keep_their_size(self):
        scene = copy.deepcopy(self.scene)
        for node in scene['nodes']:
            prefab = node.get('components',{}).get('prefab_instance',{})
            if prefab.get('prototype_node_id','').startswith('encounter-grass/'):
                prefab['prefab_asset_id'] = ('route1/encounter_grass_02' if prefab['prefab_asset_id'] == 'route1/encounter_grass_01'
                                            else 'route1/encounter_grass_01')
        self.assertEqual(self.build(scene=scene)['cover_regions'],self.document['cover_regions'])
        core = [(-1,-1),(-1,0),(-1,1),(0,-1),(0,0),(0,1)]
        original = encounter_grass_centers(core)
        self.assertEqual(layout_centers(original,(1,1)),original)
        packed = layout_centers(original,(1,.5))
        self.assertEqual(packed,[(x,z) for x in (-100,-50,50,100) for z in (-25,25,75)])
        # Twelve 1 m modules cover the original 3 m by 2 m threshold.
        self.assertEqual([min(x for x,z in packed)-50,max(x for x,z in packed)+50,
                          min(z for x,z in packed)-50,max(z for x,z in packed)+50],[-150,150,-75,125])
        with self.assertRaises(ValueError): layout_centers(original,(.01,.01))
        with self.assertRaises(ValueError): layout_centers(original,(1,float('nan')))
        # Resizing a disconnected footprint must not fill the opening.
        separated = [(-200,0),(200,0)]
        self.assertEqual(layout_centers(separated,(2,1)),[(-450,0),(-400,0),(-350,0),(350,0),(400,0),(450,0)])

    def test_corner_ramp_heights_and_planar_caps(self):
        # Expected high corners in source order NW, NE, SE, SW. A foot rises
        # from one triangular half; a crest completes the other half uphill.
        corners = ((0,0), (1,0), (1,1), (0,1))
        for direction in range(4):
            for part in range(2):
                cell = dict(x=22,z=-22,height=3,surface=0,ramp=5+2*direction+part)
                high = (direction+1) % 4
                expected = [200 if i == high else 150 for i in range(4)] if not part else [150 if i == (high+2)%4 else 200 for i in range(4)]
                self.assertEqual([height_cm(cell,22+u,-22+v) for u,v in corners], expected)
                self.assertEqual(height_cm(cell,22.5,-21.5),150 if not part else 200)
                # A clipped sub-cell straddling the fold must become two planar
                # pieces with the same area, including away from the grid centre.
                points = [(22.35,21.4),(22.6,21.4),(22.6,21.65),(22.35,21.65)]
                pieces = surface_polygons(cell,points)
                self.assertEqual(len(pieces),2)
                area = 0
                for poly in pieces:
                    area += abs(sum(a[0]*b[1]-b[0]*a[1] for a,b in zip(poly,poly[1:]+poly[:1])))/2
                    h = [blender_height(cell,*p) for p in poly]
                    mid = [sum(p[i] for p in poly)/len(poly) for i in (0,1)]
                    self.assertAlmostEqual(blender_height(cell,*mid),sum(h)/len(h),places=7)
                self.assertAlmostEqual(area,.25*.25,places=7)
        cell['ramp'] = 1
        self.assertEqual(surface_polygons(cell,points),[points])

    def test_corner_ramp_range_is_validated(self):
        for ramp in (5,6,7,8,9,10,11,12):
            cells = copy.deepcopy(self.document['cells'])
            cells[0]['ramp'] = ramp
            self.build(cells=cells)
        cells[0]['ramp'] = 13
        with self.assertRaises(ValueError): self.build(cells=cells)

    def test_direction_normalization_has_stable_runtime_bytes(self):
        expected = source_direction_float64([0, 2, 1])
        # Actual equivalent 1:2 bitangents exposed by the clearing ramp export.
        for y,z in ((0.8944271802902222,0.4472135901451111),
                    (0.8944272398948669,0.44721361994743347)):
            self.assertEqual(source_direction_float64([0,y,z]),expected)
        self.assertEqual(source_direction_float64([0,0,3]),[0,1,0])
        self.assertEqual(source_direction_float64([0,3,0]),[0,0,-1])
        for value in ([0,0,0],[float('inf'),0,0],[0,float('nan'),0]):
            with self.assertRaises(ValueError): source_direction_float64(value)

    @classmethod
    def setUpClass(cls):
        read = lambda name: json.loads((ROOT/name).read_text())
        cls.scene = read('scenes/route1_pilot.scene.json')
        cls.board = read('config/environment/route1_pilot_board_layout.json')
        cls.composition = read('config/environment/route1_environment_package.json')
        cls.document = read('config/environment/route1_pilot_gameplay.json')

    def build(self, cells=None, scene=None):
        return build_map(cells if cells is not None else self.document['cells'],
                         scene if scene is not None else self.scene, self.board, self.composition)

    def test_registration_and_directed_height_changes(self):
        result = self.build()
        self.assertEqual(result, self.document)
        self.assertEqual(len(result['playable_cells']), 64)
        self.assertEqual(len(result['reserve_cells']), 16)
        edges = {(tuple(e['from']), tuple(e['to'])): e['edge_height_delta_cm'] for e in result['connections']}
        self.assertEqual(edges[((21, -8), (21, -9))], [50, 50])
        self.assertEqual(edges[((21, -9), (21, -8))], [-50, -50])
        self.assertEqual(edges[((17, -8), (17, -9))], [0, 0])

    def test_grass_moves_with_its_node_and_hidden_grass_is_excluded(self):
        scene = copy.deepcopy(self.scene)
        region = self.document['cover_regions'][0]
        node = next(n for n in scene['nodes'] if n['id'] == region['node_id'])
        node['components']['transform']['translation'][0] += 100
        moved = next(r for r in self.build(scene=scene)['cover_regions'] if r['id'] == region['id'])
        for before, after in zip(region['polygons_source_xz_cm'], moved['polygons_source_xz_cm']):
            for a, b in zip(before, after): self.assertEqual(b, [a[0]+100, a[1]])
        node['enabled'] = False
        self.assertEqual(len(self.build(scene=scene)['cover_regions']), 3)

    def test_paint_does_not_define_playability(self):
        cells = copy.deepcopy(self.document['cells'])
        for cell in cells: cell['surface'] = 2
        result = self.build(cells=cells)
        self.assertEqual(result['playable_cells'], self.document['playable_cells'])
        self.assertEqual(result['connections'], self.document['connections'])

    def test_rendered_grass_centers_include_half_cell_origin_and_border(self):
        self.assertEqual(encounter_grass_centers([[0, 0]]),
                         [(x, z) for x in (0, 50, 100) for z in (0, 50, 100)])
        with self.assertRaises(ValueError): encounter_grass_centers([])

    def test_grass_test_south_edge_retains_cover(self):
        east = next(r for r in self.build()['cover_regions'] if 'east-hooked' in r['id'])
        def covered(x, z):
            return any(min(p[0] for p in polygon) <= x <= max(p[0] for p in polygon) and
                       min(p[1] for p in polygon) <= z <= max(p[1] for p in polygon)
                       for polygon in east['polygons_source_xz_cm'])
        self.assertTrue(covered(2450, -450))  # Rattata at (7, 5), still in dense grass.
        self.assertFalse(covered(2450, -350))  # (7, 6), clear of the grass.
        self.assertFalse(covered(2150, -450))  # Bulbasaur's open central lane.

    def test_missing_duplicate_and_stale_data_fail(self):
        with self.assertRaises(ValueError): self.build(cells=self.document['cells']*2)
        with self.assertRaises(ValueError): self.build(cells=[c for c in self.document['cells'] if (c['x'],c['z']) != (17,-10)])
        stale = copy.deepcopy(self.document)
        stale['connections'][0]['edge_height_delta_cm'] = [123, 123]
        with self.assertRaises(ValueError): validate_map(stale, self.scene, self.board, self.composition)

    def test_single_reserve_side_uses_runtime_coordinates(self):
        for side in ('north', 'south'):
            board = copy.deepcopy(self.board)
            registration = board['board_registration']
            registration['bench_sides'] = [side]
            result = build_map(self.document['cells'], self.scene, board, self.composition)
            expected_z = registration['terrain_grid_origin'][1] + (registration['board_cells'][1] if side == 'north' else -1)
            self.assertEqual({p[1] for p in result['reserve_cells']}, {expected_z})


if __name__ == '__main__': unittest.main()
