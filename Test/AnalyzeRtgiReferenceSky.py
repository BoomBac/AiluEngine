"""Verify real Reference GPU output averages fixed pixels, including raster sky, without sample rejection."""
import json
import sys
from pathlib import Path

import numpy as np
from AnalyzeTaaRtgiAcceptance import Load, Length, Rms

root = Path(sys.argv[1])
rows, observations, captures, errors = Load(root)
gates = {'transport_finite': len(rows) == 640 and not errors,
         'continuity': bool(np.all(np.diff([int(row['engine_frame']) for row in rows]) == 1))}
results = {}
for backend, start in [('compute', 192), ('dxr', 512)]:
    frames = [captures[start + j] for j in range(64)]
    sky = np.logical_and.reduce([frame['depth'][..., 0] == 0 for frame in frames[:32]])
    lengths = [Length(frame['heat'], True) for frame in frames]
    mean = np.zeros_like(frames[0]['raw'][..., :3], dtype=np.float64)
    mean_errors = []
    sky_lengths = []
    for j in range(32):
        mean += (frames[j]['raw'][..., :3] - mean) / (j + 1)
        mean_errors.append(Rms(mean - frames[j]['history'][..., :3]))
        sky_lengths.append(float(lengths[j][sky].min()) if sky.any() else 0)
    name = backend + '_reference'
    gates[name + '_sky_present'] = int(sky.sum()) > 1000
    gates[name + '_sky_progressive'] = sky_lengths == list(range(1, 33))
    gates[name + '_fixed_pixel_mean'] = max(mean_errors) < .001
    gates[name + '_camera_reset'] = float(lengths[48].max()) <= 1
    gates[name + '_explicit_reset'] = float(lengths[56].max()) <= 1
    results[name] = {'sky_pixels': int(sky.sum()), 'sky_history_length_min': sky_lengths,
                     'fixed_pixel_mean_rms_max': max(mean_errors)}
summary = json.loads((root.parent / 'run_summary.json').read_text())
validation = dict(line.split('=', 1) for line in (root / 'validation.txt').read_text().splitlines())
restoration = json.loads((root.parent / 'restoration.json').read_text())
gates['fixture_exit'] = summary['exit_code'] == 0 and validation['failed'] == '0'
gates['d3d12_error_corruption'] = validation['ERROR_CORRUPTION'] == '0'
gates['restoration'] = all(entry['restored'] for entry in restoration)
result = {'gates': gates, 'results': results, 'errors': errors}
(root / 'reference_sky_analysis.json').write_text(json.dumps(result, indent=2))
print(json.dumps(result, indent=2))
sys.exit(0 if all(gates.values()) else 1)
