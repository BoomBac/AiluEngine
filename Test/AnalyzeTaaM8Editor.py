"""Validate actual Editor sequences; preserve the limits of routed-input checks."""
import json
import sys
from pathlib import Path
import numpy as np
from AnalyzeTaaSceneAcceptance import Decode, Rms

root = Path(sys.argv[1])
sequences = {}
errors = []
for file in sorted(root.glob('*/session_*/capture.json')):
    manifest = json.loads(file.read_text())
    frames = manifest['frames']
    name = file.parent.parent.name
    valid = manifest['state'] == 'completed' and len(frames) == manifest['frame_count']
    valid &= [frame['capture_index'] for frame in frames] == list(range(len(frames)))
    valid &= all(b['engine_frame'] == a['engine_frame'] + 1 for a, b in zip(frames, frames[1:]))
    previous = None
    differences = []
    for frame in frames:
        image_file = file.parent / frame['file']
        image = Decode(image_file, manifest['format'], manifest['width'], manifest['height'])
        valid &= bool(np.isfinite(image).all())
        if manifest['format'] == 26:
            raw = image_file.read_bytes()
            size = manifest['width'] * manifest['height'] * 4
            packed = np.frombuffer(raw, dtype='<u4', offset=len(raw) - size)
            valid &= all(bool(np.all(((packed >> shift) & 31) != 31)) for shift in [6, 17, 27])
        if previous is not None: differences.append(Rms(image[..., :3] - previous[..., :3]))
        previous = image
    if not valid: errors.append(name)
    sequences[name] = {'valid': valid, 'frames': len(frames),
                       'dimensions': [manifest['width'], manifest['height']],
                       'max_adjacent_rms': max(differences, default=0)}
completion = json.loads((root / 'completion.json').read_text())
original = json.loads((root / 'original_state.json').read_text())
restored = json.loads((root / 'restored_state.json').read_text())
gates = {'transport_and_finite': bool(sequences) and not errors,
         'edit_scene_returned': all(original.get(key) == restored.get(key)
                                    for key in ['editor_mode', 'scene_guid', 'scene_name', 'entity_count']),
         'files_restored': all(value['restored'] for value in json.loads((root / 'restoration.json').read_text()))}
if 'play_stop_cycles' in completion:
    gates['three_play_stop_cycles'] = completion['play_stop_cycles'] == 3 and len(sequences) == 7
else:
    gates['play_entered'] = completion['play_entered']
    gates['selection_observed'] = completion['selection_observed']
    gates['actual_resize'] = len({tuple(value['dimensions']) for name, value in sequences.items()
                                  if name.startswith('resize_')}) == 3
report = {'pass': all(gates.values()), 'gates': gates, 'sequences': sequences,
          'capture_count': sum(value['frames'] for value in sequences.values()),
          'completion': completion, 'errors': errors,
          'scope': 'Image integrity, window resize, selection and play/stop routing. '
                   'No first-frame seed or perceptual ghosting oracle; no asserted gizmo transform movement.'}
(root / 'editor_analysis.json').write_text(json.dumps(report, indent=2))
print(json.dumps({key: value for key, value in report.items() if key != 'sequences'}, indent=2))
sys.exit(0 if report['pass'] else 1)
