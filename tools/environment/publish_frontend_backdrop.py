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


def validate_image(path, expected_hash, width, height):
    if digest(path) != expected_hash:
        raise ValueError(f'Image changed after rendering: {path.name}')
    header = path.read_bytes()[:24]
    if header[:8] != b'\x89PNG\r\n\x1a\n' or struct.unpack('>II', header[16:24]) != (width, height):
        raise ValueError(f'Backdrop dimensions disagree with the recipe: {path.name}')


def runtime_path(value):
    relative = Path(value)
    if relative.is_absolute() or '..' in relative.parts or relative.parts[:3] != ('assets', 'ui', 'backdrops'):
        raise ValueError('Frontend images must stay under assets/ui/backdrops')
    return relative


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
    validate_image(image, report['image_sha256'], recipe['width'], recipe['height'])
    relative = runtime_path(recipe['runtime_path'])
    images = [(image, relative)]
    sequence = recipe.get('camera_sequence')
    if sequence:
        if not (2 <= sequence['frame_count'] <= 128 and 1 <= sequence['columns'] <= 4
                and 1 <= sequence['rows'] <= 4 and 1 <= sequence['padding'] <= 8
                and 1 <= sequence['frame_width'] <= 2048 and 1 <= sequence['frame_height'] <= 2048
                and (sequence['frame_width']+2*sequence['padding'])*sequence['columns'] <= 4096
                and (sequence['frame_height']+2*sequence['padding'])*sequence['rows'] <= 4096
                and sequence['frame_width']*recipe['height'] == sequence['frame_height']*recipe['width']):
            raise ValueError('Invalid camera sequence dimensions or frame count')
        per_page = sequence['columns'] * sequence['rows']
        pages = (sequence['frame_count'] + per_page - 1) // per_page
        expected = [(f'atlas_{i}.png', sequence['atlas_prefix']+str(i)+'.png',
                     (sequence['frame_width']+2*sequence['padding'])*sequence['columns'],
                     (sequence['frame_height']+2*sequence['padding'])*sequence['rows']) for i in range(pages)]
        expected.append(('final.png', sequence['final_image'], recipe['width'], recipe['height']))
        records = report.get('sequence_images', [])
        if len(records) != len(expected):
            raise ValueError('Camera render is missing one or more sequence images')
        for record, (name, destination, width, height) in zip(records, expected):
            if (record['file'], record['runtime_path'], record['width'], record['height']) != (name, destination, width, height):
                raise ValueError('Camera sequence report disagrees with the recipe')
            path = args.render / name
            validate_image(path, record['sha256'], width, height)
            images.append((path, runtime_path(destination)))
    if len({destination for _, destination in images}) != len(images):
        raise ValueError('Frontend image destinations must be distinct')
    package = args.depot / 'pokemon-autochess'
    # Back up editable source and evidence before making the image active.
    authoring = package / 'authoring' / 'frontends' / args.blend.stem
    atomic_copy(args.blend, authoring / args.blend.name)
    atomic_copy(args.recipe, authoring / 'recipe.json')
    atomic_copy(report_path, authoring / 'render-report.json')
    for image, destination in images:
        atomic_copy(image, package / 'runtime' / destination)
        atomic_copy(image, root / destination)
    print(json.dumps({'published': recipe['id'], 'runtime_path': relative.as_posix(), **report}, indent=2))


if __name__ == '__main__':
    main()
