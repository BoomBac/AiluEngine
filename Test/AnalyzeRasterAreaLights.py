"""Independent pixel oracle for the nine-area-light Editor acceptance captures."""
import json
import sys
from pathlib import Path
import numpy as np
sys.path.insert(0, str(Path(__file__).parent/'BlenderLinkValidation'))
from material_capture_pixels import LoadFrame


def Main():
    output = Path(sys.argv[1])
    checks = []
    def Load(name):
        manifest_path = next((output/name).glob('session_*/capture.json'))
        manifest = json.loads(manifest_path.read_text())
        assert manifest['state'] == 'completed' and len(manifest['frames']) == 3
        frames = [LoadFrame(manifest_path.parent/frame['file'], manifest)[..., :3]
                  for frame in manifest['frames']]
        assert all(np.isfinite(frame).all() for frame in frames)
        assert all(np.array_equal(frames[0], frame) for frame in frames[1:])
        return frames[-1]
    def Check(name, valid, **details):
        checks.append(dict(name=name, passed=bool(valid), **details))
    zero = Load('zero')
    singles = [Load('single'+str(index)) for index in range(9)]
    Check('zero_intensity_baseline', np.max(zero) == 0)
    for index, frame in enumerate(singles):
        Check('independent_light_'+str(index), float((frame-zero).mean()) > .001,
              mean_contribution=float((frame-zero).mean()))
    def SumCheck(name, actual, terms):
        expected = sum(terms)-(len(terms)-1)*zero
        # Each production R11G11B10 capture may truncate one ULP per channel.
        def Ulp(values):
            return 2.0**(np.floor(np.log2(np.maximum(values, 2.0**-14)))-np.array([6,6,5]))
        tolerance = sum(Ulp(term) for term in terms)+Ulp(actual)+2e-5
        Check(name, np.all(abs(actual-expected) <= tolerance),
              actual_mean=float(actual.mean()), expected_mean=float(expected.mean()),
              max_abs_error=float(abs(actual-expected).max()))
    all_lights = Load('all9')
    SumCheck('nine_light_additivity', all_lights, singles)
    SumCheck('shrink_to_four', Load('shrink4'), singles[:4])
    regrown = Load('regrow9')
    SumCheck('regrow_to_nine', regrown, singles)
    Check('delete_all_no_stale_light', np.max(Load('empty')) == 0)
    result = dict(status='PASS' if all(check['passed'] for check in checks) else 'FAIL', checks=checks)
    (output/'pixel_analysis.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))
    return 0 if result['status'] == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(Main())
