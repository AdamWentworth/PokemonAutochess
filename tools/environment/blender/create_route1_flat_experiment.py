"""Create a separate, editable flat-board experiment from the approved entrance.

The input blend is never saved. Routine edits use arena_pilot.py afterward.
"""
import argparse
import json
from pathlib import Path
import sys

import bpy
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import arena_pilot as arena
import arena_tiles as tiles


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
    output = args.output.resolve()
    if output == Path(bpy.data.filepath).resolve() or output.exists():
        raise RuntimeError('The experiment must be saved to a new, separate file.')
    scene = bpy.context.scene
    cells = tiles.read_cells()
    for cell in cells:
        # A half-tile visual breathing space is supplied by the neighbouring
        # lawn cells. Both reserve rows remain outside combat navigation.
        if 16 <= cell['x'] <= 25 and -11 <= cell['z'] <= -1:
            cell.update(height=0, ramp=0)
            if 17 <= cell['x'] <= 24:
                cell['surface'] = 1
            else:
                cell['surface'] = 0
    config = json.loads(scene['pilot_blueprint'])
    config['name'] = 'South Entrance - Flat Dirt Experiment'
    config['tile_cells'] = cells
    scene['pilot_authoring_recipe'] = 'config/environment/route1_flat_experiment.authoring.json'
    scene['pilot_reference'] = 'Independent flat dirt combat experiment; approved entrance preserved separately'
    scene['pilot_composition_pass'] = 1
    tiles.create_guide(arena, cells)
    arena.rebuild_terrain(config)
    removed = []
    for group in ('PAC_PREFABS', 'PAC_EDIT_PATCH'):
        for obj in list(arena.collection(group).objects):
            if obj.get('phlosion_patch_id') == 'arena-pilot/terrain':
                continue
            corners = [obj.matrix_world @ Vector(p) for p in obj.bound_box]
            # Blender +Y is source north. Clear combat and reserves, including
            # the dense grass bed's visible half-cell border.
            overlaps = (min(p.x for p in corners) < 25.1 and max(p.x for p in corners) > 16.9
                        and min(p.y for p in corners) < 11.1 and max(p.y for p in corners) > 0.9)
            if overlaps:
                removed.append(obj.name)
                bpy.data.objects.remove(obj, do_unlink=True)
    output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(output))
    (output.parent / 'bootstrap-report.json').write_text(json.dumps({
        'source_preserved': True, 'removed_overlapping_props': removed,
        'board_bounds': [17, -10, 24, -3], 'height_level': 0,
        'output': str(output)}, indent=2) + '\n')


if __name__ == '__main__':
    main()
