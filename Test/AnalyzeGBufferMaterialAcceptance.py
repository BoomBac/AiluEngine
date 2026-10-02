"""Compare raw Cornell GBuffer captures at a fixed SceneView pose; no tonemapping color oracle."""
import json
import re
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw


def Decode(file, fmt, width, height):
    if file.suffix == '.png':
        return np.asarray(Image.open(file)).astype(float) / 255.
    data = file.read_bytes()
    if fmt == 28:
        return np.frombuffer(data, dtype='u1', offset=len(data) - width * height * 4).reshape(height, width, 4) / 255.
    if fmt == 34:
        return np.frombuffer(data, dtype='<f2', offset=len(data) - width * height * 4).reshape(height, width, 2).astype(float)
    if fmt == 26:
        packed = np.frombuffer(data, dtype='<u4', offset=len(data) - width * height * 4).reshape(height, width)
        channels = []
        for shift, bits in [(0, 6), (11, 6), (22, 5)]:
            value = (packed >> shift) & ((1 << (bits + 5)) - 1)
            mantissa, exponent = value & ((1 << bits) - 1), value >> bits
            channels.append(np.where(exponent == 0, mantissa * 2.**(-14 - bits),
                                     (1 + mantissa / 2.**bits) * 2.**(exponent.astype(float) - 15)))
        return np.stack(channels, axis=-1)
    raise ValueError(f'Unsupported format {fmt}')


def UnpackNormal(value):
    xy = value * 2 - 1
    normal = np.concatenate([xy, 1 - abs(xy).sum(axis=-1, keepdims=True)], axis=-1)
    correction = np.clip(-normal[..., 2:3], 0, 1)
    normal[..., :2] += np.where(normal[..., :2] >= 0, -correction, correction)
    return normal / np.maximum(np.linalg.norm(normal, axis=-1, keepdims=True), 1e-8)


