# Volumetric fog sky composition

## Purpose
Restore volumetric fog on sky pixels and compose scattering plus transmittance consistently.

## Scope / Non-goals
Make volume generation precede its consumer; move composition out of DeferredLighting into a feature-owned pass after sky,
before transparency. Preserve current density, lighting injection and temporal algorithm. Transparent material fog and
distant analytic fog are outside this change.

## Architecture constraints
Feature owns passes/materials; explicit RenderGraph accesses and current-view camera data. Preserve unrelated dirty work.
Use build-debug only. Capture evidence beside the exercised Editor executable.

## Relevant files
VolumetricFog.h/.cpp, deferred_lighting.hlsl, Compute/volumetric_light.hlsl, new volumetric_fog_composite shader.

## Current milestone
Complete: M1 baseline, M2 implementation/build, M3 final capture and analysis.

## Progress
- Source diagnosis: DeferredLighting stencil excludes sky; sky runs later and volume generation follows lighting.
- Capture client mentioned by skill and ExecPlan template are absent; use current automation pipe protocol.
- M1 complete: baseline DrawSkyBox, DeferredLightingPass and CopyColor each have 8 frames at 960x576;
  CopyDepthPass and CopyColor also captured as native DDS. Initial camera saved in evidence directory.
- M2 implemented: feature-owned composition at event 400, injection at 300, current-view camera, published accumulation
  handle, alpha transmittance, removed DeferredLighting fog binding/sampling. Debug build passed.
- M3 complete: fresh final Debug Editor, 16 stationary + 240 continuous moving + 16 settled frames, native depth/HDR
  captures, fresh-process scattering/transmittance controls, source/camera restoration and configuration hash checks.

## Decision Log
- Independent full-screen composition after sky; output S + T * background using fixed-function blending.
- Sky samples the final volume slice; geometry samples its view depth.
- Retain generation at event 300 to preserve its existing debug-ray placement. Once DeferredLighting no longer reads
  fog, generation need only precede composition at 400; the published written handle establishes same-frame dependency.

## Surprises & Discoveries
- DeferredLighting uses stencil NotEqual 0: sky never executes its volumetric sampling at all.
- Volume accumulation alpha currently contains slice index instead of transmittance.
- No Editor initially running; selected existing Debug executable and AiluTestProj/empty.json.

## Validation
Baseline complete. Depth-stencil capture unsupported; captured float CopyDepthPass output instead. Source diff check
and final build-debug passed. Final 960x576 depth exactly matches baseline; all linear outputs finite. Frame indices and
engine frames contiguous. Stationary sky RMS 0.272, P99 1 (8-bit); unobstructed sky ROI in motion RMS/P99 0. Capture
load about 21.45 fps with 9 camera updates over the 3-second path; no normal-frame-rate or mouse-input claim.
Independent native HDR controls give sky T approximately 4.45e-5 and geometry T 0.672..0.961. Composition formula RMS
error 0.00123; controls are different stochastic frames and use R11G11B10 quantization. Full quality tuning is deferred.
Evidence directory: F:/AiluEngine/artifacts/out/editor/bin/x64/debug/VolumetricFog_20261001.

Three-dimensional DDS encoding failed; use composition shader controls for volume samples. Initial hot-reload control
attempt did not load modified source and is excluded. Verified controls restart Editor and record shader compile logs.

## Outcomes
Sky receives local fog scattering and attenuation after sky rendering; geometry uses its own depth. Native visual
evidence reviewed; aesthetic/user live acceptance, transparent material fog, texture-sky mode, and enabled temporal
reprojection remain unverified. Default density/light parameters preserved. Camera restored, no scene saved, only
task-started Editor instances closed. See evidence REPORT.md and accepted_comparison.png.
