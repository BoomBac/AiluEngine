"""Create a disposable first-hit fixture beside Editor.exe and capture both path tracing backends."""
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

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--editor', type=Path, required=True)
args = parser.parse_args()
editor = args.editor.resolve()
if any(p['ProcessName'] == 'Editor' for p in lifecycle.active_interactive_processes()):
    raise RuntimeError('An existing Editor is running; preserve its state')
output = editor.parent / ('OidnM4Editor_' + time.strftime('%Y%m%d_%H%M%S'))
output.mkdir()
print('Evidence=' + str(output), flush=True)
project = output / 'project'
(project / 'Assets').mkdir(parents=True)
(project / 'Library').mkdir()
spec = importlib.util.spec_from_file_location('scene_helpers', root / 'Tools/generate_ddgi_test_scenes.py')
helpers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helpers)
config = json.loads((root / 'Engine/Res/ProjTemplate/empty/empty.json').read_text())
config.update(_project_guid=str(uuid.uuid4()), _project_name='OIDN FirstHit', _startup_scene='project://guides.almap')
(project / 'guides.json').write_text(json.dumps(config, indent=2))
database = {'assets': []}
materials = {}
for name, albedo, emission, roughness, metallic in [
    ('White', (.65, .65, .65), (0, 0, 0), .8, 0),
    ('Dark', (.05, .05, .05), (0, 0, 0), .8, 0),
    ('Red', (.7, .06, .03), (0, 0, 0), .8, 0),
    ('Metal', (.9, .65, .2), (0, 0, 0), .25, 1),
    ('Glass', (1, 1, 1), (0, 0, 0), .01, 0),
    ('Emitter', (1, 1, 1), (8, 7, 6), .8, 0),
]:
    document = helpers.material_document('OidnM4' + name, albedo, emission, roughness, metallic)
    if name == 'Glass':
        for field in document['_float_properties']:
            if field['_name'] == '_Transmission': field['_value'] = 1.0
            if field['_name'] == '_IOR': field['_value'] = 1.5
    relative = name + '.alasset'
    (project / 'Assets' / relative).write_text(json.dumps(document))
    materials[name] = document['_header']['_guid']
    helpers.register_asset(database, materials[name], 'project://' + relative, helpers.ASSET_TYPE_MATERIAL)
entities = [helpers.box('Floor', (0, -1, 3), (8, .1, 9), materials['White']),
            helpers.box('Light', (0, 3, 3), (3, .1, 3), materials['Emitter'])]
for row in range(8):
    for column in range(16):
        entities.append(helpers.box(f'Checker_{row}_{column}', ((column - 7.5) * .45, (row - 3.5) * .45, 6),
                                   (.45, .45, .05), materials['White' if (row + column) % 2 else 'Dark']))
for name, x in [('Red', -1.4), ('Metal', 0), ('Glass', 1.4)]:
    entities.append(helpers.ball(name, (x, 0, 2.8), 1.5, materials[name]))
volume = helpers.postprocess_volume('OIDNVolume', 1)
volume['_postprocess_volume_component']['_settings'].update(
    _bloom={'_enabled': False}, _tone_mapping={'_mode': 'kAgx', '_exposure': 1},
    _depth_of_field={'_enabled': False})
entities.append(volume)
scene = helpers.scene_document('OIDNFirstHit', 'Matte, rough metal, glass and checker edges', entities)
(project / 'Assets/guides.almap').write_text(json.dumps(scene))
helpers.register_asset(database, scene['_header']['_guid'], 'project://guides.almap', helpers.ASSET_TYPE_SCENE)
(project / 'Library/AssetDatabase.json').write_text(json.dumps(database))
snapshots = {p: (p.exists(), p.read_bytes() if p.is_file() else None) for p in lifecycle.SHARED_STATE}
backup = output / 'shared_backups'
backup.mkdir()
for index, (path, (existed, data)) in enumerate(snapshots.items()):
    if data is not None: (backup / str(index)).write_bytes(data)
(backup / 'index.json').write_text(json.dumps([
    {'path': str(p), 'existed': existed, 'file': str(index)}
    for index, (p, (existed, _)) in enumerate(snapshots.items())], indent=2))
summary = {'status': 'FAIL', 'editor': str(editor), 'output': str(output)}
process = None
try:
    settings = json.loads((root / 'Editor/EditorConfig.json').read_text(encoding='utf-8-sig'))
    settings.update(_scene_path='project://guides.almap', _window_size=[1000, 720])
    (root / 'Editor/EditorConfig.json').write_text(json.dumps(settings, indent=2))
    engine_config = json.loads((root / 'Editor/EngineConfig.json').read_text(encoding='utf-8-sig'))
    engine_config.update(_enable_d3d12_debug_layer=True, _enable_pix=False, _enable_rdc=False)
    (root / 'Editor/EngineConfig.json').write_text(json.dumps(engine_config, indent=2))
    with (output / 'stdout.log').open('wb') as log:
        process = start_editor([str(editor), str(project / 'guides.json')], root, log,
                               dict(os.environ, AILU_BLENDER_LIVE_LINK_PORT=str(lifecycle.find_free_port())),
                               output / 'debug.log')
        summary['pid'] = process.pid
        lifecycle.wait_for(lambda: lifecycle.has_editor_session(project, process.pid), 480, 'session', {'Editor': process})
        with (output / 'capture.log').open('wb') as capture_log:
            result = subprocess.run(['node', str(root / 'Test/OidnGuidesEditorAcceptance.mjs'),
                                     str(project), str(output)], cwd=root, stdout=capture_log,
                                    stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW, timeout=300)
        if result.returncode: raise RuntimeError('Capture failed; see capture.log')
    summary['status'] = 'PASS'
except Exception as error:
    summary['error'] = str(error)
finally:
    lifecycle.close_owned_editor(process)
    if (root / 'FileAppender_log.txt').exists():
        (output / 'FileAppender_log.txt').write_bytes((root / 'FileAppender_log.txt').read_bytes())
    summary['restoration'] = []
    for path, (existed, data) in snapshots.items():
        if existed: path.write_bytes(data)
        elif path.exists(): path.unlink()
        summary['restoration'].append({'path': str(path),
                                       'restored': path.read_bytes() == data if existed else not path.exists()})
    (output / 'summary.json').write_text(json.dumps(summary, indent=2))
    print(json.dumps(summary, indent=2), flush=True)
sys.exit(0 if summary['status'] == 'PASS' else 1)
