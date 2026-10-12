# Path tracer Reference accumulation

## Purpose
Diagnose persistent flashing points in the user's stationary DXR Reference screenshot and repair confirmed accumulation errors.

## Scope / Non-goals
Reference progressive means, including raster sky and subpixel jitter. Keep realtime reprojection and lighting sampling unchanged.
Do not clamp bright samples or claim the original screenshot scene is accepted without capturing it.

## Architecture constraints
CPU camera/scene signature/reset handling remains authoritative. Reference averages independent samples of a fixed output pixel.

## Relevant files
RayTraceGI.cpp, raytrace_gi.hlsl, TaaGpuAcceptance.cpp, AnalyzeTaaRtgiAcceptance.py, AnalyzeRtgiReferenceSky.py, rendering.md.

## Current milestone
Accumulation implementation, automated checks and live BMW sample-retention retest complete.

## Progress
- [x] Source diagnosis and fixed-pixel Reference mean; preserve realtime branch.
- [x] Debug build, sky regression, existing production fixture regression, final diff review.
- [x] Background Editor BMW retest: all pixels retain Reference lengths 990..997.

## Decision Log
Reference camera changes already reset history globally; raster depth/normal rejection is unnecessary and discards samples.
Sky has reverse-Z linear depth zero, failing IsValidSurface and receiving history length zero indefinitely.

## Surprises & Discoveries
Initial automation checks had no local Editor process. The user later authorized background Editor startup;
the running Blender BMW scene reconnects automatically. Debug target 0 was confirmed during the retest.

## Validation
- build-debug and offline Denoise cs_6_6 compile passed; targeted diff check passed.
- PathReference_20261010_Sky_1: 16/16 gates, 640 contiguous observations/4992 captures,
  normal exit, finite pixels, zero D3D12 ERROR/CORRUPTION and 8/8 restoration checks.
  DXR: 50516 stable sky pixels retained lengths 1..32; fixed-pixel raw mean RMS error max 9.51e-8.
  Compute: 50434 sky pixels, mean RMS error max 9.45e-8. Camera and explicit resets passed.
- PathReference_20261010_Regression_1: 103/105 gates; the same two pre-existing TAA reset seed
  failures as the optimization baseline remain. New per-pixel sample retention and mean gates pass.
  Normal exit, zero D3D12 errors, 8/8 restoration checks; montage reviewed.
- DXR Reference static history adjacent RMS: 0.0882033 before (PathLightOptimization_20261010_RTGI_3)
  to 0.00220044 after (97.5% reduction). Raw RMS remains 0.0884141; no lighting sample clamp was added.
- Runtime evidence is beside F:/AiluEngine/artifacts/out/editor/bin/x64/debug/Editor.exe;
  use run1/reference_sky_analysis.json and run1/analysis.json in the corresponding directories.
- Live BMW: PathSunNee_20261010_Car_1 captures Disabled/Realtime/Reference at 896x512, including 30-second static tails.
  Its 217 frames pass 30/30 transport/settings/history/restoration checks; Reference lengths 990..997 at all pixels.
  The dense bright-point root cause was solar NEE variance, repaired separately in path-bindless-divergence.md.
  A few localized bright samples remain; multi-camera and full lighting-fidelity acceptance are not established.

## Outcomes
Reference retains independent samples of fixed output pixels, including sky and silhouettes.
Source/build, bounded automated means and live BMW sample-retention checks pass as described above.
This closes the accumulation milestone; residual lighting variance is tracked separately.
