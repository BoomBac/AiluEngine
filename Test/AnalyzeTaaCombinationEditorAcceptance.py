"""Check M8 capture transport and pixels; these checks do not accept temporal image quality."""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from AnalyzeTaaSceneAcceptance import Decode, Rms


def Main(root):
    sequences = {}
    panels = []
    for path in sorted(root.glob('*/session_*/capture.json')):
        manifest = json.loads(path.read_text())
        name = path.parent.parent.name
        frames = sorted(manifest['frames'], key=lambda frame: frame['capture_index'])
        previous = None
        finite = True
        rms = []
        last = None
        for frame in frames:
            file = path.parent / frame['file']
            last = Decode(file, manifest['format'], manifest['width'], manifest['height'])
            finite &= bool(np.isfinite(last).all())
            if manifest['format'] == 26:
                raw = file.read_bytes()
                size = manifest['width'] * manifest['height'] * 4
                packed = np.frombuffer(raw, dtype='<u4', offset=len(raw) - size)
                finite &= all(bool(np.all(((packed >> shift) & 31) != 31)) for shift in [6, 17, 27])
            if previous is not None:
                rms.append(Rms(last - previous))
            previous = last
        continuous = all(b['engine_frame'] == a['engine_frame'] + 1 for a, b in zip(frames, frames[1:]))
        indexes = [frame['capture_index'] for frame in frames]
        complete = manifest['state'] == 'completed' and indexes == list(range(manifest['frame_count']))
        sequences[name] = {'frames': len(frames), 'dimensions': [manifest['width'], manifest['height']],
                           'complete': complete, 'continuous': continuous, 'finite': finite,
                           'last_rgb_mean': float(np.mean(last[..., :3])) if last is not None else None,
                           'max_adjacent_rms': max(rms, default=0),
                           'mean_adjacent_rms': np.mean(rms).item() if rms else 0}
        if name.startswith('matrix_') and name.endswith('_static') and last is not None:
            panel = Image.new('RGB', (544, 310), '#202020')
            rgb = np.uint8(np.clip(np.nan_to_num(last[..., :3]), 0, 1) * 255)
            panel.paste(Image.fromarray(rgb).resize((544, 288)), (0, 22))
            ImageDraw.Draw(panel).text((5, 4), name, fill='white')
            panels.append(panel)
    completion = json.loads((root / 'completion.json').read_text()) if (root / 'completion.json').exists() else None
    matrix = [value for name, value in sequences.items() if name.startswith('matrix_')]
    summary = {'sequences': sequences, 'capture_count': sum(value['frames'] for value in sequences.values()),
               'matrix_sequences': len(matrix), 'completion': completion,
               'transport_pixel_pass': bool(completion and len(matrix) == 42 and all(
                   value['complete'] and value['continuous'] and value['finite'] for value in sequences.values())),
               'scope': 'Capture integrity and finite output only; no quality oracle or continuous long-run image gate.'
               }
    (root / 'matrix_analysis.json').write_text(json.dumps(summary, indent=2))
    if panels:
        canvas = Image.new('RGB', (1088, 310 * ((len(panels) + 1) // 2)), '#202020')
        for index, panel in enumerate(panels):
            canvas.paste(panel, ((index % 2) * 544, (index // 2) * 310))
        canvas.save(root / 'matrix_montage.png')
    print(json.dumps({key: value for key, value in summary.items() if key != 'sequences'}, indent=2))
    return 0 if summary['transport_pixel_pass'] else 1


if __name__ == '__main__':
    sys.exit(Main(Path(sys.argv[1])))
