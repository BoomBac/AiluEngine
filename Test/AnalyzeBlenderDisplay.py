"""Compare native ToneMapping captures with source Standard/sRGB exposure and Gamma."""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).parent / 'BlenderLinkValidation'))
from material_capture_pixels import LoadFrame


def Encode(values):
    return np.where(values <= .0031308, values * 12.92, 1.055 * values ** (1 / 2.4) - .055)


def Main():
    native, reference, output = map(Path, sys.argv[1:4])
    settings = json.loads((reference / 'blender_reference/reference.json').read_text())['settings']
    checks = []

    def Load(name):
        path = next((native / name).glob('session_*/capture.json'))
        manifest = json.loads(path.read_text())
        frames = manifest['frames']
        assert manifest['state'] == 'completed' and len(frames) == 3
        indices = [frame['engine_frame'] for frame in frames]
        assert all(right == left + 1 for left, right in zip(indices, indices[1:]))
        values = [LoadFrame(path.parent / frame['file'], manifest)[..., :3] for frame in frames]
        assert all(np.isfinite(value).all() for value in values)
        return values

    source_frames, actual_frames = Load('display_input'), Load('display_final')
    source, actual = source_frames[-1], actual_frames[-1]
    def Transform(values):
        exposed = np.clip(values * 2 ** settings['exposure_ev'], 0, 1)
        encoded = Encode(exposed) ** (1 / settings['gamma'])
        return np.where(encoded <= .04045, encoded / 12.92, ((encoded + .055) / 1.055) ** 2.4)
    expected = Transform(source)
    # Scene input/output captures occur on separate frames. Bound the observed input variation;
    # strict operator accuracy is checked independently by the constant-input ToneMappingSmoke.
    transformed = np.stack([Transform(frame) for frame in source_frames])
    low, high = transformed.min(axis=0), transformed.max(axis=0)
    tolerance = 2.0 ** (np.floor(np.log2(np.maximum(high, 2.0 ** -14))) - np.array([6, 6, 5])) + 3e-5
    outside = (actual < low - tolerance) | (actual > high + tolerance)
    error = abs(actual - expected)
    checks.append(dict(name='production_linear_transfer_observed_frame_interval',
                       passed=bool(not np.any(outside)), max_abs_error=float(error.max()),
                       out_of_bound_channels=int(np.count_nonzero(outside)),
                       max_input_frame_variation=float(np.ptp(np.stack(source_frames), axis=0).max())))
    np.save(reference / 'spaceship_display_input.npy', source)
    preview = np.clip(np.rint(Encode(np.maximum(actual, 0)) * 255), 0, 255).astype(np.uint8)
    Image.fromarray(preview).save(output / 'engine_display.png')
    blender_path = reference / 'blender_same_engine_hdr.png'
    if blender_path.exists():
        blender = np.asarray(Image.open(blender_path).convert('RGB'), dtype=float) / 255
        display_error = abs(preview.astype(float) / 255 - blender)
        checks.append(dict(name='blender_same_hdr_display', passed=bool(display_error.max() <= 2 / 255 + 1e-8),
                           max_code_value_error=float(display_error.max() * 255),
                           p99_code_value_error=float(np.quantile(display_error, .99) * 255)))
    result = dict(status='PASS' if all(check['passed'] for check in checks) else 'FAIL', settings=settings,
                  native_run=str(native), checks=checks)
    (output / 'spaceship_display_analysis.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))
    return 0 if result['status'] == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(Main())
