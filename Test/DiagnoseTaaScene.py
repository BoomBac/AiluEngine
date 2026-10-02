"""Offline causal experiments on captured real-geometry TAA; does not change production shader/config."""
import csv
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from AnalyzeTaaSceneAcceptance import Decode


def Sample(array, x, y):
    height, width = array.shape
    ix, iy = np.floor(x).astype(int), np.floor(y).astype(int)
    fx, fy = x - ix, y - iy
    def At(dx, dy):
        return array[np.clip(iy + dy, 0, height - 1), np.clip(ix + dx, 0, width - 1)]
    return ((At(0, 0) * (1 - fx) + At(1, 0) * fx) * (1 - fy) +
            (At(0, 1) * (1 - fx) + At(1, 1) * fx) * fy)


def Reproject(array, x, y, bicubic=True):
    if not bicubic:
        return Sample(array, x, y)
    tx, ty = np.floor(x), np.floor(y)
    def Weights(f):
        return [-.5*f**3+f**2-.5*f, 1.5*f**3-2.5*f**2+1, -1.5*f**3+2*f**2+.5*f, .5*f**3-.5*f**2]
    wx, wy = Weights(x-tx), Weights(y-ty)
    x12, y12 = wx[1]+wx[2], wy[1]+wy[2]
    cx, cy = tx+wx[2]/x12, ty+wy[2]/y12
    terms = [(cx, ty-1, x12*wy[0]), (tx-1, cy, wx[0]*y12), (cx, cy, x12*y12),
             (tx+2, cy, wx[3]*y12), (cx, ty+2, x12*wy[3])]
    return sum(Sample(array, xx, yy)*weight for xx, yy, weight in terms) / sum(weight for _, _, weight in terms)


def Quantize(array):
    array = np.maximum(array, 0)
    exponent = np.floor(np.log2(np.maximum(array, 2.**-14)))
    step = 2.**(exponent-6)
    return np.round(array/step)*step


def Mitchell(x):
    a = .3333
    return (a+2)*x**3-(a+3)*x**2+1 if x < 1 else a*x**3-5*a*x**2+8*a*x-4*a


