# Blender sync performance regression

## Purpose / scope

Restore practical large-scene import time and bounded temporary memory after native graph baking caused hour-long
Full Sync and memory exhaustion. Preserve direct image textures/alpha and retain explicit native bake support.

## Architecture constraints / relevant files

Blender main thread owns transport, graph evaluation and temporary datablocks. Reuse material identities on the
existing direct PBR path; complex bakes must not accumulate scene-wide meshes or all channel/slot image targets.
Tools/Blender/ailu_live_link/{material_bake,material_sync,mesh_sync,object_sync}.py and native regression fixtures.

## Current milestone / progress

- [x] Diagnose unconditional object-wide bake policy, 1,620 identities vs 122 source materials, retained channels
  and scene-wide atlas cache. Previous small/graph-only tests did not establish large-scene time or peak memory.
- [x] Restore direct image export where supported; preserve native fallback/explicit quality override.
- [x] Bound bake/cache lifetime and run representative/native full-scene time and memory validation.
- [x] Build/package/install and document measured scope.

## Decision log / discoveries

Direct image extraction already handles nested groups and primary image branches with explicit approximation
diagnostics. Baking every linked coordinate/group node changes cost from source materials to object instances.
Geometry-sensitive full baking remains an explicit quality choice; supported image branches use shared transport.

AUTO keeps original UVs and reported primary-image/color/mapping approximations. Unresolved graphs use native
256px channel bakes with source UVs; overlapping islands can approximate spatial variation. Explicit BAKE retains
unique atlas/native graph evaluation and a 1024px default; large-mesh atlas generation remains expensive.

Full Sync streams materials immediately before each mesh, after creating object identities. Atlas meshes have
a four-entry LRU; per-channel slot targets are removed before the next pass. Newly decoded clean file image
buffers are released, while preloaded/dirty/generated user images retain ownership and content.

Million-face tree/grass/ivy meshes exposed two independent geometry costs: Edit Mode atlas packing and Python
float/tuple expansion. AUTO source UV reuse removes packing; typed bulk reads remove Python per-corner objects.
An initial real-TCP run still overflowed the 256MiB send limit because a single expanded mesh was too large.
One vertex per mesh loop now retains split attributes while triangle indices reuse those vertices. Protocol
v1 layout and the send limit are unchanged.

## Validation / outcomes

Default native full-scene test uses the original Hidden Alley file, a Blender UI timer, actual Cycles baking,
actual binary mesh encoding and a separate-process TCP peer. No 16px override or graph-only substitution.

- Final full-scene real-TCP PASS: 206,473.67ms generation (3m26s), 2,967 packets / 784,883,856 bytes received,
  1,412 objects, 1,363 meshes, 182 materials, seven lights and one camera. Material phase 186,343.57ms;
  geometry/other 19,576.03ms. All source datablock counts and source scene/view-layer restored.
- Blender OS PeakWorkingSet64: 6,293,446,656 bytes (5.86GiB). Monitoring started late, but this OS counter
  includes the process lifetime peak. Private-memory samples cover only the final monitoring interval and
  are not a whole-run private-memory peak. No memory guard termination in the final run.
- Earlier run 01 was stopped by a 12GiB guard; run 02 was interrupted for repairs. Run 03 passed export
  without a real peer in 201.8s; run 04 failed transport overflow. Only run 05 is final transport acceptance.
- Direct graph, native bake channels, indexed mesh attributes/UV/tangents, source image ownership and bounded
  channel/cache lifetime smoke tests PASS. Installed TCP and connected native timer bake PASS; no Python workers.
- Debug build PASS. Package/install PASS, 13 Python files verified; prior add-on/preferences backed up to
  Saved/BlenderAddonInstallation/20261009-232226. Restart the user's existing Blender instance to load the update.

Evidence: Saved/BlenderTextureValidation/performance-full-scene-05.{json,log},
performance-full-scene-05-memory.json, performance-{direct,bake,mesh,lifetime}-final.log,
performance-installed-{transport.log,bake.json}, performance-build-final.log and
Saved/BlenderAddonInstallation/performance-install.log.

These results establish Blender-side export/transport behavior, not actual Editor import time, GPU memory,
visual equivalence or deterministic reproduction of the earlier native crash. Fresh Engine-side full-scene
loading/visual acceptance remains separate. The original .blend was not saved or modified.
