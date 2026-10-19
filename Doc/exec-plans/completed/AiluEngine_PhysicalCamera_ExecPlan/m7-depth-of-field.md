# M7 — Physical Depth of Field & Bokeh

## Purpose
Use the existing camera focal length, aperture and focus distance to produce a camera-specific depth-of-field effect with an aperture-shaped bokeh filter.

## Scope
- Add an opt-in Depth of Field setting to Postprocess Volume. It defaults off for existing scenes.
- Derive signed circle-of-confusion diameter from thin-lens geometry and the active camera's sensor height, focal length, aperture and focus distance.
- Add a bounded, depth-aware polygonal gather filter to the RenderGraph PostprocessStack before Bloom and ToneMapping.
- Expose enabled, maximum CoC diameter in output pixels and bokeh blade count through the Postprocess Volume Inspector and component automation.
- Preserve the HDR/pre-exposed color domain, read the current view depth, and support TAA plus spatial/SR upscaled color dimensions.
- Verify the Debug build, shader compilation/runtime graph when available, and image behavior. The user performs final live visual acceptance.

## Non-goals
- Motion blur, lens distortion/aberration, chromatic aberration, anamorphic aperture, autofocus, and physical sensor simulation.
- Temporal accumulation and exact reconstruction of surfaces hidden behind foreground geometry.
- Changes to exposure, TAA, upscaler, or legacy rendering behavior when DOF is disabled.

## Architecture Constraints
- Camera lens data remains per-camera; enable/quality settings remain Postprocess Volume data.
- The effect is a RenderGraph pass owned by the existing postprocess stack; Engine has no Editor dependency.
- Declare color and depth reads and output writes explicitly. Preserve the source's dimensions and color domain.
- Use the active camera's near/far and physical lens settings. Do not serialize derived EV/CoC or runtime resources.
- Default-off settings preserve existing assets and scenes.

## Relevant Files
- `Engine/Inc/Render/PostprocessSettings.h`
- `Engine/Inc/Render/Features/DepthOfFieldPass.h`
- `Engine/Src/Render/Features/DepthOfFieldPass.cpp`
- `Engine/Src/Render/Features/PostprocessStack.cpp`
- `Engine/Res/Shaders/hlsl/PostProcess/depth_of_field.hlsl`
- `Editor/Src/Inspector/ComponentEditors/PostprocessVolumeComponentEditor.cpp`
- `Editor/Src/Inspector/ComponentInspectorFields.cpp`
- `Editor/Src/Automation/AutomationReadModel.cpp`

## Progress
- 2026-10-18 follow-up: user requested layered Bokeh DOF and authorized an isolated background Editor fixture/captures.
  Current milestone: replace the single gather with half-resolution signed CoC prefilter, separable near-support
  dilation, independent far/near gathers and full-resolution coverage composition. Preserve lens/settings contracts.
  Implementation and controlled capture validation complete; user-scene live acceptance remains separate.
- [x] Define reflected default-off settings and expose controls.
- [x] Implement and register the depth-aware physical CoC/bokeh pass.
- [x] Debug build passed; source review completed. Git diff inspection was blocked by inaccessible loose objects.
- [ ] User live visual acceptance.

## Decision Log
- The 2026-10-18 quality revision supersedes the initial single-gather choice below: six transient graph passes,
  half-resolution signed CoC prefilter/support dilation/near-far gather and full-resolution coverage composition.
  Each gather uses 256 deterministic polygon samples with layer-conditioned color reconstruction. Background
  behind near edges is approximated from visible non-near neighbors; fully hidden surfaces remain unavailable.
- Use one full-resolution bounded gather for the first implementation; avoid persistent histories and additional cross-frame state.
- Interpret maximum CoC as a diameter in output pixels and cap the blur radius to half that value.
- Use a regular polygon aperture with 3–9 blades; the camera's aperture number controls physical CoC size.

## Surprises & Discoveries
- `CameraDepthTex` is sampled as device depth by TAA/reprojection paths, so the DOF shader converts it through
  `LinearEyeDepth` using the active camera near/far planes before calculating the physical CoC.
- DXC reported a parse error at `InsideAperture(float2 point)`: `point` is an HLSL modifier keyword. Renamed the
  parameter to `sample_point`; the subsequent expression diagnostics were parser cascades from that declaration.
- The SceneView camera defaults to Legacy exposure while still serving as the active rendering camera; focus distance
  and aperture therefore need to remain editable in Legacy mode for DOF.
- Live Editor feedback showed directional streaking on strongly defocused floor detail with a 40 px CoC cap. The
  original 15 fixed offsets were too sparse at that blur radius, so the gather now uses a 48-point Vogel disk and
  bilinear color sampling to avoid the visibly separated spokes.

## Validation
- Default-off scene path must remain unchanged.
- `cmake --build --preset build-debug` must compile and link Engine and Editor.
- Shader/runtime RenderGraph/image acceptance is reported separately from build validation.

## Outcomes
- 2026-10-18 layered Bokeh follow-up: Debug build and six-pass runtime shader execution passed. Isolated Editor
  fixture at 704x448 produced 35 final frames, including native HDR prefilter/near/far captures; all multi-frame
  manifests were contiguous. Zero cap and f/128 matched disabled DOF exactly, and the focused checker interior
  had zero RGB RMS difference. Foreground transition width increased from 5 to 20 px; isolated bokeh interior
  variation fell from 0.570 to 0.0147. Three/six/nine blades and focus changes were captured and inspected.
  TAA eight-frame worst adjacent RMS/P99 improved from 6.705/36 to 3.018/12 but fluctuation remains; no motion
  stability or GPU performance claim is made. No D3D12 errors; common startup/PSO warnings remain in both runs.
  All ten shared files were restored byte-for-byte and owned Editors closed without saving the fixture scene.
  Evidence/report: `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/dof_bokeh/20261018-094203/report.md`;
  old-gather image control: sibling session `20261018-094125`. Source hashes, scripts, manifests and comparison
  images are retained there. DLSS/FSR, GPU profiling, motion and user-scene live acceptance remain unverified.
- The following entries describe the original implementation and its earlier validation history.
- Added default-off reflected/serialized volume settings for DOF, maximum CoC diameter, and aperture blade count.
- Added a full-resolution polygonal depth-aware gather before Bloom/ToneMapping, using the current post-upscale/TAA
  HDR input and camera device depth. It uses a 48-point Vogel disk, bilinear color samples, and rejects samples from
  substantially nearer geometry to reduce background edge bleeding. Lens focal length uses physical focal length in
  Physical Focal Length mode and derives from vertical FOV otherwise.
- Exposed controls in the Postprocess Volume Inspector and component automation; made focus distance and aperture
  available in the Camera Inspector and SceneView camera panel for Legacy exposure too.
- `cmake --build --preset build-debug` passed and linked Engine and Editor. Runtime HLSL compilation, RenderGraph
  execution/capture, image analysis, and visual acceptance remain for the user. The shader-only sampling revision
  has not yet been compiled by DXC or checked in the live Editor.
- Follow-up crash trace reached `Material::SetInt`, whose pass loop dereferenced `_p_active_shader` without checking
  null. DOF shader loading now follows the standard `.alasset` path through `LoadAssetRef<Shader>` and creates the
  material only after the asset is available and compiles successfully. If loading or compilation fails, DOF is
  skipped. `Material::SetInt` itself remains unchanged.
- `git status` / `git diff --check` could not run because the checkout's Git object database has inaccessible loose
  objects and Git config ignore-file permissions; no Git cleanliness claim is made.
