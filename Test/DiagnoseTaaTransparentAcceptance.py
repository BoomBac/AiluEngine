"""Offline causal replay of M5 disappearance captures. No renderer, shader or runtime setting is changed."""
import csv
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from AnalyzeTaaSceneAcceptance import Decode, Rms
from DiagnoseTaaScene import Mitchell, Reproject


def ToWorking(rgb, mode):
    if mode != 'ycocg_once':
        return rgb
    r, g, b = np.moveaxis(rgb, -1, 0)
    bias = 128 / 255
    return np.stack([.25 * r + .5 * g + .25 * b, .5 * (r - b) + bias,
                     -.25 * r + .5 * g - .25 * b + bias], axis=-1)


def ToScene(color, mode):
    if mode != 'ycocg_once':
        return color
    y, co, cg = np.moveaxis(color, -1, 0)
    co, cg = co - 128 / 255, cg - 128 / 255
    return np.stack([y + co - cg, y + cg, y - co - cg], axis=-1)


def Quantize(rgb):
    rgb = np.maximum(rgb, 0)
    exponent = np.floor(np.log2(np.maximum(rgb, 2.**-14)))
    step = 2.**(exponent - np.array([6, 6, 5]))
    return np.round(rgb / step) * step


def Neighborhood(current):
    height, width = current.shape[:2]
    yy, xx = np.mgrid[:height, :width]
    offsets = [(x, y) for y in [-1, 0, 1] for x in [-1, 0, 1]]
    samples = np.stack([current[np.clip(yy + y, 0, height - 1), np.clip(xx + x, 0, width - 1)]
                        for x, y in offsets])
    mean = samples.mean(axis=0)
    deviation = np.sqrt(abs((samples * samples).mean(axis=0) - mean * mean))
    low = np.maximum(samples.min(axis=0), mean - deviation)
    high = np.minimum(samples.max(axis=0), mean + deviation)
    weights = np.array([Mitchell(np.hypot(x, y)) for x, y in offsets])
    filtered = (samples * weights[:, None, None, None]).sum(axis=0) / weights.sum()
    uniform = np.all(samples == samples[0], axis=(0, 3))
    count = np.zeros((height, width), dtype=int)
    guard_low, guard_high = low.copy(), high.copy()
    for y in range(-2, 3):
        for x in range(-2, 3):
            if abs(x) != 2 and abs(y) != 2:
                continue
            sample = current[np.clip(yy + y, 0, height - 1), np.clip(xx + x, 0, width - 1)]
            count += np.any(sample != samples[0], axis=-1)
            guard_low = np.minimum(guard_low, sample)
            guard_high = np.maximum(guard_high, sample)
    candidate = uniform & (count > 0) & (count <= 4)
    return low, high, guard_low, guard_high, candidate, filtered


def Resolve(current, history, dx, dy, mode, neighborhood):
    height, width = current.shape[:2]
    yy, xx = np.mgrid[:height, :width]
    sign = -1 if mode == 'reverse_jitter' else 1
    projected = np.stack([Reproject(history[..., channel], xx + sign * dx, yy + sign * dy,
                                    mode != 'bilinear_history') for channel in range(3)], axis=-1)
    projected = ToWorking(projected, mode)
    low, high, guard_low, guard_high, candidate, filtered = neighborhood
    if mode != 'no_guard':
        rejected = np.any((projected < low) | (projected > high), axis=-1)
        expand = (candidate & rejected)[..., None]
        low, high = np.where(expand, guard_low, low), np.where(expand, guard_high, high)
    middle = .5 * (low + high)
    extent = .5 * (high - low) + 1e-6
    distance = np.max(abs((projected - middle) / extent), axis=-1, keepdims=True)
    clipped = middle + (projected - middle) / np.maximum(distance, 1)
    center = ToWorking(current, mode) if mode == 'no_center_filter' else filtered
    output = ToScene(.05 * center + .95 * clipped, mode)
    outside = (xx + .5 + sign * dx < 0) | (xx + .5 + sign * dx > width)
    outside |= (yy + .5 + sign * dy < 0) | (yy + .5 + sign * dy > height)
    output[outside] = current[outside]
    return Quantize(output), projected, clipped, low, high


