"""Analyze matched production TAA/current/motion captures from --scene; retain local edge evidence."""
import csv
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw


def Decode(path, fmt, width, height):
    data = path.read_bytes()
    if fmt == 26:
        packed = np.frombuffer(data, dtype='<u4', offset=len(data) - width * height * 4).reshape(height, width)
        channels = []
        for shift, bits in [(0, 6), (11, 6), (22, 5)]:
            value = (packed >> shift) & ((1 << (bits + 5)) - 1)
            mantissa, exponent = value & ((1 << bits) - 1), value >> bits
            channels.append(np.where(exponent == 0, mantissa * 2.**(-14 - bits),
                                     (1. + mantissa / 2.**bits) * 2.**(exponent.astype(float) - 15)))
        return np.stack(channels, axis=-1).astype(np.float32)
    if fmt == 10:
        return np.frombuffer(data, dtype='<f2', offset=len(data) - width * height * 8).reshape(height, width, 4).astype(np.float32)
    if fmt == 34:
        return np.frombuffer(data, dtype='<f2', offset=len(data) - width * height * 4).reshape(height, width, 2).astype(np.float32)
    if fmt == 61:
        return np.frombuffer(data, dtype='u1', offset=len(data) - width * height).reshape(height, width, 1) / 255.
    raise ValueError(f'Unsupported DDS format {fmt}')


def Rms(value):
    return float(np.sqrt(np.mean(value.astype(np.float64)**2)))


