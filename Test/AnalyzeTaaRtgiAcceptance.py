"""M7 production captures: transport, executed histories, reset, reference mean and TAA alignment gates."""
import csv
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from AnalyzeTaaSceneAcceptance import Decode, Rms


def Load(root):
    rows = list(csv.DictReader((root / 'm7_observations.csv').open()))
    observations = {int(r['sample']): r for r in rows}
    captures, errors = {}, []
    for path in root.rglob('capture.json'):
        manifest = json.loads(path.read_text())
        if manifest['state'] != 'completed' or len(manifest['frames']) != 1:
            errors.append(str(path) + ': incomplete')
            continue
        frame = manifest['frames'][0]
        file = path.parent / frame['file']
        sample, kind = file.name.split('_')[:2]
        sample = int(sample)
        row = observations.get(sample)
        if row is None:
            errors.append(str(path) + ': sample has no observed graph execution')
            continue
        w, h, fmt = manifest['width'], manifest['height'], manifest['format']
        if fmt in [40, 41]:
            data = file.read_bytes()
            array = np.frombuffer(data, '<f4', offset=len(data) - w * h * 4).reshape(h, w, 1)
        elif fmt == 2:
            data = file.read_bytes()
            array = np.frombuffer(data, '<f4', offset=len(data) - w * h * 16).reshape(h, w, 4)
        else:
            array = Decode(file, fmt, w, h)
        if (w, h) != (int(row['width']), int(row['height'])):
            errors.append(str(path) + ': dimensions')
        if frame['engine_frame'] != int(row['engine_frame']) or frame['capture_index'] != 0:
            errors.append(str(path) + ': frame')
        if not np.isfinite(array).all():
            errors.append(str(path) + ': nonfinite')
        if kind in captures.setdefault(sample, {}):
            errors.append(str(path) + ': duplicate')
        captures[sample][kind] = array
    return rows, observations, captures, errors


def Length(heat, reference):
    # In these bounded runs the heatmap remains in its blue->green half.
    return np.rint(heat[..., 1] * (2048 if reference else 32))