def Main(source, output):
    output.mkdir(parents=True, exist_ok=False)
    rows = {int(row['sample']): row for row in csv.DictReader((source / 'm5_observations.csv').open())}
    captures = {}
    for path in source.rglob('capture.json'):
        manifest = json.loads(path.read_text())
        assert manifest['state'] == 'completed' and len(manifest['frames']) == 1
        frame = manifest['frames'][0]
        sample, kind = frame['file'].split('_')[:2]
        sample = int(sample)
        # Include the preceding actual history and all 32 disappearance frames for each colored target.
        if sample // 112 not in [0, 1, 3] or sample % 112 < 79:
            continue
        assert frame['engine_frame'] == int(rows[sample]['engine_frame'])
        captures.setdefault(sample, {})[kind] = Decode(path.parent / frame['file'], manifest['format'],
                                                     manifest['width'], manifest['height'])[..., :3].astype(float)
    modes = ['baseline', 'no_guard', 'no_center_filter', 'bilinear_history', 'ycocg_once']
    result = {'source': str(source), 'kind': 'CPU offline counterfactual, not GPU repair acceptance',
              'parameters': {'history_factor': .95, 'variance_scale': 1, 'clamp_quality': 2}, 'materials': {}}
    montage = Image.new('RGB', (6 * 288, 3 * 312), '#181818')
    draw = ImageDraw.Draw(montage)
    for row_index, (kind, segment) in enumerate([('blend', 0), ('sprite', 1), ('cutout', 3)]):
        start = segment * 112
        sequence = {mode: captures[start + 79]['resolved'].copy() for mode in modes}
        one_step_errors = {mode: [] for mode in modes + ['reverse_jitter']}
        recurrence_metrics = {mode: [] for mode in modes}
        events = []
        chosen = {}
        for offset in range(80, 112):
            sample = start + offset
            current = captures[sample]['current']
            actual = captures[sample]['resolved']
            # Depth dilation cannot alter velocity when every source motionXY is exactly zero.
            assert np.all(captures[sample]['motion'][..., :2] == 0)
            dx = (float(rows[sample]['previous_jitter_x']) - float(rows[sample]['jitter_x'])) * 320
            dy = (float(rows[sample]['previous_jitter_y']) - float(rows[sample]['jitter_y'])) * 180
            rgb_neighborhood = Neighborhood(current)
            for mode in modes + ['reverse_jitter']:
                neighborhood = Neighborhood(ToWorking(current, mode)) if mode == 'ycocg_once' else rgb_neighborhood
                prediction, projected, clipped, low, high = Resolve(
                    current, captures[sample - 1]['resolved'], dx, dy, mode, neighborhood)
                one_step_errors[mode].append(Rms(prediction[20:160, 40:300] - actual[20:160, 40:300]))
                if mode == 'baseline' and offset == 88:
                    red = np.maximum(actual[..., 0] - actual[..., 1], 0)
                    y, x = np.unravel_index(red.argmax(), red.shape)
                    neighborhood_rgb = current[y - 1:y + 2, x - 1:x + 2]
                    events.append({'sample': sample, 'pixel': [int(x), int(y)],
                        'current_rgb': current[y, x].tolist(), 'actual_gpu_rgb': actual[y, x].tolist(),
                        'reprojected_history_rgb': projected[y, x].tolist(),
                        'clipped_history_rgb': clipped[y, x].tolist(), 'variance_low_rgb': low[y, x].tolist(),
                        'variance_high_rgb': high[y, x].tolist(), 'predicted_rgb': prediction[y, x].tolist(),
                        'inner_neighborhood_uniform': bool(np.all(neighborhood_rgb == neighborhood_rgb[0, 0])),
                        'neighborhood_red_max': float((neighborhood_rgb[..., 0] - neighborhood_rgb[..., 1]).max()),
                        'guard_can_change_selected_bounds': bool(rgb_neighborhood[4][y, x]),
                        'history_inside_variance_box': bool(np.all(projected[y, x] >= low[y, x]) and
                                                           np.all(projected[y, x] <= high[y, x]))})
                if mode == 'reverse_jitter':
                    continue
                sequence[mode] = Resolve(current, sequence[mode], dx, dy, mode, neighborhood)[0]
                excess = np.maximum(sequence[mode][..., 0] - sequence[mode][..., 1], 0)
                recurrence_metrics[mode].append(float(excess.max()))
                if offset == 88:
                    chosen[mode] = sequence[mode].copy()
        actual_red = [float(np.maximum(captures[start + offset]['resolved'][..., 0] -
                                       captures[start + offset]['resolved'][..., 1], 0).max())
                      for offset in range(80, 112)]
        result['materials'][kind] = {
            'all_disappearance_motion_xy_zero': True,
            'one_step_gpu_rms': {mode: Rms(np.array(errors)) for mode, errors in one_step_errors.items()},
            'recurrence_max_red_excess': recurrence_metrics, 'actual_gpu_max_red_excess': actual_red,
            'selected_events': events,
            'last_disappearance_offset_above_003': {mode: max((i for i, v in enumerate(values) if v > .03), default=-1)
                                                  for mode, values in recurrence_metrics.items()}}
        y, x = events[0]['pixel'][1], events[0]['pixel'][0]
        top, left = min(max(y - 24, 0), 132), min(max(x - 24, 0), 272)
        arrays = [('actual_gpu', captures[start + 88]['resolved'])] + list(chosen.items())
        for column, (name, array) in enumerate(arrays):
            crop = array[top:top + 48, left:left + 48]
            picture = Image.fromarray(np.uint8(np.clip(crop, 0, 1) * 255)).resize((288, 288))
            montage.paste(picture, (column * 288, row_index * 312 + 24))
            draw.text((column * 288 + 4, row_index * 312 + 4), kind + ': ' + name, fill='white')
    (output / 'diagnosis.json').write_text(json.dumps(result, indent=2))
    montage.save(output / 'counterfactuals.png')
    compact = {kind: {'one_step_gpu_rms': value['one_step_gpu_rms'],
                     'last_offset': value['last_disappearance_offset_above_003'],
                     'selected_event': value['selected_events'][0]} for kind, value in result['materials'].items()}
    print(json.dumps(compact, indent=2))


if __name__ == '__main__':
    Main(Path(sys.argv[1]), Path(sys.argv[2]))
