"""Render the active camera in an existing, hand-editable frontend .blend."""
import argparse
from array import array
import hashlib
import json
from pathlib import Path
import sys

import bpy


def render_png(scene, path, width, height, frame):
    scene.frame_set(frame)
    scene.render.resolution_x, scene.render.resolution_y = width, height
    scene.render.filepath = str(path)
    bpy.ops.render.render(write_still=True)


def pack_atlas(paths, output, config):
    """Pack already color-managed PNGs without applying a second view transform."""
    fw, fh, pad = config['frame_width'], config['frame_height'], config['padding']
    tile_w, tile_h = fw + 2*pad, fh + 2*pad
    width, height = tile_w*config['columns'], tile_h*config['rows']
    pixels = array('f', [0.0]) * (width*height*4)
    for index, path in enumerate(paths):
        image = bpy.data.images.load(str(path), check_existing=False)
        image.colorspace_settings.name = 'Non-Color'
        if tuple(image.size) != (fw, fh):
            raise ValueError('Camera frame dimensions changed during rendering')
        data = array('f', [0.0]) * (fw*fh*4)
        image.pixels.foreach_get(data)
        x = (index % config['columns']) * tile_w
        y = (config['rows'] - 1 - index // config['columns']) * tile_h + pad
        for row in range(-pad, fh+pad):
            source_row = min(fh-1, max(0, row))
            values = data[source_row*fw*4:(source_row+1)*fw*4]
            values = values[:4]*pad + values + values[-4:]*pad
            start = ((y+row)*width+x)*4
            pixels[start:start+tile_w*4] = values
        bpy.data.images.remove(image)
    atlas = bpy.data.images.new('Camera atlas', width=width, height=height, alpha=False)
    atlas.colorspace_settings.name = 'Non-Color'
    atlas.pixels.foreach_set(pixels)
    atlas.filepath_raw = str(output)
    atlas.file_format = 'PNG'
    atlas.save()
    bpy.data.images.remove(atlas)
    return width, height


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
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGB'
    sequence = recipe.get('camera_sequence')
    first = sequence['first_frame'] if sequence else scene.frame_current
    render_png(scene, args.output / 'backdrop.png', recipe['width'], recipe['height'], first)
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    report = {'schema_version': 1, 'id': recipe['id'], 'recipe_sha256': sha(args.recipe),
              'source_blend_sha256': sha(source), 'image_sha256': sha(args.output / 'backdrop.png'),
              'width': recipe['width'], 'height': recipe['height']}
    if sequence:
        if not (2 <= sequence['frame_count'] <= 128 and 1 <= sequence['columns'] <= 4
                and 1 <= sequence['rows'] <= 4 and 1 <= sequence['padding'] <= 8
                and 1 <= sequence['frame_width'] <= 2048 and 1 <= sequence['frame_height'] <= 2048
                and (sequence['frame_width']+2*sequence['padding'])*sequence['columns'] <= 4096
                and (sequence['frame_height']+2*sequence['padding'])*sequence['rows'] <= 4096
                and sequence['frame_width']*recipe['height'] == sequence['frame_height']*recipe['width']):
            raise ValueError('Camera sequence exceeds the bounded frontend atlas layout')
        if scene.frame_end < first + sequence['frame_count'] - 1:
            raise ValueError('The source camera animation is shorter than the recipe')
        frame_root = args.output / 'frames'
        frame_root.mkdir(exist_ok=True)
        frames = []
        samples = scene.eevee.taa_render_samples
        scene.eevee.taa_render_samples = min(samples, 64)
        for index in range(sequence['frame_count']):
            path = frame_root / f'{index:03d}.png'
            render_png(scene, path, sequence['frame_width'], sequence['frame_height'], first+index)
            frames.append(path)
        images = []
        per_page = sequence['columns'] * sequence['rows']
        for start in range(0, len(frames), per_page):
            page = start // per_page
            path = args.output / f'atlas_{page}.png'
            width, height = pack_atlas(frames[start:start+per_page], path, sequence)
            images.append({'file': path.name, 'runtime_path': sequence['atlas_prefix']+str(page)+'.png',
                           'width': width, 'height': height, 'sha256': sha(path)})
        scene.eevee.taa_render_samples = samples
        path = args.output / 'final.png'
        render_png(scene, path, recipe['width'], recipe['height'], first+sequence['frame_count']-1)
        images.append({'file': path.name, 'runtime_path': sequence['final_image'],
                       'width': recipe['width'], 'height': recipe['height'], 'sha256': sha(path)})
        report['sequence_images'] = images
    (args.output / 'render-report.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