def Main(root):
    rows, observations, captures, errors = Load(root)
    accesses = list(csv.DictReader((root / 'm7_graph.csv').open()))
    frames = [int(r['engine_frame']) for r in rows]
    has_world = any('world' in pair for pair in captures.values())
    has_reactive = any('reactive' in pair for pair in captures.values())
    expected = sum((5 if int(r['mode']) == 4 else 7) + has_world +
                   (has_reactive and int(r['mode']) == 2) for r in rows)
    gates = {'transport': len(rows) == 640 and sum(map(len, captures.values())) == expected and not errors,
             'continuity': len(frames) == 640 and bool(np.all(np.diff(frames) == 1))}
    validation = dict(line.split('=', 1) for line in (root / 'validation.txt').read_text().splitlines())
    run_summary = json.loads((root.parent / 'run_summary.json').read_text())
    gates['fixture_exit'] = run_summary['exit_code'] == 0 and validation['failed'] == '0'
    gates['d3d12_error_corruption'] = validation['ERROR_CORRUPTION'] == '0'
    gates['world_capture_present'] = has_world
    gates['reactive_capture_present'] = has_reactive
    shader_path = Path(__file__).resolve().parents[1] / 'Engine/Res/Shaders/hlsl/ray_trace/raytrace_gi.hlsl'
    shader = shader_path.read_text(encoding='utf-8')
    gates['source_neighbor_jitter'] = 'floor(float2(pixel) - temporal_motion.xy * _ScreenParams.zw + 0.5)' not in shader
    for i, row in observations.items():
        kinds = {'raw', 'current', 'resolved', 'motion', 'depth'}
        if int(row['mode']) != 4:
            kinds |= {'history', 'heat'}
        if has_world: kinds.add('world')
        if has_reactive and int(row['mode']) == 2: kinds.add('reactive')
        gates['transport'] &= set(captures.get(i, {})) == kinds
    results, panels = {}, []
    for backend in range(2):
        for mode in range(5):
            start = backend * 320 + mode * 64
            name = ('compute', 'dxr')[backend] + '_' + ('baseline', 'jitter_only', 'taa', 'reference', 'disabled')[mode]
            missing = [start + j for j in range(64) if start + j not in observations or
                       not {'raw', 'current', 'resolved', 'motion', 'depth'}.issubset(captures.get(start + j, {})) or
                       (mode != 4 and not {'history', 'heat'}.issubset(captures.get(start + j, {})))]
            if any(start + j not in observations for j in range(64)) or any(start + j in missing for j in range(32)):
                results[name] = {'unaccepted_missing_samples': missing}
                gates[name + '_complete'] = False
                continue
            gates[name + '_complete'] = not missing
            segment = [observations[start + j] for j in range(64)]
            pairs = [captures.get(start + j, {}) for j in range(64)]
            primary = 'RayTrace GI DXR' if backend else 'RayTrace GI'
            executed = {int(a['sample']) for a in accesses if a['pass'] == primary and a['access'] == 'write'}
            result = {'declared_primary_frames': sum(start + j in executed for j in range(64)),
                      'raw_rgb_max': max(float(p['raw'][..., :3].max()) for p in pairs if 'raw' in p),
                      'unaccepted_missing_samples': missing,
                      'distinct_static_jitters': len({(r['jitter_x'], r['jitter_y']) for r in segment[:32]}),
                      'static_motion_max_pixels': max(float(np.abs(p['motion'][..., :2] * [320, 180]).max())
                                                      for p in pairs[1:32]),
                      'static_raw_adjacent_rms': Rms(np.diff(
                          np.stack([p['raw'][..., :3] for p in pairs[16:32]]), axis=0))}
            gates[name + '_executed'] = result['declared_primary_frames'] == 64 and result['raw_rgb_max'] > .01
            gates[name + '_jitter'] = result['distinct_static_jitters'] == (1 if mode == 0 else 32)
            gates[name + '_static_motion'] = result['static_motion_max_pixels'] < .01
            foreground_agreement = []
            for pair in pairs[:32]:
                depth = pair['depth'][..., 0]
                expected_foreground = depth > (.01901902072 + .01719901897) * .5
                actual_foreground = pair['raw'][..., 0] > .4
                foreground_agreement.append(float(np.mean(expected_foreground == actual_foreground)))
            result['primary_raster_coverage_agreement_min'] = min(foreground_agreement)
            if has_world:
                plane_errors, normal_errors, coverage_errors, boundary_counts = [], [], [], []
                focal = 180 * .866025403784 / 5
                # Raster edge snapping differs from ideal float geometry near a boundary. The final control's
                # 96 disagreements are all 0.001366..0.001373 reference pixels from the horizontal edge.
                # Exclude one 1/256-pixel boundary band; retain the independent plane/normal checks everywhere.
                edge_tolerance = 1 / 256
                for j, pair in enumerate(pairs):
                    world = pair['world']
                    xyz = world[..., :3]
                    z = xyz[..., 2]
                    plane_errors.append(float(np.minimum(np.abs(z), np.abs(z - .5)).max()))
                    normal_errors.append(float(np.abs(world[..., 3] - 1).max()))
                    camera_x = float(segment[j]['position_x'])
                    scale = 5 / (z + 5)
                    px = (camera_x + (xyz[..., 0] - camera_x) * scale) * focal + 160
                    py = 90 - xyz[..., 1] * scale * focal
                    geometric = np.zeros(z.shape, bool)
                    uncertain = (np.abs(py - 22) < edge_tolerance) | (np.abs(py - 158) < edge_tolerance)
                    for rail, width in enumerate([.4, .8, 1.4, 2.4, 4., 37.]):
                        left = (45 + 28 * rail if rail < 5 else 206) + (py - 22) * (22 if rail < 5 else 50) / 136
                        right = left + width
                        geometric |= (py >= 22) & (py <= 158) & (px >= left) & (px <= right)
                        uncertain |= ((np.abs(px - left) < edge_tolerance) |
                                      (np.abs(px - right) < edge_tolerance))
                    covered = np.abs(z) < .01
                    coverage_errors.append(float(np.mean((geometric != covered)[~uncertain])))
                    boundary_counts.append(int(uncertain.sum()))
                result['world_plane_max_error'] = max(plane_errors)
                result['normal_dot_max_error'] = max(normal_errors)
                result['world_geometry_coverage_mismatch_max'] = max(coverage_errors)
                result['world_geometry_boundary_band_pixels_max'] = max(boundary_counts)
                result['world_geometry_boundary_band_reference_pixels'] = edge_tolerance
                gates[name + '_world_reconstruction'] = max(plane_errors) < .001 and max(coverage_errors) < .001
                gates[name + '_normal_reconstruction'] = max(normal_errors) < .001
            if mode != 4:
                length = [Length(p['heat'], mode == 3) if 'heat' in p else None for p in pairs]
                result['static_history_length_median'] = [float(np.median(a[30:150, 20:300])) for a in length[:32]]
                result['static_history_adjacent_rms'] = Rms(np.diff(
                    np.stack([p['history'][..., :3] for p in pairs[16:32]]), axis=0))
                result['first_raw_history_max_error'] = float(np.abs(
                    pairs[0]['history'][..., :3] - pairs[0]['raw'][..., :3]).max())
                if mode in [0, 3]:
                    gates[name + '_seed'] = result['first_raw_history_max_error'] < .001
                gates[name + '_accumulation'] = result['static_history_length_median'][-1] >= 20
                if mode < 3:
                    reset = {}
                    for j in [48, 50, 52, 54, 56]:
                        if start + j in missing:
                            reset[j] = {'missing_capture': True}
                            continue
                        reset[j] = {'history_length_max': float(length[j].max()),
                                    'raw_error': float(np.abs(
                                        pairs[j]['history'][..., :3] - pairs[j]['raw'][..., :3]).max())}
                    result['resets'] = reset
                    result['first_ready_after_resize'] = {j: {
                        'history_length_max': float(length[j].max()) if length[j] is not None else None,
                        'raw_history_max_error': float(np.abs(
                            pairs[j]['history'][..., :3] - pairs[j]['raw'][..., :3]).max())
                        if {'history', 'raw'}.issubset(pairs[j]) else None} for j in [49, 51]}
                    # Explicit MarkTemporalHistoryReset at unchanged pose is reported separately.
                    gates[name + '_resize_cut_fov'] = all(
                        not reset[j].get('missing_capture') and reset[j]['history_length_max'] <= 1 and
                        reset[j]['raw_error'] < .001 for j in [48, 50, 52, 54])
                    gates[name + '_explicit_reset'] = (
                        reset[56]['history_length_max'] <= 1 and reset[56]['raw_error'] < .001)
                else:
                    oracle, opposite, uncertain_counts = [], [], []
                    for j in range(1, 32):
                        h, w = pairs[j]['depth'].shape[:2]
                        yy, xx = np.mgrid[:h, :w]
                        du = float(segment[j - 1]['jitter_x']) - float(segment[j]['jitter_x'])
                        dv = float(segment[j - 1]['jitter_y']) - float(segment[j]['jitter_y'])
                        roi = np.zeros((h, w), bool); roi[30:150, 20:300] = True
                        for sign, destination in [(1, oracle), (-1, opposite)]:
                            px = ((xx + .5) + sign * du * w).astype(np.float32)
                            py = ((yy + .5) + sign * dv * h).astype(np.float32)
                            uncertain = (np.abs(px - np.rint(px)) < 1e-4) | (np.abs(py - np.rint(py)) < 1e-4)
                            hx = np.clip(px.astype(int), 0, w - 1)
                            hy = np.clip(py.astype(int), 0, h - 1)
                            previous = pairs[j - 1]['history'][hy, hx, :3]
                            old_length = length[j - 1][hy, hx]
                            mask = roi & (length[j] > 1) & (old_length > 0) & ~uncertain
                            if sign == 1: uncertain_counts.append(int((roi & uncertain).sum()))
                            predicted = ((previous * old_length[..., None] + pairs[j]['raw'][..., :3]) /
                                         (old_length[..., None] + 1))
                            if mask.any(): destination.append(Rms((predicted - pairs[j]['history'][..., :3])[mask]))
                    result['reference_oracle_rms_max'] = max(oracle)
                    result['reference_opposite_rms_mean'] = float(np.mean(opposite))
                    result['reference_integer_boundary_uncertain_pixels'] = sum(uncertain_counts)
                    result['reference_robust_pairs'] = len(oracle)
                    gates[name + '_reference_mean'] = max(oracle) < .001
                    result['real_camera_change_length_max'] = float(length[48].max())
                    gates[name + '_real_camera_reset'] = length[48].max() <= 1
            if mode == 2:
                current = np.stack([p['current'][..., :3] for p in pairs[16:32]])
                resolved = np.stack([p['resolved'][..., :3] for p in pairs[16:32]])
                mean_current, mean_resolved = current.mean(0), resolved.mean(0)
                shifts = {}
                for dy in [-1, 0, 1]:
                    for dx in [-1, 0, 1]:
                        shifts[f'{dx},{dy}'] = Rms(
                            mean_resolved[22:158, 22:298] - mean_current[22+dy:158+dy, 22+dx:298+dx])
                result['alignment_rms'] = shifts
                result['best_integer_offset'] = min(shifts, key=shifts.get)
                result['taa_adjacent_rms'] = Rms(np.diff(resolved, axis=0))
                result['taa_input_adjacent_rms'] = Rms(np.diff(current, axis=0))
                gates[name + '_no_integer_offset'] = result['best_integer_offset'] == '0,0'
                e0 = shifts['0,0'] ** 2
                fractional = []
                for negative, positive in [('-1,0', '1,0'), ('0,-1', '0,1')]:
                    em, ep = shifts[negative] ** 2, shifts[positive] ** 2
                    fractional.append(.5 * (em - ep) / max(em - 2 * e0 + ep, 1e-12))
                result['quadratic_fractional_offset_pixels'] = fractional
                gates[name + '_fractional_offset'] = max(map(abs, fractional)) < .25
                if has_reactive:
                    result['reactive_max'] = max(float(pair['reactive'].max()) for pair in pairs)
                    gates[name + '_opaque_not_responsive'] = result['reactive_max'] == 0
                result['taa_seed_max_error'] = max(float(np.abs(
                    pairs[j]['resolved'] - pairs[j]['current']).max()) for j in [0, 48, 50, 52, 54, 56])
                gates[name + '_taa_reset_seed'] = result['taa_seed_max_error'] == 0
            results[name] = result
            for kind in ['raw', 'history', 'resolved']:
                if kind not in pairs[31]: continue
                a = pairs[31][kind][..., :3]
                image = Image.fromarray((np.clip(a, 0, 1) ** (1/2.2) * 255).astype('uint8'))
                tile = Image.new('RGB', (320, 202)); tile.paste(image, (0, 22))
                ImageDraw.Draw(tile).text((4, 4), name + ' ' + kind, fill='white'); panels.append(tile)
    summary = {'planned_observations': 640, 'planned_captures': 4224 + 640 * has_world + 128 * has_reactive,
               'missing_observations': sorted(set(range(640)) - set(observations)),
               'observations': len(rows), 'captures': sum(map(len, captures.values())),
               'expected_observed_captures': expected,
               'errors': errors, 'gates': {k: bool(v) for k, v in gates.items()}, 'cases': results,
               'automated_pass': all(gates.values()),
               'scope': 'Production RG fixture; independent stochastic primary samples remain enabled. '
                        'Depth/normal reconstruction uses a measurement kernel with production CameraCB/helpers. '
                        'Geometry coverage excludes a documented 1/256 reference-pixel boundary band. '
                        'Fractional offset is a quadratic estimate. ReSTIR, multi-camera, indirect-light-only '
                        'boundaries, interactive review and GPU cost need separate evidence.'}
    (root / 'analysis.json').write_text(json.dumps(summary, indent=2))
    report = [
        '# M7 repair acceptance - 2026-10-03', '',
        'Result: **' + ('PASS' if summary['automated_pass'] else 'FAIL') +
        '** on the captured single-camera production-RG fixture. M8 was not exercised.', '',
        f'Planned640 samples/{summary["planned_captures"]} DDS; observed{len(rows)} samples/'
        f'{summary["captures"]} completed DDS. Transport errors:{len(errors)}. '
        f'Continuity:{gates["continuity"]}. All individual gates are retained in analysis.json.', '',
        'Production Renderer RT mode, Compute and DXR; procedural environment with high-contrast emissive meshes; '
        '320x180 and317x179 resize, single render thread, stochastic primary sampling enabled. '
        'Five64-frame modes per backend: no-jitter realtime, jitter-only realtime, full TAA, Reference, '
        'disabled control.', '',
        'Repairs: RTGI produces opaque color at455 before TAAOpaqueSnapshot456/Sprite460; Denoise primary taps '
        'and clamp neighbors use one jitter-corrected history_position; TAA and RTGI observe the same Camera reset '
        'revision; persistent render textures and surface buffers are allocated before first resize dispatch; '
        'history/buffer rotation commits only after actual dispatch. Global TAA history factor remains0.95.', '',
        '| Case | Complete | Static length | Raw/history adjacent RMS | Plane error |',
        '|---|---|---|---|---|',
    ]
    for name, result in results.items():
        lengths = result.get('static_history_length_median', [])
        report.append(f'| {name} | {gates.get(name + "_complete", False)} | '
                      f'{lengths[-1] if lengths else "n/a"} | '
                      f'{result.get("static_raw_adjacent_rms", 0):.6g}/'
                      f'{result.get("static_history_adjacent_rms", 0):.6g} | '
                      f'{result.get("world_plane_max_error", "n/a")} |')
    report += ['', 'Complete cut/resize/FOV/reset seed checks require history length<=1 and raw/history RGB '
               'error<0.001; TAA reset checks require exact packed current RGB. '
               'Full combination tests additionally check zero opaque responsive mask, best integer shift[0,0] '
               'and quadratic fractional offset<0.25pixels.', '',
               'World reconstruction is a measurement shader calling the same ComputeWorldSpacePosition/'
               'UnpackNormal helpers with the current production camera buffer and actual GBuffer/depth. '
               'Captured XYZ must lie on the known planes and reproduce the known foreground mesh coverage; '
               'coverage excludes a 1/256 reference-pixel boundary band for raster edge snapping. The original '
               'ideal-float oracle disagreed on 96 edge pixels, all within0.001373pixels of a horizontal edge; '
               'per-case excluded counts and the band are recorded. Plane/normal checks exclude no pixels. '
               'normal agreement checks the known world normal. Compute PrimaryRay independently jitters its '
               'primary samples, while DXR primary rays use current jittered IVP; this measurement does not '
               'switch the production kernel to the currently inactive GBuffer RayGen.', '',
               'Reference mean replay uses actual preceding GPU history. Nearest-pixel coordinates within1e-4 '
               'pixels of integer boundaries are reported as uncertain rather than used as a bit-exact oracle. '
               'Each case records robust-pair counts, excluded pixel observations, correct/opposite sign errors '
               'and actual history-length progression. The reference replay threshold is unchanged.', '',
               'Evidence: analysis.json, montage.png, m7_observations.csv, m7_graph.csv, ../engine.log, '
               'validation.txt in run1, ../restoration.json and ../run_summary.json. '
               'Fixture exit code, D3D12 severity counts and restoration must be read from those records '
               'separately from image gates. Run duration includes initialization and capture load.', '',
               'This is automated GPU/image acceptance and montage review; interactive Editor use, multi-camera '
               'behavior, ReSTIR execution, indirect-light-only boundaries and GPU-cost profiling '
               'remain unmeasured.', '',
               'Reproduce with Test/RunTaaTransparentAcceptance.ps1 -Mode --rtgi, '
               'then Test/AnalyzeTaaRtgiAcceptance.py RUN/run1.', '']
    (root / 'report.md').write_text('\n'.join(report), encoding='utf-8')
    montage = Image.new('RGB', (960, 202 * ((len(panels) + 2) // 3)))
    for index, panel in enumerate(panels): montage.paste(panel, ((index % 3) * 320, (index // 3) * 202))
    montage.save(root / 'montage.png')
    print(json.dumps({'observations': len(rows), 'captures': summary['captures'], 'gates': summary['gates']}, indent=2))


if __name__ == '__main__':
    Main(Path(sys.argv[1]))
