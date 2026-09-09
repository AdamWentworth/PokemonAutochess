"""Render the active camera in an existing, hand-editable frontend .blend."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

import bpy


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--recipe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
    args.recipe = args.recipe.resolve()
    args.output = args.output.resolve()
    recipe = json.loads(args.recipe.read_text(encoding='utf-8-sig'))
    scene = bpy.context.scene
    if recipe['kind'] != 'frontend_backdrop_recipe' or recipe['schema_version'] != 1:
        raise ValueError('Unsupported frontend backdrop recipe')
    if not scene.camera or scene.get('presentation_role') != recipe['presentation_role']:
        raise ValueError('Blender source must have a camera and the intended frontend presentation role')
    source = Path(bpy.data.filepath)
    if source.name != recipe['authoring_file']:
        raise ValueError('Unexpected authoring file')
    args.output.mkdir(parents=True, exist_ok=True)
    scene.render.resolution_x = recipe['width']
    scene.render.resolution_y = recipe['height']
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGB'
    scene.render.filepath = str(args.output / 'backdrop.png')
    bpy.ops.render.render(write_still=True)
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    report = {'schema_version': 1, 'id': recipe['id'], 'recipe_sha256': sha(args.recipe),
              'source_blend_sha256': sha(source), 'image_sha256': sha(Path(scene.render.filepath)),
              'width': recipe['width'], 'height': recipe['height']}
    (args.output / 'render-report.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
