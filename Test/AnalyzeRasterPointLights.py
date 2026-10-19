"""Independent 32/33-point-light capacity oracle and half-power diagnostic."""
import json
import sys
from pathlib import Path
import numpy as np
sys.path.insert(0, str(Path(__file__).parent / 'BlenderLinkValidation'))
from material_capture_pixels import LoadFrame

output = Path(sys.argv[1])
checks = []
def Load(name):
    path = next((output / name).glob('session_*/capture.json'))
    manifest = json.loads(path.read_text())
    assert manifest['state'] == 'completed' and len(manifest['frames']) == 3
    frames = manifest['frames']
    assert all(b['engine_frame'] == a['engine_frame'] + 1 for a, b in zip(frames, frames[1:]))
    values = [LoadFrame(path.parent / frame['file'], manifest)[..., :3] for frame in frames]
    assert all(np.isfinite(value).all() for value in values)
    assert all(np.array_equal(values[0], value) for value in values[1:])
    return values[-1]

def Check(name, passed, **details):
    checks.append(dict(name=name, passed=bool(passed), **details))

def Ulp(values):
    return 2.0 ** (np.floor(np.log2(np.maximum(values, 2.0 ** -14))) - np.array([6, 6, 5]))

zero, first = Load('zero'), Load('single0')
Check('zero_baseline', np.max(zero) == 0)
Check('first_light', first.mean() > .001, mean=float(first.mean()))
for index in [4, 15, 31]:
    value = Load('single' + str(index))
    Check('light_' + str(index + 1), np.array_equal(value, first), mean=float(value.mean()))
Check('light_33_safely_skipped', np.max(Load('single32')) == 0)
all_lights = Load('all32')
for count, name in [(32, 'all32'), (4, 'first4')]:
    actual = Load(name)
    expected = first * count
    tolerance = Ulp(first) * count + Ulp(actual) + 2e-5
    Check(name + '_additivity', np.all(abs(actual - expected) <= tolerance),
          actual_mean=float(actual.mean()), expected_mean=float(expected.mean()))
Check('overflow_33_preserves_32', np.array_equal(Load('overflow33'), all_lights))
Check('delete_all_no_stale_light', np.max(Load('empty')) == 0)
half = Load('half_power')
ratio = float(half.mean() / first.mean())
Check('point_power_linearity', abs(ratio - .5) <= .03, observed_ratio=ratio)
for name, white, colored in [('point', first, Load('colored_point')),
                              ('spot', Load('spot_full'), Load('colored_spot'))]:
    expected = white * np.array([.25,.5,1])
    tolerance = Ulp(white) * np.array([.25,.5,1]) + Ulp(colored) + 2e-5
    Check(name + '_color_once', np.all(abs(colored - expected) <= tolerance))
spot_ratio = float(Load('spot_half').mean() / Load('spot_full').mean())
Check('spot_power_linearity', abs(spot_ratio - .5) <= .03, observed_ratio=spot_ratio)
result = dict(status='PASS' if all(check['passed'] for check in checks) else 'FAIL', checks=checks,
              half_power_diagnostic=dict(expected_linear_ratio=.5, observed_ratio=ratio,
                  status='PASS' if abs(ratio - .5) <= .03 else 'FAIL'))
(output / 'point_analysis.json').write_text(json.dumps(result, indent=2))
print(json.dumps(result, indent=2))
sys.exit(0 if result['status'] == 'PASS' else 1)
