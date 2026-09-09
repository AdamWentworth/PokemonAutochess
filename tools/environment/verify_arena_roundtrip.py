"""Prove that exporting a restored source preserves the installed arena."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import shutil
import sys
import bpy

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent/'blender'))
import arena_pilot
from arena_recipe import DEFAULT_RECIPE, load_recipe, export_name


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--recipe', default=DEFAULT_RECIPE)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:])
    source = Path(bpy.data.filepath)
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    before = sha(source)
    recipe = load_recipe(ROOT, args.recipe)
    arena_pilot.export(args.output, args.recipe)
    read = lambda p: json.loads(p.read_text(encoding='utf-8-sig'))
    assert read(args.output/export_name(recipe, 'scene_path')) == read(ROOT/recipe['scene_path']), 'Export changed the approved scene'
    assert read(args.output/'arena-map.json') == read(ROOT/recipe['gameplay_map_path']), 'Gameplay data differs from the saved source'
    result = subprocess.run([str(ROOT/'build/Release/PhlosionForge.exe'), 'compile-environment-patch',
        str(args.output/'terrain.patch.json'), str(args.output/'terrain.phpatch')], cwd=ROOT,
        capture_output=True, text=True, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    (args.output/'compile.log').write_text(result.stdout+'\n'+result.stderr)
    assert result.returncode == 0, 'Terrain cook failed; see compile.log'
    assert sha(args.output/'terrain.phpatch') == sha(ROOT/recipe['terrain_path']), 'Export changed the installed terrain bytes'
    with tempfile.TemporaryDirectory(prefix='arena-roundtrip-') as temporary:
        stage = Path(temporary)
        for key, source_path in {'scene_path': args.output/export_name(recipe, 'scene_path'),
                                 'gameplay_map_path': args.output/'arena-map.json',
                                 'terrain_path': args.output/'terrain.phpatch',
                                 'board_path': ROOT/recipe['board_path'],
                                 'composition_path': ROOT/recipe['composition_path']}.items():
            destination = stage/recipe[key]
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source_path, destination)
        (stage/'recipe.json').write_text(json.dumps(recipe))
        candidate = args.output/'arena.phscene'
        result = subprocess.run([str(ROOT/'build/Release/PhlosionForge.exe'), 'cook-arena-bundle',
                                 str(stage), 'recipe.json', str(candidate.resolve())], cwd=ROOT,
                                capture_output=True, text=True, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        (args.output/'bundle.log').write_text(result.stdout+'\n'+result.stderr)
        assert result.returncode == 0, 'Arena qualification failed; see bundle.log'
        assert sha(candidate) == sha(ROOT/recipe['bundle_path']), 'Export changed the active arena archive'
    assert sha(source) == before, 'Verification modified the working source'
    report = {'passed': True, 'source_unchanged': True, 'source_sha256': before,
              'bundle_byte_identical': True, 'bundle_sha256': sha(args.output/'arena.phscene'),
              'scene_equivalent': True, 'gameplay_map_equivalent': True,
              'terrain_byte_identical': True, 'terrain_sha256': sha(args.output/'terrain.phpatch')}
    (args.output/'roundtrip-report.json').write_text(json.dumps(report, indent=2)+'\n')
    print('ARENA_ROUNDTRIP_PASS '+json.dumps(report))


if __name__ == '__main__': main()
