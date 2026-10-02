"""Measure stationary screen displacement independently of TAA's reprojection implementation."""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from AnalyzeTaaSceneAcceptance import Decode, Rms


def Halton(index, base):
    value, fraction = 0., 1.
    while index:
        fraction /= base
        value += fraction * (index % base)
        index //= base
    return value


def Sample(array, dx, dy):
    y, x = np.mgrid[:array.shape[0], :array.shape[1]]
    x, y = x + dx, y + dy
    ix, iy = np.floor(x).astype(int), np.floor(y).astype(int)
    fx, fy = x - ix, y - iy
    def At(ox, oy):
        return array[np.clip(iy + oy, 0, array.shape[0] - 1), np.clip(ix + ox, 0, array.shape[1] - 1)]
    return (At(0, 0) * (1 - fx) + At(1, 0) * fx) * (1 - fy) + (At(0, 1) * (1 - fx) + At(1, 1) * fx) * fy


def Displacement(array, reference):
    # Solve for the subpixel translation using image gradients; crop out boundaries and moving effects.
    dx, dy = 0., 0.
    gy, gx = np.gradient(reference)
    mask = (gx * gx + gy * gy) > .25
    mask[:4] = mask[-4:] = False
    mask[:, :4] = mask[:, -4:] = False
    gradients = np.stack([gx[mask], gy[mask]], axis=1)
    for _ in range(5):
        residual = (Sample(array, dx, dy) - reference)[mask]
        delta = np.linalg.lstsq(gradients, residual, rcond=None)[0]
        dx -= delta[0]
        dy -= delta[1]
    return [dx, dy]


