"""Analyze native radiance, presentation, continuity and sample retention in the live BMW Editor retest."""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image


def Sequence(root, name):
    manifests = list((root / name).rglob('capture.json'))
    if len(manifests) != 1:
        raise ValueError(f'{name}: expected one capture session, got {len(manifests)}')
    path = manifests[0]
    manifest = json.loads(path.read_text())
    frames = sorted(manifest['frames'], key=lambda frame: frame['capture_index'])
    if manifest['state'] != 'completed' or len(frames) != manifest['frame_count']:
        raise ValueError(f'{name}: incomplete transport')
    if not np.all(np.diff([frame['engine_frame'] for frame in frames]) == 1):
        raise ValueError(f'{name}: noncontiguous engine frames')
    w, h = manifest['width'], manifest['height']
    arrays = []
    for frame in frames:
        file = path.parent / frame['file']
        if manifest['output_format'] == 'png':
            array = np.asarray(Image.open(file).convert('RGB'), dtype=np.float32) / 255
        else:
            if manifest['format'] != 2:
                raise ValueError(f'{name}: expected RGBA32F radiance, got {manifest["format"]}')
            data = file.read_bytes()
            array = np.frombuffer(data, '<f4', offset=len(data) - w * h * 16).reshape(h, w, 4)[..., :3]
        if not np.isfinite(array).all():
            raise ValueError(f'{name}: nonfinite pixels')
        arrays.append(array)
    return np.stack(arrays), manifest


def Metrics(array):
    peak = np.max(array, axis=-1)
    return {'rgb_max': float(array.max()), 'rgb_mean': float(array.mean()),
            'rgb_p99': float(np.quantile(array, .99)), 'rgb_p999': float(np.quantile(array, .999)),
            'pixels_above_1_mean': float(np.mean(np.sum(peak > 1, axis=(1, 2)))),
            'pixels_above_10_mean': float(np.mean(np.sum(peak > 10, axis=(1, 2)))),
            'adjacent_rms': float(np.sqrt(np.mean(np.diff(array, axis=0).astype(np.float64) ** 2)))}


root = Path(sys.argv[1])
results = {}
gates = {}
frame_count = 0
for mode in ['disabled', 'realtime', 'reference']:
    for phase in ['early', 'raw', 'late', 'present']:
        name = mode + '_' + phase
        array, manifest = Sequence(root, name)
        expected = 32 if phase == 'late' else 3 if phase == 'present' else 16
        gates[name + '_transport'] = len(array) == expected
        gates[name + '_size'] = (manifest['width'], manifest['height']) == (896, 512)
        results[name] = Metrics(array)
        if phase != 'present':
            results[name]['rois'] = {
                'wall': Metrics(array[:, 32:144, 64:256]),
                'floor': Metrics(array[:, 380:480, 80:260])}
        frame_count += len(array)
    settings = json.loads((root / (mode + '_observed.json')).read_text())
    gates[mode + '_settings'] = (settings['hardware_ray_tracing'] and settings['debug_target'] == 0 and
                                settings['accumulation_mode'] == mode and settings['output_size'] == [896, 512])
    if mode != 'disabled':
        heat, _ = Sequence(root, mode + '_length')
        half = 2048 if mode == 'reference' else 32
        lengths = np.rint(np.where(heat[..., 0] > 0, half * (1 + heat[..., 0]), half * heat[..., 1]))
        frame_count += len(heat)
        results[mode + '_length'] = {'min': [float(frame.min()) for frame in lengths],
                                    'max': [float(frame.max()) for frame in lengths]}
        if mode == 'reference':
            gates['reference_progressive_all_pixels'] = (bool(np.all(lengths > 32)) and
                                                         bool(np.all(np.diff(lengths, axis=0) == 1)))
summary = json.loads((root / 'run_summary.json').read_text())
restoration = json.loads((root / 'restoration.json').read_text())
gates['driver_exit'] = summary['exit_code'] == 0 and (root / 'completed.json').exists()
gates['restoration'] = all(entry['restored'] for entry in restoration)
result = {'gates': gates, 'frames': frame_count, 'results': results,
          'limits': 'Sequential captures; early Reference already has samples. Radiance and presentation are separate.'}
(root / 'car_analysis.json').write_text(json.dumps(result, indent=2))
print(json.dumps(result, indent=2))
sys.exit(0 if all(gates.values()) else 1)
