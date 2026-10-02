# GBuffer material upload repair — 2026-10-04

## Purpose and scope

Repair the reported incorrect GBuffer normals and BaseColor in AiluTestProj/NewScene. Preserve existing TAA,
volumetric fog, skinning, material and Editor changes. Do not change asset files, scene contents or feature scheduling.

## Diagnosis and decision

The Cornell white/red/green assets store `_MaterialID = UINT32_MAX`. Standard Lit maps all non-checker authoring IDs
to GPU ID 0 in its property block. The recent variant-aware `GetPropertyBlockForFrame` reconstruction then copied
the raw `_common_uint_property` value over that mapped ID. `GBufferPSMain` previously handled only IDs 0, 1 and 2;
the invalid ID skipped SurfaceData initialization and wrote undefined normal/color data.

After copying canonical properties into the actual variant layout, explicitly write Standard Lit's mapped GPU
`_MaterialID` and current `_sampler_mask`. Preserve authoring/serialized values. Unknown GBuffer shader IDs now use
the default surface initializer; checker ID 2 keeps its existing behavior.

Relevant files: `Engine/Src/Render/Material.cpp`, `Engine/Res/Shaders/hlsl/defered_standard_lit.hlsl` and
`Doc/architecture/rendering.md`. Automation is in `Test/GBufferMaterialEditorAcceptance.mjs`,
`Test/RunGBufferMaterialEditorAcceptance.ps1` and `Test/AnalyzeGBufferMaterialAcceptance.py`.

## Validation and outcome

- `cmake --build --preset build-debug`: PASS, including Engine and Editor compilation/linking.
- Targeted `git diff --check` and JavaScript syntax check: PASS.
- Actual Editor: `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/Editor.exe`.
- Project: `C:/Users/BoomBac/OneDrive/AiluTestProj/empty.json`; scene: NewScene, 19 entities.
- Evidence root: `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/GBufferMaterial_20261004`.
- Production multi-thread rendering and graphics jobs remained enabled in the accepted runs. No feature toggles;
  SSAO, VolumetricFog, DDGI and SurfelGI remained enabled, RayTraceGI disabled.
- Fixed SceneView position approximately `(11.6992, 4.53, -9.564)`, pitch 0.306, yaw 1768.02905, horizontal FOV 60.
- Raw `DeferedGeometryPass` outputs `_GBuffer0` (RG16F) and `_GBuffer1` (RGBA8), plus DeferredLightingPass output.
  Final run also captures ToneMapping PNGs. Resolution 1088 x 576, camera ordinal 0, AA modes None and TAA.
- Accepted runs: `before_native` (48 frames), `after_upload` (48), `after_final` (64). All 20 sequences contain
  8 frames with capture indices 0–7 and consecutive engine-frame numbers. Total 160 frames, 144 DDS and 16 PNG.
- Single-factor proof: `after_upload` restores the original shader branch and applies only the material upload fix.
  `after_final` additionally enables the defensive shader default branch.
- Interior ROIs `(x0,y0,x1,y1)`: floor `(70,470,200,535)`, front `(275,345,305,410)`, side `(395,345,450,405)`.
  Baseline colors were respectively green, black and blue; all three decoded normals were `(1,0,0)`.
  Both repaired runs yield white floor, authored gray front (0.8 linear RGB), authored green side, and normals near
  `(0,1,0)`, `(0,0,-1)`, `(1,0,0)`. Color max error is 0; normal component error stays below 0.003. All ROI adjacent
  RMS values are 0 for both AA modes. Whole-frame jitter edge differences are excluded from the flat-interior oracle.
- Camera controller floating-point drift is below 2e-6 world units; comparison tolerance is 1e-5. Projection/FOV and
  dimensions match. Scene revisions/entity count and all 9 backed-up files are restored for each accepted run.
- D3D12 ERROR/CORRUPTION severity counts are 0 in all accepted runs. Existing warning-severity messages remain;
  these are not classified as an error-free startup.
- Initial `before` run is excluded: inherited wrapper disabled render threading and returned a null child exit code.
  The dedicated wrapper preserves EngineConfig and invokes Node directly; `before_native` reruns the baseline.
- Visual inspection of decoded GBuffer and final ToneMapping images confirms the reported color/normal defect is gone.
  This proves the captured static SceneView path; manual Editor interaction and moving-camera acceptance were not tested.

## Reproduction

Run `Test/RunGBufferMaterialEditorAcceptance.ps1` with an Editor directory, project JSON, and fresh output directory
beside Editor.exe. It backs up affected Editor/session/log files and restores them in `finally`; the driver restores
the runtime camera without saving scene changes. The analyzer expects the documented NewScene pose, resolution and
run directory names. It is a scoped regression fixture, not a generic scene correctness test.

Final status: completed for the requested GBuffer defect and automated before/after validation.
