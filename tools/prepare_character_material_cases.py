"""Create isolated native material fixtures from the published character catalog."""
import argparse
import copy
import json
from pathlib import Path
import shutil


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + '\n')


def prepare(game, output, profile_path):
    overlay = output / 'game-data'
    shutil.copytree(game / 'config', overlay / 'config', dirs_exist_ok=True)
    profile = json.loads((game / profile_path).read_text())
    for section in ('opengl', 'd3d12', 'opengl_vertex', 'd3d12_vertex', 'vulkan'):
        for source in profile.get(section, {}).values():
            destination = overlay / source
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(game / source, destination)
    # Both names allow comparison against the engine revision preceding the move.
    for name in ('field_materials', 'world_materials'):
        write(overlay / f'config/render/{name}.json', profile)
    catalog = json.loads((overlay / 'config/pokemon_config.json').read_text())
    cohorts = json.loads((game / 'config/render/native_character_matrix.json').read_text())['cohorts']
    matrix = json.loads((game / 'config/render_parity_scene_matrix.json').read_text())
    matrix['scenes'] = []
    for cohort_index, cohort in enumerate(cohorts):
        index = cohort_index * 6
        models = cohort["models"]
        snapshot = json.loads((game / 'config/debug/editor_route1_flat_experiment_benches.json').read_text())
        snapshot['session'].update(combat_active=False, round_phase='Planning', state_kind='scripted')
        snapshot['world']['bench_units'] = []
        units = []
        for slot, model in enumerate(models):
            species = 'materialqa' + str(index + slot)
            catalog[species] = copy.deepcopy(catalog['charmander'])
            catalog[species]['model'] = model + '.phmodel'
            catalog[species]['modelVariants'] = {'regular': model + '.phmodel'}
            units.append(dict(name=species, side='player', bench_slot=-1, col=1+(slot%3)*2,
                row=3+(slot//3)*3, level=5, hp=100, energy=0, alive=True, fainting=False,
                capture_in_progress=False, model_variant='regular'))
        snapshot['world']['board_units'] = units
        name = f'character-cohort-{index//6+1}'
        snapshot_path = output / f'{name}.json'
        write(snapshot_path, snapshot)
        matrix['scenes'].append(dict(name=name, focus=', '.join(models),
            coverage=['world', 'materials', 'animation', 'character-materials'],
            snapshotPath=str(snapshot_path), screenshotFrame=164, width=1440, height=1000,
            contentGuards=[dict(name='visible-arena', x=.3, y=.2, width=.4, height=.5,
                maximumNearBlackPixelRatio=.12, minimumMidtonePixelRatio=.3)] + cohort["contentGuards"]))
    write(overlay / 'config/pokemon_config.json', catalog)
    write(output / 'native-matrix.json', matrix)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--profile', type=Path, default=Path('config/render/world_materials.json'))
    args = parser.parse_args()
    prepare(Path(__file__).resolve().parents[1], args.output.resolve(), args.profile)
