# Preserve Blender diffuse-only materials

## Purpose
Approximate imported pure Diffuse BSDF materials as roughness-one, nonmetallic Standard Lit materials.

## Scope / Non-goals
Map pure Diffuse/neutral closures to roughness=1, metallic=0 in direct and baked exports.
Preserve existing Glossy/Principled/glass behavior. No two-lobe wheel BRDF or true displacement geometry.

## Architecture constraints
Keep the existing Standard Lit shader and material packet schema; no new shader keyword, GBuffer field or ABI flag.
Keep user dirty changes and original Blender intact. Isolated runtime captures restore camera/settings/files.

## Relevant files
material_sync.py, material_bake.py, plugin-system.md, diffuse_roughness_smoke.py and BMW export/capture/analyzer scripts.

## Current milestone
Complete: user-selected diffuse-to-rough-Standard-Lit approximation.

## Progress
- [x] M1 direct/baked conversion repair; pure diffuse, transparent mix, Glossy and Principled smoke passed.
- [x] M2 build-debug; glass and ordinary native bake regression passed.
- [x] M3 BMW wheel A/B GBuffer/final captures; native material/glass conversion regressions passed.

## Decision Log
User explicitly selected the simpler roughness-one Standard Lit approximation.
Removed this task's initial dedicated diffuse branches/flags from import, GBuffer, forward and path BSDFs.
Retain closure classification only inside the exporter; ignore Diffuse roughness textures for the GGX approximation.
Standard Lit retains its broad dielectric highlight; this is an approximation, not exact pure Lambert shading.

## Surprises & Discoveries
Diffuse BSDF Roughness=0 is Lambert diffuse; Standard Lit roughness controls its GGX lobe.
TireRubber displacement texture is not transported; physical displacement remains outside this focused repair.

## Validation
Native Blender diffuse mapping, glass transport and material bake smoke passed. ZIP matches current source modules;
installed module copies verified with backups. Build-debug compiled and linked.
Wheel_1 captures a fixed GBuffer and final image, then changes only TireRubber roughness to zero for the old control.
Wheel_1: 24 captured frames, contiguous and finite, 21/21 integrity/parameter/restoration gates.
3667 tire pixels have actual GBuffer roughness=1 versus old control=0; RGB and central roundel are unchanged.
The first RGB-only mask included 77 BMWBlack roundel pixels with the same quantized dark color. Exclude the independent
central badge rectangle rather than filtering by expected roughness, and verify its complete GBuffer remains unchanged.
Viewed the local wheel comparison: broad rough tire shading and preserved rim appearance. Camera/settings/files restored.
Shader/ABI modifications from the interrupted approach were removed; no additional rendering regression is needed for
the final exporter-only change. Shared path material construction reads this same roughness property; no DXR capture
was performed in this task.

## Outcomes
Completed. See artifacts/diffuse_roughness_20261011/result.md. Installed material modules and packaged ZIP refreshed;
user's running Blender still needs module reload/restart and Full Sync for newly exported values.
