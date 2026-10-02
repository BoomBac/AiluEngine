"""Match static inputs and produce an unscaled before/after detail comparison."""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from AnalyzeTaaSceneAcceptance import Decode


def Load(root):
    frames = {}
    for path in root.rglob('capture.json'):
        manifest = json.loads(path.read_text())
        capture = manifest['frames'][0]
        sample, kind = capture['file'].split('_')[:2]
        if int(sample) >= 128 or kind == 'motion':
            continue
        frames.setdefault(int(sample), {})[kind] = Decode(path.parent/capture['file'], manifest['format'],
                                                         manifest['width'], manifest['height'])[..., :3]
    return frames


def Main(before, after, output):
    original, repaired = Load(before), Load(after)
    matched = all(np.array_equal(original[i]['current'], repaired[i]['current']) for i in range(128))
    result = {'static_current_128_frames_bit_identical': matched,
              'before': json.loads((before/'analysis.json').read_text())['regions'],
              'after': json.loads((after/'analysis.json').read_text())['regions']}
    (output/'before_after.json').write_text(json.dumps(result, indent=2))
    canvas = Image.new('RGB', (780, 600), (28, 28, 28))
    draw = ImageDraw.Draw(canvas)
    for column, (label, array) in enumerate([('current', repaired[127]['current']),
                                             ('before', original[127]['resolved']), ('after', repaired[127]['resolved'])]):
        image = Image.fromarray((np.clip(array[20:160, 40:100], 0, 1)*255).astype('uint8'))
        canvas.paste(image.resize((240, 560), Image.Resampling.NEAREST), (column*260+10, 30))
        draw.text((column*260+10, 8), label, fill='white')
    canvas.save(output/'before_after.png')
    print(json.dumps({'static_current_128_frames_bit_identical': matched}))
    return 0 if matched else 1


if __name__ == '__main__':
    sys.exit(Main(*(Path(value) for value in sys.argv[1:4])))