def Main(root):
    rows = {int(row['sample']): row for row in csv.DictReader((root / 'scene_observations.csv').open())}
    captures = {}
    transport_errors = []
    for manifest in root.rglob('capture.json'):
        entry = json.loads(manifest.read_text())
        if entry['state'] != 'completed' or len(entry['frames']) != 1:
            transport_errors.append(str(manifest))
            continue
        frame = entry['frames'][0]
        path = manifest.parent / frame['file']
        index, kind = path.name.split('_')[:2]
        index = int(index)
        array = Decode(path, entry['format'], entry['width'], entry['height'])
        if frame['engine_frame'] != int(rows[index]['engine_frame']) or not np.isfinite(array).all():
            transport_errors.append(str(manifest))
        if (entry['width'], entry['height']) != (int(rows[index]['width']), int(rows[index]['height'])):
            transport_errors.append(str(manifest) + ': dimensions')
        captures.setdefault(index, {})[kind] = array[..., :3] if kind != 'motion' else array[..., :2]
    current_only = {}
    presence = True
    for index, row in rows.items():
        pair = captures[index]
        should_resolve = row['feature_active'] == '1' and row['aa_mode'] == '2'
        presence &= ('resolved' in pair) == should_resolve
        if row['expected_current_only'] == '1':
            difference = pair['resolved'] - pair['current']
            current_only[index] = {'phase': row['phase'], 'identical': bool(np.array_equal(pair['resolved'], pair['current'])),
                                   'max_error': float(abs(difference).max())}
    current = np.stack([captures[index]['current'] for index in range(128)])
    resolved = np.stack([captures[index]['resolved'] for index in range(128)])
    reference = current.mean(axis=0)
    # Independent edge neighborhoods selected by the fixture geometry, with all five thin rails retained separately.
    regions = {f'rail_{width}px': (42 + 28 * i, 20, 70 + 28 * i + int(np.ceil(width)), 160)
               for i, width in enumerate([.4, .8, 1.4, 2.4, 4.0])}
    regions['diagonal'] = (202, 20, 302, 160)
    metrics = {}
    background = float(np.median(reference[:, :30]))
    for name, (left, top, right, bottom) in regions.items():
        a, b = current[:, top:bottom, left:right], resolved[:, top:bottom, left:right]
        ref = reference[top:bottom, left:right]
        metrics[name] = {'current_adjacent_rms': Rms(np.diff(a[64:], axis=0)),
                         'resolved_adjacent_rms': Rms(np.diff(b[64:], axis=0)),
                         'early_output_reference_rms': Rms(b[1:17] - ref),
                         'late_output_reference_rms': Rms(b[112:128] - ref),
                         'current_reference_rms': Rms(a[112:128] - ref),
                         'mean_current': float(a.mean()), 'mean_resolved': float(b[64:].mean()),
                         'input_range': float(a.max() - a.min())}
        adjacent = np.sqrt(np.mean(np.diff(b[64:], axis=0).astype(np.float64)**2, axis=(1, 2, 3)))
        worst = int(np.argmax(adjacent)) + 64
        metrics[name]['worst_output_pair'] = [worst, worst + 1]
        metrics[name]['signal_retention'] = float(np.maximum(b[64:] - background, 0).mean() / np.maximum(a - background, 0).mean())
        metrics[name]['variation_reduction_percent'] = 100 * (1 - metrics[name]['resolved_adjacent_rms'] / metrics[name]['current_adjacent_rms'])
    summary = {'observations': len(rows), 'captures': sum(map(len, captures.values())),
               'transport_errors': transport_errors, 'resolve_presence_pass': bool(presence),
               'current_only_cases': current_only, 'current_only_pass': all(case['identical'] for case in current_only.values()),
               'static_frames_contiguous': bool(np.all(np.diff([int(rows[i]['engine_frame']) for i in range(128)]) == 1)),
               'static_unique_jitters': len({(rows[i]['jitter_x'], rows[i]['jitter_y']) for i in range(128)}),
               'static_default_factor': all(rows[i]['factor'] == '0.95' for i in range(128)),
               'rotation_forward': [float(rows[230][key]) for key in ['forward_x', 'forward_y', 'forward_z']] if 'forward_x' in rows[230] else None,
               'static_motion_max_xy': max(float(abs(captures[i]['motion']).max()) for i in range(1, 128)),
               'regions': metrics,
               'reset_dimensions': {i: [rows[i]['width'], rows[i]['height']] for i in range(208, 214)}}
    summary['static_geometry_pass'] = all(metric['input_range'] > .1 and metric['current_adjacent_rms'] > .001 for metric in metrics.values())
    summary['static_variance_reduction_pass'] = all(metric['resolved_adjacent_rms'] < metric['current_adjacent_rms'] and
                                         metric['late_output_reference_rms'] < metric['current_reference_rms'] for metric in metrics.values())
    # Transport/history gates and variance reduction cannot alone accept visual detail preservation.
    expected_rows = 334 if 'control_reset' in {row['phase'] for row in rows.values()} else 262
    summary['core_history_transport_pass'] = (len(rows) == expected_rows and summary['captures'] == expected_rows * 3 - 16 and not transport_errors and presence and
                       summary['current_only_pass'] and summary['static_frames_contiguous'] and
                       summary['static_default_factor'] and summary['static_geometry_pass'])
    canvas = Image.new('RGB', (960, 3 * 220), (28, 28, 28))
    draw = ImageDraw.Draw(canvas)
    for row_index, index in enumerate([0, 64, 127]):
        for column, kind in enumerate(['current', 'resolved', 'reference']):
            array = reference if kind == 'reference' else captures[index][kind]
            image = Image.fromarray((np.clip(array, 0, 1) * 255).astype('uint8'))
            canvas.paste(image, (column * 320, row_index * 220 + 30))
            draw.text((column * 320 + 8, row_index * 220 + 8), f'sample {index} / {kind}', fill='white')
    canvas.save(root / 'static_comparison.png')
    detail = Image.new('RGB', (3 * 260, 600), (28, 28, 28))
    detail_draw = ImageDraw.Draw(detail)
    for column, kind in enumerate(['current', 'resolved', '128-frame input mean']):
        array = reference if column == 2 else captures[127][kind]
        image = Image.fromarray((np.clip(array[20:160, 40:100], 0, 1) * 255).astype('uint8'))
        detail.paste(image.resize((240, 560), Image.Resampling.NEAREST), (column * 260 + 10, 30))
        detail_draw.text((column * 260 + 10, 8), kind, fill='white')
    detail.save(root / 'thin_rail_detail.png')
    for index in [0, 127, 136, 160, 192, 208, 246]:
        for kind in ['current', 'resolved']:
            array = captures[index][kind]
            Image.fromarray((np.clip(array, 0, 1) * 255).astype('uint8')).save(root / f'{index}_{kind}.png')
    (root / 'analysis.json').write_text(json.dumps(summary, indent=2))
    print(json.dumps(summary, indent=2))
    return 0 if summary['core_history_transport_pass'] else 1


if __name__ == '__main__':
    sys.exit(Main(Path(sys.argv[1])))
