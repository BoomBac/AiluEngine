# Volumetric fog silhouette diagnosis and repair

## Purpose
Investigate the unexplained trapezoidal dark fog region shown by the user and repair its demonstrated cause.

## Scope / Non-goals
Single-factor shadow/injection/composition controls and before/after captures. Temporary scene allowed.
No unrelated rendering refactor or general fog quality redesign.

## Architecture constraints
Existing feature ownership and explicit RenderGraph resources. Preserve pre-existing dirty changes.
Use build-debug only. Restore runtime camera, scene and diagnostic shader controls.

## Relevant files
Compute/volumetric_light.hlsl, shadow.hlsli, volumetric_fog_composite.hlsl, VolumetricFog.cpp.

## Current milestone
Complete: M1 isolation, M2 minimal binding repair/Debug build, M3 capture analysis and restoration.

## Progress
- Source review: XY mapping agrees; injection jitter crosses slices; accumulated endpoint/texture-center mismatch.
- CSM hard sampling lacks Z-domain check; causal connection to user image remains unverified.
- No Editor initially running; launching selected Debug Editor with current project/config.
- M1: removed shadow injection, checked Z domain, disabled jitter/bias, and isolated every mesh in memory.
  Ground-only reproduces the left trapezoid; analytic ground occlusion does not. Camera-buffer binding has no effect.
- Root cause: default local compute Texture2DArray binding selected the first-layer SRV. CSM shader array indices
  therefore did not address the matching depth layer. Default bindings now use the full main SRV; explicit subresources
  retain single-layer views. Shader.cpp is the only permanent implementation change.
- M2: build-debug passed after the final binding change; diff check passed.
- M3: ground-only 8 frames, three full-scene stationary views with 16 frames each, 120 continuous moving frames,
  16 settled frames, native depth/HDR captures completed. All final capture/engine indices contiguous.

## Decision Log
- Establish controls before choosing the permanent fix.
- Rejected fixes: Z guard, zero bias/jitter, explicit camera buffer. Do not retain unrelated quality changes.

## Surprises & Discoveries
- Repository capture client and ExecPlan template are absent; reuse inspected artifact RPC protocol.
- First diagnostic Z replacement accidentally activated commented code; restored the initially clean shadow.hlsli
  and limited later replacements to the hard-shadow function. Failed startup outputs excluded.
- Cold dependency recompilation exceeded an initial 7-second startup delay; use bounded RPC readiness polling.

## Validation
Evidence: F:/AiluEngine/artifacts/out/editor/bin/x64/debug/FogSilhouette_20261001.
1280x704, NewScene, fog default density 1/distance 50m, temporal reprojection off. Ground ROI x=0..499/y=300..599:
RMS against analytic ground control 9.16/P99 22 before, 0/0 after (8-bit RGB). Ground depth exactly identical;
full-scene wide depth max difference 1.05e-9 device depth across process restarts (floating-point variation).
Final stationary ROI RMS 0.325..0.564/P99 1..2. Native HDR finite. Movement uses smooth yaw 275..295..275,
12 requested updates over about 7.1 seconds under capture load; no normal-frame-rate claim or registered motion metrics.
Comparison and motion filmstrip inspected. No dedicated unit test added; runtime captures exercise actual array SRVs.

## Outcomes
False ground-to-sky trapezoid eliminated in isolated and full-scene views. Default compute array SRV semantics documented
in rhi-dx12.md. All diagnostic shaders and in-memory mesh moves restored, camera restored, scene not saved;
three backed-up user config files hash-identical. Only task-started Editors closed. User live visual acceptance and
temporal reprojection enabled remain unverified. PIX capture was not replayed; validation uses fresh Editor captures.
