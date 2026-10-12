# Raster thin glass

## Purpose
Render BMW lamp covers and windows with background transmission and dielectric reflection in raster mode.

## Scope / Non-goals
First thin-surface model; straight screen-space transmission, independent coverage alpha and IOR reflection.
No volumetric refraction, sorted multilayer transmission or arbitrary Blender closure reconstruction.

## Architecture constraints
ForwardPass owns a per-view RenderGraph HDR snapshot, independent of TAA. Never sample the active color RTV.
Preserve ordinary transparent shading and the existing path-tracer material payload/coverage semantics.

## Relevant files
CommonPasses.cpp, forwardlit.hlsl, lighting.hlsli, brdf.hlsli, LiveLinkSession.cpp, rendering.md.

## Current milestone
Complete: M1 implementation and M2 BMW raster/control/regression acceptance.

## Progress
- [x] Confirm ownership, transparent queue, material import and existing BRDF/fog behavior.
- [x] M1 implementation, scene/preview shader compilation and build-debug.
- [x] M2 BMW raster captures, opaque control, AA-off/TAA checks and ordinary transparency regression.

## Decision Log
- Capture immediately before ForwardPass, including preceding Sprite/cloud composition, using explicit RG read/write.
- Transmission affects diffuse and background throughput; alpha remains geometric coverage.
- Extend existing BRDF parameters with unchanged defaults for ordinary surfaces; reflection uses IOR-derived F0.
- Fog the new surface contribution while retaining already fogged background transmission.
- Live Link classifies positive transmission as transparent even when texture/base alpha is one.

## Surprises & Discoveries
- BMW lamp Glass IOR=1 is imported with alpha_blend=false and opaque alpha; queue classification caused opaque covers.
- Repository TEMPLATE.md does not exist; required ExecPlan sections are maintained here directly.
- Car_1 was rejected as evidence: the user's running Blender auto-connected before the test exporter.
  Added an optional process-local Live Link port override (default remains 17777) to isolate the headless test.
- Car_2 revealed an incorrect extra cbuffer: DXC allocated b0, reserved for primitive draw constants, while
  the graphics root signature only supports named built-in cbuffers. Removed the extra cbuffer and use the
  snapshot dimensions (1x1 black fallback means unavailable). Snapshot already contained the correct lamp rings.
- Car_2 opaque FinalBlit PNG was black despite nonempty linear TransparentPass output; this capture fails acceptance.
- Car_3 also revealed a source-material issue: channel baking averaged Glass+Glossy into metalness~0.5 and
  tinted glass albedo. Preserve primary Glass/Refraction color, roughness and nonmetallic channels for Add Shader;
  coverage and emission retain existing composition. Native Glass+Glossy and ordinary bake fixtures pass.
- The ordinary bake fixture still referenced retired packed roughness_metallic_path; updated it to roughness_path
  without changing its independent numeric expectations.
- Car_4 uses final shaders and corrected primary-closure bakes. Reviewed lamp rings/interior and window seats in
  actual AA-off screenshot. AA-off/TAA captures completed; full numerical analysis awaits fresh opaque control.
- Changing all material surface queues inside the same Editor session produced black FinalBlit output although
  linear scene output remained nonempty. Fresh-session control isolates this unresolved output transition issue.
- Fresh opaque control also exposed this implementation's uninitialized optional
  RGHandle: no default member initialization exists. Value-initialize glass_color{} so no-glass frames bind black
  fallback without declaring an invalid RG dependency. Reject pre-fix no-glass captures and restart regression.
- The initialized-handle fix removed invalid reads but did not fix the final black image. Opaque_4 proved the
  same _CameraColoTexture was nonempty at CopyColor and black at FinalBlit input. ToneMapping/Bloom are disabled.
  Explicit Load in GizmoPass alone did not repair Car_6. Opaque_5's actual pass dump includes PickBuffer and
  EditorDebugPass: Gizmo input was black, Gizmo output had grid pixels, FinalBlit input was black again.
  All three overlay entry points now explicitly load existing color/depth; selection outline also loads color.
  Skip empty forward passes; compare controls using the common CopyColor pass. Car_7 passed after all overlay fixes.

## Validation
Source shader variants, native Blender glass/ordinary material bake smoke tests and build-debug pass.
Transparent_4 passed the final ordinary transparent/Sprite/TAA regression (784 contiguous frames, 3136 captures,
D3D12 debug layer enabled, ERROR_CORRUPTION=0, failed=0, restoration 8/8).
Car_6 passed 37/38 integrity/control gates: its opaque FinalBlit was black. Reject that run as final acceptance.
Fresh Car_7 passed 38/38 integrity/control gates across seven sequences (65 frames, 1536x832).
Visual review of the native final PNG comparison confirms lamp rings and interior seats visible with AA off and TAA.
Same-scene opaque controls remain opaque; settings/camera and eight configuration files are restored.
Build and shader/bake evidence remain separate from image and visual acceptance.

## Outcomes
First raster thin-glass version completed. See artifacts/raster_glass_20261011/result.md and comparison images.
Architecture docs updated; installed Blender material modules and distribution ZIP refreshed with backups.
Existing running Blender requires restart and Full Sync to reload material modules. No user process was closed.
Physical refraction, thickness, multilayer ordering, transparent shadows and full Blender equivalence remain outside scope.
