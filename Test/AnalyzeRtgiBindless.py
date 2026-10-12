"""Verify divergent mesh/material reads against known red and blue primary-ray emission."""
import json
import sys
from pathlib import Path

import numpy as np
from AnalyzeTaaRtgiAcceptance import Load


def Interior(mask):
    result = mask.copy()
    for dy in range(-2, 3):
        for dx in range(-2, 3):
            result &= np.roll(mask, (dy, dx), (0, 1))
    result[:2] = result[-2:] = False
    result[:, :2] = result[:, -2:] = False
    return result


root = Path(sys.argv[1])
rows, observations, captures, errors = Load(root)
gates = {'transport_finite': len(rows) == 640 and len(captures) == 640 and not errors,
         'continuity': bool(np.all(np.diff([int(row['engine_frame']) for row in rows]) == 1))}
results = {}
for backend, offset in [('compute', 0), ('dxr', 320)]:
    for mode, start in [('baseline', offset), ('disabled', offset + 256)]:
        max_error = 0.0
        counts = []
        for j in range(32):
            frame = captures[start + j]
            depth = frame['depth'][..., 0]
            red = Interior(depth > .0181)
            blue = Interior((depth > 0) & (depth < .0181))
            counts.append([int(red.sum()), int(blue.sum())])
            for mask, expected in [(red, [1, 0, 0]), (blue, [0, 0, 1])]:
                if mask.any():
                    max_error = max(max_error, float(np.abs(frame['raw'][..., :3][mask] - expected).max()))
        name = backend + '_' + mode
        gates[name + '_both_resources'] = all(min(count) > 1000 for count in counts)
        gates[name + '_known_emission'] = max_error < .001
        results[name] = {'interior_pixels_min': np.min(counts, axis=0).tolist(), 'rgb_max_error': max_error}
summary = json.loads((root.parent / 'run_summary.json').read_text())
validation = dict(line.split('=', 1) for line in (root / 'validation.txt').read_text().splitlines())
restoration = json.loads((root.parent / 'restoration.json').read_text())
gates['fixture_exit'] = summary['exit_code'] == 0 and validation['failed'] == '0'
gates['d3d12_error_corruption'] = validation['ERROR_CORRUPTION'] == '0'
gates['restoration'] = all(entry['restored'] for entry in restoration)
result = {'gates': gates, 'results': results, 'errors': errors}
(root / 'bindless_analysis.json').write_text(json.dumps(result, indent=2))
print(json.dumps(result, indent=2))
sys.exit(0 if all(gates.values()) else 1)
