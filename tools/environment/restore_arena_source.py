"""Restore the latest verified arena source to a new working copy."""
import argparse
from pathlib import Path
import shutil

from publish_arena import read, sha
from blender.arena_recipe import DEFAULT_RECIPE, load_recipe, relative_path


def restore(game_root, recipe_path, depot, destination=None):
    recipe = load_recipe(game_root, recipe_path)
    depot = Path(depot).resolve()
    source = (depot/recipe['depot_source_relative']).resolve()
    source.relative_to(depot)
    manifest_path = source.parent/'latest-source.json'
    expected = None
    if manifest_path.is_file():
        manifest = read(manifest_path)
        if manifest['schema_version'] != 1 or manifest['scene_id'] != recipe['scene_id']:
            raise ValueError('Source backup belongs to a different arena or schema')
        source_root = source.parent
        source = (source_root/relative_path(manifest['source_relative'])).resolve()
        source.relative_to(source_root)
        expected = manifest['source_sha256']
    source_hash = sha(source)
    if expected and source_hash != expected: raise ValueError('The source backup failed its recorded hash check')
    destination = Path(destination).resolve() if destination else depot.parent/recipe['working_source_relative']
    destination.parent.mkdir(parents=True, exist_ok=True)
    # Exclusive creation protects an existing working copy, including a race
    # with another restore. Only this newly created file is removed on failure.
    with destination.open('xb') as output:
        try:
            with source.open('rb') as input_file: shutil.copyfileobj(input_file, output)
        except BaseException:
            output.close(); destination.unlink(); raise
    if sha(destination) != source_hash:
        destination.unlink()
        raise ValueError('Restored copy failed its hash check')
    return destination


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--game-root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--recipe', default=DEFAULT_RECIPE)
    parser.add_argument('--depot', type=Path, required=True)
    parser.add_argument('--destination', type=Path)
    args = parser.parse_args()
    print('Restored and verified: '+str(restore(args.game_root, args.recipe, args.depot, args.destination)))


if __name__ == '__main__': main()
