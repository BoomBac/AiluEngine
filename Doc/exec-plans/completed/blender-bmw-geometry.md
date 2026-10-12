# BMW Blender Live Link geometry correspondence

## Purpose
Restore BMW ground/body geometry and collection-instance layout from `D:/Downloads/bmw27/bmw27_cpu.blend`.

## Scope / Non-goals
UV origin, tangent handedness, evaluated mesh instances, viewport/camera visibility and one transform scale typo.
No renderer redesign, protocol version change, shader-lobe equivalence, independent ray visibility or shear transport.

## Architecture constraints
Blender evaluates geometry/materials on its main thread; Engine plugin applies existing protocol messages on its main thread.
Preserve source scene and unrelated dirty changes. Preserve Editor configuration/session bytes during runtime tests.

## Relevant files
- Tools/Blender/ailu_live_link/{mesh_sync,object_sync,dirty_tracker}.py
- Engine/Src/Framework/Math/Transform.cpp
- Doc/architecture/plugin-system.md
- Test/{BlenderMeshBulkSmoke,BlenderInstanceSyncSmoke}.py

## Current milestone
Completed.

## Progress
- [x] Reproduce native BMW export into Debug Editor with original material/mesh packets.
- [x] Convert UV V and tangent handedness; before/after shows complete floor and car shell.
- [x] Export 75 nested collection mesh instances with stable IDs and evaluated world transforms.
- [x] Incremental idle/move/source-edit/delete regression.
- [x] Respect viewport visibility and camera-invisible emitter helper.
- [x] Debug build and real DX12 capture, restore all nine backed-up files.
- [x] Final package/install audit; ZIP rebuilt, 13 installed modules match source, old installation backed up.

## Decision Log
- Preserve protocol v1: duplicate mesh payload under each instance's GUID/revision; no Engine mesh alias API.
- Snapshot depsgraph instance properties immediately; RNA iterator wrappers expire after advancing.
- Reuse source material identities for instances; exact per-instance world-coordinate baking remains unsupported.
- Camera visibility maps to entity visibility because separate primary-ray/shadow/GI masks are unavailable in this transport.

## Surprises & Discoveries
- Transform::ToMatrix incorrectly scaled m[0][1] by Z; fixed X. Native BMW comparison showed negligible error from this typo,
  so it was not the cause of the screenshot's large apparent deformation.
- Vertex positions were identical with/without baking. Missing V conversion sampled transparent atlas regions and clipped geometry.
- BMW contains 75 evaluated mesh instances beyond direct scene objects; original transport omitted them.
- The large white Light mesh is camera-invisible in Blender and previously occluded the second car in Editor.
- Run 01 is invalid: diagnostic material used the wrong GUID field and camera faced away. Run 02 validates uniform geometry only.

## Validation
- `cmake --build --preset build-debug`: PASS; workspace-external F-drive build output required sandbox escalation.
- Blender 5.0.1 `--background --factory-startup --python Test/BlenderMeshBulkSmoke.py`: PASS.
- Native `Test/BlenderInstanceSyncSmoke.py`: PASS; IDs, placement, idle, transform-only reuse, source revisions and deletion.
- Actual BMW export: 273 packets, 110 meshes including 75 instances, approximately 6.5 seconds; recorded production packets sent
  to real Debug Editor. This is not a live Blender UI timing or shader-lighting equivalence claim.
- Captures beside `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/Editor.exe`:
  `BmwGeometryValidation-20261010-03` reproduces broken floor/body;
  `-04` restores geometry after UV fix;
  `-05` adds wheels/instances but emitter occludes second body;
  `-06` shows both complete cars, wheels and continuous floor after visibility correction.
- Run 06: GBuffer/final/overview, three PNGs per sequence, 896x512. Manifests verify three consecutive engine_frame values in each sequence.
- All nine configuration/session backups restored after each run; original blend was never saved.

## Outcomes
BMW geometry correspondence is repaired. Lighting/material fidelity remains the existing Standard Lit approximation.

## Follow-up: front grille acceptance (2026-10-10)
- Earlier fixed-delay captures began 5 seconds after packet submission. The actual full import lasted 16.194 seconds in the diagnostic run; these captures can show unresolved parent references and incomplete mesh instances. They are insufficient for final geometry acceptance.
- Native Blender hierarchy reconstruction agrees with evaluated mesh vertices within 2.53e-7. During import, lightsOutline/kidneyBars/kidneyChrome temporarily attach to the link root; Full Sync End correctly attaches them to carShell.
- BmwFrontValidation-20261010-06 waits 30 seconds before inspection/capture. Engine poses confirm all three parents equal carShell; final close-up shows correctly aligned grille, lamps, intake, wheels and both cars. Three final PNG frames captured; all nine backed-up files restored.
- A flattened world-transform control also aligns the front, but is diagnostic only. No hierarchy workaround or blend edits were applied. Temporary world-pose inspection code removed.
- Future final acceptance must wait for Full Sync completion rather than use the previous 5-second delay.