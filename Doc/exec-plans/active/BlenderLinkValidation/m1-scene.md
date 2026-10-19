# M1 — Independent Scene Correctness

## Purpose
Prove Blender source structures are actually imported correctly into Engine, not merely transmitted successfully.

## Scope
Independent read-only bpy evaluated-depsgraph reference, source-ID↔GUID mapping including nested collection instances, paginated Engine `scene.query_entities`/`scene.inspect_entity` capture, comparator and per-property failures for local/world transforms, hierarchy/visibility, UV/mesh slots/material source GUID/light/camera where inspectable.

## Non-goals
No full Blender Cycles pixel parity, no promise that sheared world transforms have a lossless TRS decomposition, no large new RPC absent evidence.

## Architecture Constraints
Compute independent coordinate/camera reference rather than production conversion helpers for properties under test. Blender source must remain unmodified on disk. Account for direct objects + evaluated instances; check parents after End; no false PASS on unreadable fields.

## Baseline
`Test/BlenderInstanceSyncSmoke.py`, `Test/BlenderCameraReference.py`, `Test/BlenderCameraSmoke.cpp` and `AutomationSceneService.cpp`.

## Target Outcome
`blender_reference.json`, `engine_snapshot.json`, `assertions.json`, `failures.json` for deterministic fixtures with L2 PASS/FAIL and precise source/engine GUID mapping.

## Relevant Files
`Tools/Blender/ailu_live_link/{identity,coordinate,object_sync,mesh_sync,material_sync,camera_sync,light_sync}.py`, `Editor/Src/Automation/AutomationSceneService.cpp`.

## Milestones
M1 gated by accepted M0; M2 gated by accepted M1.

## Current Milestone
M1 only after M0.

## Progress
Accepted on 2026-10-16. The M1 suite now generates isolated hierarchy/mesh, nested-instance, lighting, and camera fixtures, writes an independent bpy reference, captures the Engine scene over read-only automation RPC, compares supported fields and verifies seeded comparator corruptions.

## Decision Log
Use controlled fixture tolerance: local/world matrix and position abs <= 1e-4, UV abs <= 1e-5, camera FOV abs <= 0.05 degrees, and local quaternion angular error <= 1e-4 radians rather than raw coefficient equality. World correctness compares the complete matrix so parented nonuniform scale and rotation retain shear. Pixel-projection parity is explicitly unsupported because the current scene RPC does not expose the imported camera projection matrix.

Use `scene.inspect_entities` for one bounded read-only snapshot (up to 256 entity GUIDs) and opt-in bounded CPU mesh data. Mesh inspection is capped at 4096 vertices, 65536 indices, and 32 submeshes/material slots. Compare winding-preserving triangle multisets because Engine BVH construction reorders triangles within each submesh.

## Surprises & Discoveries
`scene.inspect_entity` returns readable properties but may not expose full vertex/texture internals. Mark absent oracle INCONCLUSIVE; only add read-only diagnostics where coverage matters.

## Validation
Fixtures: parent/child transform including negative/nonuniform scale and rotation; cube UV/winding/tangent; two material slots with source GUIDs; a hidden mesh; nested collection instances; perspective/orthographic camera and point/spot/area/directional light. Inject copy-only wrong parent, swapped axis, absent instance, material name and source GUID mismatch, world matrix, visibility, camera type, and orthographic size corruptions: each must fail with exact field/expected/actual. Numeric material values are independently compared on the unmodified snapshot. Verify live Engine GUIDs and no unexpected extras in the linked subset.

M1 expanded suite accepted by native hidden Editor/Blender runs recorded in `progress.md`: hierarchy/mesh `20261016-141920-d5b9d8b8`; nested instances `20261016-141955-9db62def`; lights `20261016-142032-6a90fa79`; cameras `20261016-142109-ce923d18`. The camera run with orthographic-size corruption injection was repeated as `20261016-142212-e43dd3de`. Each run emitted `blender_reference.json`, `engine_snapshot.json`, `assertions.json`, and `failures.json`; all baseline comparisons and applicable corruption injections passed with `unsupported_fields=[]`.

## Acceptance Criteria
Each required fixture passes L2, each seeded comparator corruption fails, all source hashes unchanged, and restoration proof is present. Any fields outside the supported M1 oracle remain explicitly tracked as unsupported.

## Known Follow-ups
Bulk read-only snapshot endpoint if RPC rate actually bottlenecks high-object scenes.

## Outcomes
M1 accepted. The independent source-to-Engine oracle covers stable source GUID mapping, parent links, local transforms, full world matrices, visibility, mesh positions/normals/tangents/UVs/submesh triangles/material slots/Blender source GUIDs and values, all supported light parameters, and camera projection type/FOV/orthographic size/clips. No M2 rendering assertions were added.

## Resume Protocol
Only after M1 acceptance; update progress and pass/fail evidence before M2.
