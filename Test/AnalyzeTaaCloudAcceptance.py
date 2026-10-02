"""Separate M6 transport from executed cloud history and image acceptance; fail closed on missing producers."""
import csv
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from AnalyzeTaaSceneAcceptance import Decode
from DiagnoseTaaScene import Sample


def Reproject(history, motion, previous, current, sign=1):
    height, width = history.shape[:2]
    y, x = np.mgrid[:height, :width]
    du = sign * (float(previous['jitter_x']) - float(current['jitter_x']))
    dv = sign * (float(previous['jitter_y']) - float(current['jitter_y']))
    u = (x + .5) / width - motion[..., 0] + du
    v = (y + .5) / height - motion[..., 1] + dv
    # Fractional coordinate precision matches this device's DX12 linear sampler; retain half-float store rounding.
    sx = np.round((u * width - .5) * 256) / 256
    sy = np.round((v * height - .5) * 256) / 256
    result = np.stack([Sample(history[..., c], sx, sy) for c in range(4)], axis=-1)
    result[(u < 0) | (u > 1) | (v < 0) | (v > 1)] = [0, 0, 0, 1]
    uncertain = (np.abs(u) < 1e-7) | (np.abs(u - 1) < 1e-7) | (np.abs(v) < 1e-7) | (np.abs(v - 1) < 1e-7)
    return result.astype(np.float16).astype(np.float32), uncertain


