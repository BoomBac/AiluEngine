"""Run a disposable nine-area-light fixture, with persistent backups and shared-state restoration."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import uuid

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / 'Test/BlenderLinkValidation'))
import runner as lifecycle
from windows_debug_output import start_editor

parser = argparse.ArgumentParser(description='Isolated nine-area-light raster acceptance')
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--editor', type=Path, required=True)
parser.add_argument('--project-template', type=Path, required=True)
parser.add_argument('--light-type', choices=['area', 'point'], default='area')
parser.add_argument('--metallic-probe', action='store_true')
args = parser.parse_args()
point_test = args.light_type == 'point'
out, editor = args.output.resolve(), args.editor.resolve()
if out.exists():
    raise RuntimeError('Output must be a new directory')
if editor.parent not in out.parents:
    raise RuntimeError('Output must be below the selected Editor directory')
if any(item['ProcessName'] == 'Editor' for item in lifecycle.active_interactive_processes()):
    raise RuntimeError('External Editor present; leave untouched')
lifecycle.acquire_run_lock(editor.parent / '.raster_area_validation.lock')
spec = importlib.util.spec_from_file_location('scene_helpers', root / 'Tools/generate_ddgi_test_scenes.py')
h = importlib.util.module_from_spec(spec)
spec.loader.exec_module(h)

out.mkdir(parents=True)
project = out / 'project'
(project / 'Assets').mkdir(parents=True)
(project / 'Library').mkdir()
config = json.loads(args.project_template.read_text(encoding='utf-8-sig'))
config.update(_project_name='RasterAreaLightAcceptance', _project_guid=str(uuid.uuid4()),
              _startup_scene='project://agx.almap')
(project / 'agx.json').write_text(json.dumps(config, indent=2))
db = {'assets': []}
mat = h.material_document('AreaGray', (.5, .5, .5), (0, 0, 0), roughness=.8)
(project / 'Assets/AreaGray.alasset').write_text(json.dumps(mat, indent=2))
h.register_asset(db, mat['_header']['_guid'], 'project://AreaGray.alasset', h.ASSET_TYPE_MATERIAL)
entities = [h.box('Floor', (0, -.1, 0), (12, .2, 12), mat['_header']['_guid'])]
if args.metallic_probe:
    entities = []
    for index, (name, color) in enumerate([('Red', (1,0,0)), ('Green', (0,1,0)), ('Blue', (0,0,1))]):
        probe = h.material_document('Metal' + name, color, (0,0,0), roughness=.3, metallic=1)
        relative = 'Metal' + name + '.alasset'
        (project / 'Assets' / relative).write_text(json.dumps(probe, indent=2))
        h.register_asset(db, probe['_header']['_guid'], 'project://' + relative, h.ASSET_TYPE_MATERIAL)
        entities.append(h.box('Probe' + name, (0 if index == 0 else 1000, -.1, 0),
                              (12,.2,12), probe['_header']['_guid']))
for index in range(1 if args.metallic_probe else 33 if point_test else 9):
    light = {'_type': 'kPoint' if point_test else 'kArea', '_light_schema_version': 1,
             '_light': {'_light_color': [1, 1, 1, 1], '_intensity': 0, '_intensity_unit': 'kWatt', '_range': 0},
             '_area': {'_width': 1, '_height': 1, '_is_two_side': False}, '_shadow': {'_is_cast_shadow': False}}
    if args.metallic_probe:
        light['_light']['_intensity'] = 20
    name = f'PointLight{index:02}' if point_test else 'Area' + str(index)
    entities.append(h.entity(name, position=(0, 3, 0), light=light))
if args.metallic_probe:
    scaled_light = json.loads(json.dumps(light))
    scaled_light['_light']['_intensity'] = 0
    scaled_light['_light'].update(_area_width=.5, _area_height=.5)
    scaled_light['_area'].update(_width=.5, _height=.5)
    entities.append(h.entity('ScaledArea', position=(0, 3, 0), scale=(2, 1, 2), light=scaled_light))
volume = h.postprocess_volume('Neutral', 1)
volume['_postprocess_volume_component']['_settings']['_tone_mapping']['_mode'] = 'kNone'
volume['_postprocess_volume_component']['_settings']['_bloom']['_enabled'] = False
entities.append(volume)
scene = h.scene_document('AreaRegression', 'Nine independent raster area lights', entities)
(project / 'Assets/agx.almap').write_text(json.dumps(scene, indent=2))
h.register_asset(db, scene['_header']['_guid'], 'project://agx.almap', h.ASSET_TYPE_SCENE)
(project / 'Library/AssetDatabase.json').write_text(json.dumps(db, indent=2))

paths = list(dict.fromkeys(list(lifecycle.SHARED_STATE) + [root / 'imgui.ini']))
snapshots = {path: (path.exists(), path.read_bytes() if path.is_file() else None) for path in paths}
backup = out / 'shared_backups'
backup.mkdir()
for index, (path, (existed, data)) in enumerate(snapshots.items()):
    if data is not None:
        (backup / str(index)).write_bytes(data)
(backup / 'index.json').write_text(json.dumps([
    {'path': str(path), 'existed': existed, 'file': str(index)}
    for index, (path, (existed, _)) in enumerate(snapshots.items())], indent=2))
summary, process = {'status': 'FAIL', 'phases': []}, None
try:
    editor_config = root / 'Editor/EditorConfig.json'
    settings = json.loads(editor_config.read_text(encoding='utf-8-sig'))
    settings['_scene_path'] = 'project://agx.almap'
    editor_config.write_text(json.dumps(settings, indent=2))
    folder = out / 'initial'
    folder.mkdir()
    env = dict(os.environ, AILU_BLENDER_LIVE_LINK_PORT=str(lifecycle.find_free_port()))
    with (folder / 'stdout.log').open('wb') as log:
        process = start_editor([str(editor), str(project / 'agx.json')], root, log, env, folder / 'debug.log')
        summary['phases'].append({'phase': 'initial', 'pid': process.pid})
        lifecycle.wait_for(lambda: lifecycle.has_editor_session(project, process.pid), 600,
                           'owned Editor automation session', {'Editor': process})
        def Initialized():
            path = folder / 'debug.log'
            return path.exists() and 'EditorApp::Initialize' in path.read_text(encoding='utf-8', errors='replace')
        lifecycle.wait_for(Initialized, 600, 'Editor initialization complete', {'Editor': process})
        time.sleep(1)
        with (folder / 'node.log').open('wb') as node_log:
            child = subprocess.run(['node', str(root / ('Test/MetallicAreaLightAcceptance.mjs' if args.metallic_probe else
                                               'Test/RasterPointLightAcceptance.mjs' if point_test
                                               else 'Test/RasterAreaLightAcceptance.mjs')), str(project),
                                    str(folder), 'initial'], cwd=root, stdout=node_log, stderr=subprocess.STDOUT,
                                   creationflags=subprocess.CREATE_NO_WINDOW, timeout=240)
        if child.returncode:
            raise RuntimeError('Capture driver failed: ' + str(folder / 'node.log'))
        lifecycle.close_owned_editor(process)
        summary['phases'][-1]['exit_code'] = process.returncode
        process = None
        time.sleep(1)
    summary['status'] = 'PASS'
except Exception as error:
    summary['error'] = str(error)
finally:
    lifecycle.close_owned_editor(process)
    external = [item for item in lifecycle.active_interactive_processes() if item['ProcessName'] == 'Editor']
    summary['restoration'] = []
    for path, (existed, data) in snapshots.items():
        if external:
            summary['restoration'].append({'path': str(path), 'restored': False})
            continue
        if existed:
            path.write_bytes(data)
        elif path.exists():
            path.unlink()
        restored = path.is_file() and path.read_bytes() == data if existed else not path.exists()
        summary['restoration'].append({'path': str(path), 'restored': restored})
    if not all(item['restored'] for item in summary['restoration']):
        summary['status'] = 'FAIL'
    (out / 'summary.json').write_text(json.dumps(summary, indent=2))
    print(json.dumps(summary, indent=2))
    lifecycle.release_run_lock()
sys.exit(0 if summary['status'] == 'PASS' else 1)
