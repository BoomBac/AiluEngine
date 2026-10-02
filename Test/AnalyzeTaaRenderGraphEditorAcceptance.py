import json
import re
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

root = Path(sys.argv[1])
results = {}
previews = []
for capture_path in sorted(root.glob('*/session_*/capture.json')):
    manifest = json.loads(capture_path.read_text())
    name = capture_path.parent.parent.name
    frames = sorted(manifest['frames'], key=lambda frame: frame['capture_index'])
    w, h = manifest['width'], manifest['height']
    arrays = []
    for frame in frames:
        source = capture_path.parent / frame['file']
        if source.suffix == '.png':
            arrays.append(np.asarray(Image.open(source)).astype(np.float32) / 255)
            continue
        raw = source.read_bytes()
        if manifest['format'] == 26:
            packed = np.frombuffer(raw, dtype='<u4', offset=len(raw) - w * h * 4).reshape(h, w)
            channels = []
            for shift, bits in [(0, 6), (11, 6), (22, 5)]:
                value = (packed >> shift) & ((1 << (bits + 5)) - 1)
                mantissa = value & ((1 << bits) - 1)
                exponent = value >> bits
                channels.append(np.where(exponent == 0, mantissa * 2. ** (-14 - bits),
                    (1 + mantissa / 2. ** bits) * 2. ** (exponent.astype(float) - 15)))
            arrays.append(np.stack(channels, axis=-1).astype(np.float32))
        elif manifest['format'] == 10:
            arrays.append(np.frombuffer(raw, dtype='<f2', offset=len(raw) - w * h * 8).reshape(h, w, 4).astype(np.float32))
        elif manifest['format'] == 41:
            arrays.append(np.frombuffer(raw, dtype='<f4', offset=len(raw) - w * h * 4).reshape(h, w, 1))
        else:
            raise ValueError(manifest['format'])
    values = np.stack(arrays)
    delta = np.diff(values, axis=0)
    results[name] = {'count': len(frames), 'dimensions': [w, h], 'format': manifest['format'],
        'completed': manifest['state'] == 'completed' and len(frames) == manifest['frame_count'],
        'continuous': all(b['engine_frame'] - a['engine_frame'] == 1 for a, b in zip(frames, frames[1:])),
        'finite': bool(np.isfinite(values).all()), 'min': float(values.min()), 'max': float(values.max()),
        'adjacent_rms': float(np.sqrt(np.mean(delta * delta))),
        'adjacent_p99': float(np.percentile(abs(delta), 99)),
        'first_frame': frames[0]['engine_frame'], 'last_frame': frames[-1]['engine_frame']}
    if name == 'taa_motion':
        results[name]['max_geometric_motion_uv'] = float(abs(values[..., :2]).max())
    if name in ['taa_output', 'taa_current', 'copycolor', 'tonemapping', 'toggle_output', 'cut_output', 'return_output']:
        rgb = values[-1, ..., :3]
        image = Image.fromarray((np.clip(rgb, 0, 1) * 255).astype(np.uint8)).resize((544, 288))
        image.save(root / (name + '.png'))
        tile = Image.new('RGB', (544, 310), 'white')
        tile.paste(image, (0, 22))
        ImageDraw.Draw(tile).text((5, 4), name, fill='black')
        previews.append(tile)
sheet = Image.new('RGB', (1088, 310 * ((len(previews) + 1) // 2)), 'white')
for index, tile in enumerate(previews):
    sheet.paste(tile, ((index % 2) * 544, (index // 2) * 310))
sheet.save(root / 'comparison.png')
log = (root / 'editor_runtime.log').read_text(errors='replace')
targets = {}
for match in re.finditer(r'Graph target session=(\d+) pass=(.*?) resource=(.*?) input=(\d) index=(\d+) texture=(\w+) handle=\((\d+), (\d+)\)', log):
    session, render_pass, resource, input_, access, pointer, handle, generation = match.groups()
    targets.setdefault(session, []).append({'pass': render_pass, 'resource': resource, 'input': int(input_),
        'access': int(access), 'pointer': pointer, 'handle': [int(handle), int(generation)]})
results['bindings'] = {session: {'count': len(records), 'resources': sorted(set(r['resource'] for r in records)),
    'alternating': all(a['resource'] != b['resource'] for a, b in zip(records, records[1:])),
    'pointers': sorted(set(r['pointer'] for r in records)), 'first': records[0]} for session, records in targets.items()}
results['d3d12_error_corruption'] = len(re.findall(r'D3D12 debug layer[^\n]*severity=[01]', log))
(root / 'analysis.json').write_text(json.dumps(results, indent=2))
print(json.dumps({name: value for name, value in results.items() if name != 'bindings'}, indent=2))
