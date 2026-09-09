"""Publish a verified Blender camera render to the UI depot and local runtime.

No source-game parsing, board definition, terrain patch, or arena export is involved.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import tempfile


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def atomic_copy(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=destination.name + '.', suffix='.partial', dir=destination.parent)
    try:
        with os.fdopen(fd, 'wb') as out, source.open('rb') as src:
            shutil.copyfileobj(src, out)
        if digest(Path(temporary)) != digest(source):
            raise ValueError('Published file failed copy verification')
        os.replace(temporary, destination)
    finally:
        Path(temporary).unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--recipe', type=Path, required=True)
    parser.add_argument('--blend', type=Path, required=True)
    parser.add_argument('--render', type=Path, required=True)
    parser.add_argument('--depot', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    recipe = json.loads(args.recipe.read_text(encoding='utf-8-sig'))
    report_path = args.render / 'render-report.json'
    report = json.loads(report_path.read_text(encoding='utf-8-sig'))
    image = args.render / 'backdrop.png'
    if recipe['schema_version'] != 1 or recipe['kind'] != 'frontend_backdrop_recipe':
        raise ValueError('Unsupported frontend recipe')
    if report['id'] != recipe['id'] or args.blend.name != recipe['authoring_file']:
        raise ValueError('Render belongs to a different frontend')
    for field, path in [('recipe_sha256', args.recipe), ('source_blend_sha256', args.blend), ('image_sha256', image)]:
        if report[field] != digest(path):
            raise ValueError(f'{field} changed after rendering')
    header = image.read_bytes()[:24]
    if header[:8] != b'\x89PNG\r\n\x1a\n' or struct.unpack('>II', header[16:24]) != (recipe['width'], recipe['height']):
        raise ValueError('Backdrop dimensions disagree with the recipe')
    relative = Path(recipe['runtime_path'])
    if relative.is_absolute() or '..' in relative.parts or relative.parts[:3] != ('assets', 'ui', 'backdrops'):
        raise ValueError('Frontend image must stay under assets/ui/backdrops')
    package = args.depot / 'pokemon-autochess'
    # Back up editable source and evidence before making the image active.
    authoring = package / 'authoring' / 'frontends' / args.blend.stem
    atomic_copy(args.blend, authoring / args.blend.name)
    atomic_copy(args.recipe, authoring / 'recipe.json')
    atomic_copy(report_path, authoring / 'render-report.json')
    atomic_copy(image, package / 'runtime' / relative)
    atomic_copy(image, root / relative)
    print(json.dumps({'published': recipe['id'], 'runtime_path': relative.as_posix(), **report}, indent=2))


if __name__ == '__main__':
    main()
