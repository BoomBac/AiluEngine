# M1 — Independent Scene Correctness

## Purpose
Prove Blender source structures are actually imported correctly into Engine, not merely transmitted successfully.

## Scope
Independent read-only bpy evaluated-depsgraph reference, source-ID↔GUID mapping including nested collection instances, paginated Engine `scene.query_entities`/`scene.inspect_entity` capture, comparator and per-property failures for transforms/hierarchy/UV/mesh slots/material/light/camera where inspectable.

## Non-goals
No full Blender Cycles pixel parity, no unsupported shear conversion promise, no large new RPC absent evidence.

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
Pending.

## Decision Log
Use controlled fixture tolerance: position abs <= 1e-4, UV abs <= 1e-5, camera projection fixture <= 0.5 px (calibrate), and quaternion angular comparison, not raw coefficient equality.

## Surprises & Discoveries
`scene.inspect_entity` returns readable properties but may not expose full vertex/texture internals. Mark absent oracle INCONCLUSIVE; only add read-only diagnostics where coverage matters.

## Validation
Fixtures: parent/child transform including negative scale; cube UV/winding/tangent; two material slots; nested collection instances; perspective/orthographic camera and point/spot/area/directional light. Inject copy-only wrong parent, swapped axis, absent instance, material mismatch: each must fail with exact field/expected/actual. Verify live Engine GUIDs and no unexpected extras in the linked subset.

## Acceptance Criteria
Each required fixture passes L2, each seeded comparator corruption fails, all source hashes unchanged, restoration proof present and every unsupported field explicitly tracked.

## Known Follow-ups
Bulk read-only snapshot endpoint if RPC rate actually bottlenecks high-object scenes.

## Outcomes
Pending.

## Resume Protocol
Only after M0 acceptance; update progress and pass/fail evidence before M2.