def Main(root):
    results = {}
    sequences = {}
    camera = json.loads((root / 'camera_aa2.json').read_text())
    # Verify the frame-to-Halton mapping against the measured camera rather than assuming the seed offset.
    seed_offsets = range(-2, 3)
    jitter = np.array(camera['jitter'][2:]) * [camera['width'], camera['height']]
    seed_offset = min(seed_offsets, key=lambda o: np.linalg.norm(jitter -
        [Halton((camera['frame'] + o) % 1024, 2) - .5, .5 - Halton((camera['frame'] + o) % 1024, 3)]))
    results['camera_seed_offset'] = seed_offset
    for capture_path in sorted(root.glob('*/session_*/capture.json')):
        manifest = json.loads(capture_path.read_text())
        name = capture_path.parent.parent.name
        frames = sorted(manifest['frames'], key=lambda f: f['capture_index'])
        arrays = []
        for frame in frames:
            source = capture_path.parent / frame['file']
            arrays.append(np.asarray(Image.open(source)).astype(np.float32) if source.suffix == '.png' else
                          Decode(source, manifest['format'], manifest['width'], manifest['height']))
        values = np.stack(arrays)
        sequences[name] = values
        result = {'count': len(frames), 'dimensions': [manifest['width'], manifest['height']],
                  'completed': manifest['state'] == 'completed' and len(frames) == manifest['frame_count'],
                  'continuous': all(b['engine_frame'] == a['engine_frame'] + 1 for a, b in zip(frames, frames[1:])),
                  'finite': bool(np.isfinite(values).all()), 'adjacent_rms': Rms(np.diff(values, axis=0)),
                  'adjacent_p99': float(np.percentile(abs(np.diff(values, axis=0)), 99))}
        if name == 'taa_motion':
            result['max_motion_pixels'] = float(np.max(abs(values[..., :2]) * [manifest['width'], manifest['height']]))
        if name == 'taa_reactive':
            result['coverage_fraction'] = float(np.mean(values > 0))
        if name.startswith('tone_'):
            # Building silhouette and window: fixed opaque geometry, no transparent layer or sky sun.
            roi = values[:, 80:445, 510:870, :3].mean(axis=-1)
            shifts = np.array([Displacement(a, roi.mean(axis=0)) for a in roi])
            predicted = np.array([[Halton((f['engine_frame'] + seed_offset) % 1024, 2) - .5,
                                   .5 - Halton((f['engine_frame'] + seed_offset) % 1024, 3)] for f in frames])
            result['roi'] = [510, 80, 870, 445]
            result['roi_adjacent_rms_8bit'] = Rms(np.diff(roi, axis=0))
            result['displacement_std_pixels'] = shifts.std(axis=0).tolist()
            result['displacement_range_pixels'] = np.ptp(shifts, axis=0).tolist()
            result['jitter_response_slope'] = [float(np.cov(shifts[:, i], predicted[:, i])[0, 1] /
                                                   np.var(predicted[:, i], ddof=1)) for i in range(2)]
            result['shifts_pixels'] = shifts.tolist()
            worst = int(np.argmax(np.mean(np.diff(roi, axis=0)**2, axis=(1, 2))))
            sheet = Image.new('RGB', (720, 390), 'white')
            for column, index in enumerate([worst, worst + 1]):
                sheet.paste(Image.fromarray(values[index].astype(np.uint8)).crop((510, 80, 870, 445)), (column * 360, 25))
                ImageDraw.Draw(sheet).text((column * 360 + 5, 5), f'{name} frame {index}', fill='black')
            sheet.save(root / (name + '_worst.png'))
            Image.fromarray(values[-1].astype(np.uint8)).save(root / (name + '_preview.png'))
        results[name] = result
    results['restoration'] = all(e['restored'] for e in json.loads((root / 'restoration.json').read_text()))
    log = (root / 'engine.log').read_text(errors='replace')
    import re
    results['d3d12_errors'] = len(re.findall(r'D3D12 debug layer[^\n]*severity=[01]', log))
    if len(sys.argv) > 2:
        before_root = Path(sys.argv[2])
        before = json.loads((before_root / 'jitter_analysis.json').read_text())
        old, new = before['tone_aa2'], results['tone_aa2']
        results['comparison'] = {
            'roi_variation_reduction_percent': 100 * (1 - new['roi_adjacent_rms_8bit'] / old['roi_adjacent_rms_8bit']),
            'screen_stability_pass': max(new['displacement_range_pixels']) < .1 and
                max(abs(np.array(new['jitter_response_slope']))) < .1 and
                new['roi_adjacent_rms_8bit'] < old['roi_adjacent_rms_8bit'] * .5,
            'transport_pass': all(v['completed'] and v['continuous'] and v['finite'] for v in results.values()
                if isinstance(v, dict) and 'completed' in v),
            'criteria': 'Original-pose static opaque ROI: displacement <0.1px, jitter response <0.1, RMS reduction >50%.'}
        results['comparison']['pass'] = results['comparison']['screen_stability_pass'] and \
            results['comparison']['transport_pass'] and results['restoration'] and results['d3d12_errors'] == 0
        if 'tone_return' in results:
            observations = json.loads((root / 'motion_camera_observations.json').read_text())
            positions = np.array([o['position'] for o in observations])
            actual_motion = float(np.ptp(positions[:, 0]))
            results['comparison']['actual_camera_x_range'] = actual_motion
            results['comparison']['camera_motion_and_settling_pass'] = actual_motion > .1 and \
                max(results['tone_return']['displacement_range_pixels']) < .1
            results['comparison']['pass'] &= results['comparison']['camera_motion_and_settling_pass']
        old_paths = sorted((before_root / 'tone_aa2').glob('session_*/capture.json'))
        old_manifest = json.loads(old_paths[0].read_text())
        old_frames = sorted(old_manifest['frames'], key=lambda f: f['capture_index'])
        animation = []
        for index in range(min(len(old_frames), len(sequences['tone_aa2']))):
            sheet = Image.new('RGB', (1080, 570), 'white')
            old_image = Image.open(old_paths[0].parent / old_frames[index]['file'])
            new_image = Image.fromarray(sequences['tone_aa2'][index].astype(np.uint8))
            for column, (picture, label) in enumerate([(old_image, 'Before'), (new_image, 'After')]):
                sheet.paste(picture.crop((510, 80, 870, 445)).resize((540, 548)), (column * 540, 22))
                ImageDraw.Draw(sheet).text((column * 540 + 5, 5), label + ' TAA, stationary camera', fill='black')
            animation.append(sheet)
        animation[0].save(root / 'before_after.gif', save_all=True, append_images=animation[1:], duration=80, loop=0)
        animation[0].save(root / 'before_after.png')
    (root / 'jitter_analysis.json').write_text(json.dumps(results, indent=2))
    print(json.dumps({k: {a: b for a, b in v.items() if a != 'shifts_pixels'} if isinstance(v, dict) else v
                      for k, v in results.items()}, indent=2))
    return 0 if results.get('comparison', {}).get('pass', True) else 1


if __name__ == '__main__':
    sys.exit(Main(Path(sys.argv[1])))
