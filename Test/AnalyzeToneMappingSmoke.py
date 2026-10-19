"""Compare production ToneMapping shader GPU captures with independent double-precision references."""
import json
import re
import sys
from pathlib import Path
import numpy as np


def Decode(path, manifest):
    width, height = manifest['width'], manifest['height']
    data = path.read_bytes()
    if data[:4] != b'DDS ':
        raise ValueError('Expected native DDS')
    if manifest['format'] == 2:
        return np.frombuffer(data, '<f4', offset=len(data)-width*height*16).reshape(height, width, 4)[..., :3]
    if manifest['format'] != 26:
        raise ValueError('Unexpected ToneMapping target format')
    packed = np.frombuffer(data, '<u4', offset=len(data)-width*height*4).reshape(height, width)
    channels = []
    for shift, bits in ((0, 6), (11, 6), (22, 5)):
        value = (packed >> shift) & ((1 << (bits+5))-1)
        mantissa, exponent = value & ((1 << bits)-1), value >> bits
        channel = np.where(exponent == 0, mantissa * 2.0**(-14-bits),
                           (1+mantissa/2.0**bits) * 2.0**(exponent.astype(float)-15))
        channels.append(np.where(exponent == 31, np.inf, channel))
    return np.stack(channels, axis=-1)


def Reference(values, mode):
    if mode == 3:
        return np.clip(values, 0, 1)
    if mode == 0:
        return values
    if mode == 1:
        return np.clip((values*(2.51*values+.03))/(values*(2.43*values+.59)+.14), 0, 1)
    inset = np.array([[.84247906,.07843360,.07922375],[.04232824,.87846864,.07916613],
                      [.04237565,.07843360,.87914297]])
    outset = np.array([[1.19687901,-.09802088,-.09902974],[-.05289685,1.15190313,-.09896118],
                       [-.05297164,-.09804345,1.15107367]])
    wide = np.maximum(values, 0) @ inset.T
    encoded = np.clip((np.log2(np.maximum(wide, 1e-10))+12.47393)/16.499999, 0, 1)
    # Polynomial evaluation uses Horner form independently of the shader's power expansion.
    contrast = np.polyval([15.5,-40.14,31.96,-6.868,.4298,.1191,-.00232], encoded)
    return np.clip(np.maximum(contrast @ outset.T, 0)**2.2, 0, 1)


def main():
    output = Path(sys.argv[1])
    samples = np.array([[0,0,0],[.001,.001,.001],[.18,.18,.18],[1,1,1],[4,4,4],[4,0,0],[0,4,0],[0,0,4]])
    checks, failures = [], []
    for path in output.glob('session_*/capture.json'):
        manifest = json.loads(path.read_text())
        frames = manifest['frames']
        name = frames[0]['file'] if frames else ''
        match = re.search(r'mode(\d)_exposure([0-9.]+)_gamma([0-9.]+)_(packed|float)', name)
        if not match:
            continue
        mode, exposure = int(match[1]), float(match[2])
        actual = Decode(path.parent/name, manifest)[0]
        gamma = float(match[3])
        expected = Reference(samples*exposure, mode)
        if gamma != 1:
            encoded = np.where(expected <= .0031308, expected*12.92, 1.055*expected**(1/2.4)-.055)
            encoded = encoded**(1/gamma)
            expected = np.where(encoded <= .04045, encoded/12.92, ((encoded+.055)/1.055)**2.4)
        error = float(np.max(abs(actual-expected)))
        # Production R11G11B10 can truncate by one ULP; its blue mantissa has only five bits.
        # Validate the operator strictly in RGBA32F and validate production storage separately.
        packed = manifest['format'] == 26
        if packed:
            exponent = np.floor(np.log2(np.maximum(expected, 2.0**-14)))
            tolerance = 2.0**(exponent-np.array([6,6,5])) + 2e-5
        else:
            tolerance = np.full_like(expected, .0001)
        valid = (manifest['state'] == 'completed' and len(frames) == 1
                 and np.isfinite(actual).all() and np.all(abs(actual-expected) <= tolerance))
        if mode == 2:
            valid = valid and np.min(actual) >= 0 and np.max(actual) <= 1
            if not packed:
                valid = valid and np.max(np.ptp(actual[:5], axis=1)) <= .003
            valid = valid and np.all(np.diff(actual[:5].mean(axis=1)) >= -.00001)
        record = dict(name=name, mode=mode, exposure=exposure, gamma=gamma, format=manifest['format'], max_abs_error=error,
                      actual=actual.tolist(), expected=expected.tolist(),
                      tolerance=tolerance.tolist(), passed=bool(valid))
        checks.append(record)
        if not valid:
            failures.append(record)
    if len(checks) != 40:
        failures.append(dict(expected_capture_count=40, actual=len(checks)))
    result = dict(status='FAIL' if failures else 'PASS', capture_count=len(checks),
                  max_abs_error=max((c['max_abs_error'] for c in checks), default=None),
                  float_max_abs_error=max((c['max_abs_error'] for c in checks if c['format']==2), default=None),
                  checks=checks, failures=failures)
    (output/'numeric_analysis.json').write_text(json.dumps(result, indent=2))
    print(json.dumps({k:v for k,v in result.items() if k not in ['checks','failures']}))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
