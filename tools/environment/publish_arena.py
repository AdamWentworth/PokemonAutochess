"""Stage, qualify, back up, and atomically activate a complete arena archive."""
from contextlib import contextmanager
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).parent/'blender'))
from arena_recipe import DEFAULT_RECIPE, load_recipe, export_name
from arena_map import digest, validate_map


def sha(path):
    with Path(path).open('rb') as stream: return hashlib.file_digest(stream, 'sha256').hexdigest()


def read(path): return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def atomic_copy(source, destination):
    destination = Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.is_file() and sha(source) == sha(destination): return
    descriptor, temporary = tempfile.mkstemp(prefix=destination.name+'.', suffix='.partial', dir=destination.parent)
    try:
        with os.fdopen(descriptor, 'wb') as output, Path(source).open('rb') as input_file:
            shutil.copyfileobj(input_file, output)
            output.flush()
            os.fsync(output.fileno())
        if sha(temporary) != sha(source): raise ValueError('Copy verification failed')
        os.replace(temporary, destination)
    finally:
        Path(temporary).unlink(missing_ok=True)


@contextmanager
def publication_lock(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('a+b') as stream:
        stream.seek(0, os.SEEK_END)
        if stream.tell() == 0: stream.write(b'0'); stream.flush()
        stream.seek(0)
        if os.name == 'nt':
            import msvcrt
            msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            import fcntl
            fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
        try: yield
        finally:
            stream.seek(0)
            if os.name == 'nt': msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
            else: fcntl.flock(stream, fcntl.LOCK_UN)


def publish(game_root, recipe_path, export, blend, cook, depot=None, checkpoint=lambda phase: None):
    """cook must create and qualify the staged PHSC before activation.

    checkpoint is a failure-injection seam for tests, never a CLI option.
    OS-owned locks release on crashes; an interrupted publisher leaves the last
    whole runtime archive active even if review mirrors were partly updated.
    """
    root, export, blend = Path(game_root).resolve(), Path(export).resolve(), Path(blend).resolve()
    recipe = load_recipe(root, recipe_path)
    report = read(export/'export-report.json')
    if Path(report['source_blend']).resolve() != blend or report['scene_id'] != recipe['scene_id']:
        raise ValueError('Export belongs to a different Blender source or arena')
    if report['authoring_recipe_sha256'] != digest(recipe): raise ValueError('Authoring recipe changed after export')
    if report['source_blend_sha256'] != sha(blend): raise ValueError('Blender source changed after export')
    names = (export_name(recipe, 'scene_path'), 'arena-map.json', 'terrain.patch.json', 'tile-layout.json')
    for name in names:
        if report['export_files_sha256'].get(name) != sha(export/name): raise ValueError(f'Export input changed: {name}')
    scene, game_map = read(export/names[0]), read(export/'arena-map.json')
    validate_map(game_map, scene, read(root/recipe['board_path']), read(root/recipe['composition_path']))
    if game_map['cells'] != sorted(read(export/'tile-layout.json')['cells'], key=lambda c:(c['z'],c['x'])):
        raise ValueError('Gameplay cells disagree with the exported tile blueprint')
    target = root/recipe['bundle_path']
    lock_id = hashlib.sha256(recipe['bundle_path'].encode()).hexdigest()[:20]
    with publication_lock(root/'.phlosion'/f'arena-{lock_id}.lock'):
        with tempfile.TemporaryDirectory(prefix='arena-stage-', dir=root/'.phlosion') as temporary:
            stage = Path(temporary)
            inputs = {'scene_path': export/names[0], 'gameplay_map_path': export/'arena-map.json',
                      'board_path': root/recipe['board_path'], 'composition_path': root/recipe['composition_path']}
            for key, source in inputs.items():
                destination = stage/recipe[key]
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, destination)
            (stage/'recipe.json').write_text(json.dumps(recipe, indent=2)+'\n')
            shutil.copyfile(export/'terrain.patch.json', stage/'terrain.patch.json')
            shutil.copyfile(export/'tile-layout.json', stage/'tile-layout.json')
            # Check staged bytes too: an export racing the copies must fail.
            checkpoint('staged')
            staged_exports = {names[0]: stage/recipe['scene_path'],
                              'arena-map.json': stage/recipe['gameplay_map_path'],
                              'terrain.patch.json': stage/'terrain.patch.json',
                              'tile-layout.json': stage/'tile-layout.json'}
            for name, path in staged_exports.items():
                if sha(path) != report['export_files_sha256'][name]:
                    raise ValueError(f'Export changed during staging: {name}')
            validate_map(read(stage/recipe['gameplay_map_path']), read(stage/recipe['scene_path']),
                         read(stage/recipe['board_path']), read(stage/recipe['composition_path']))
            candidate = stage/'arena.phscene'
            cook(stage, recipe, candidate)
            if not candidate.is_file() or candidate.stat().st_size == 0: raise ValueError('Cook did not produce an arena archive')
            checkpoint('qualified')
            revision = sha(candidate)
            backup = None
            if depot:
                depot = Path(depot).resolve()
                with publication_lock(depot/f'.arena-{lock_id}.lock'):
                    source_root = (depot/recipe['depot_source_relative']).resolve().parent
                    source_root.relative_to(depot)
                    source_hash = report['source_blend_sha256']
                    if sha(blend) != source_hash: raise ValueError('Blender source changed during publication')
                    source_revision = revision+'-'+source_hash
                    backup = source_root/'revisions'/source_revision
                    atomic_copy(blend, backup/'source.blend')
                    if sha(backup/'source.blend') != source_hash:
                        raise ValueError('Blender source changed during backup')
                    atomic_copy(candidate, backup/'arena.phscene')
                    source_manifest = {'schema_version':1, 'scene_id':recipe['scene_id'], 'revision':revision,
                                       'source_relative':f'revisions/{source_revision}/source.blend', 'source_sha256':source_hash}
                    manifest = stage/'latest-source.json'
                    manifest.write_text(json.dumps(source_manifest, indent=2)+'\n')
                    atomic_copy(candidate, depot/'pokemon-autochess/runtime'/recipe['bundle_path'])
                    atomic_copy(manifest, source_root/'latest-source.json')
                checkpoint('backed_up')
            # Review mirrors can be recovered by repeating publication. Runtime
            # readers only observe the old archive until the final replacement.
            for key in ('scene_path', 'terrain_path', 'gameplay_map_path'):
                atomic_copy(stage/recipe[key], root/recipe[key])
                checkpoint('mirror_'+key)
            checkpoint('before_activation')
            if sha(blend) != report['source_blend_sha256']: raise ValueError('Blender source changed during publication')
            atomic_copy(candidate, target)
            checkpoint('activated')
            return {'passed':True, 'scene_id':recipe['scene_id'], 'revision':revision,
                    'bundle':str(target), 'source_backup':str(backup) if backup else None}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--game-root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--recipe', default=DEFAULT_RECIPE)
    parser.add_argument('--export', type=Path, required=True)
    parser.add_argument('--blend', type=Path, required=True)
    parser.add_argument('--forge', type=Path, required=True)
    parser.add_argument('--depot', type=Path)
    args = parser.parse_args()
    forge = args.forge.resolve()
    def cook(stage, recipe, candidate):
        terrain = stage/recipe['terrain_path']
        terrain.parent.mkdir(parents=True, exist_ok=True)
        for command in (['compile-environment-patch', str(stage/'terrain.patch.json'), str(terrain)],
                        ['cook-arena-bundle', str(stage), 'recipe.json', str(candidate)],
                        ['validate-arena-bundle', str(candidate)]):
            result = subprocess.run([str(forge), *command], cwd=args.game_root, capture_output=True, text=True,
                                    creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            (args.export/(command[0]+'.log')).write_text(result.stdout+'\n'+result.stderr)
            if result.returncode: raise RuntimeError(f'{command[0]} failed; see export log')
    result = publish(args.game_root, args.recipe, args.export, args.blend, cook, args.depot)
    (args.export/'install-report.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result))


if __name__ == '__main__': main()
