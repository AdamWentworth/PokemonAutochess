"""Explicit, project-relative authoring inputs; shared by Blender and tools."""
import json
from pathlib import Path, PurePosixPath

DEFAULT_RECIPE = 'config/environment/route1_south_entrance.authoring.json'
PATH_FIELDS = ('board_path', 'composition_path', 'scene_path', 'terrain_path', 'gameplay_map_path', 'bundle_path')


def relative_path(value):
    if not isinstance(value, str) or not value or '\\' in value or ':' in value:
        raise ValueError(f'Expected a project-relative POSIX path: {value!r}')
    path = PurePosixPath(value)
    if path.is_absolute() or any(p in ('', '.', '..') for p in value.split('/')):
        raise ValueError(f'Unsafe arena path: {value}')
    return value


def load_recipe(game_root, recipe_path=DEFAULT_RECIPE):
    game_root = Path(game_root).resolve()
    path = Path(recipe_path)
    if not path.is_absolute(): path = game_root/path
    path = path.resolve()
    path.relative_to(game_root)
    recipe = json.loads(path.read_text(encoding='utf-8-sig'))
    if recipe.get('kind') != 'pokemon_autochess_arena_authoring' or recipe.get('schema_version') != 1:
        raise ValueError('Unsupported arena authoring recipe')
    for key in PATH_FIELDS:
        relative_path(recipe[key])
        (game_root/recipe[key]).resolve().relative_to(game_root)
    for key in ('depot_source_relative', 'working_source_relative'):
        relative_path(recipe[key])
    if len({recipe[k] for k in PATH_FIELDS}) != len(PATH_FIELDS):
        raise ValueError('Arena input and output paths must be distinct')
    for key in ('scene_id', 'base_environment_asset_id', 'terrain_node_id'):
        if not isinstance(recipe.get(key), str) or not recipe[key]: raise ValueError(f'Missing {key}')
    if type(recipe.get('ground_material_index')) is not int or recipe['ground_material_index'] < 0:
        raise ValueError('Missing ground material identity')
    return recipe


def export_name(recipe, key):
    return Path(recipe[key]).name
