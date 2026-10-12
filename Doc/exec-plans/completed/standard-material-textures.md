# Separate Standard Lit scalar textures

## Purpose
Expose independent roughness/metallic textures and multiply each red-channel sample by its scalar factor.
Remove Standard Lit legacy volume absorption; retain attenuation color/distance.

## Scope / Non-goals
Raster forward/deferred, shared Compute/DXR material evaluation, GI metallic, Inspector and Blender direct/baked export.
No change to cloud/fog absorption. No arbitrary Blender shader graph equivalence.

## Architecture constraints
Preserve Material/LiveLinkSession ownership, main-thread import and transient CPU/HLSL MaterialData agreement.
Inspector is shader-property driven; all texture setters must update independent sampler bits.
Preserve unrelated dirty changes. New/modified code follows 120 columns.

## Relevant files
Material.{h,cpp}; ShaderInterop.h; Renderer.cpp; SceneRayTracingProxy.cpp; standard_lit_common.hlsli;
forwardlit.hlsl; defered_standard_lit.hlsl; ray_trace/{path_tracer_material,gi_ray_lighting}.hlsli;
LiveLinkSession.cpp; Tools/Blender/ailu_live_link/material_{sync,bake}.py; AssetHandlers.cpp.

## Current milestone
Implementation, final Debug build and focused Blender/Editor numerical validation complete.

## Progress
- [x] Trace property, UI, texture import and raster/ray-tracing consumers.
- [x] Split texture slots and sampler bits; multiply independent R samples by coefficients.
- [x] Split direct/baked Blender export and plugin import; rebuild add-on ZIP.
- [x] Remove legacy absorption property, setter state and asset-load compatibility branch.
- [x] Focused Blender direct/baked export and GPU numerical validation.
- [x] Debug build, diff check and architecture update.

## Decision Log
Roughness mask 8, metallic mask 64; preserve existing specular/anisotropy bits.
MaterialData transient stride changes 92 -> 96 bytes with matching HLSL declaration and offset assertions.
Linked Blender scalar textures replace socket defaults: exporter sends factor 1 for each successful map.
Bundled old combined texture slots all have empty GUIDs and are migrated without scalar changes.
External materials with old packed maps must be re-exported/reassigned as separate scalar images.

## Surprises & Discoveries
Inspector's property-ID texture setter did not update sampler flags. Centralize flags there for all callers.
Setting specular previously discarded other sampler bits; preserve them so independent maps remain enabled.
Default build writes outside workspace at F:/AiluEngine; sandbox attempt failed, escalated final build passed.
A concurrent Editor caused incremental-link PDB failure; rerunning after test exit passed.
Existing Blender auto-reconnected before the test client; early test connection isolates the controlled fixture.
Direct and baked scalar exports initially shared filenames. GPU analysis caught the overwrite; use distinct direct
_scalar_ filenames and verify that native baking leaves direct maps unchanged.
Repository ExecPlan template path is absent; use the required sections directly.

## Validation
- `cmake --build --preset build-debug`: final PASS, including Engine, Editor and Live Link plugin.
- Native Blender 5.0.1 direct/baked smoke: PASS. Separate source R/G channels, independent paths, factor 1 for maps,
  scalar fallback after unlink, valid UV pixels, and direct maps unchanged by subsequent baking.
- Standalone DXC HLSL 2018: ForwardPSMain, GBufferPSMain, Compute PrimaryRay, GI RayGen and DXR library PASS.
  Existing manifold shadowing/precedence warnings remain outside this change.
- ZIP/source/installed material modules match byte for byte. Original installed modules backed up under
  C:/Users/BoomBac/.codex/visualizations/2026/10/11/01a12965-18e8-72b0-92df-5ee3cf4a3984/addon-backup.
- Editor ScalarMaterial_20261011_4: 8/8 scalar gates PASS, 24 finite frames, three consecutive engine frames per capture.
  Both/roughness-only/metallic-only/neither exercise multiplication and independent texture removal.
  Center 16x16 ROI lies on the white fixture cube; tolerance 2/255. Both maps measured roughness 0.1254902 for
  0.25 * 0.5 and metallic 0.2980392 for 0.75 * 0.4. Correct resources and cube preview inspected.
  Driver exit 0; 9/9 configuration/state files restored. Evidence beside selected Editor.exe:
  F:/AiluEngine/artifacts/out/editor/bin/x64/debug/ScalarMaterial_20261011_4/analysis/report.json.
- Startup logs contain texture ETextureMaxSize enum/deserialization errors outside this controlled material test;
  no claim of a globally clean Editor run. GPU assertions passed despite those startup asset diagnostics.
- Initial cold-start and occupied-client attempts did not count as acceptance. The filename-overwrite attempt failed
  its numerical gate, was repaired, then rerun using the same four-case acceptance.
- Source/JSON/Python syntax and targeted `git diff --check`: PASS. Existing dirty work preserved.

## Outcomes
Standard Lit and Blender Link use separate scalar maps with independent multiplier semantics; Inspector metadata
exposes two rows and property-ID assignment/removal maintains masks. Legacy surface absorption compatibility is gone;
attenuation color/distance remains. Installed addon modules and ZIP are updated; running Blender must restart/reload
its addon to discard old in-memory modules. Full user-scene visual equivalence, manual Inspector interaction and live
Compute/DXR image comparisons were not retested; shader compilation is distinct from those runtime acceptance levels.
