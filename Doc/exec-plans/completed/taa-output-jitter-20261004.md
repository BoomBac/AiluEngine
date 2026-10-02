# TAA output jitter repair

## Purpose
Diagnose and repair visible shaking with TAA enabled, then verify continuous real Editor output.

## Scope / Non-goals
Camera jitter, TAA resolve/output coordinates and associated regressions. Preserve the in-progress TAA integration.
Do not expand into the earlier M8 feature flags/history retirement issues.

## Architecture constraints
Camera owns raster jitter; MotionVector is jitter-free. TAA histories are feature-owned per-camera textures.
RenderGraph reads/writes must describe actual accesses. Keep frame-slot, GPU lifetime and postprocess ordering intact.

## Relevant files
Camera.cpp, TemporalAA.cpp, Compute/taa.hlsl, PostprocessStack.cpp, rendering.md,
Test/TaaJitterEditorAcceptance.mjs and existing GPU acceptance fixtures.

## Current milestone
Complete: implementation, Debug build, targeted regressions, continuous Editor image acceptance and restoration.

## Progress
- Read camera, TAA and postprocess flow; preserve existing dirty work.
- Added targeted original-pose AA off/on continuous capture driver with camera restoration.
- Baseline real Editor: 1088x576, original SceneView, production features unchanged, 240 contiguous frames.
  Static motion is zero, responsive coverage is zero; ToneMapping geometric displacement range is
  0.9906/0.9942 pixels with jitter response slopes 1.0162/1.0087. AA-off is stationary.
- Implemented a separate presentation dispatch with current-jitter removal, preserving jittered histories and
  Policy A responsive coverage. RG resources declare accumulation/mask SRV reads and stable transient UAV write.
- Added explicit `_taa_history_color` for history-oracle captures; `_taa_color` is stable presentation.
- First after-run: 240 contiguous frames, displacement range 0.0281/0.0291 pixels, ROI adjacent RMS
  1.2018 -> 0.3855 in 8-bit space, jitter response slopes -0.0044/-0.0117. D3D12 errors 0, restoration 9/9.
- Final real Editor run: nine sequences / 464 frames, each sequence contiguous, all dimensions1088x576 and pixels finite.
  Original static opaque ROI [510,80,870,445]: displacement range0.02833/0.02904 pixels; local adjacent RMS reduction67.82%.
  Smooth lateral camera sweep measured0.5000m actual motion; stationary return range0.03028/0.03056 pixels.
  Screen gate: displacement<0.1pixel, jitter-response magnitude<0.1 and RMS reduction>50%, PASS.
  Final ToneMapping capture ~15.2fps under PNG load; native stable output ~53fps under DDS capture load.
- Core GPU/history retest:85 matched pairs /170DDS,64 contiguous static jitters, reset/factor0 bit-identical.
  Original correct/opposite jitter-reference RMS0.00383365/0.21269702; all unchanged oracle gates PASS.
- Real-geometry controls:334 observations /986DDS,22 current-only cases exact, thin0.4pixel rail retention86.94%,
  diagonal97.98%; static variance/reference, color change, motion, occlusion and disocclusion-growth gates PASS.
  These core tests explicitly inspect jittered history; final screen stability is established by the Editor sequences.
- Seven transparent/Sprite/perspective+orthographic billboard/alpha-test cases:784 contiguous frames /3136DDS.
  Final stabilized presentation passes all original responsiveness, smear and stable-raster gates; current and
  departure-protected RGB errors are exactly0. D3D12 ERROR/CORRUPTION0 for all final runs.
- Final full `cmake --build --preset build-debug` and targeted `git diff --check` PASS. Reviewed final before/after
  image and worst stationary frame-pair. Runtime state restoration:Editor9/9, each native fixture8/8.
  No scene saved, no remaining test Editor/fixture process; unrelated dirty work preserved.

## Decision Log
Measure geometric displacement and image variation, not just successful capture transport.
Keep jittered accumulation as an explicit `_taa_history_color` contract; presentation is a separate transient output
and never feeds history back. Preserve both current and reprojected previous Policy A responsiveness without
carrying that coverage recursively into the next mask.

## Surprises & Discoveries
Confirmed: current shader aligns previous history onto the current jittered grid and sends it directly to postprocess.
Earlier history tests validated that alignment but did not validate stable screen coordinates.
Initial transparency experiment exposed changed newly uncovered pixels: previous coverage must also bypass the
presentation filter. Added the same previous-mask reprojection used by resolve, guarded by valid history; retest
passes without relaxing numerical criteria. The initial Core/Transparent wrappers returned a null ExitCode despite
successful fixture diagnostics; retaining the native Process handle before WaitForExit repaired the wrapper.
Final CoreRetest/TransparentRetest have explicit exit0. Intermediate runs remain diagnostic evidence only.

## Validation
Selected evidence beside `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/Editor.exe`:

- `TAA_Jitter_Before_20261004`: pre-repair AA-off/on comparison and jitter analysis.
- `TAA_Jitter_FinalRetest_20261004`: final Editor manifests, jitter_analysis.json, before_after.gif/png,
  camera_motion observations, captures and restoration.
- `TAA_Jitter_CoreRetest_20261004/run1`: core analysis.json, validation.txt and170DDS.
- `TAA_Jitter_SceneControls_20261004/run1`: history and controls analysis, validation.txt and986DDS.
- `TAA_Jitter_TransparentRetest_20261004/run1`: final stable-output policy analysis, validation.txt and3136DDS.

Reproduction uses `Test/RunGBufferMaterialEditorAcceptance.ps1 -DriverFile Test/TaaJitterEditorAcceptance.mjs`,
the existing GPU runner with `--rg`, `--scene-controls`, `--transparent`, and corresponding analyzers.
Supply explicit Editor/project/output paths; use fresh output directories. Final analyzer takes final run and
baseline run as two arguments and exits nonzero on a failed screen or sequence gate.

## Outcomes
The reported whole-image jitter is repaired: stable opaque output no longer follows raster jitter. Automated image
and GPU acceptance passed in the recorded scene/path, including an actual camera sweep and settling.
Interactive mouse/gizmo review, performance profiling, universal transparent AA and unrelated M8 blockers are
outside this diagnosis/repair. Policy A blended content retains its existing current-only behavior.
