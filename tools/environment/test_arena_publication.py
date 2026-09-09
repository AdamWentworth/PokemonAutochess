"""Asset-free publication failure tests; native PHSC validation has separate tests."""
import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from publish_arena import publish, publication_lock, sha, read
from blender.arena_recipe import load_recipe, export_name
from blender.arena_map import build_map, digest

ROOT = Path(__file__).resolve().parents[2]
RECIPE = 'config/environment/route1_south_entrance.authoring.json'


def cook_fixture(stage, recipe, candidate):
    # Transaction tests isolate installation from the native codec. Native
    # tests independently reject corrupt PHSC bytes and map/mesh mismatches.
    terrain = stage/recipe['terrain_path']
    terrain.parent.mkdir(parents=True, exist_ok=True)
    terrain.write_bytes(b'new-terrain')
    candidate.write_bytes(b'complete-new-arena')


class PublicationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='pac-publication-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.export = self.root/'export'
        self.export.mkdir()
        self.blend = self.root/'source.blend'
        self.blend.write_bytes(b'private-source-fixture')
        self.recipe = read(ROOT/RECIPE)
        self.write(self.root/RECIPE, self.recipe)
        for key in ('board_path', 'composition_path', 'scene_path', 'gameplay_map_path'):
            self.write(self.root/self.recipe[key], read(ROOT/self.recipe[key]))
        self.active = self.root/self.recipe['bundle_path']
        self.active.parent.mkdir(parents=True)
        self.active.write_bytes(b'complete-previous-arena')
        self.prepare_export()

    def write(self, path, value):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value)+'\n')

    def prepare_export(self):
        scene = read(self.root/self.recipe['scene_path'])
        old_map = read(self.root/self.recipe['gameplay_map_path'])
        data = build_map(old_map['cells'], scene, read(self.root/self.recipe['board_path']),
                         read(self.root/self.recipe['composition_path']))
        self.write(self.export/export_name(self.recipe, 'scene_path'), scene)
        self.write(self.export/'arena-map.json', data)
        self.write(self.export/'tile-layout.json', {'cells': data['cells']})
        self.write(self.export/'terrain.patch.json', {'fixture': True})
        names = (export_name(self.recipe, 'scene_path'), 'arena-map.json', 'tile-layout.json', 'terrain.patch.json')
        self.write(self.export/'export-report.json', {
            'scene_id': self.recipe['scene_id'], 'source_blend': str(self.blend),
            'source_blend_sha256': sha(self.blend),
            'authoring_recipe_sha256': digest(self.recipe),
            'export_files_sha256': {name: sha(self.export/name) for name in names}})

    def run_publish(self, **kwargs):
        return publish(self.root, RECIPE, self.export, self.blend, cook_fixture, **kwargs)

    def test_every_pre_activation_failure_retains_previous_arena_and_recovers(self):
        for phase in ('staged', 'qualified', 'mirror_scene_path', 'mirror_terrain_path',
                      'mirror_gameplay_map_path', 'before_activation'):
            with self.subTest(phase=phase):
                self.active.write_bytes(b'complete-previous-arena')
                def fail(current):
                    if current == phase: raise RuntimeError('injected interruption')
                with self.assertRaises(RuntimeError): self.run_publish(checkpoint=fail)
                self.assertEqual(self.active.read_bytes(), b'complete-previous-arena')
                self.assertTrue(self.run_publish()['passed'])
                self.assertEqual(self.active.read_bytes(), b'complete-new-arena')

    def test_process_death_releases_lock_and_preserves_active_archive(self):
        code = """
import os, sys
from pathlib import Path
sys.path.insert(0, sys.argv[1])
from test_arena_publication import cook_fixture
from publish_arena import publish
root=Path(sys.argv[2])
publish(root, sys.argv[3], root/'export', root/'source.blend', cook_fixture,
        checkpoint=lambda phase: os._exit(71) if phase=='before_activation' else None)
"""
        result = subprocess.run([sys.executable, '-c', code, str(Path(__file__).parent), str(self.root), RECIPE])
        self.assertEqual(result.returncode, 71)
        self.assertEqual(self.active.read_bytes(), b'complete-previous-arena')
        self.assertTrue(self.run_publish()['passed'])

    def test_stale_export_recipe_and_map_are_rejected(self):
        (self.export/'terrain.patch.json').write_text('{}')
        with self.assertRaises(ValueError): self.run_publish()
        self.prepare_export()
        changed = copy.deepcopy(self.recipe); changed['scene_id'] = 'another-arena'
        self.write(self.root/RECIPE, changed)
        with self.assertRaises(ValueError): self.run_publish()
        self.write(self.root/RECIPE, self.recipe)
        board = read(self.root/self.recipe['board_path'])
        board['board_registration']['terrain_grid_origin'][0] += 1
        self.write(self.root/self.recipe['board_path'], board)
        with self.assertRaises(ValueError): self.run_publish()
        self.assertEqual(self.active.read_bytes(), b'complete-previous-arena')

    def test_copied_bytes_are_verified_against_the_export(self):
        import shutil
        original = shutil.copyfile
        def racing_copy(source, destination, *args, **kwargs):
            result = original(source, destination, *args, **kwargs)
            if Path(destination).name == 'terrain.patch.json': Path(destination).write_text('{}')
            return result
        with patch('publish_arena.shutil.copyfile', side_effect=racing_copy):
            with self.assertRaises(ValueError): self.run_publish()
        self.assertEqual(self.active.read_bytes(), b'complete-previous-arena')

    def test_depot_failure_blocks_activation(self):
        depot = self.root/'depot'
        depot.write_text('a file cannot be an asset depot')
        with self.assertRaises(OSError): self.run_publish(depot=depot)
        self.assertEqual(self.active.read_bytes(), b'complete-previous-arena')

    def test_backups_are_verified_immutable_and_publication_is_idempotent(self):
        depot = self.root/'depot'
        first = self.run_publish(depot=depot)
        stamp = self.active.stat().st_mtime_ns
        self.assertEqual(first, self.run_publish(depot=depot))
        self.assertEqual(stamp, self.active.stat().st_mtime_ns)
        self.blend.write_bytes(b'new-source-same-cooked-geometry')
        with self.assertRaises(ValueError): self.run_publish(depot=depot)
        self.prepare_export()
        second = self.run_publish(depot=depot)
        self.assertNotEqual(first['source_backup'], second['source_backup'])
        self.assertEqual((Path(first['source_backup'])/'source.blend').read_bytes(), b'private-source-fixture')
        manifest = read((depot/self.recipe['depot_source_relative']).parent/'latest-source.json')
        self.assertEqual(manifest['source_sha256'], sha(self.blend))

    def test_restore_uses_latest_verified_source_and_preserves_working_copies(self):
        from restore_arena_source import restore
        depot = self.root/'depot'
        result = self.run_publish(depot=depot)
        destination = self.root/'restored.blend'
        restore(self.root, RECIPE, depot, destination)
        self.assertEqual(destination.read_bytes(), self.blend.read_bytes())
        with self.assertRaises(FileExistsError): restore(self.root, RECIPE, depot, destination)
        (Path(result['source_backup'])/'source.blend').write_bytes(b'corrupt-backup')
        with self.assertRaises(ValueError): restore(self.root, RECIPE, depot, self.root/'another.blend')
        self.assertFalse((self.root/'another.blend').exists())

    def test_parallel_writer_is_rejected(self):
        lock = self.root/'test.lock'
        with publication_lock(lock):
            with self.assertRaises(OSError):
                with publication_lock(lock): pass

    def test_second_recipe_has_isolated_outputs_and_unsafe_paths_fail(self):
        second = copy.deepcopy(self.recipe)
        for key in ('scene_path', 'gameplay_map_path', 'terrain_path', 'bundle_path'):
            second[key] = 'second/'+Path(second[key]).name
        second['scene_id'] = 'routes/second-fixture'
        for key in ('scene_path', 'gameplay_map_path'):
            document = read(self.root/self.recipe[key]); document['scene_id'] = second['scene_id']
            self.write(self.root/second[key], document)
        self.recipe = second
        self.write(self.root/RECIPE, second)
        self.prepare_export()
        self.assertTrue(self.run_publish()['passed'])
        self.assertEqual(self.active.read_bytes(), b'complete-previous-arena')
        second['bundle_path'] = '../escape.phscene'
        self.write(self.root/RECIPE, second)
        with self.assertRaises(ValueError): load_recipe(self.root, RECIPE)


if __name__ == '__main__': unittest.main()
