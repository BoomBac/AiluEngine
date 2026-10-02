"""Reject stale colors/occluded geometry and motion trails outside the coverage footprint."""
import csv
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from AnalyzeTaaSceneAcceptance import Decode


def Main(root):
    rows = {int(row['sample']): row for row in csv.DictReader((root/'scene_observations.csv').open())}
    pairs = {}
    for path in root.rglob('capture.json'):
        manifest = json.loads(path.read_text())
        frame = manifest['frames'][0]
        sample, kind = frame['file'].split('_')[:2]
        if int(sample) < 262:
            continue
        value = Decode(path.parent/frame['file'], manifest['format'], manifest['width'], manifest['height'])
        pairs.setdefault(int(sample), {})[kind] = value[..., :2] if kind == 'motion' else value[..., :3]
    results = {}
    for sample in range(262, 334):
        pair = pairs[sample]
        current, resolved = pair['current'], pair['resolved']
        entry = {'phase': rows[sample]['phase'], 'current_max': float(current.max()),
                 'resolved_max': float(resolved.max()), 'motion_max_xy': float(abs(pair['motion']).max()),
                 'frame': int(rows[sample]['engine_frame'])}
        if entry['phase'] == 'color_drop':
            entry['stale_bright_excess'] = float(np.maximum(resolved-current.max(axis=(0, 1)), 0).max())
        if entry['phase'] == 'occluded':
            entry['max_error'] = float(abs(resolved-current).max())
            entry['uniform_input'] = float(current.max()-current.min()) < .003
        if entry['phase'] == 'rigid_motion':
            height, width = current.shape[:2]
            y, x = np.mgrid[:height, :width]
            maximum = current.max(axis=-1)
            for dy in range(-2, 3):
                for dx in range(-2, 3):
                    maximum = np.maximum(maximum, current[np.clip(y+dy, 0, height-1),
                                                        np.clip(x+dx, 0, width-1)].max(axis=-1))
            background = np.median(current.reshape(-1, 3), axis=0)
            flat_background = maximum <= background.max()+.003
            entry['background_trail_excess'] = float(np.maximum(resolved[flat_background]-background, 0).max())
        if entry['phase'] == 'disoccluded':
            entry['positive_signal'] = float(np.maximum(resolved-np.median(current[:, :30]), 0).mean())
        results[sample] = entry
    summary = {'cases': results,
               'color_change_pass': all(case.get('stale_bright_excess', 0) < .003 for case in results.values()),
               'occlusion_pass': all(case.get('max_error', 0) < .003 and case.get('uniform_input', True) for case in results.values()),
               'motion_pass': all(case.get('background_trail_excess', 0) < .003 for case in results.values()),
               'motion_fixture_valid': all(results[i]['motion_max_xy'] > .01 for i in range(294, 302)),
               'frames_contiguous': bool(np.all(np.diff([results[i]['frame'] for i in range(262, 334)]) == 1)),
               'disocclusion_signal_grows': results[333]['positive_signal'] > results[318]['positive_signal']}
    summary['pass'] = all(value for key, value in summary.items() if key != 'cases')
    canvas = Image.new('RGB', (960, 4*220), (28, 28, 28))
    draw = ImageDraw.Draw(canvas)
    for row, sample in enumerate([278, 294, 310, 333]):
        for col, kind in enumerate(['current', 'resolved']):
            image = Image.fromarray((np.clip(pairs[sample][kind], 0, 1)*255).astype('uint8'))
            canvas.paste(image, (col*320, row*220+30))
            draw.text((col*320+8, row*220+8), f'{sample} {rows[sample]["phase"]} {kind}', fill='white')
    canvas.save(root/'controls_comparison.png')
    (root/'controls_analysis.json').write_text(json.dumps(summary, indent=2))
    print(json.dumps({key: value for key, value in summary.items() if key != 'cases'}, indent=2))
    return 0 if summary['pass'] else 1


if __name__ == '__main__':
    sys.exit(Main(Path(sys.argv[1])))