def Main(root):
    rows = list(csv.DictReader((root / 'm6_observations.csv').open()))
    observations = {int(row['sample']): row for row in rows}
    accesses = list(csv.DictReader((root / 'm6_graph.csv').open()))
    captures, errors = {}, []
    manifests = list(root.rglob('capture.json'))
    for path in manifests:
        manifest = json.loads(path.read_text())
        if manifest['state'] != 'completed' or len(manifest['frames']) != 1:
            errors.append(str(path) + ': incomplete')
            continue
        frame = manifest['frames'][0]
        file = path.parent / frame['file']
        sample, kind = file.name.split('_')[:2]
        sample = int(sample)
        row = observations[sample]
        array = Decode(file, manifest['format'], manifest['width'], manifest['height'])
        if (manifest['width'], manifest['height']) != (int(row['width']), int(row['height'])):
            errors.append(str(path) + ': dimensions')
        if frame['engine_frame'] != int(row['engine_frame']) or frame['capture_index'] != 0:
            errors.append(str(path) + ': frame mismatch')
        if not np.isfinite(array).all():
            errors.append(str(path) + ': nonfinite')
        if kind in captures.setdefault(sample, {}):
            errors.append(str(path) + ': duplicate')
        captures[sample][kind] = array
    enabled = [i for i, row in observations.items() if row['enabled'] == '1']
    has_reactive = any('reactive' in pair for pair in captures.values())
    expected = sum((5 if row['enabled'] == '1' else 3) +
                   (1 if has_reactive and int(row['combination']) >= 2 else 0) for row in rows)
    complete = len(rows) == 384 and len(manifests) == expected and not errors
    for i, row in observations.items():
        kinds = {'cloud', 'sparse', 'current', 'resolved', 'motion'} if row['enabled'] == '1' else {
            'current', 'resolved', 'motion'}
        if has_reactive and int(row['combination']) >= 2:
            kinds.add('reactive')
        complete &= set(captures.get(i, {})) == kinds
    reprojection = sorted({int(a['sample']) for a in accesses if a['pass'] == 'VolumetricClouds_TileReproject'})
    motion_dependency = []
    for sample in reprojection:
        frame_access = [a for a in accesses if int(a['sample']) == sample]
        writes = [a for a in frame_access if a['pass'] == 'DynamicMotion' and a['access'] == 'write']
        reads = [a for a in frame_access if a['pass'] == 'VolumetricClouds_TileReproject' and a['access'] == 'read']
        motion_dependency.append(any((a['id'], a['version']) == (b['id'], b['version']) and
                                     int(a['ordinal']) < int(b['ordinal']) for a in writes for b in reads))
    cloud_writes = [a for a in accesses if a['pass'] == 'VolumetricClouds_Compute' and a['access'] == 'write']
    names = sorted({a['resource'] for a in cloud_writes})
    lut_reads = [a for a in accesses if a['pass'] == 'VolumetricClouds_Compute' and a['access'] == 'read'
                 and 'SkyView' in a['resource']]
    lut_producers = [a for a in accesses if a['access'] == 'write' and 'SkyView' in a['resource']]
    sky_reads = [a for a in accesses if a['pass'] == 'DrawSkyBox' and a['access'] == 'read'
                 and 'SkyView' in a['resource']]
    frames = [int(row['engine_frame']) for row in rows]
    finite_alpha_seed = {}
    for i in [0, 96, 192, 288, 368, 370, 373, 376, 380]:
        a = captures[i]['cloud']
        finite_alpha_seed[i] = {'finite': bool(np.isfinite(a).all()), 'rgb_max': float(np.nanmax(a[..., :3])),
                                'alpha_min': float(np.nanmin(a[..., 3])), 'alpha_max': float(np.nanmax(a[..., 3]))}
    cloud_signal = any(float(np.nanmax(captures[i]['cloud'][..., :3])) > .001 for i in enabled)
    offset_table = [(0, 0), (2, 2), (2, 0), (0, 2), (1, 1), (3, 3), (3, 1), (1, 3),
                    (1, 0), (3, 2), (3, 0), (1, 2), (0, 1), (2, 3), (2, 1), (0, 3)]
    oracle, jitter_control = [], []
    for i in reprojection:
        if i - 1 not in captures or 'cloud' not in captures[i - 1]:
            errors.append(f'{i}: missing preceding cloud')
            continue
        a = captures[i]['cloud']
        y, x = np.mgrid[:a.shape[0], :a.shape[1]]
        ox, oy = offset_table[int(observations[i]['engine_frame']) % 16]
        tiles = (x % 4 == ox) & (y % 4 == oy)
        predicted, uncertain = Reproject(captures[i - 1]['cloud'], captures[i]['motion'], observations[i - 1], observations[i])
        opposite, _ = Reproject(captures[i - 1]['cloud'], captures[i]['motion'], observations[i - 1], observations[i], -1)
        support = ~tiles & ~uncertain
        error = np.abs(a[support] - predicted[support])
        mismatch = np.max(np.abs(a - predicted), axis=-1) > .01
        mismatch &= ~tiles
        mismatch_y, mismatch_x = np.where(mismatch)
        scale = max(float(np.max(np.abs(a))), 1)
        entry = {'sample': i, 'rms': float(np.sqrt(np.mean(error ** 2))), 'max_error': float(np.max(error)),
                 'normalized_rms': float(np.sqrt(np.mean(error ** 2))) / scale,
                 'tile_exact': bool(np.array_equal(a[tiles], captures[i]['sparse'][tiles])),
                 'boundary_uncertain_pixels': int((~tiles & uncertain).sum()),
                 'large_error_count': int(mismatch.sum()),
                 'large_error_bounds': [int(mismatch_x.min()), int(mismatch_y.min()),
                                        int(mismatch_x.max()), int(mismatch_y.max())] if mismatch.any() else []}
        oracle.append(entry)
        if int(observations[i]['combination']) == 3 and int(observations[i]['local']) < 32:
            jitter_control.append([entry['rms'], float(np.sqrt(np.mean((a[support] - opposite[support]) ** 2)))])
    expected_seed = [0, 96, 192, 288, 368, 370, 373, 376]
    seed_gate = all(i not in reprojection and np.array_equal(captures[i]['cloud'], captures[i]['sparse'])
                    for i in expected_seed)
    rotation_gate = all(cloud_writes[i]['texture'] != cloud_writes[i - 1]['texture']
                        for i in range(1, len(cloud_writes)))
    reactive_max = max((float(np.max(pair['reactive'])) for pair in captures.values()
                        if 'reactive' in pair), default=1)
    alignment = {}
    for combination in [2, 3]:
        indexes = range(combination * 96 + 16, combination * 96 + 32)
        current_stack = np.stack([captures[i]['current'][..., :3] for i in indexes])
        resolved_stack = np.stack([captures[i]['resolved'][..., :3] for i in indexes])
        reference = current_stack.mean(axis=0).mean(axis=-1)
        resolved = resolved_stack.mean(axis=0).mean(axis=-1)
        scores = {}
        for dy in [-1, 0, 1]:
            for dx in [-1, 0, 1]:
                scores[dx, dy] = float(np.mean((resolved[8:-8, 8:-8] -
                                               reference[8 + dy:reference.shape[0] - 8 + dy,
                                                         8 + dx:reference.shape[1] - 8 + dx]) ** 2))
        best = min(scores, key=scores.get)
        shifts = []
        for axis in [0, 1]:
            low = scores[(-1, 0) if axis == 0 else (0, -1)]
            center = scores[0, 0]
            high = scores[(1, 0) if axis == 0 else (0, 1)]
            shifts.append(.5 * (low - high) / max(low - 2 * center + high, 1e-12))
        alignment[combination] = {'best_integer_offset': list(best), 'quadratic_offset_pixels': shifts,
                                  'current_adjacent_rms': float(np.sqrt(np.mean(np.diff(current_stack, axis=0) ** 2))),
                                  'resolved_adjacent_rms': float(np.sqrt(np.mean(np.diff(resolved_stack, axis=0) ** 2)))}
    gates = {'current_motion_dependency': bool(reprojection) and all(motion_dependency),
             'sky_lut_bound': len(lut_reads) == len(enabled),
             'tile_history_executed': len(reprojection) > 0,
             'ping_pong_observed': names == ['CloudTexA', 'CloudTexB'],
             'cloud_signal_present': cloud_signal,
             'seed_resize_toggle_cut': seed_gate,
             'single_ping_pong_per_frame': rotation_gate,
             'reprojection_oracle': bool(oracle) and all(o['normalized_rms'] < .001 and o['tile_exact'] for o in oracle),
             'jitter_sign_control': bool(jitter_control) and bool(
                 np.mean([v[0] for v in jitter_control]) < np.mean([v[1] for v in jitter_control]) * .25),
             'cloud_not_transparent_reactive': has_reactive and reactive_max == 0,
             'combined_static_alignment': all(a['best_integer_offset'] == [0, 0] and
                                              max(abs(v) for v in a['quadratic_offset_pixels']) < .25
                                              for a in alignment.values())}
    summary = {'transport_pass': bool(complete), 'samples': len(rows), 'manifests': len(manifests),
               'expected_manifests': expected, 'engine_frame_steps': sorted(set(np.diff(frames).tolist())),
               'errors': errors, 'enabled_frames': len(enabled), 'reprojection_frames': len(reprojection),
               'cloud_write_resources': names, 'cloud_lut_read_count': len(lut_reads),
               'sky_lut_producer_count': len(lut_producers), 'seed_pixels': finite_alpha_seed, 'gates': gates,
               'sky_pass_lut_read_count': len(sky_reads),
               'acceptance_pass': bool(complete and all(gates.values())),
               'oracle': oracle, 'jitter_control': jitter_control,
               'reactive_max': reactive_max,
               'static_alignment': alignment,
               'unverified': ['interactive Editor visual review', 'multi-camera Cloud histories',
                              'explicit Camera history-reset notification without pose/projection change']}
    canvas = Image.new('RGB', (640, 4 * 204), '#202020')
    draw = ImageDraw.Draw(canvas)
    for index, sample in enumerate([16, 112, 208, 304]):
        for column, kind in enumerate(['cloud', 'resolved']):
            a = captures[sample][kind]
            rgb = np.uint8(np.clip(np.nan_to_num(a[..., :3]), 0, 1) * 255)
            canvas.paste(Image.fromarray(rgb), (column * 320, index * 204 + 24))
            draw.text((column * 320 + 4, index * 204 + 4), f'combination {index}: {kind}', fill='white')
    canvas.save(root / 'combinations.png')
    (root / 'analysis.json').write_text(json.dumps(summary, indent=2))
    print(json.dumps(summary, indent=2))
    return 0 if summary['acceptance_pass'] else 1


if __name__ == '__main__':
    sys.exit(Main(Path(sys.argv[1])))
