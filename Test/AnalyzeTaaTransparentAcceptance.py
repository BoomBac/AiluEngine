"""Analyze --transparent production-pass captures; thresholds describe this fixture, not shipping quality."""
import csv
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from AnalyzeTaaSceneAcceptance import Decode, Rms
from DiagnoseTaaScene import Sample


def Dilate(mask, radius):
    padded = np.pad(mask, radius)
    return np.logical_or.reduce([padded[y:y + mask.shape[0], x:x + mask.shape[1]]
                                 for y in range(radius * 2 + 1) for x in range(radius * 2 + 1)])


def Red(array):
    return np.maximum(array[..., 0] - array[..., 1], 0)


def Panel(array, label, scale=2):
    rgb = np.uint8(np.clip(array[..., :3], 0, 1) * 255)
    picture = Image.fromarray(rgb).resize((array.shape[1] * scale, array.shape[0] * scale))
    canvas = Image.new('RGB', (picture.width, picture.height + 24), '#181818')
    canvas.paste(picture, (0, 24))
    ImageDraw.Draw(canvas).text((5, 5), label, fill='white')
    return canvas


def Main(root):
    rows = {int(row['sample']): row for row in csv.DictReader((root / 'm5_observations.csv').open())}
    captures = {}
    errors = []
    manifests = list(root.rglob('capture.json'))
    for manifest in manifests:
        entry = json.loads(manifest.read_text())
        if entry['state'] != 'completed' or len(entry['frames']) != 1:
            errors.append(str(manifest))
            continue
        frame = entry['frames'][0]
        file = manifest.parent / frame['file']
        sample, kind = file.name.split('_')[:2]
        sample = int(sample)
        array = Decode(file, entry['format'], entry['width'], entry['height'])
        if (entry['width'], entry['height']) != (320, 180) or not np.isfinite(array).all():
            errors.append(str(manifest) + ': pixels/dimensions')
        if frame['engine_frame'] != int(rows[sample]['engine_frame']) or frame['capture_index'] != 0:
            errors.append(str(manifest) + ': frame mismatch')
        if kind in captures.setdefault(sample, {}):
            errors.append(str(manifest) + ': duplicate')
        captures[sample][kind] = array
    frames = [int(row['engine_frame']) for row in rows.values()]
    repaired_run = len(rows) == 784
    expected_count = 784 if repaired_run else 448
    expected_kinds = {'current', 'resolved', 'motion'}
    if repaired_run:
        expected_kinds.add('reactive')
    transport = len(rows) == expected_count and len(manifests) == expected_count * len(expected_kinds) and not errors
    transport &= all(set(captures.get(sample, {})) == expected_kinds for sample in range(expected_count))
    result = {'transport_pass': transport, 'manifest_count': len(manifests), 'sample_count': len(rows),
              'engine_frame_gaps': sorted(set(np.diff(frames).tolist())), 'errors': errors,
              'history_factor_values': sorted(set(float(row['factor']) for row in rows.values())),
              'criteria': {'outside_5x5_red_excess_hdr': .03, 'disappearance_red_excess_hdr': .03,
                           'persistent_frames': 3, 'scope': 'fixture-local automated acceptance'},
              'materials': {}}
    montage = Image.new('RGB', (1280, 4 * 384))
    disappearance_sheet = Image.new('RGB', (6 * 320, 3 * 204))
    trail_detail = Image.new('RGB', (3 * 288, 3 * 312))
    for index, kind in enumerate(['blend', 'sprite', 'billboard', 'cutout']):
        start = index * 112
        static = [captures[start + offset] for offset in range(32, 48)]
        current = np.stack([pair['current'][..., :3] for pair in static])
        resolved = np.stack([pair['resolved'][..., :3] for pair in static])
        motion_max = []
        trail = []
        disappearance = []
        centroid_lag = []
        for offset in range(48, 80):
            pair = captures[start + offset]
            a, b = Red(pair['current']), Red(pair['resolved'])
            mask = a > .025
            exterior = ~Dilate(mask, 2)
            trail.append(float(np.maximum(b - a, 0)[exterior].max(initial=0)))
            motion_max.append(float(np.max(abs(pair['motion'][..., :2]) * [320, 180])))
            xs = np.arange(320)[None, :]
            if a.sum() > 0 and b.sum() > 0:
                centroid_lag.append(float((xs * a).sum() / a.sum() - (xs * b).sum() / b.sum()))
        for offset in range(80, 112):
            pair = captures[start + offset]
            disappearance.append(float(np.maximum(Red(pair['resolved']) - Red(pair['current']), 0).max()))
        significant = [offset for offset, value in enumerate(disappearance) if value > .03]
        visible = float(current[..., 0].max() - current[..., 0].min())
        metrics = {'static_current_adjacent_rms': Rms(np.diff(current[:, 55:125, 80:145], axis=0)),
                   'static_resolved_adjacent_rms': Rms(np.diff(resolved[:, 55:125, 80:145], axis=0)),
                   'static_red_pixel_count': int((Red(current[0]) > .025).sum()),
                   'visible_range': visible, 'motion_max_pixels': max(motion_max),
                   'outside_5x5_red_excess_max': max(trail),
                   'outside_5x5_excess_frames_above_003': sum(v > .03 for v in trail),
                   'disappearance_red_excess': disappearance,
                   'last_disappearance_offset_above_003': max(significant, default=-1),
                   'max_red_centroid_lag_pixels': max(centroid_lag, default=0),
                   'diagnostic_smear_gate': max(trail) <= .03 and max(significant, default=-1) < 3}
        if kind == 'billboard':
            metrics['diagnostic_smear_gate'] = None
            metrics['limitation'] = ('Billboard is grayscale; red separation does not apply. '
                                     'Inspect separate image/ROI evidence.')
            bounds = []
            for offset in range(16, 48):
                gray = captures[start + offset]['current'][..., 0]
                yy, xx = np.where((gray > .055) & (gray < .2))
                if len(xx):
                    bounds.append([int(xx.min()), int(yy.min()), int(xx.max()), int(yy.max())])
            metrics['static_billboard_bounds'] = sorted(set(tuple(box) for box in bounds))
            metrics['visible_static_frames'] = len(bounds)
            metrics['perspective_stable_raster_gate'] = (len(bounds) == 32 and
                                                        len(metrics['static_billboard_bounds']) == 1)
        else:
            for column, offset in enumerate([0, 3, 8, 16, 24, 31]):
                pair = captures[start + 80 + offset]
                row = ['blend', 'sprite', 'cutout'].index(kind)
                disappearance_sheet.paste(Panel(pair['resolved'], f'{kind}: disappearance +{offset} frames', 1),
                                          (column * 320, row * 204))
            pair = captures[start + 88]
            red = Red(pair['resolved'])
            y, x = np.unravel_index(red.argmax(), red.shape)
            neighbors = pair['current'][max(0, y - 1):y + 2, max(0, x - 1):x + 2, :3]
            top, left = min(max(0, y - 24), 180 - 48), min(max(0, x - 24), 320 - 48)
            difference = np.zeros((180, 320, 3))
            difference[..., 0] = Red(pair['resolved'])
            row = ['blend', 'sprite', 'cutout'].index(kind)
            for column, (array, label) in enumerate([
                (pair['current'], 'current: target absent'), (pair['resolved'], 'TAA final: stale color'),
                (difference, 'R-G positive excess')]):
                crop = array[top:top + 48, left:left + 48]
                trail_detail.paste(Panel(crop, f'{kind} +8 frames: {label}', 6), (column * 288, row * 312))
            metrics['chroma_rejection_example'] = {'sample': start + 88, 'pixel': [int(x), int(y)],
                'current': pair['current'][y, x, :3].tolist(), 'resolved': pair['resolved'][y, x, :3].tolist(),
                'current_3x3_min_rgb': neighbors.min(axis=(0, 1)).tolist(),
                'current_3x3_max_rgb': neighbors.max(axis=(0, 1)).tolist()}
        if kind == 'cutout':
            pair = captures[start + 63]
            coverage = Red(pair['current']) > .025
            velocities = abs(pair['motion'][..., 0]) * 320
            metrics['coverage_with_nonzero_velocity_fraction'] = float(np.mean(velocities[coverage] > 1))
        result['materials'][kind] = metrics
        sample = start + 63
        montage.paste(Panel(captures[sample]['current'], kind + ': moving current'), (0, index * 384))
        montage.paste(Panel(captures[sample]['resolved'], kind + ': TAA resolved'), (640, index * 384))
        if kind == 'sprite':
            # All source velocities are checked to be zero, so depth dilation cannot alter the selected velocity.
            # This is the CPU-derived production reprojection UV, not a GPU debug-target readback.
            pair = captures[sample]
            row = rows[sample]
            yy, xx = np.mgrid[:180, :320]
            uv = np.stack([(xx + .5) / 320, (yy + .5) / 180], axis=-1)
            delta = np.array([float(row['previous_jitter_x']) - float(row['jitter_x']),
                              float(row['previous_jitter_y']) - float(row['jitter_y'])])
            history_uv = uv - pair['motion'][..., :2] + delta
            np.save(root / 'sprite_history_uv_derived.npy', history_uv)
            result['sprite_history_uv'] = {'sample': sample, 'engine_frame': row['engine_frame'],
                                          'zero_motion_exact': bool(np.all(pair['motion'][..., :2] == 0)),
                                          'jitter_delta_uv': delta.tolist(),
                                          'source': 'CPU derived from captured velocity and Camera jitter'}
            mv = np.zeros((180, 320, 3), dtype=float)
            mv[..., :2] = .5 + pair['motion'][..., :2] * [320, 180] / 8
            uv_rgb = np.concatenate([history_uv, np.zeros((180, 320, 1))], axis=-1)
            detail = Image.new('RGB', (1280, 768))
            for location, picture in zip([(0, 0), (640, 0), (0, 384), (640, 384)], [
                Panel(pair['current'], 'Sprite current'), Panel(pair['resolved'], 'Sprite TAA final'),
                Panel(mv, 'MotionVector: neutral gray = zero'),
                Panel(uv_rgb, 'History UV: CPU-derived production equation')]):
                detail.paste(picture, location)
            detail.save(root / 'sprite_four_views.png')
    montage.save(root / 'moving_comparison.png')
    disappearance_sheet.save(root / 'disappearance_comparison.png')
    trail_detail.save(root / 'trail_detail.png')
    if repaired_run:
        responsiveness = {}
        for segment, kind in enumerate(['blend', 'sprite', 'billboard', 'cutout',
                                        'grayblend', 'graysprite', 'orthobillboard']):
            start = segment * 112
            counts, errors = [], []
            for offset in range(16, 80):
                pair = captures[start + offset]
                mask = pair['reactive'][..., 0] > 0
                counts.append(int(mask.sum()))
                if mask.any():
                    errors.append(float(abs(pair['resolved'][..., :3] - pair['current'][..., :3])[mask].max()))
            pair = captures[start + 80]
            previous_mask = captures[start + 79]['reactive'][..., 0]
            yy, xx = np.mgrid[:180, :320]
            row = rows[start + 80]
            dx = (float(row['previous_jitter_x']) - float(row['jitter_x'])) * 320
            dy = (float(row['previous_jitter_y']) - float(row['jitter_y'])) * 180
            assert np.all(pair['motion'][..., :2] == 0)
            # The captured GPU rounds bilinear subtexel coordinates to 1/256 texel. An ideal float
            # sampler falsely includes a <1/256 fringe when the shader actually samples zero coverage.
            sample_x = np.round((xx + dx) * 256) / 256
            sample_y = np.round((yy + dy) * 256) / 256
            protected = np.logical_or.reduce([Sample(previous_mask, sample_x + x, sample_y + y) > 0
                                              for y in [-1, 0, 1] for x in [-1, 0, 1]])
            ideal_weight = np.maximum.reduce([Sample(previous_mask, xx + dx + x, yy + dy + y)
                                              for y in [-1, 0, 1] for x in [-1, 0, 1]])
            departure_rgb_error = abs(pair['resolved'][..., :3] - pair['current'][..., :3]).max(axis=-1)
            departure_error = float(departure_rgb_error[protected].max(initial=0))
            robust_error = float(departure_rgb_error[ideal_weight > 1 / 256].max(initial=0))
            value = {'coverage_pixels_min': min(counts), 'coverage_pixels_max': max(counts),
                     'current_coverage_max_rgb_error': max(errors, default=0),
                     'departure_protected_pixels': int(protected.sum()), 'departure_max_rgb_error': departure_error,
                     'departure_sampler_subtexel_precision': 256,
                     'departure_robust_support_max_rgb_error': robust_error,
                     'departure_ideal_sampler_fringe_pixels': int(((ideal_weight > 0) & ~protected).sum()),
                     'background_mask_zero': all(np.all(captures[start + i]['reactive'] == 0) for i in range(16)),
                     'disappearance_mask_zero': bool(np.all(pair['reactive'] == 0))}
            if kind != 'cutout':
                value['responsive_gate'] = (min(counts) > 0 and max(errors, default=1) == 0 and
                                            departure_error == 0 and robust_error == 0)
            else:
                value['opaque_exclusion_gate'] = max(counts) == 0
            if kind in ['billboard', 'orthobillboard']:
                bounds = []
                for offset in range(16, 48):
                    gray = captures[start + offset]['current'][..., 0]
                    ys, xs = np.where((gray > .055) & (gray < .2))
                    if len(xs):
                        bounds.append([int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())])
                value['static_raster_bounds'] = sorted(set(tuple(box) for box in bounds))
                value['stable_raster_gate'] = len(bounds) == 32 and len(value['static_raster_bounds']) == 1
            responsiveness[kind] = value
        result['responsiveness'] = responsiveness
        result['automated_repair_gate'] = (transport and result['engine_frame_gaps'] == [1] and
            all(result['materials'][kind]['diagnostic_smear_gate'] for kind in ['blend', 'sprite', 'cutout']) and
            all(value.get('responsive_gate', value.get('opaque_exclusion_gate')) and value['background_mask_zero'] and
                value['disappearance_mask_zero'] and value.get('stable_raster_gate', True)
                for value in responsiveness.values()))
    (root / 'analysis.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))
    return 0 if not repaired_run or result['automated_repair_gate'] else 1


if __name__ == '__main__':
    sys.exit(Main(Path(sys.argv[1])))
