"""Asset-independent checks for authored map data and scene drift detection."""
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent/'blender'))
from arena_map import build_map, validate_map, encounter_grass_centers


class ArenaMapTests(unittest.TestCase):
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
