# Path tracer Heitz 2018 VNDF

## Purpose
Switch GGX microfacet sampling from NDF to Heitz 2018 VNDF, retain a compile-time NDF control, and produce a controlled Editor image comparison.

## Scope / Non-goals
Shared Compute/DXR sampling and matching reflection/transmission/MIS PDFs. No random sequence, BRDF model, denoiser or unrelated rendering changes.

## Architecture constraints
Shared sampling.hlsli owns sampling/evaluation. Preserve glass sidedness and refraction Jacobians. Preserve dirty checkout and isolate scene/config changes.

## Relevant files
Engine/Res/Shaders/hlsl/ray_trace/sampling.hlsli; Test/TaaEditorAcceptanceClient.mjs; existing automation capture/settings services.

## Current milestone
Complete: implementation, build, macro compilation, controlled HDR image comparison and cleanup.

## Progress
- Source inspection: default NDF; dormant VNDF sampler exists but its opaque PDFs are incorrect; glass still uses NDF.
- Read rendering/render-graph architecture and capture workflow. Repository ExecPlan template is absent.
- No running Editor observed. Debug Editor exists at F:/AiluEngine/artifacts/out/editor/bin/x64/debug/Editor.exe.
- M1 complete: isolated project in VndfValidation_20261013 beside Debug Editor; five entities, HDR cave environment,
  roughness 0.55 silver/floor and 0.65 gold, 640x360, fixed camera, 256 contiguous raw NDF samples.
- Initial procedural environment produced black lighting; rejected that capture and used the verified HDR texture.
- M2 implementation complete: numeric default-on switch, corrected Heitz sampler and visible-normal PDFs shared by
  opaque reflection, rough glass reflection/transmission and analytic MIS. Original NDF expressions preserved at 0.
- build-debug completed successfully; script syntax and git diff whitespace checks passed.
- VNDF Editor is compiling changed Compute/DXR dependencies; fresh DXR pipeline creation is slow in Debug.
- M3 complete: each DXR arm produced 256 contiguous 640x360 RGBA32F raw frames. Matched engine frames
  770-993 give 224 paired initial seeds per arm. Renderer::_FrameIndex and capture engine_frame both use
  GraphicsContext::GetFrameCount(); InitSeed consumes that _FrameIndex.
- Four disjoint 16-spp trials were compared to a held-out pooled finite reference (320 estimates from
  160 common seed pairs). Variance uses all fourteen 16-spp blocks per arm.
- Floor variance decreased 39.0427%; finite-reference MSE decreased 36.5514%; mean RGB changed -0.02519%.
  Gold/silver variance decreased 3.5539%/5.2258%; diffuse changed +0.2721% (essentially unchanged).
- Compute VNDF smoke: 8 contiguous raw frames, all finite, nonblack, mean RGB 0.05858545.
- Final DXC compilation passed for both macro 0 and 1 in DXR lib_6_6 and Compute PrimaryRay cs_6_6.
  Standalone DXC needs SHADER_DXC=1, like the engine compiler; missing it initially selected incompatible
  existing color-space ternaries. No unrelated shader code was changed to handle that invocation issue.
- Final source check also enforces the visible-normal upper-hemisphere support for analytic glass PDFs.
  This does not affect the opaque comparison scene. Glass was compiled, but not visually validated.
- Closed only the test Editor. EditorConfig.json, dock_layout.json and main_widget.json restored from
  exact pre-run backups and verified byte-for-byte by hashes. Existing dirty user changes remain.

## Decision Log
Use a numeric SAMPLE_BRDF_VNDF_GGX switch with default 1 and override 0. Update all matching PDFs without changing BSDF values.
Use offline raw averaging for exact equal-spp comparisons. DXR seeds depend on global engine frame numbers, so
record frame windows and align their intersection. Initial windows overlapped by 224 frames, allowing identical
initial seeds without a production RNG change. Branching after initialization may consume random numbers differently.

## Surprises & Discoveries
Sandbox identity differs from checkout owner; some read-only checks require elevated execution. Existing user changes are widespread and must remain intact.

## Validation
M1: isolated scene baseline. M2: numerical sampler/PDF checks and build-debug. M3: equal-spp raw image A/B, high-spp control, comparison image, cleanup.
Completed source review, Debug build, four final macro/backend Shader compilations, capture transport,
sequence integrity and HDR numerical/PNG inspection. No GPU performance claim. No user live viewport acceptance.
Reference is finite and pooled across correlated arms; it is not analytic ground truth. Variance reduction is
the primary evidence, limited to this static rough-metal/HDR scene with MNEE off and environment NEE on.

## Outcomes
Artifacts: F:/AiluEngine/artifacts/out/editor/bin/x64/debug/VndfValidation_20261013.
comparison.png contains full 16/224-spp NDF/VNDF and pooled-reference views; floor_comparison.png shows
the 16-spp grazing-metal ROI at 2x with nearest-neighbor enlargement. Both use fixed exposure 1, Reinhard and sRGB.
analysis.json contains ROIs, frame ranges, variance/MSE and mean comparisons; raw DDS, manifests, source snapshots,
macro DXIL/logs, configuration backups and validation_editor.log remain beside Editor.exe.
Production default is VNDF. Edit/override SAMPLE_BRDF_VNDF_GGX to 0 for NDF, recompile shaders and reset accumulation.
