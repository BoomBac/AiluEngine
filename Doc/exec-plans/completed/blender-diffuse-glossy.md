# Diffuse + Glossy export conversion

## Purpose / Scope

Repair classroom woodFloor's near-mirror export: native source is 98% Diffuse and 2% Glossy with reflection
roughness 0.5623; the old baked output is roughness 0.01176 and metallic 0.01961.
Implement only the approved first step: lobe-specific roughness, nonmetallic conversion and diffuse pigment.
No independent reflection weight, shader, GBuffer or protocol change.

## Architecture constraints / Relevant files

Native Blender main-thread channel baking retains node group, image and Mix/Add evaluation.
material_sync.py classifies supported closure combinations; material_bake.py evaluates weighted channels.
Glass/Principled/standalone Glossy and pure Diffuse behavior must remain compatible.

## Current milestone / Progress

- [x] Confirm exact source and old native baked values; inspect current architecture and dirty work.
- [x] Implement filtered lobe sums and normalization for Diffuse/Glossy mixtures.
- [x] Native regressions and classroom before/after bake.
- [x] Debug build, package/install, diff check and outcome record.

## Decision Log

Diffuse pigment and reflection roughness use separate normalized lobe weights. Zero reflection weight falls
back to roughness 1; zero diffuse weight uses black diffuse pigment. No Glossy weight becomes metallic.
Mixtures containing other reflective closures retain their existing approximation.

## Validation / Outcomes

Use selected Debug Editor directory F:/AiluEngine/artifacts/out/editor/bin/x64/debug/ for unique diagnostic
artifacts; no Editor process is currently running. Native export is not live Engine visual acceptance.

- Native classroom bake exit 0: roughness texture is uniformly 0.56078434 (143/255), versus old 0.01176471.
  Authored Glossy roughness is 0.56234133. Metallic is uniformly 0, versus old 0.01960784. Normal statistics
  are unchanged. Source scene was opened only in a separate background process and was never saved.
- Test/BlenderDiffuseGlossySmoke.py exit 0: actual channel bake covers weights 0, 0.02, 0.5, 1, diffuse pigment,
  group shader inputs, nested reflective normalization, spatially varying image weights and Add Shader.
  Compatibility gates cover pure Diffuse, standalone Glossy, Principled and Glass + Glossy Add Shader.
- Existing BlenderScalarMaterialSmoke.py exit 0: independent roughness/metallic paths, channel extraction,
  linked factors, unlinked defaults and direct/baked cache separation remain valid.
- `cmake --build --preset build-debug` exit 0; Python modules compile and packaged modules match source.
- ZIP rebuilt and installed material_sync.py/material_bake.py match source; old modules are backed up in the
  unique Editor-side evidence directory's installed-backup/. User's active Blender was not terminated/reloaded.
- Relevant diff whitespace check passed. Existing unrelated dirty changes were retained.

## Outcomes

Approved exporter-only first step is complete. Node group and native texture evaluation preserve spatial weights;
closure-filtered sums normalize reflective roughness separately from diffuse pigment. No shader/ABI/GBuffer
changes or independent reflection-strength transport were introduced. Restart Blender and Full Sync to apply
the new export to a running session. Actual Engine GBuffer/final-image capture was not executed: no Editor was
running during this validation, and native export acceptance does not imply final visual equivalence.
