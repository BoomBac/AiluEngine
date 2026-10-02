"""Matched production-RG M8 checks; failed contribution checks are not waived by feature-active flags."""
import csv
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from AnalyzeTaaSceneAcceptance import Decode, Rms


def Main(root):
    rows = {int(row['sample']): row for row in csv.DictReader((root / 'm8_observations.csv').open())}
    captures = {}
    errors = []
    for path in root.glob('session_*/capture.json'):
        manifest = json.loads(path.read_text())
        if manifest['state'] != 'completed' or len(manifest['frames']) != 1:
            errors.append(str(path))
            continue
        frame = manifest['frames'][0]
        file = path.parent / frame['file']
        sample, kind = file.stem.split('_')[:2]
        sample = int(sample)
        value = Decode(file, manifest['format'], manifest['width'], manifest['height'])
        row = rows.get(sample)
        if row is None or frame['engine_frame'] != int(row['frame']): errors.append(f'{sample}: frame mismatch')
        if not np.isfinite(value).all(): errors.append(f'{sample}: nonfinite {kind}')
        if manifest['format'] == 26:
            raw = file.read_bytes()
            size = manifest['width'] * manifest['height'] * 4
            packed = np.frombuffer(raw, dtype='<u4', offset=len(raw) - size)
            if any(np.any(((packed >> shift) & 31) == 31) for shift in [6, 17, 27]):
                errors.append(f'{sample}: nonfinite packed {kind}')
        if row and value.shape[:2] != (int(row['height']), int(row['width'])):
            errors.append(f'{sample}: dimensions')
        captures.setdefault(sample, {})[kind] = value
    graph = {}
    with (root / 'm8_graph.csv').open() as stream:
        for row in csv.DictReader(stream): graph.setdefault(int(row['sample']), []).append(row)
    validation = dict(line.split('=', 1) for line in (root / 'validation.txt').read_text().splitlines())
    required = {1: ['SSAO'], 2: ['VolumetricFog'], 3: ['Surfel Lighting Update'], 4: ['DDGI Probe Trace'],
                5: ['VolumetricFog', 'Surfel Lighting Update', 'DDGI Probe Trace'],
                6: ['VolumetricClouds'], 7: ['RayTrace'], 8: ['Sprite'],
                9: ['SSAO', 'VolumetricFog', 'Surfel Lighting Update', 'DDGI Probe Trace', 'VolumetricClouds', 'RayTrace']}
    gates = {'all_observations': len(rows) == 1280, 'transport': not errors,
             'continuous': len(rows) == 1280 and all(int(rows[i + 1]['frame']) == int(rows[i]['frame']) + 1
                                                  for i in range(1279)),
             'd3d12': validation['ERROR_CORRUPTION'] == '0'}
    gates['requested_feature_flags_respected'] = all(
        int(row[name]) == bool(int(row['mask']) & bit)
        for row in rows.values()
        for name, bit in [('ssao_active', 1), ('fog_active', 2), ('rtgi_active', 32)])
    results = {}
    seed_errors = []
    baseline = np.mean([captures[i]['current'][..., :3] for i in range(32, 48)], axis=0)
    panels = []
    for case in range(10):
        start = case * 128
        samples = [captures[i] for i in range(start + 32, start + 48)]
        current = np.stack([value['current'][..., :3] for value in samples])
        resolved = np.stack([value['resolved'][..., :3] for value in samples])
        mean = current.mean(axis=0)
        passes = {entry['pass'] for i in range(start + 32, start + 48) for entry in graph.get(i, [])}
        missing = [name for name in required.get(case, []) if not any(name in value for value in passes)]
        delta = Rms(mean - baseline)
        results[case] = {'mask': int(rows[start]['mask']), 'passes': sorted(passes), 'missing_passes': missing,
                         'current_adjacent_rms': Rms(np.diff(current, axis=0)),
                         'resolved_adjacent_rms': Rms(np.diff(resolved, axis=0)),
                         'baseline_difference_rms': delta}
        gates[f'case_{case}_passes'] = not missing
        if case:
            gates[f'case_{case}_effective_contribution'] = delta > .0001
        gates[f'case_{case}_static_stability'] = Rms(np.diff(resolved, axis=0)) <= Rms(np.diff(current, axis=0)) + .001
        for local in range(128):
            sample = start + local
            pair = captures[sample]
            expected_resolve = local not in [96, 98]
            if ('resolved' in pair) != expected_resolve: errors.append(f'{sample}: AA output presence')
            if rows[sample]['reset'] == '1' and 'resolved' in pair:
                error = float(np.max(abs(pair['current'][..., :3] - pair['resolved'][..., :3])))
                seed_errors.append({'sample': sample, 'max_rgb_error': error})
            entries = graph.get(sample, [])
            motion_writes = [entry for entry in entries if entry['pass'] == 'DynamicMotion' and entry['access'] == 'write']
            taa_reads = [entry for entry in entries if entry['pass'] == 'TemporalAA' and entry['access'] == 'read']
            if expected_resolve and motion_writes:
                latest = motion_writes[-1]
                if not any(entry['id'] == latest['id'] and entry['version'] == latest['version'] for entry in taa_reads):
                    errors.append(f'{sample}: stale TAA motion version')
        panel = Image.new('RGB', (640, 202), '#202020')
        for column, value in enumerate([mean, resolved[-1]]):
            panel.paste(Image.fromarray(np.uint8(np.clip(value, 0, 1) * 255)), (column * 320, 22))
        ImageDraw.Draw(panel).text((5, 4), f'case {case}, mask {rows[start]["mask"]}, current mean / TAA', fill='white')
        panels.append(panel)
    gates['reset_seed_exact'] = bool(seed_errors) and all(value['max_rgb_error'] == 0 for value in seed_errors)
    gates['output_and_resource_versions'] = not errors
    gates['both_cameras'] = {row['camera'] for row in rows.values()} == {'0', '1'}
    gates['both_sizes'] = {(row['width'], row['height']) for row in rows.values()} == {('320', '180'), ('317', '179')}
    summary = {'observations': len(rows), 'captures': sum(len(value) for value in captures.values()),
               'errors': errors, 'gates': gates, 'cases': results, 'seeds': seed_errors,
               'pass': all(gates.values()),
               'scope': 'Production combined fixture; main/scene views alternate, rather than render simultaneously.'}
    (root / 'analysis.json').write_text(json.dumps(summary, indent=2))
    canvas = Image.new('RGB', (1280, 202 * 5), '#202020')
    for index, panel in enumerate(panels): canvas.paste(panel, ((index % 2) * 640, (index // 2) * 202))
    canvas.save(root / 'combinations.png')
    print(json.dumps({key: value for key, value in summary.items() if key not in ['cases', 'seeds']}, indent=2))
    return 0 if summary['pass'] else 1


if __name__ == '__main__':
    sys.exit(Main(Path(sys.argv[1])))
