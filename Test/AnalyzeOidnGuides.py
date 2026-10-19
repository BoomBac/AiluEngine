"""Check native first-hit guide semantics and make a labeled Editor comparison; no equal-SPP quality claim."""
import json
from pathlib import Path
import re
import sys

import numpy as np
from PIL import Image, ImageDraw
from AnalyzeToneMappingSmoke import Decode, Reference


def Main():
    output = Path(sys.argv[1])
    captures = {}

    def Load(name):
        status = json.loads((output / (name + '_status.json')).read_text())
        manifest_path = next(Path(status['output_directory']).glob('session_*/capture.json'), None)
        if manifest_path is None:
            manifest_path = Path(status['output_directory']) / 'capture.json'
        manifest = json.loads(manifest_path.read_text())
        frames = sorted(manifest['frames'], key=lambda item: item['capture_index'])
        assert manifest['state'] == 'completed' and len(frames) == 1
        path = manifest_path.parent / frames[0]['file']
        values = Decode(path, manifest).astype(float)
        assert np.isfinite(values).all(), name + ' contains non-finite values'
        captures[name] = {'engine_frame': frames[0]['engine_frame'], 'format': manifest['format'],
                          'width': manifest['width'], 'height': manifest['height'], 'file': str(path)}
        alpha = None
        if manifest['format'] == 2:
            raw = path.read_bytes()
            alpha = np.frombuffer(raw, '<f4', offset=len(raw) - values.shape[0] * values.shape[1] * 16)
            alpha = alpha.reshape(*values.shape[:2], 4)[..., 3]
        return values, alpha

    def Display(values):
        linear = Reference(np.maximum(values, 0), 2)
        encoded = np.where(linear <= .0031308, linear * 12.92, 1.055 * linear ** (1 / 2.4) - .055)
        return np.uint8(np.clip(np.rint(encoded * 255), 0, 255))

    report = {'scope': 'Finite/range/coverage checks; captures have different SPP and are not a quality acceptance',
              'backends': {}, 'captures': captures}
    rows = []
    for backend in ['compute', 'dxr']:
        color, _ = Load(backend + '_color')
        guided, _ = Load(backend + '_guides')
        reference, _ = Load(backend + '_reference')
        assert guided.max() > .01 and reference.max() > .01, backend + ' lighting output is black'
        albedo, coverage = Load(backend + '_albedo')
        normal, _ = Load(backend + '_normal')
        normal_length = np.linalg.norm(normal, axis=-1)
        assert albedo.min() >= -1e-6 and albedo.max() <= 1.0001
        assert normal.min() >= -1.0001 and normal.max() <= 1.0001
        assert normal_length.max() <= 1.001
        assert coverage is not None and coverage.min() >= 0 and coverage.max() <= 1.0001
        assert np.count_nonzero(normal_length > .1) > 100 and np.count_nonzero(normal < -.05) > 100
        assert float(albedo.std()) > .001
        metrics = {'albedo_range': [float(albedo.min()), float(albedo.max())],
                   'normal_range': [float(normal.min()), float(normal.max())],
                   'max_normal_length': float(normal_length.max()),
                   'coverage_range': [float(coverage.min()), float(coverage.max())],
                   'surface_pixels': int(np.count_nonzero(coverage > .5)),
                   'hdr_peak': float(guided.max()), 'roi_log_rmse': {}}
        width = color.shape[1]
        for index, name in enumerate(['matte', 'metal', 'glass']):
            region = np.s_[color.shape[0] // 4:color.shape[0] * 3 // 4,
                           index * width // 3:(index + 1) * width // 3]
            metrics['roi_log_rmse'][name] = {
                mode: float(np.sqrt(np.mean((np.log1p(np.maximum(image[region], 0)) -
                                              np.log1p(np.maximum(reference[region], 0))) ** 2)))
                for mode, image in [('color_only', color), ('guides', guided)]}
        report['backends'][backend] = metrics
        rows.append([Display(color), Display(guided), Display(reference), np.uint8(albedo * 255),
                     np.uint8(np.clip(normal * .5 + .5, 0, 1) * 255)])
    height, width = rows[0][0].shape[:2]
    sheet = Image.new('RGB', (width * 5, (height + 32) * 2), '#181818')
    draw = ImageDraw.Draw(sheet)
    labels = ['Color only / Balanced', 'Guides / High', 'Raw reference / later SPP',
              'First-hit albedo', 'World normal / display encoded']
    for row, backend in enumerate(['Compute', 'DXR']):
        for column, label in enumerate(labels):
            x, y = column * width, row * (height + 32)
            draw.text((x + 8, y + 8), backend + ': ' + label, fill='white')
            sheet.paste(Image.fromarray(rows[row][column]), (x, y + 32))
    sheet.save(output / 'comparison.png')
    log_text = '\n'.join((output / name).read_text(encoding='utf-8', errors='replace')
                         for name in ['debug.log', 'FileAppender_log.txt'])
    report['d3d12_failures'] = re.findall(
        r'^.*(?:D3D12 ERROR|PIPELINE_STATE_TYPE_MISMATCH|D3D12 debug layer[^\n]*severity=[01],).*$',
        log_text, re.MULTILINE)
    assert not report['d3d12_failures'], 'D3D12 validation failure; inspect logs'
    report['status'] = 'PASS'
    (output / 'analysis.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report['backends'], indent=2))


if __name__ == '__main__':
    Main()
