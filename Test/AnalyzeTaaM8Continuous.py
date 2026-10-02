"""Continuous-frame transport, numerical validity and camera-history retirement checks."""
import csv
import json
import sys
from pathlib import Path
import numpy as np
from AnalyzeTaaSceneAcceptance import Decode, Rms

root = Path(sys.argv[1])
rows = list(csv.DictReader((root / 'm8_observations.csv').open()))
by_sample = {int(row['sample']): row for row in rows}
captures = {}
errors = []
for file in root.glob('session_*/capture.json'):
    value = json.loads(file.read_text())
    if value['state'] != 'completed' or len(value['frames']) != 1:
        errors.append(str(file)); continue
    frame = value['frames'][0]
    sample, kind = Path(frame['file']).stem.split('_')[:2]
    sample = int(sample)
    row = by_sample.get(sample)
    if row is None or int(row['frame']) != frame['engine_frame']:
        errors.append(f'{sample}: engine frame mismatch')
    captures.setdefault(sample, {})[kind] = (file.parent / frame['file'], value)
previous = None
adjacent = []
for sample, row in by_sample.items():
    kinds = captures.get(sample, {})
    expected = {'current', 'motion'} if sample % 128 in [96, 98] else {'current', 'motion', 'resolved', 'reactive'}
    if set(kinds) != expected: errors.append(f'{sample}: missing output')
    for kind, (file, manifest) in kinds.items():
        image = Decode(file, manifest['format'], manifest['width'], manifest['height'])
        if not np.isfinite(image).all(): errors.append(f'{sample}: nonfinite {kind}')
        if manifest['format'] == 26:
            raw = file.read_bytes()
            size = manifest['width'] * manifest['height'] * 4
            packed = np.frombuffer(raw, dtype='<u4', offset=len(raw) - size)
            if any(np.any(((packed >> shift) & 31) == 31) for shift in [6, 17, 27]):
                errors.append(f'{sample}: nonfinite packed {kind}')
        if kind == 'resolved':
            if previous is not None and 17 <= sample % 128 < 48:
                adjacent.append(Rms(image[..., :3] - previous[..., :3]))
            previous = image
    if 'resolved' not in kinds: previous = None
late = [row for row in rows if float(row['elapsed']) >= 90 and 16 <= int(row['local']) < 48]
memory = [int(row['render_texture_bytes']) for row in late]
validation = dict(line.split('=', 1) for line in (root / 'validation.txt').read_text().splitlines())
gates = {'three_minutes': float(rows[-1]['elapsed']) >= 179,
         'continuous_engine_frames': all(int(b['frame']) == int(a['frame']) + 1 for a, b in zip(rows, rows[1:])),
         'transport_and_finite': not errors, 'd3d12_errors_zero': validation['ERROR_CORRUPTION'] == '0',
         'retired_camera_histories_released': all(int(row['history_cameras']) <= 2
                                                for row in rows if int(row['sample']) >= 306),
         'late_fixed_size_memory_stable': bool(memory) and max(memory) == min(memory)}
report = {'gates': gates, 'pass': all(gates.values()), 'observations': len(rows),
          'captures': sum(len(value) for value in captures.values()), 'elapsed_seconds': float(rows[-1]['elapsed']),
          'max_history_cameras': max(int(row['history_cameras']) for row in rows),
          'final_history_cameras': int(rows[-1]['history_cameras']),
          'late_memory_bytes': {'min': min(memory), 'max': max(memory)},
          'stable_phase_adjacent_rms': {'max': max(adjacent), 'median': float(np.median(adjacent))},
          'errors': errors,
          'scope': 'Native production RG; camera idle/orbit, feature/AA toggles, resize, temporary cameras. '
                   'No Editor selection/gizmo or scene reload in this fixture; no perceptual flash oracle.'}
(root / 'continuous_analysis.json').write_text(json.dumps(report, indent=2))
print(json.dumps(report, indent=2))
sys.exit(0 if report['pass'] else 1)
