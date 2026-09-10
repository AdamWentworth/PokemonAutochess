"""Publication must validate the complete camera package before copying anything."""
import contextlib
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import zlib

import publish_frontend_backdrop as publisher


def png(path, width, height):
    def chunk(kind, value):
        return struct.pack('>I', len(value)) + kind + value + struct.pack('>I', zlib.crc32(kind+value))
    pixels = (b'\0' + bytes([120, 180, 90])*width)*height
    path.write_bytes(b'\x89PNG\r\n\x1a\n'
                     + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(pixels)) + chunk(b'IEND', b''))


class PublicationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.blend = self.root / 'OaksLab.blend'
        self.blend.write_bytes(b'fixture source')
        self.recipe_path = self.root / 'recipe.json'
        self.recipe = {'schema_version': 1, 'kind': 'frontend_backdrop_recipe', 'id': 'lab',
                       'authoring_file': 'OaksLab.blend', 'runtime_path': 'assets/ui/backdrops/lab.png',
                       'width': 8, 'height': 5, 'camera_sequence': {
                           'frame_count': 2, 'columns': 4, 'rows': 2, 'padding': 2,
                           'frame_width': 8, 'frame_height': 5,
                           'atlas_prefix': 'assets/ui/backdrops/intro_',
                           'final_image': 'assets/ui/backdrops/table.png'}}
        png(self.root/'backdrop.png', 8, 5)
        png(self.root/'atlas_0.png', 48, 18)
        png(self.root/'final.png', 8, 5)
        self.report = {'id': 'lab', 'source_blend_sha256': publisher.digest(self.blend),
                       'image_sha256': publisher.digest(self.root/'backdrop.png'), 'sequence_images': [
                           {'file': 'atlas_0.png', 'runtime_path': 'assets/ui/backdrops/intro_0.png',
                            'width': 48, 'height': 18, 'sha256': publisher.digest(self.root/'atlas_0.png')},
                           {'file': 'final.png', 'runtime_path': 'assets/ui/backdrops/table.png',
                            'width': 8, 'height': 5, 'sha256': publisher.digest(self.root/'final.png')}]}

    def publish(self, rejected=False):
        self.recipe_path.write_text(json.dumps(self.recipe))
        self.report['recipe_sha256'] = publisher.digest(self.recipe_path)
        (self.root/'render-report.json').write_text(json.dumps(self.report))
        argv = ['publish', '--recipe', str(self.recipe_path), '--blend', str(self.blend),
                '--render', str(self.root), '--depot', str(self.root/'depot')]
        with patch('sys.argv', argv), patch.object(publisher, 'atomic_copy') as copy:
            with contextlib.redirect_stdout(io.StringIO()):
                if rejected:
                    with self.assertRaises(ValueError):
                        publisher.main()
                    copy.assert_not_called()
                else:
                    publisher.main()
                    self.assertEqual(copy.call_count, 9) # Source/recipe/report plus three images in two destinations.

    def test_complete_package(self):
        self.publish()

    def test_missing_sequence_record(self):
        self.report['sequence_images'].pop()
        self.publish(rejected=True)

    def test_changed_atlas(self):
        (self.root/'atlas_0.png').write_bytes(b'changed')
        self.publish(rejected=True)

    def test_wrong_dimensions_even_with_matching_hash(self):
        png(self.root/'atlas_0.png', 47, 18)
        self.report['sequence_images'][0]['sha256'] = publisher.digest(self.root/'atlas_0.png')
        self.publish(rejected=True)

    def test_destination_escape(self):
        self.recipe['camera_sequence']['final_image'] = 'assets/ui/backdrops/../../../outside.png'
        self.report['sequence_images'][1]['runtime_path'] = self.recipe['camera_sequence']['final_image']
        self.publish(rejected=True)

    def test_invalid_atlas_layout(self):
        self.recipe['camera_sequence']['columns'] = 0
        self.publish(rejected=True)


if __name__ == '__main__':
    unittest.main()
