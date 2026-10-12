import bpy
import addon_utils
import hashlib
import json
import shutil
import zipfile
from pathlib import Path
from datetime import datetime

root = Path(r'C:\Users\liusongchen_e\OneDrive\AiluEngine')
archive_path = root / 'Tools/Blender/dist/ailu_live_link.zip'
backup = root / '.codex/tmp' / ('blender-addon-install-' + datetime.now().strftime('%Y%m%d-%H%M%S'))
backup.mkdir(parents=True, exist_ok=False)
addon_dir = Path(bpy.utils.user_resource('SCRIPTS', path='addons', create=True)) / 'ailu_live_link'
preferences = Path(bpy.utils.user_resource('CONFIG')) / 'userpref.blend'
if addon_dir.exists():
    shutil.copytree(addon_dir, backup / 'ailu_live_link')
if preferences.exists():
    shutil.copy2(preferences, backup / 'userpref.blend')
addon_utils.disable('ailu_live_link', default_set=False)
result = bpy.ops.preferences.addon_install(filepath=str(archive_path), overwrite=True)
if 'FINISHED' not in result:
    raise RuntimeError('Blender add-on installation failed: ' + str(result))
addon_utils.modules_refresh()
module = addon_utils.enable('ailu_live_link', default_set=True, persistent=True)
if module is None or not addon_utils.check('ailu_live_link')[1]:
    raise RuntimeError('Installed add-on did not enable')
files = []
with zipfile.ZipFile(archive_path) as archive:
    for entry in archive.infolist():
        if entry.is_dir():
            continue
        relative = Path(entry.filename).relative_to('ailu_live_link')
        target = addon_dir / relative
        expected = archive.read(entry)
        if target.read_bytes() != expected:
            raise RuntimeError('Installed file differs from package: ' + str(target))
        files.append({'path': str(relative), 'sha256': hashlib.sha256(expected).hexdigest()})
from ailu_live_link import addon
if not bpy.app.timers.is_registered(addon._timer):
    raise RuntimeError('Live Link timer not registered')
if addon._on_load not in bpy.app.handlers.load_post:
    raise RuntimeError('Persistence reload handler not registered')
if 'FINISHED' not in bpy.ops.wm.save_userpref():
    raise RuntimeError('Failed to save enabled add-on preference')
report = {'blender_version': bpy.app.version_string, 'destination': str(addon_dir), 'backup': str(backup),
          'enabled': True, 'files': files}
(backup / 'installation.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print('AILU_ADDON_INSTALL_OK', json.dumps({key: value for key, value in report.items() if key != 'files'}))
