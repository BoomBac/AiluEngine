"""Verify actual LTC highlights inherit the metal color under one identical white emitter."""
import json
import sys
from pathlib import Path
import numpy as np
sys.path.insert(0, str(Path(__file__).parent / 'BlenderLinkValidation'))
from material_capture_pixels import LoadFrame

output = Path(sys.argv[1])
checks = []
baseline = None
for channel, color in enumerate(['Red', 'Green', 'Blue']):
    path = next((output / ('Probe' + color)).glob('session_*/capture.json'))
    manifest = json.loads(path.read_text())
    assert manifest['state'] == 'completed' and len(manifest['frames']) == 3
    frames = manifest['frames']
    assert all(b['engine_frame'] == a['engine_frame'] + 1 for a, b in zip(frames, frames[1:]))
    values = [LoadFrame(path.parent / item['file'], manifest)[..., :3] for item in frames]
    assert all(np.isfinite(value).all() and np.array_equal(values[0], value) for value in values)
    mean = values[-1].mean(axis=(0, 1))
    if color == 'Red':
        baseline = values[-1]
    other = max(mean[index] for index in range(3) if index != channel)
    checks.append(dict(color=color, mean=mean.tolist(), passed=bool(mean[channel] > max(.0001, other * 4))))
path = next((output / 'ScaledArea').glob('session_*/capture.json'))
manifest = json.loads(path.read_text())
scaled = LoadFrame(path.parent / manifest['frames'][-1]['file'], manifest)[..., :3]
checks.append(dict(name='same_world_emitter_area', passed=bool(np.array_equal(scaled, baseline)),
                   max_error=float(np.max(abs(scaled - baseline)))))
result = dict(status='PASS' if all(item['passed'] for item in checks) else 'FAIL', checks=checks)
(output / 'metal_analysis.json').write_text(json.dumps(result, indent=2))
print(json.dumps(result, indent=2))
sys.exit(0 if result['status'] == 'PASS' else 1)
