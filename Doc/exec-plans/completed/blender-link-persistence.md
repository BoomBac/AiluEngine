# Blender Link persistence and reconnect

## Purpose
Save linked scenes/resources for offline reopening, then reconnect to the same Blender source and resume updates.

## Scope / Non-goals
Persistent source identities, native mesh assets, project textures/materials and GUID snapshot reconciliation.
Blender owns imported fields. Engine overrides, offline delta replay and hash negotiation are deferred.

## Architecture constraints
Main-thread command application; ResourceMgr owns assets, handlers own persistence. No Editor dependency or plugin serialized type.
Preserve existing dirty changes. Optional scene metadata defaults to unlinked for old documents.

## Relevant files
LiveLinkSession, Component.h, AssetDocument.h, EntitySerializer.cpp, AssetHandlers.cpp, Blender addon/dirty tracker.

## Current milestone
M1 complete: persistence/reconciliation implemented and non-visual validation passed. Editor/Blender acceptance is assigned to the user.

## Progress
- Inspected scene/resource ownership, mesh artifact APIs and reconnect.
- Doc/exec-plans/TEMPLATE.md is absent; maintain required sections here.

## Decision Log
- Procedural meshes use existing versioned MeshArtifact as durable sidecars, independent of DDC.
- Source scene/object GUID metadata is optional and stored with entity tags.
- Reconnect reconciles a full snapshot, then resumes dirty updates.

## Surprises & Discoveries
Current entities are transient. Mesh handler needs an external source to reload procedural meshes.

## Validation
Focused smoke checks and build-debug; user performs Editor/Blender acceptance.

## Outcomes
Implementation complete; runtime/visual acceptance is intentionally left to the user.

## M1 implementation update
- Source metadata roundtrips through generated reflection; native mesh handler and copied texture/material assets implemented.
- Begin rebuilds persisted source maps; End reparents before pruning; failures preserve unseen entities.
- Python smoke passed: syntax, duplicate GUIDs, deletes, reconnect, scene switch and file reload.
- First Debug build failed in AssetHandlers.cpp: assignment through const AssetSaveContext. Fixed by storing the source path in the document.
- C++ smoke initially lacked FBX/PIX DLL search path and logging initialization; the debugger traced the exception to a Mesh::SetUVs warning using an uninitialized LogMgr. Harness setup corrected.
- Full snapshot payloads remain on reconnect; loaded equal mesh geometry is reused. Old mesh versions remain as assets.

## Final validation (2026-10-12)
- cmake --build --preset build-debug -- /m:1: PASS (compile/link, including generated reflection, plugin, Editor and smoke targets).
- python -B Test/BlenderPersistenceSmoke.py: PASS (syntax, duplicate GUID repair, delete membership, reconnect, scene switch, file reload, manual snapshot deduplication).
- BlenderPersistenceSmoke.exe: PASS (source identity JSON roundtrip, legacy defaults, native mesh binary disk roundtrip and truncated-file rejection).
- BlenderMeshCacheSmoke.exe: PASS (identity, byte-collision rejection, slot mapping, weak lifetime and capacity).
- C++ smoke uses Editor debug DLL directory in process PATH for existing FBX/PIX runtime dependencies.
- Plugin ZIP rebuilt and every packaged Python file compared byte-for-byte with current source: PASS.
- Editor/GPU/visual acceptance: not run, per user's explicit self-acceptance instruction.

## User acceptance
1. Install Tools/Blender/dist/ailu_live_link.zip; run the Debug Editor with a project.
2. Connect and wait for Full Sync to finish. Save the .blend file so generated source UUIDs survive, then save the engine scene.
3. Close Blender/Editor. Reopen the engine scene alone: hierarchy, geometry, materials, textures, lights and cameras should remain available.
4. Reopen the same .blend and reconnect: existing objects should be reused; unchanged loaded meshes should not be rebuilt.
5. Check transform/mesh/material edits, duplication, new objects and deletion after reconnect.
6. While disconnected delete a parent and retain/reparent its child, then reconnect: the retained child must survive snapshot pruning.
7. Check two objects which shared geometry, then edit one: after saving/reopening both must retain their respective geometry.
8. Switch Blender source scenes or reload .blend: a new snapshot should precede deltas; source roots remain separate.
9. When switching the active engine scene while connected, issue Full Sync explicitly to establish its baseline.

## Remaining limits
Blender owns imported fields; no engine override tracking. Reconnect transmits full geometry/material payloads; no network hash negotiation.
Changed geometry produces new mesh assets and old versions are retained. No automatic orphan-asset deletion. Failed snapshots avoid pruning but do not roll back already applied updates.