def Main(source, output):
    output.mkdir(parents=True, exist_ok=True)
    rows = {int(row['sample']): row for row in csv.DictReader((source/'scene_observations.csv').open())}
    pairs = {}
    for path in source.rglob('capture.json'):
        manifest = json.loads(path.read_text())
        frame = manifest['frames'][0]
        index, kind = frame['file'].split('_')[:2]
        if int(index) >= 128 or kind == 'motion':
            continue
        array = Decode(path.parent/frame['file'], manifest['format'], manifest['width'], manifest['height'])[..., 0]
        pairs.setdefault(int(index), {})[kind] = array.astype(np.float64)
    current = np.stack([pairs[i]['current'] for i in range(128)])
    actual = np.stack([pairs[i]['resolved'] for i in range(128)])
    height, width = current.shape[1:]
    y, x = np.mgrid[:height, :width]
    offsets = [(dx, dy) for dy in [-1, 0, 1] for dx in [-1, 0, 1]]
    weights = np.array([Mitchell(np.hypot(dx, dy)) for dx, dy in offsets])
    filtered, low, high, empty = [], [], [], []
    background = float(np.median(current[:, :, :30]))
    for a in current:
        samples = np.stack([a[np.clip(y+dy, 0, height-1), np.clip(x+dx, 0, width-1)] for dx, dy in offsets])
        mean = samples.mean(axis=0)
        deviation = np.sqrt(np.abs((samples*samples).mean(axis=0)-mean*mean))
        filtered.append(np.sum(samples*weights[:, None, None], axis=0)/weights.sum())
        low.append(np.minimum(samples.min(axis=0), mean-deviation))
        high.append(np.maximum(samples.max(axis=0), mean+deviation))
        empty.append(samples.max(axis=0) <= background)
    filtered, low, high, empty = map(np.stack, [filtered, low, high, empty])
    modes = ['baseline', 'no_clip', 'no_center_filter', 'float_history', 'bilinear_history',
             'reverse_jitter', 'no_clip_no_filter', 'coverage_guard']
    sequences = {}
    one_step_errors = {mode: [] for mode in ['baseline', 'reverse_jitter']}
    clip_removed, filter_delta, clip_events = [], [], []
    for mode in modes:
        sequence = [current[0]]
        for i in range(1, 128):
            dx = (float(rows[i-1]['jitter_x'])-float(rows[i]['jitter_x']))*width
            dy = (float(rows[i-1]['jitter_y'])-float(rows[i]['jitter_y']))*height
            if mode == 'reverse_jitter':
                dx, dy = -dx, -dy
            hist = Reproject(sequence[-1], x+dx, y+dy, mode != 'bilinear_history')
            bound_low, bound_high = low[i], high[i]
            if mode == 'coverage_guard':
                bound_low, bound_high = bound_low.copy(), bound_high.copy()
                for oy in range(-2, 3):
                    for ox in range(-2, 3):
                        color = current[i, np.clip(y+oy, 0, height-1), np.clip(x+ox, 0, width-1)]
                        bound_low = np.minimum(bound_low, color)
                        bound_high = np.maximum(bound_high, color)
            clipped = np.clip(hist, bound_low, bound_high) if mode not in ['no_clip', 'no_clip_no_filter'] else hist
            center = current[i] if mode in ['no_center_filter', 'no_clip_no_filter'] else filtered[i]
            result = .05*center+.95*clipped
            outside = ((x+.5+dx < 0) | (x+.5+dx > width) | (y+.5+dy < 0) | (y+.5+dy > height))
            result[outside] = current[i][outside]
            if mode != 'float_history':
                result = Quantize(result)
            sequence.append(result)
            if mode in one_step_errors:
                gpu_hist = Reproject(actual[i-1], x+dx, y+dy)
                prediction = Quantize(.05*filtered[i]+.95*np.clip(gpu_hist, low[i], high[i]))
                one_step_errors[mode].append(np.mean((prediction[20:160, 42:199]-actual[i, 20:160, 42:199])**2))
            if mode == 'baseline':
                gpu_hist = Reproject(actual[i-1], x+dx, y+dy)
                clip_removed.append((gpu_hist-np.clip(gpu_hist, low[i], high[i]))[20:160, 42:71])
                filter_delta.append((filtered[i]-current[i])[20:160, 42:71])
                candidate = (empty[i] & (gpu_hist > background+.01))
                candidate[:, :42] = candidate[:, 71:] = False
                candidate[:20] = candidate[160:] = False
                positions = np.argwhere(candidate)
                if len(positions):
                    yy, xx = positions[np.argmax(gpu_hist[candidate])]
                    clip_events.append({'sample': i, 'pixel': [int(xx), int(yy)],
                                        'current': float(current[i, yy, xx]), 'low': float(low[i, yy, xx]),
                                        'high': float(high[i, yy, xx]), 'reprojected_history': float(gpu_hist[yy, xx]),
                                        'clipped_history': float(np.clip(gpu_hist[yy, xx], low[i, yy, xx], high[i, yy, xx])),
                                        'actual_output': float(actual[i, yy, xx])})
        sequences[mode] = np.stack(sequence)
    sequences['actual_gpu'] = actual
    regions = {f'rail_{rail}px': (42+28*i, 70+28*i+int(np.ceil(rail))) for i, rail in enumerate([.4, .8, 1.4, 2.4, 4.])}
    result = {'weights': weights.tolist(), 'weight_sum': float(weights.sum()), 'background': background,
              'one_step_reference_rms': {mode: float(np.sqrt(np.mean(error))) for mode, error in one_step_errors.items()},
              'full_sequence_baseline_gpu_rms': float(np.sqrt(np.mean((sequences['baseline'][:, 20:160, 42:199]-actual[:, 20:160, 42:199])**2))),
              'modes': {}}
    for mode, sequence in sequences.items():
        metrics = {}
        for name, (left, right) in regions.items():
            a, b = current[:, 20:160, left:right], sequence[64:, 20:160, left:right]
            metrics[name] = {'positive_signal_retention': float(np.maximum(b-background, 0).mean()/np.maximum(a-background, 0).mean()),
                             'signed_signal_retention': float((b-background).mean()/(a-background).mean()),
                             'adjacent_rms': float(np.sqrt(np.mean(np.diff(b, axis=0)**2)))}
        result['modes'][mode] = metrics
    result['thin_rail_empty_neighborhood_fraction'] = float(empty[:, 20:160, 42:71].mean())
    footprint = current[:, 20:160, 42:71].mean(axis=0) > background+.01
    result['thin_rail_footprint_pixels'] = int(footprint.sum())
    result['empty_neighborhood_on_rail_footprint_fraction'] = float(empty[:, 20:160, 42:71][:, footprint].mean())
    result['thin_rail_filter_signed_mean_delta'] = float(np.mean(filter_delta))
    result['thin_rail_clipping_signed_mean_removed'] = float(np.mean(clip_removed))
    result['thin_rail_clipping_positive_mean_removed'] = float(np.maximum(clip_removed, 0).mean())
    result['empty_neighborhood_clipping_events'] = clip_events
    (output/'diagnosis.json').write_text(json.dumps(result, indent=2))
    chosen = ['actual_gpu', 'baseline', 'no_clip', 'no_center_filter', 'float_history', 'bilinear_history']
    canvas = Image.new('RGB', (len(chosen)*260, 600), (28, 28, 28))
    draw = ImageDraw.Draw(canvas)
    for col, mode in enumerate(chosen):
        image = Image.fromarray((np.clip(sequences[mode][127, 20:160, 40:100], 0, 1)*255).astype('uint8'))
        canvas.paste(image.convert('RGB').resize((240, 560), Image.Resampling.NEAREST), (260*col+10, 30))
        draw.text((260*col+10, 8), mode, fill='white')
    canvas.save(output/'ablations.png')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    Main(Path(sys.argv[1]), Path(sys.argv[2]))
