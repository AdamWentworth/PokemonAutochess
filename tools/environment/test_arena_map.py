"""Asset-independent checks for authored map data and scene drift detection."""
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent/'blender'))
from arena_map import build_map, validate_map


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

    def test_missing_duplicate_and_stale_data_fail(self):
        with self.assertRaises(ValueError): self.build(cells=self.document['cells']*2)
        with self.assertRaises(ValueError): self.build(cells=[c for c in self.document['cells'] if (c['x'],c['z']) != (17,-10)])
        stale = copy.deepcopy(self.document)
        stale['connections'][0]['edge_height_delta_cm'] = [123, 123]
        with self.assertRaises(ValueError): validate_map(stale, self.scene, self.board, self.composition)


if __name__ == '__main__': unittest.main()