def Main(root):
    results, values, previews = {}, {}, []
    for run in ['before_native', 'after_upload', 'after_final']:
        run_path = root / run
        if not run_path.exists():
            continue
        for manifest_path in sorted(run_path.glob('*/session_*/capture.json')):
            manifest = json.loads(manifest_path.read_text())
            name = manifest_path.parent.parent.name
            frames = sorted(manifest['frames'], key=lambda frame: frame['capture_index'])
            images = np.stack([Decode(manifest_path.parent / frame['file'], manifest['format'],
                                      manifest['width'], manifest['height']) for frame in frames])
            valid = manifest['state'] == 'completed' and len(frames) == manifest['frame_count'] == 8
            valid &= [frame['capture_index'] for frame in frames] == list(range(8))
            valid &= all(b['engine_frame'] == a['engine_frame'] + 1 for a, b in zip(frames, frames[1:]))
            valid &= bool(np.isfinite(images).all())
            valid &= (manifest['width'], manifest['height']) == (1088, 576)
            status = json.loads((run_path / (name + '_status.json')).read_text())
            if name.startswith(('normal', 'basecolor')):
                expected_resource = '_GBuffer0' if name.startswith('normal') else '_GBuffer1'
                valid &= status['pass_name'] == 'DeferedGeometryPass' and status['resource_name'] == expected_resource
            values[run, name] = images
            results[run + '/' + name] = {'valid': valid, 'frames': len(frames), 'format': manifest['format'],
                'dimensions': [manifest['width'], manifest['height']],
                'engine_frames': [frame['engine_frame'] for frame in frames],
                'adjacent_rms': float(np.sqrt(np.mean(np.diff(images, axis=0)**2)))}
            display = UnpackNormal(images[-1]) * .5 + .5 if name.startswith('normal') else images[-1, ..., :3]
            image = Image.fromarray((np.clip(display, 0, 1) * 255).astype('uint8'))
            image.save(run_path / (name + '.png'))
            if name in ['basecolor_aa0', 'normal_aa0']:
                tile = Image.new('RGB', (544, 312), (28, 28, 28))
                tile.paste(image.resize((544, 288)), (0, 24))
                ImageDraw.Draw(tile).text((8, 5), run + ' / ' + name, fill='white')
                previews.append(tile)
        log = (run_path / 'engine.log').read_text(errors='replace')
        results[run + '/d3d12'] = {'error_corruption_messages': len(re.findall(r'severity=[01],', log)),
                                 'warning_messages': len(re.findall(r'severity=2,', log))}
        restored = json.loads((run_path / 'restoration.json').read_text())
        results[run + '/restoration'] = all(entry['restored'] for entry in restored)
    rois = {'floor': (70, 470, 200, 535), 'front': (275, 345, 305, 410), 'side': (395, 345, 450, 405)}
    expected_colors = {'floor': [1, 1, 1], 'front': [.8, .8, .8], 'side': [0, 1, .00784313725490196]}
    expected_normals = {'floor': [0, 1, 0], 'front': [0, 0, -1], 'side': [1, 0, 0]}
    gates = {}
    for run in ['before_native', 'after_upload', 'after_final']:
        if (run, 'normal_aa0') not in values:
            continue
        for mode in [0, 2]:
            for kind in ['basecolor', 'normal']:
                images = values[run, kind + '_aa' + str(mode)]
                images = UnpackNormal(images) if kind == 'normal' else images[..., :3]
                expected = expected_normals if kind == 'normal' else expected_colors
                for name, (x0, y0, x1, y1) in rois.items():
                    samples = images[:, y0:y1, x0:x1]
                    error = float(abs(samples - expected[name]).max())
                    temporal_rms = float(np.sqrt(np.mean(np.diff(samples, axis=0)**2)))
                    key = f'{run}/{kind}_aa{mode}/{name}'
                    results[key] = {'mean': samples.mean(axis=(0, 1, 2)).tolist(), 'expected': expected[name],
                                    'max_error': error, 'adjacent_rms': temporal_rms}
                    if run != 'before_native':
                        gates[key] = error <= .003 and temporal_rms <= 1e-6
        if run != 'before_native':
            gates[run + '/transport'] = all(entry['valid'] for key, entry in results.items()
                                          if key.startswith(run + '/') and isinstance(entry, dict) and 'valid' in entry)
            gates[run + '/restoration'] = results[run + '/restoration']
            gates[run + '/d3d12'] = results[run + '/d3d12']['error_corruption_messages'] == 0
            camera_match = True
            for mode in [0, 2]:
                before = json.loads((root / 'before_native' / f'camera_aa{mode}.json').read_text())
                after = json.loads((root / run / f'camera_aa{mode}.json').read_text())
                position_error = float(abs(np.array(before['position']) - after['position']).max())
                camera_match &= position_error <= 1e-5
                camera_match &= all(before[key] == after[key] for key in
                                    ['pitch_degrees', 'yaw_degrees', 'fov_degrees', 'width', 'height', 'aa_mode'])
                results[f'{run}/camera_aa{mode}'] = {'max_position_error': position_error, 'tolerance': 1e-5}
            gates[run + '/same_camera'] = camera_match
            original = json.loads((root / run / 'original_state.json').read_text())
            restored = json.loads((root / run / 'restored_state.json').read_text())
            gates[run + '/same_scene'] = all(original[key] == restored[key] for key in
                                           ['scene_guid', 'scene_edit_revision', 'scene_structure_revision', 'entity_count'])
    results['gates'] = gates
    results['pass'] = 'after_final/restoration' in gates and all(gates.values())
    results['roi_scope'] = {'rectangles_xyxy': rois, 'dimensions': [1088, 576],
        'note': 'Fixed interior pixels in this NewScene pose. Whole-frame jitter differences include coverage edges.'}
    canvas = Image.new('RGB', (544 * 2, 312 * ((len(previews) + 1) // 2)), (28, 28, 28))
    for index, tile in enumerate(previews):
        canvas.paste(tile, ((index % 2) * 544, (index // 2) * 312))
    canvas.save(root / 'comparison.png')
    (root / 'analysis.json').write_text(json.dumps(results, indent=2))
    print(json.dumps({'pass': results['pass'], 'gates': gates}, indent=2))
    return 0 if results['pass'] else 1


if __name__ == '__main__':
    sys.exit(Main(Path(sys.argv[1])))
