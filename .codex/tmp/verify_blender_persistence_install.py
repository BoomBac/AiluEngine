import bpy
import addon_utils
import hashlib
import json
import zipfile
from pathlib import Path

root = Path(r'C:\Users\liusongchen_e\OneDrive\AiluEngine')
addon_dir = Path(bpy.utils.user_resource('SCRIPTS', path='addons')) / 'ailu_live_link'
module = addon_utils.enable('ailu_live_link', default_set=True, persistent=True)
if module is None or not addon_utils.check('ailu_live_link')[1]:
    raise RuntimeError('Installed add-on failed to enable')
from ailu_live_link import addon
assert bpy.app.timers.is_registered(addon._timer), 'Live Link timer missing'
assert addon._on_load in bpy.app.handlers.load_post, 'Persistence reload handler missing'
files = []
with zipfile.ZipFile(root / 'Tools/Blender/dist/ailu_live_link.zip') as archive:
    for entry in archive.infolist():
        if entry.is_dir():
            continue
        relative = Path(entry.filename).relative_to('ailu_live_link')
        data = archive.read(entry)
        assert (addon_dir / relative).read_bytes() == data, str(relative)
        files.append({'path': str(relative), 'sha256': hashlib.sha256(data).hexdigest()})
assert 'FINISHED' in bpy.ops.wm.save_userpref(), 'Preference save failed'
backup = sorted((root / '.codex/tmp').glob('blender-addon-install-*'))[-1]
report = {'blender_version': bpy.app.version_string, 'destination': str(addon_dir), 'backup': str(backup),
          'enabled': True, 'reload_handler_registered': True, 'files': files}
(backup / 'installation.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print('AILU_ADDON_INSTALL_OK', json.dumps({key: value for key, value in report.items() if key != 'files'}))
