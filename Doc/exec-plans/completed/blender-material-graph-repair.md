# Blender material graph repair

## Purpose and scope

Repair missing surface materials, shader-layer masks, color/scalar adjustments, mapping/UV coordinates, complex
normals, continuous opacity, culling and dependency invalidation. Keep the direct path for simple materials.
World lighting, volumetric transport and view-dependent parallax cannot be represented by Standard Lit textures;
report those boundaries explicitly instead of treating them as complete shader equivalence.

## Architecture constraints and relevant files

Blender owns node evaluation and baking. Per-object baked material GUIDs must match object material slots; mesh UVs
must match the bake atlas. Temporary scenes, meshes and images must be released without editing the source scene.
Host uses existing Standard Lit surface/cull APIs. Preserve unrelated dirty changes.

Relevant files: Tools/Blender/ailu_live_link/{material_sync,material_bake,mesh_sync,object_sync,dirty_tracker}.py,
Plugins/BlenderLiveLink/Src/LiveLinkSession.cpp, Tools/Blender/tests/.

## Current milestone and progress

- [x] M1: Native Blender graph baking, dependency propagation and surface-mode transport.
- [x] M2: Blender regression tests and actual Hidden Alley material export.
- [x] M3: Debug build, package and bounded Editor evidence.

## Decision log

- Use isolated CPU Cycles emission-channel bakes and tangent-normal bakes for complex graphs, rather than writing
  another incomplete procedural evaluator. Flatten group socket routing while preserving upstream native nodes.
- Bake an object-specific UV atlas and transmit the same atlas; shared source material does not imply shared
  Generated/Object/world coordinates.
- Reuse unmodified UV-only mesh instances with identical material slots and scale; world-dependent graphs and
  modifiers keep object identities. Ignore transparent pigment when normalizing surface-layer weights.
- Native PNG emission bakes preserve HDR strength via an explicit opt-in shader factor. Continuous opacity opts
  into linear Alpha; legacy assets retain existing keyword defaults.

## Surprises and discoveries

- Prior foliage changes only selected one Principled layer; source export was verified but GPU acceptance failed.
- Repository TEMPLATE.md does not exist; this plan records the required sections directly.
- The all-material export found empty tree material slots; slot GUID lookup now guards null material entries.
- Bake target image alpha is unsuitable as a geometric footprint; an explicit coverage bake excludes atlas gaps
  from opacity classification.
- First GPU selector ToneMapping did not execute in the empty test project. FinalBlit captured black for all
  controls; the actual TransparentPass color output is the bounded surface-composition acceptance resource.
- Empty texture paths previously retained stale sampler bindings. The host now explicitly removes them.

## Validation

Blender 5.0.1 direct-export smoke and native bake regressions pass. Coverage includes weighted shader colors,
Math/MapRange/Clamp masks, native Mapping/checker and procedural Bump, transparent pigment preservation,
group-instance defaults, HDR emission factor, culling, atlas consistency, instance sharing and nested image/group
dependencies. Temporary scene/object/image/material counts and original scene/view layer are restored.

Actual Hidden Alley: all 122 mesh-slot materials exported, zero exceptions and missing texture paths; source
counts restored to 2 scenes / 1416 objects / 125 materials / 392 images. Latest all-graph export evidence is
Saved/BlenderTextureValidation/hidden-alley-all-graphs-release.{json,log}.

Debug build passes. Controlled GPU run 03 captured eight sequences of three 1280x704 frames, each with contiguous
engine_frame values and finite pixels. Gates pass for red/blue material switching, clipping, transparent queue,
continuous composition and all nine configuration/session restorations. Cutout coverage ratio 0.74368;
blend/opaque output mean ratio 0.83824 (two-sided cube compositing, not a direct scalar-alpha measurement).
Evidence: F:/AiluEngine/artifacts/out/editor/bin/x64/debug/BlenderMaterialBakeValidation-20261009-03/analysis.json.
Final run 04 passes all six gates, including texture unlink: ten sequences / 30 frames at 1280x704, three
consecutive engine_frame values per sequence, finite pixels and all nine restored files. Cutout ratio 0.74368;
blend/opaque mean ratio 0.83824. Evidence is beside the exercised Debug executable at
F:/AiluEngine/artifacts/out/editor/bin/x64/debug/BlenderMaterialBakeValidation-20261009-04/analysis.json.
Full Hidden Alley Editor visual/performance acceptance remains open.

## Outcomes

Surface coverage gaps repaired with native baking and explicit Standard Lit transport. View-dependent shader
equivalence and volume/world transfer remain documented limitations. First complex sync is more expensive;
default resolution is 1024 with per-material quality/opacity overrides. No source blend was saved or user Editor
process restarted. Add-on ZIP rebuilt; all 13 packaged Python modules match source, with no bytecode included.
Final targeted diff check passes. Implementation and bounded acceptance complete; source scene-wide export and
controlled GPU evidence do not claim full Hidden Alley visual equivalence or volume/view-dependent transfer.
